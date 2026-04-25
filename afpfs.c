/*
	afpfs.c
	
	Linux file system for AppleTalk Filing Protocol
	
	Copyright (c) 1996-2026 by: Ben Hekster <heksterb@acm.org>
	Licensed under the MIT License - see LICENSE file for details.
	SPDX-License-Identifier: MIT
	
	Tue Oct 29 17:11:48 IST 1996

BUGS
	Does not support symbolic links (aliases)
*/

#include <linux/sched.h>
#include <linux/stat.h>


#ifdef MODULE
#include <linux/module.h>
#endif

#include "atp.h"
#include "asp.h"
#include "afp.h"
#include "afpfs.h"


extern int errno;

/* *** what is this? */
enum { AFPFS_SUPER_MAGIC = ('B' << 8) + 'H' };



/*	gFileSystemType
	Define the AFP file system type
*/
static struct super_block *AFPFSReadSuper(struct super_block*, struct AFPFSMount*, int);
static void AFPFSPutSuper(struct super_block *);

static struct file_system_type gFileSystemType = {
	(struct super_block *(*)(struct super_block*, void*, int))
		&AFPFSReadSuper,	/* function to read our super block */
	"afpfs",			/* name of our file system */
	0,				/* does not require a device */
	NULL				/* linked list */
	};


/*	AFPFSFromAFPError
	Translate an AFP error to an equivalent AFPFS (Unix) error code
*/
int AFPFSFromAFPError(
	AFPError	afp
	)
{
int err;

switch (afp) {
	// Unix errors
	case 0:			err = afp; break;
	
	// AFP errors
	case afpParamErr:	err = -EINVAL; break;
	case afpObjectNotFound:	err = -ENOENT; break;
	case afpDirNotEmpty:	err = -ENOTEMPTY; break;
	case afpAccessDenied:	err = -EACCES; break;
	
	// ASP errors
	case aspSessClosed:	err = -ENETRESET; break;
	
	// ATP errors
	case atpReqAborted:	err = -EINTR; break;
	case atpTooManyReqs:	err = -ENOSR; break;
	
	default:	
			#ifdef DEBUG
			printk("AFPFSFromAFPError: unknown translation for %d\n", afp);
			#endif
			err = -EREMOTEIO; break;	// something to indicate a generic error
	}

return err;
}


/*	AFPFromAFPFSName
	AFPFSFromAFPName
	File name translations
	Return the translated name
*/
char *AFPFromAFPFSName(
	AFPName		afp,		/* (return) AFP name */
	const char	*name,
	unsigned int	nameLen
	)
{
char *n = afp;
unsigned int l = nameLen > sizeof(AFPName) - 1 ? sizeof(AFPName) - 1 : nameLen;
/* *** what to do with truncated names? */

/* translate Unix special names */
#ifdef DEBUG
if (!name) printk("AFPFromAFPFSName: name is NULL\n");
#endif
if (name)
	// current directory?
	if (nameLen == 1 && name[0] == '.')
		// represented by an empty string [IAT1 13-26 `Fifth'], but afp.c should do this translation
		;

	// parent directory?
	else if (nameLen == 2 && name[0] == '.' && name[1] == '.') {
		/* we are never called to get the parent of the root */
		*n++ = ':'; *n++ = ':';	// ***** actually this is an error, should use null bytes (IAT1 13-24)
		}

	else
		/* copy the string *** */
		while (l--) *n++ = *name++;

/* terminate the string */
*n++ = '\0';

return afp;
}


/* char *AFPFSFromAFPName(char*, unsigned int*, const AFPName); */

const unsigned int kUserGroupMapClumpx = 5; /* allocate 1 << 5 map clumps at a time */


/*	AFPFSFromAFPUser
	Return the AFPFS (i.e., local operating system) user ID corresponding
	to the specified AFP user ID, or -1.
*/
static uid_t AFPFSFromAFPUser(
	struct AFPFSSuperBlock *afpfs,
	long		user
	)
{
#if 0
/* see if the mapping exists already */
unsigned int i, min, max;
struct UserGroupMap *map;
for (
	/* binary search over all the mapped users */
	min = 0, max = afpfs->users.n;

	/* divide the search interval */
	i = (min + max) / 2,
	map = afpfs->users.map + i,
	min < max;

	/* continue the search in the lower or upper half interval */
	(user < map->afp ? max : min) = i
	)
	/* found it? */
	if (user == map->afp) return map.afpfs.user;

/* make sure there is room for a new map entry */
if (afpfs->users.n & ((1 << kUserGroupMapClump) - 1) == 0)
	/* allocate another clump of map entries */
	if (! (afpfs->users = krealloc(
		afpfs->users,
		(afpfs->users.n + (1 << kUserGroupMapClump)) * sizeof *afpfs->map),
		afpfs->users.n * sizeof *afpfs->map
		) return -1;

/* insert the new mapping */
memmove(afpfs->users + i + 1, afpfs->users + i, afpfs->users.n++ - i);
map = afpfs->users + i;

map->afp = user;
map->afpfs.user = user;

return map->afpfs.user;
#endif

return afpfs->uid;
}


/*	AFPFSFromAFPGroup
	Translate AFP into AFPFS group IDs
*/
static gid_t AFPFSFromAFPGroup(
	struct AFPFSSuperBlock *afpfs,
	long		group
	)
{
return afpfs->gid;
}


static umode_t AFPFSFromAFPAccess(
	AFPAccess	afp
	)
{
return
	(afp & afpAccessOwnerRead ? S_IRUSR : 0) |
	(afp & afpAccessOwnerWrite ? S_IWUSR : 0) |
	(afp & afpAccessOwnerSearch ? S_IXUSR : 0) |	/* ***** not correct for files */
	(afp & afpAccessGroupRead ? S_IRGRP : 0) |
	(afp & afpAccessGroupWrite ? S_IWGRP : 0) |
	(afp & afpAccessGroupSearch ? S_IXGRP : 0) |
	(afp & afpAccessWorldRead ? S_IROTH : 0) |
	(afp & afpAccessWorldWrite ? S_IWOTH : 0) |
	(afp & afpAccessWorldSearch ? S_IXOTH : 0);
}


/*

	superblock operations

*/

/*	gSuperOperations
	Superblock (file system) operations
*/
static void
	AFPFSReadInode(struct inode*),
	AFPFSWriteInode(struct inode*),
	AFPFSPutInode(struct inode*),
	AFPFSPutSuper(struct super_block*),
	AFPFSWriteSuper(struct super_block*),
	AFPFSStatFS(struct super_block*, struct statfs*, int size);

struct super_operations gSuperOperations = {
	&AFPFSReadInode,
	NULL,				/* notify_change */
	&AFPFSWriteInode,
	&AFPFSPutInode,
	&AFPFSPutSuper,
	&AFPFSWriteSuper,
	&AFPFSStatFS,
	NULL				/* remount */
	};


/*	AFPFSReadSuper
	Return the file system's super block, or NULL
	
	sb is the `superblock' structure which represents the file system
	as a whole.  It is already partially initialized when we get here.
	
	param are file system-dependent parameters to the mount system call
	(cf afpfs.h)
*/
static struct super_block *AFPFSReadSuper(
	struct super_block *sb,		/* super block */
	struct AFPFSMount *param,	/* file system-dependent parameters */
	int		i
	)
{
int err;
struct AFPFSSuperBlock *const afpfs = (struct AFPFSSuperBlock*) &sb->u;

sb->s_mounted = NULL;			/* not yet mounted */
afpfs->afp = NULL;

// one more user of the module
#ifdef MODULE
MOD_INC_USE_COUNT;
#endif

// get the parameters
if (!param || param->length < sizeof(struct AFPFSMount)) { errno = -EINVAL; goto Abort; }

// specify the socket that will be used by AFPLogin
if (err = socket_return(param->fd)) goto Abort;

// log in to the server
if (! (afpfs->afp = AFPLogin(
	&param->server, &param->info,
	param->username[0] != '\0' ? param->username : NULL,
	param->password[0] != '\0' ? param->password : NULL
	))) {
	switch (errno) {
		case afpParamErr:	errno = afpfsNoSuchUser; break;
		case afpUserNotAuth:	errno = afpfsNoSuchPassword; break;
		}
	goto Abort;
	}

// open the specified volume
if ((afpfs->volume = AFPOpenVol(afpfs->afp, param->volume, NULL, NULL, NULL, 0)), errno) {
	// return more specific error
	switch (errno) {
		case afpParamErr:	errno = afpfsNoSuchVolume; break; /* probably nonexistent volume */
		case afpAccessDenied:	errno = afpfsNoSuchPassword; break;
		}
	goto Abort;
	}

/* set file system parameters */
/* lock_super(sb); */
sb->s_blocksize = 1 << (sb->s_blocksize_bits = afpfsBlockSizex);
sb->s_magic = AFPFS_SUPER_MAGIC;	/* *** used by whom?  is this ok? */
sb->s_op = &gSuperOperations;

/* get the root inode */
if (! (sb->s_mounted = AFPFSGetInode(sb, NULL, NULL))) goto Abort;
/* unlock_super(sb); */

/* create the user and group mappings */
afpfs->users.n = 0; afpfs->users.map = NULL;
afpfs->groups.n = 0; afpfs->groups.map = NULL;
afpfs->uid = current->uid;		/* *** for now, everything is owned by the user who manages to log in */
afpfs->gid = current->gid;

sb->s_dev = -1;
sb->s_dirt = 1;

return sb;

Abort:
/* return the error code to the caller
   We have to use this awkward mechanism because Linux VFS doesn't provide
   a way of returning a specific code.  Note also that we can't return the
   code in the `param' structure itself, since it is only a copy of the
   original in the user's address space. */
if (param->error) memcpy_tofs(param->error, &errno, sizeof errno);
	// put_fs_long(errno, param->error);

AFPFSPutSuper(sb);			/* caller doesn't return the superblock on failure */
return NULL;
}


/*	AFPFSPutSuper
	Called to return the superblock to us

	Although this is a superblock and not a file system type operation,
	I put this here because it is the converse of AFPFSReadSuper, and
	because I cannot #include <linux/module.h> in more than one
	compilation unit.
*/
static void AFPFSPutSuper(
	struct super_block *sb
	)
{
struct AFPFSSuperBlock *const afpfs = (struct AFPFSSuperBlock*) &sb->u;

if (sb->s_mounted) {
	if (afpfs->users.map) free(afpfs->users.map);
	if (afpfs->groups.map) free(afpfs->groups.map);

	AFPFSPutInode(sb->s_mounted);
	}

/* ***** close the socket? */

if (afpfs->afp) {
	/* close the server volume */
	if (afpfs->volume) (void) AFPCloseVol(afpfs->afp, afpfs->volume);

	/* log out from the server */
	AFPLogout(afpfs->afp);
	}

/* lock_super */
sb->s_dev = 0;				/* ***** proc, why? */
sb->s_dirt = 0;
/* unlock_super */

/* one less user of the file system */
#ifdef MODULE
MOD_DEC_USE_COUNT;
#endif
}


/*	AFPFSWriteSuper
	Write the super block out and handle timers

NOTE
	The timer module (timer.[ch]) normally uses an alarm signal to
	expire timers, but a mounted volume does not have a process
	associated with it.  Because most of our timers do things like
	send packets, they cannot run at interrupt time.  By pretending
	that the super block is always dirty and servicing the timers
	when we are called to `clean' it, we effectively `borrow'
	the sync process for this purpose.

	This does reduce the timer resolution, but the important thing
	is that the ASP tickle is sent about every half minute.  Doing
	it this way also doesn't cost an extra process and doesn't
	pollute the ATP and timer code.
*/
static void AFPFSWriteSuper(
	struct super_block	*sb
	)
{
/* ***** implement */

/* handle system timers */
CallTimers();

/* don't mark the super block clean so that we will continue to be called */
}


#ifdef MODULE

/*	init_module
	Initialize the kernel module
*/
int init_module()
{
int err;

/* register our file system with the kernel */
if (err = register_filesystem(&gFileSystemType)) {
	/* *** how do I return an error? */
	goto Abort;
	}

Abort:
return 0;
}


/*	cleanup_module
	Shut down the kernel module
*/
void cleanup_module()
{
/* unregister our file system from the kernel */
(void) unregister_filesystem(&gFileSystemType);
}

#endif


/*

	superblock operations

*/

/*	AFPFSReadInode
	Initialize the fields of the inode according to inode->i_ino
*/
static void AFPFSReadInode(
	struct inode	*inode
	)
{
struct super_block *const sb = inode->i_sb;

// we need the superblock to be always marked dirty for our timer mechanism
if (!sb->s_dirt) {
	printk("AFPFSReadInode: found s_dirt %d\n", sb->s_dirt);
	sb->s_dirt = 1;
	}
}


/*	AFPFSWriteInode
*/
static void AFPFSWriteInode(
	struct inode	*inode
	)
{
/* mark changes written out */
inode->i_dirt = 0;
}


/*	AFPFSPutInode
*/
void AFPFSPutInode(
	struct inode	*inode
	)
{
/* ***** this is what proc does, but what is it? */
if (inode->i_nlink)
	return;

inode->i_size = 0;
}


/*	AFPFSStatFS
	Return information about the file system
*/
static void AFPFSStatFS(
	struct super_block *sb,		/* file system to return status of */
	struct statfs	*statfs,	/* (return) file system information */
	int		size		/* byte size of statfs struct */
	)
{
AFPError err;
AFPVolParms params;
struct statfs s;
char reply[64];
struct iovec replyv = { reply, sizeof reply };
struct AFPFSSuperBlock *const afpfs = (struct AFPFSSuperBlock*) &sb->u;

if (errno = AFPGetVolParms(
	afpfs->afp, afpfs->volume, &replyv, &params,
	afpGetVolBytesFree | afpGetVolBytesTotal
	)) return;

/* return the file system information */
s.f_type = AFPFS_SUPER_MAGIC;		/* ***** */
s.f_bsize = 1 << afpfsBlockSizex;	/* block size in bytes */
s.f_blocks =				/* number of blocks on file system */
	(params.bytesTotal + (1 << afpfsBlockSizex) - 1) >> afpfsBlockSizex;
s.f_bfree =				/* number of blocks free */
s.f_bavail =				/* number of blocks available (for non-root use) */
	(params.bytesFree + (1 << afpfsBlockSizex) - 1) >> afpfsBlockSizex;
s.f_files = 0;				/* ***** number of files on file system? */
s.f_ffree = 0;				/* ***** number of files free on file system? */
/* *** s.f_fsid; */
s.f_namelen = 31;			/* length of a name in the file system */

memcpy_tofs(statfs, &s, size);
}


/*	AFPFSGetInode
	Get an inode by name rather than inode number
*/
struct inode *AFPFSGetInode(
	struct super_block *sb,		/* file system */
	struct inode	*directory,	/* directory, or NULL for root */
	const char	*name		/* path, or NULL for directory */
	)
{
struct inode *inode;
AFPFileParms fileParams;
AFPDirParms dirParams;
char reply[64];
struct iovec replyv = { reply, sizeof reply };
struct AFPFSSuperBlock *const afpfs = GetAFPFSSuperBlock(sb);

/* get the file or directory information */
dirParams.cat.id = 0;			/* to see if it was a file or directory */
if (errno = AFPFSFromAFPError(AFPGetFileDirParms(
	afpfs->afp, &replyv, afpfs->volume,
	directory ? directory->i_ino : afpfsRootInode,
	name,
	&fileParams, afpGetFileID | afpGetFileParDirID | afpGetFileName | afpGetFileDataForkLength | afpGetFileModificationDate | afpGetFileCreationDate,
	&dirParams, afpGetDirID | afpGetDirParDirID | afpGetDirName | afpGetDirAccessRights | afpGetDirOffspring | afpGetDirOwnerID | afpGetDirGroupID | afpGetDirModificationDate | afpGetDirCreationDate
	))) return NULL;

/* create the inode */
if (! (inode = iget(sb, dirParams.cat.id ? dirParams.cat.id : fileParams.cat.id))) return NULL;

/* initialize the inode */
inode->i_nlink = 1;
inode->i_op = &gInodeOperations;	/* file operations */
inode->i_blksize = sb->s_blocksize;
inode->i_rdev = 0;			/* *** what is this? */

/* remember the AFP-specific inode information */
{
	AFPCatalogParms *cat = dirParams.cat.id ? &dirParams.cat : &fileParams.cat;
	struct AFPFSInode *const afpfsInode = GetAFPFSInode(inode);

	/* we need to remember this because AFP identifies a file by parent
	   directory ID and file name */
	afpfsInode->id = cat->id;
	afpfsInode->parID = cat->parID;
	strncpy(afpfsInode->name, cat->name, sizeof afpfsInode->name - 1);
	}

// is a directory?
if (dirParams.cat.id) {
	inode->i_mode = S_IFDIR | AFPFSFromAFPAccess(dirParams.access);
	inode->i_uid = AFPFSFromAFPUser(afpfs, dirParams.ownerID);
	inode->i_gid = AFPFSFromAFPGroup(afpfs, dirParams.groupID);
	inode->i_size = 2 + dirParams.offspring; // account for '.' and '..'
	inode->i_atime =
	inode->i_mtime = dirParams.cat.modificationDate;
	inode->i_ctime = dirParams.cat.creationDate;
	inode->i_blocks = 0;
	}

// is a file?
else {
	inode->i_mode =
		directory->i_mode &	// inherit permissions and ownership from containing folder
		~(S_IFDIR) |		// is not a directory, and never executable (*** how can we determine when it is?)
		S_IFREG;		// is a file
	inode->i_uid = directory->i_uid;
	inode->i_gid = directory->i_gid;
	inode->i_size = fileParams.dataForkLength; /* *** note resource fork not accounted for */
	inode->i_atime =
	inode->i_mtime = fileParams.cat.modificationDate;
	inode->i_ctime = fileParams.cat.creationDate;
	inode->i_blocks = 0; /* ***** */
	}

// we need the superblock to be always marked dirty for our timer mechanism
if (!sb->s_dirt) {
	printk("AFPFSGetInode: found s_dirt %d\n", sb->s_dirt);
	sb->s_dirt = 1;
	}

return inode;
}
