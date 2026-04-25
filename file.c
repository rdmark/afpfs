/*
	file.c
	
	Linux file system for AppleTalk Filing Protocol
	file (fork) operations
	
	Copyright (c) 1996-2026 by: Ben Hekster <heksterb@acm.org>
	Licensed under the MIT License - see LICENSE file for details.
	SPDX-License-Identifier: MIT
	
	Mon Dec 16 15:08:10 IST 1996

REFERENCES
	"Advanced Programming in the UNIX Environment", W. Richard Stevens,
	Addison-Wesley
*/

#include <asm/segment.h>
#include <asm/types.h>

#include <linux/types.h>
#include <linux/fs.h>

#include "afp.h"
#include "afpfs.h"
#include "kernel.h"

extern int errno;


/*	gFileOperations
	File operations
*/
static int
	AFPFSread(struct inode*, struct file*, char*, int),
	AFPFSwrite(struct inode*, struct file*, const char*, int),
	AFPFSreaddir(struct inode*, struct file*, void*, filldir_t),
	AFPFSopen(struct inode*, struct file*),
	AFPFSFSync(struct inode*, struct file*);
static void AFPFSRelease(struct inode*, struct file*);

struct file_operations gFileOperations = {
	NULL,				/* lseek (default) */
	&AFPFSread,
	&AFPFSwrite,
	&AFPFSreaddir,
	NULL,				/* select */
	NULL,				/* ioctl */
	NULL,				/* mmap */
	&AFPFSopen,
	&AFPFSRelease,
	&AFPFSFSync,
	NULL,				/* fasync */
	NULL,				/* check_media_change */
	NULL,				/* revalidate */
	};



/*	GetFileAFPFork
	SetFileAFPFork
	Return or set the AFP fork of the specified open file
*/
static inline AFPFork GetFileAFPFork(
	const struct file *file
	)
{
/* we store it in the struct file private_data pointer field (ok?) */
return *(AFPFork*) &file->private_data;
}

static inline void SetFileAFPFork(
	struct file	*file,
	AFPFork		fork
	)
{
*(AFPFork*) &file->private_data = fork;
}


/*	AFPFSread
	Read data from an open file
	Return the number of bytes read
*/
static int AFPFSread(
	struct inode	*inode,
	struct file	*file,
	char		*buffer,
	int		length
	)
{
AFPError err;
struct AFPFSSuperBlock *const afpfs = GetAFPFSSuperBlock(inode->i_sb);
struct iovec bufferv = { malloc(length), length };

inode->i_sb->s_dirt = 1;

/* allocate a read buffer in kernel space */
if (!bufferv.iov_base) { err = -ENOSR; goto Abort; }

/* read the bytes from the fork */
if (err = AFPRead(afpfs->afp, GetFileAFPFork(file), &bufferv, file->f_pos))
	if (err == afpEOFErr)
		err = 0;	/* *** is there a way of reporting end of file to the caller? */
	else
		goto AbortDeallocate;

/* return the bytes to the caller */
memcpy_tofs(buffer, bufferv.iov_base, bufferv.iov_len);

/* update the file pointer */
file->f_pos += bufferv.iov_len;

AbortDeallocate:
free(bufferv.iov_base);

/* return the number of bytes read */
Abort:
#ifdef DEBUG
if (err) printk("AFPFSread: %d\n", err);
#endif
return !err ? bufferv.iov_len : AFPFSFromAFPError(err);
}


/*	AFPFSwrite
	Write bytes to an open file
	Return the number of bytes written, or a negative error code
*/
static int AFPFSwrite(
	struct inode	*inode,
	struct file	*file,
	const char	*buffer,
	int		length
	)
{
int err;
struct AFPFSSuperBlock *const afpfs = GetAFPFSSuperBlock(inode->i_sb);
struct iovec bufferv = { malloc(length), length };

/* return the bytes to the caller */
if (!bufferv.iov_base) return -ENOSR;
memcpy_fromfs(bufferv.iov_base, buffer, bufferv.iov_len);

/* write the data */
if (err = AFPFSFromAFPError(AFPWrite(
	afpfs->afp, GetFileAFPFork(file),
	&bufferv, file->f_pos, 0
	))) goto Abort;

/* update the file pointer */
file->f_pos += bufferv.iov_len;

Abort:
free(bufferv.iov_base);

/* return the number of bytes written */
return !err ? bufferv.iov_len : err;
}


/*	AFPFSreaddir
	Enumerate directory entries
	file->f_pos is the index of the first entry to return
	Return a negative error code, or 0
*/
static int AFPFSreaddir(
	struct inode	*inode,		/* directory inode */
	struct file	*file,		/* directory file */
	void		*param,		/* parameter to filldir */
	filldir_t	filldir		/* function to return results */
	)
{
struct AFPFSSuperBlock *const afpfs = GetAFPFSSuperBlock(inode->i_sb);

/* where to start indexing */
switch (file->f_pos) {
	case 0:
		if ((*filldir)(param, ".", 1, file->f_pos, inode->i_ino) < 0) return 0;
		file->f_pos++;

	case 1:
		/* ***** i_ino */
		if ((*filldir)(param, "..", 2, file->f_pos, inode->i_ino) < 0) return 0;
		file->f_pos++;

	default: {
		/* *** whenever we're called we never seem able to return more
		   than one directory entry at a time (filldir returns EINVAL)
		   why? */
		AFPFileDirParms catalog[1], *c;
		char buffer[0x0080];
		struct iovec replyv = { buffer, sizeof buffer };
		unsigned int count;

		/* get a block of directory offspring */
		if (count = AFPEnumerate(
			afpfs->afp, &replyv, afpfs->volume,
			inode->i_ino, NULL,
			afpGetFileName | afpGetFileID,
			afpGetDirName | afpGetDirID,
			catalog,
			file->f_pos - 2, sizeof catalog / sizeof *catalog
			)) {
			/* report each of the found entries back to the kernel */
			for (c = catalog; count-- > 0; c++) {
				if ((*filldir)(param, c->cat.name, strlen(c->cat.name), file->f_pos, c->cat.id) < 0) return 0;
				file->f_pos++;
				}
			}
		if (errno && errno != afpObjectNotFound) return AFPFSFromAFPError(errno);
		}
	}

return 0;
}


/*	AFPFSopen
	Open a file or directory
*/
static int AFPFSopen(
	struct inode	*inode,
	struct file	*file
	)
{
int err = 0;
struct AFPFSSuperBlock *const afpfs = GetAFPFSSuperBlock(inode->i_sb);
struct AFPFSInode *const afpfsi = GetAFPFSInode(inode);

/* ***** O_TRUNC, O_APPEND, O_NONBLOCK, O_SYNC */

/* is a file? */
if (inode->i_mode & S_IFREG) {
	AFPFork fork;
	
	// create the fork
	if (file->f_mode & O_CREAT) {
		printk("AFPFSopen: creating\n");
		if (err = AFPFSFromAFPError(AFPCreateFile(
			afpfs->afp, afpfs->volume, afpfsi->parID,
			!(file->f_mode & O_EXCL),	// may delete the file if O_EXCL not set
			afpfsi->name
			))) goto Abort;
		}
	
	// open the fork
	if (fork = AFPOpenFork(
		afpfs->afp, NULL, afpfs->volume,
		afpfsi->parID,
		((file->f_mode & FMODE_READ) ? afpAccessRead : 0) |
		((file->f_mode & FMODE_WRITE) ? afpAccessWrite : 0),
		afpfsi->name,
		0, NULL, 0
		), errno) {
		err = AFPFSFromAFPError(errno); goto Abort;
		}
	
	SetFileAFPFork(file, fork);
	}

// directory?
else if (inode->i_mode & S_IFDIR)
	// nothing to be done
	;

else
	// don't know how to open
	err = -EINVAL;

Abort:
return err;
}


/*	AFPFSRelease
	Close a file or directory
*/
static void AFPFSRelease(
	struct inode	*inode,
	struct file	*file
	)
{
struct AFPFSSuperBlock *const afpfs = GetAFPFSSuperBlock(inode->i_sb);

/* is a file? */
if (inode->i_mode & S_IFREG) {
	/* close the fork */
	(void) AFPCloseFork(afpfs->afp, GetFileAFPFork(file));
	SetFileAFPFork(file, 0);
	}

else
	/* nothing to be done */
	;
}


/*	AFPFSFSync
	Write in-core parts of file to disk
*/
static int AFPFSFSync(
	struct inode	*inode,
	struct file	*file
	)
{
struct AFPFSSuperBlock *const afpfs = GetAFPFSSuperBlock(inode->i_sb);
int err = 0;

/* is a file? */
if (inode->i_mode & S_IFREG)
	err = AFPFSFromAFPError(AFPFlushFork(afpfs->afp, GetFileAFPFork(file)));

return err;
}
