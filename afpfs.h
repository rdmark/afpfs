/*
	afpfs.h
	
	Linux file system for AppleTalk Filing Protocol
	
	Copyright (c) 1996-2026 by: Ben Hekster <heksterb@acm.org>
	Licensed under the MIT License - see LICENSE file for details.
	SPDX-License-Identifier: MIT
	
	Tue Oct 29 17:11:48 IST 1996
*/

#ifndef AFPFS_H
#define AFPFS_H

struct sk_buff;

#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/atalk.h>

#include "afp.h"


/* root inode number */
enum { afpfsRootInode = 2 };


/* AFP putative block size (1 << afpfsBlockSizex) */
/* AFP doesn't actually have blocks but linux' VFS needs a block size */
enum { afpfsBlockSizex = 10 };


/*	AFPFSError
	Mount errors
*/
typedef enum {
	afpfsNoSuchVolume = 1, afpfsNoSuchUser, afpfsNoSuchPassword
	} AFPFSError;


/*	AFPFSMount
	File system-dependent parameters to mount(2)
*/
struct AFPFSMount {
	unsigned int	length;		/* length of this parameter block */
	AFPFSError	*error;		/* (return) mount error, or NULL */
	int		fd;		/* open socket */
	struct sockaddr_at server;	/* server to connect to */
	char		volume[28];	/* server volume */
	AFPSrvrInfo	info;		/* server information */
	char		username[32],	/* authentication */
			password[8];
	};


#ifdef __KERNEL__
/* *** ugly */

struct UserGroupMap {
	long		afp;
	union { uid_t user; gid_t group; } afpfs;
	};


/*	AFPFSSuperBlock
	GetAFPFSSuperBlock
	SetAFPFSSuperBlock
	AFP file system-specific super block information
*/
struct AFPFSSuperBlock {
	struct AFP	*afp;			/* AFP session */
	AFPVolume	volume;			/* server volume */
	int		uid, gid;		/* owner and group of files under this mount point */
	struct {
		struct UserGroupMap *map;
		unsigned int n;
		} users, groups;
	};

extern inline struct AFPFSSuperBlock *GetAFPFSSuperBlock(struct super_block *sb)
{
return (struct AFPFSSuperBlock*) &sb->u;
}


/*	AFPFSInode
	GetAFPFSInode
	AFP file system-specific inode information
	
	The afpfs file system's inode numbers are just the AFP file and directory IDs.
	Since AFP catalog nodes (i.e., files and directories) may be referenced by
	directory ID and relative path, we need (at least for files) to store the
	AFP name as well in order to translate an inode back to an AFP node.  The file ID
	is meaningful but not used anywhere in AFP catalog node specifications.
BUGS
	I think this means that if another user renames a file out from under us
	(which is perfectly legal), the inode loses its identity.
*/
struct AFPFSInode {
	AFPDirectory	id,			/* file or directory ID of this catalog node */
			parID;			/* directory ID of the parent directory */
	AFPName		name;			/* AFP name of this inode */
	};

extern inline struct AFPFSInode *GetAFPFSInode(struct inode *inode)
{
return (struct AFPFSInode*) &inode->u;
}


extern struct file_operations gFileOperations;
extern struct inode_operations gInodeOperations;


struct inode *AFPFSGetInode(struct super_block*, struct inode*, const char*);


/*

	kernel-to-AFP translations

*/

int AFPFSFromAFPError(AFPError);
char *AFPFromAFPFSName(AFPName, const char*, unsigned int);
char *AFPFSFromAFPName(char*, unsigned int*, const AFPName);

#endif

#endif
