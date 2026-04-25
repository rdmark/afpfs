/*
	inode.c
	
	Linux file system for AppleTalk Filing Protocol
	inode (file) operations
	
	Copyright (c) 1996-2026 by: Ben Hekster <heksterb@acm.org>
	Licensed under the MIT License - see LICENSE file for details.
	SPDX-License-Identifier: MIT
	
	Mon Dec 16 15:15:29 IST 1996
*/

#include <asm/segment.h>
#include <asm/types.h>

#include <linux/types.h>
#include <linux/fs.h>

#include "afp.h"
#include "afpfs.h"

extern int errno;



/*	afpfs_inode_operations
	inode (directory) operations
*/
static int
	AFPFScreate(struct inode*, const char*, int, int, struct inode**),
	AFPFSlookup(struct inode*, const char*, int, struct inode**),
	AFPFSunlink(struct inode*, const char*, int),
	AFPFSmkdir(struct inode*, const char*, int, int),
	AFPFSrmdir(struct inode*, const char*, int),
	AFPFSrename(struct inode*, const char*, int, struct inode*, const char*, int, int);

static void
	AFPFStruncate(struct inode*);

struct inode_operations gInodeOperations = {
	&gFileOperations,		// default file operations
	&AFPFScreate,
	&AFPFSlookup,
	NULL,				// link
	&AFPFSunlink,
	NULL,				// symlink
	&AFPFSmkdir,
	&AFPFSrmdir,
	NULL,				// mknod
	&AFPFSrename,
	NULL,				// readlink
	NULL,				// follow_link
	NULL,				// readpage
	NULL,				// writepage
	NULL,				// bmap
	&AFPFStruncate,
	NULL,				// permission
	NULL				// smap
	};


/*	AFPFScreate
	Create a file
NOTE
	`mode' is ignored, as AFP does not have privileges on individual files
*/
static int AFPFScreate(
	struct inode	*directory,	/* where to create the file */
	const char	*name,
	int		nameLen,
	int		mode,		/* file mode (ignored) */
	struct inode	**inode
	)
{
int err;
struct AFPFSSuperBlock *afpfs = GetAFPFSSuperBlock(directory->i_sb);
AFPName afpName;

// create the file
if (err = AFPFSFromAFPError(AFPCreateFile(
	afpfs->afp, afpfs->volume,
	GetAFPFSInode(directory)->id,
	1,				// creat may always delete existing file
	AFPFromAFPFSName(afpName, name, nameLen)
	)))
	goto Abort;

// return the created file's inode
if (! (*inode = AFPFSGetInode(directory->i_sb, directory, afpName))) { err = errno; goto Abort; }

Abort:
#ifdef DEBUG
if (err && err != -EACCES) printk("AFPFScreate: failed on \"%s\" (%d)\n", name, err);
#endif
iput(directory);
return err;
}


/*	AFPFSlookup
	Find directory items
*/
static int AFPFSlookup(
	struct inode	*directory,
	const char	*name,
	int		nameLen,
	struct inode	**item
	)
{
int err = 0;
AFPName afpName;

/* *** */
if (nameLen == 2 && name[0] == '.' && name[1] == '.')
	*item = iget(directory->i_sb, GetAFPFSInode(directory)->parID);

// create the inode from AFP
else if (! (*item = AFPFSGetInode(
	directory->i_sb, directory,
	AFPFromAFPFSName(afpName, name, nameLen)
	))) err = errno;

Abort:
#ifdef DEBUG
if (err && err != -ENOENT) printk("AFPFSlookup: fail on \"%s\" (%d)\n", name, err);
#endif
iput(directory);
return err;
}


/*	AFPFSunlink
	Delete a link to a file
*/
static int AFPFSunlink(
	struct inode	*inode,		/* directory containing the file */
	const char	*name,
	int		nameLen
	)
{
int err;
struct AFPFSSuperBlock *afpfs = GetAFPFSSuperBlock(inode->i_sb);
struct AFPFSInode *afpfsi = GetAFPFSInode(inode);
AFPName afpName;

// delete the file
err = AFPFSFromAFPError(AFPDelete(
	afpfs->afp, afpfs->volume, afpfsi->id,
	AFPFromAFPFSName(afpName, name, nameLen)
	));

iput(inode);
#ifdef DEBUG
if (err && err != -EACCES) printk("AFPFSunlink: fail on \"%s\" (%d)\n", name, err);
#endif
return err;
}


/*	AFPFSmkdir
	Create a directory
*/
static int AFPFSmkdir(
	struct inode	*inode,		/* parent directory */
	const char	*name,
	int		nameLen,
	int		mode
	)
{
int err = 0;
struct AFPFSSuperBlock *afpfs = GetAFPFSSuperBlock(inode->i_sb);
struct AFPFSInode *afpfsi = GetAFPFSInode(inode);
AFPName afpName;

// create the directory
if (!AFPCreateDir(afpfs->afp, afpfs->volume, afpfsi->id, AFPFromAFPFSName(afpName, name, nameLen)))
	err = AFPFSFromAFPError(errno);

// ***** set parameters for owner, mode, &c.

iput(inode);
return err;
}


/*	AFPFSrmdir
	Delete an empty directory
*/
static int AFPFSrmdir(
	struct inode	*inode,		/* directory to delete */
	const char	*name,
	int		nameLen
	)
{
int err;
struct AFPFSSuperBlock *afpfs = GetAFPFSSuperBlock(inode->i_sb);
struct AFPFSInode *afpfsi = GetAFPFSInode(inode);

// delete the directory
err = AFPFSFromAFPError(AFPDelete(afpfs->afp, afpfs->volume, afpfsi->id, NULL));

iput(inode);
return err;
}


/*	AFPFSrename
	Move and rename a file or directory
*/
static int AFPFSrename(
	struct inode	*fromDir,
	const char	*fromName,
	int		fromNameLen,
	struct inode	*toDir,
	const char	*toName,
	int		toNameLen,
	int		mustBeDir		/* ***** nfs */
	)
{
int err;
struct AFPFSSuperBlock *const afpfs = GetAFPFSSuperBlock(fromDir->i_sb);
struct AFPFSInode
	*const fromi = GetAFPFSInode(fromDir),
	*const toi = GetAFPFSInode(toDir);
AFPName from, to;

AFPFromAFPFSName(from, fromName, fromNameLen);
AFPFromAFPFSName(to, toName, toNameLen);

// move and rename
err = AFPFSFromAFPError(AFPMoveAndRename(
	afpfs->afp, afpfs->volume,
	fromi->id, from, 
	toi->id, NULL, to
	));

iput(fromDir);
iput(toDir);

return err;
}


/*	AFPFStruncate
	Shorten a file
	The file may be open or closed
*/
static void AFPFStruncate(
	struct inode	*inode
	)
{
int err;
struct AFPFSSuperBlock *const afpfs = GetAFPFSSuperBlock(inode->i_sb);
struct AFPFSInode *const afpfsi = GetAFPFSInode(inode);
AFPFork fork;
AFPFileParms params;
char reply[6];				// fork refNum, bit map, and attributes
struct iovec replyv = { reply, sizeof reply };

/* In AFP, a file must be open to be truncated-- that is, the operation applies
   to the fork.  Since the file may be closed, and because we anyway don't have 
   access to the system's struct file, we ask AFP to (re)open the file and take
   note of whether the fork was already open before closing it. */
// open the fork (possibly already open)
if (fork = AFPOpenFork(
	afpfs->afp, &replyv, afpfs->volume, afpfsi->parID, afpAccessWrite, afpfsi->name, 0,
	&params, afpGetFileAttributes
	), errno) { err = errno; goto Abort; }

// set the fork's new length
params.dataForkLength = inode->i_size;
err = AFPSetForkParms(afpfs->afp, fork, &params, afpGetFileDataForkLength);

// fork wasn't already open when we opened it?
if (!params.attributes.dataAlreadyOpen) {
	// close the fork
	const int closeErr = AFPCloseFork(afpfs->afp, fork);
	if (!err) err = closeErr;
	}

Abort:
#ifdef DEBUG
if (err) printk("AFPFStruncate: %d\n", err);
#endif
}

