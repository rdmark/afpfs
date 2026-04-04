/*
	afp.c
	
	AppleTalk Filing Protocol, Workstation
	Copyright (c) 1996-1997 by: Ben Hekster <heksterb@acm.org>
	
	Mon Oct 21 17:39:38 IST 1996
	
	The API is an implementation of the specifications in:
	
	[IAT]	Inside AppleTalk, Addison-Wesley, first edition
	[A3D]	AppleShare 3.0 Developer's Kit, AppleTalk Filing Protocol Version 2.1
	
	Refer to these for detailed information on its use.

NOTE
	Calls relating to the Macintosh Desktop Database have not yet
	been implemented

NOTE
	The API does not support short path names, because we are not
	running under DOS.  You can still use DOS-based AFP servers,
	though, because they are required to support long path names.
	
	"Sink my hands into the sea
	Reach your fingers up to me
		Trust me one more time
		I can only try
		Throw yourself to me
	and I'll try to pull you free"
	--Undertow, Lush;  Split
*/

#include <asm/byteorder.h>

#include <sys/uio.h>

#include <limits.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "atp.h"
#include "asp.h"
#include "afp.h"
#include "mac.h"


extern int errno;


/* AFP function codes */
enum AFPFunction {
	afpByteRangeLock = 1, afpCloseVol, afpCloseDir, afpCloseFork,
	afpCopyFile, afpCreateDir, afpCreateFile,
	afpDelete, afpEnumerate, afpFlush, afpFlushFork,
	afpGetForkParms = 14, afpGetSrvrInfo, afpGetSrvrParms,
	afpGetVolParms, afpLogin, afpLoginCont, afpLogout, afpMapName,
	afpMapID, afpMoveAndRename, afpOpenVol, afpOpenDir, afpOpenFork,
	afpRead, afpRename, afpSetDirParms, afpSetFileParms,
	afpSetForkParms, afpSetVolParms, afpWrite, afpGetFileDirParms,
	afpGetSrvrMsg = 38
	};

/* AFP path specification types */
enum PathType { kShortPathType = 1, kLongPathType };

// AFP protocol versions
enum Protocol { kAFPVersion1dot1, kAFPVersion2dot0, kAFPVersion2dot1 };

// AFP authentication methods
enum Authentication { kAFPNoAuthentication, kAFPCleartext };


/*	AFP
	AFP session
*/
struct AFP {
	struct ASP	*asp;
	unsigned int	loggedIn;	/* completed AFPLogin */
	enum Protocol	protocol;	/* protocol version */
	};



/*

	time conversions

*/

/* time bases relative to Macintosh (unsigned, January 1, 1904) */
static const unsigned long
	timeOriginEpoch = 0x7c25b080,	/* unsigned, January 1, 1970 */
	timeOriginAFP = 0xb492f400;	/* signed, January 1, 2000 */

/*	afp2unixdate
	unix2afpdate
	Convert between AFP and Epoch dates
NOTE
	Dates that are unrepresentable are clipped, so conversions are
	invertible only between approx. 1931 and ?.
*/
static time_t afp2unixdate(
	signed long	afp
	)
{
const signed long startEpochInAFP = (signed long) (timeOriginEpoch - timeOriginAFP);

/* date unrepresentable in Unix? */
return afp < startEpochInAFP ? 0UL : (unsigned long) (afp - startEpochInAFP);
}

static signed long unix2afpdate(
	time_t		unixx
	)
{
const unsigned long startAFPInEpoch = (unsigned long) (timeOriginAFP - timeOriginEpoch);

/* date unrepresentable on Mac? */
return (unsigned long) unixx >= startAFPInEpoch ? LONG_MAX : (signed long) ((unsigned long) unixx - startAFPInEpoch);
}



/*

	parameter block parsing

*/

/*	UnpackVolParams
	Unpack the volume parameters returned by OpenVol or GetVolParms
*/
static void UnpackVolParams(
	void		*const pstart,	/* volume parameters */
	const void	*const pend,
	AFPVolParms	*params,
	AFPVolParmsSelect wantParams
	)
{
register void *p = pstart;

/* parse the parameter block */
if (wantParams & afpGetVolAttributes) *(__s16*) &params->attributes = ntohs(*((__s16*) p)++);
if (wantParams & afpGetVolSignature) params->signature = ntohs(*((__s16*) p)++);
if (wantParams & afpGetVolCreationDate) params->creationDate = afp2unixdate(ntohl(*((__s32*) p)++));
if (wantParams & afpGetVolModificationDate) params->modificationDate = afp2unixdate(ntohl(*((__s32*) p)++));
if (wantParams & afpGetVolBackupDate) params->backupDate = afp2unixdate(ntohl(*((__s32*) p)++));
if (wantParams & afpGetVolID) params->id = ntohs(*((__s16*) p)++);
if (wantParams & afpGetVolBytesFree) params->bytesFree = ntohl(*((__s32*) p)++);
if (wantParams & afpGetVolBytesTotal) params->bytesTotal = ntohl(*((__s32*) p)++);
if (wantParams & afpGetVolName) {
	char *s = pstart + (__u16) ntohs(*((__s16*) p)++);
	params->name = (void*) s < pend ? (p2cstr(s), s) : NULL;
	}
}


/*	PackVolParams
	Write the volume parameters as accepted by SetVolParms
NOTE
	Not all of these parameters can actually be set, I just included
	them for symmetry
BUGS
	Check for buffer overruns
*/
static void *PackVolParams(
	void		*const pstart,	/* (return) volume parameters */
	const void	*const pend,
	const AFPVolParms *params,
	AFPVolParmsSelect wantParams
	)
{
register void *p = pstart;
__s16 *nameOffset = NULL;

/* write the fixed-length parameters */
if (wantParams & afpGetVolAttributes) *((__s16*) p)++ = htons(*(__s16*) &params->attributes);
if (wantParams & afpGetVolSignature) *((__s16*) p)++ = params->signature;
if (wantParams & afpGetVolCreationDate) *((__s32*) p)++ = htonl(unix2afpdate(params->creationDate));
if (wantParams & afpGetVolModificationDate) *((__s32*) p)++ = htonl(unix2afpdate(params->modificationDate));
if (wantParams & afpGetVolBackupDate) *((__s32*) p)++ = htonl(unix2afpdate(params->backupDate));
if (wantParams & afpGetVolID) *((__s16*) p)++ = htons(params->id);
if (wantParams & afpGetVolBytesFree) *((__s32*) p)++ = htonl(params->bytesFree);
if (wantParams & afpGetVolBytesTotal) *((__s32*) p)++ = htonl(params->bytesTotal);
if (wantParams & afpGetVolName) nameOffset = ((__s16*) p)++;

/* write variable-length parameters */
if (nameOffset) {
	*nameOffset = p - pstart;
	p = c2pstrncpy(p, params->name, sizeof params->name - 1);
	}

/* check for buffer overruns *** damage has been done */
if (p > pend) { errno = aspSizeErr; return NULL; }

return p;
}


/*	UnpackDirParams
	Unpack the directory parameters returned by GetFileDirParms
*/
static AFPError UnpackDirParams(
	void		*const pstart,	/* directory parameters */
	const void	*const pend,
	AFPDirParmsSelect wantDirParams,
	AFPDirParms	*params
	)
{
/* see the notes in UnpackFileParams */
register void *p = pstart;

if (wantDirParams & afpGetDirAttributes) *(__s16*) &params->attributes = ntohs(*((__s16*) p)++);
if (wantDirParams & afpGetDirParDirID) params->cat.parID = ntohl(*((__s32*) p)++);
if (wantDirParams & afpGetDirCreationDate) params->cat.creationDate = afp2unixdate(ntohl(*((__s32*) p)++));
if (wantDirParams & afpGetDirModificationDate) params->cat.modificationDate = afp2unixdate(ntohl(*((__s32*) p)++));
if (wantDirParams & afpGetDirBackupDate) params->cat.backupDate = afp2unixdate(ntohl(*((__s32*) p)++));
if (wantDirParams & afpGetDirFinderInfo) {
	params->finderInfo.extent[0] = ntohs(*((__s16*) p)++);
	params->finderInfo.extent[1] = ntohs(*((__s16*) p)++);
	params->finderInfo.extent[2] = ntohs(*((__s16*) p)++);
	params->finderInfo.extent[3] = ntohs(*((__s16*) p)++);
	*(__s16*) &params->finderInfo.flags = ntohs(*((__s16*) p)++);
	params->finderInfo.location[0] = ntohs(*((__s16*) p)++);
	params->finderInfo.location[1] = ntohs(*((__s16*) p)++);
	params->finderInfo.view = ntohs(*((__s16*) p)++);
	params->finderInfo.scroll[0] = ntohs(*((__s16*) p)++);
	params->finderInfo.scroll[1] = ntohs(*((__s16*) p)++);
	params->finderInfo.openChain = ntohl(*((__s16*) p)++);
	params->finderInfo.unused = ntohs(*((__s16*) p)++);
	params->finderInfo.comment = ntohs(*((__s16*) p)++);
	params->finderInfo.putAway = ntohl(*((__s16*) p)++);
	}
if (wantDirParams & afpGetDirName) {
	char *s = pstart + (__u16) ntohs(*((__s16*) p)++);
	params->cat.name = (void*) s < pend ? (p2cstr(s), s) : NULL;
	}
if (wantDirParams & afpGetDirShortName) {
	char *s = pstart + (__u16) ntohs(*((__s16*) p)++);
	params->cat.shortName = (void*) s < pend ? (p2cstr(s), s) : NULL;
	}
if (wantDirParams & afpGetDirID) params->cat.id = ntohl(*((__s32*) p)++);
if (wantDirParams & afpGetDirOffspring) params->offspring = ntohs(*((__s16*) p)++);
if (wantDirParams & afpGetDirOwnerID) params->ownerID = ntohl(*((__s32*) p)++);
if (wantDirParams & afpGetDirGroupID) params->groupID = ntohl(*((__s32*) p)++);
if (wantDirParams & afpGetDirAccessRights) params->access = ntohl(*((__s32*) p)++);
if (wantDirParams & afpGetDirProDOSInfo) memcpy(&params->cat.proDOSInfo, p, sizeof params->cat.proDOSInfo), p += 6;

/* check that we didn't overrun the buffer ***** the damage has already been done */
if (p > pend) return aspSizeErr;

return 0;
}


/*	PackDirParams
	Write directory parameters acceptable to SetDirParms
	Return the first byte after the written parameters
NOTE
	Note that not all these parameters can actually be written,
	I just included them for symmetry
BUGS
	Check buffer overrun
*/
static char *PackDirParams(
	void		*const pstart,
	const void	*const pend,
	AFPDirParmsSelect wantParams,
	const AFPDirParms *params
	)
{
/* see the notes in UnpackFileParams */
register void *p = pstart;
__s16 *dirNameOffset = NULL, *dirShortNameOffset = NULL;

/* pack the requested fixed-length parameters */
if (wantParams & afpGetDirAttributes) *((__s16*) p)++ = htons(*(__s16*) &params->attributes);
if (wantParams & afpGetDirParDirID) *((__s32*) p)++ = htonl(params->cat.parID);
if (wantParams & afpGetDirCreationDate) *((__s32*) p)++ = htonl(unix2afpdate(params->cat.creationDate));
if (wantParams & afpGetDirModificationDate) *((__s32*) p)++ = htonl(unix2afpdate(params->cat.modificationDate));
if (wantParams & afpGetDirBackupDate) *((__s32*) p)++ = htonl(unix2afpdate(params->cat.backupDate));
if (wantParams & afpGetDirFinderInfo) {
	*((__s16*) p)++ = htons(params->finderInfo.extent[0]);
	*((__s16*) p)++ = htons(params->finderInfo.extent[1]);
	*((__s16*) p)++ = htons(params->finderInfo.extent[2]);
	*((__s16*) p)++ = htons(params->finderInfo.extent[3]);
	*((__s16*) p)++ = htons(*(__s16*) &params->finderInfo.flags);
	*((__s16*) p)++ = htons(params->finderInfo.location[0]);
	*((__s16*) p)++ = htons(params->finderInfo.location[1]);
	*((__s16*) p)++ = htons(params->finderInfo.view);
	*((__s16*) p)++ = htons(params->finderInfo.scroll[0]);
	*((__s16*) p)++ = htons(params->finderInfo.scroll[1]);
	*((__s32*) p)++ = htonl(params->finderInfo.openChain);
	*((__s16*) p)++ = htons(params->finderInfo.unused);
	*((__s16*) p)++ = htons(params->finderInfo.comment);
	*((__s32*) p)++ = htonl(params->finderInfo.putAway);
	}
if (wantParams & afpGetDirName) dirNameOffset = ((__s16*) p)++;
if (wantParams & afpGetDirShortName) dirShortNameOffset = ((__s16*) p)++;
if (wantParams & afpGetDirID) *((__s32*) p)++ = htonl(params->cat.id);
if (wantParams & afpGetDirOffspring) *((__s16*) p)++ = htons(params->offspring);
if (wantParams & afpGetDirOwnerID) *((__s32*) p)++ = htonl(params->ownerID);
if (wantParams & afpGetDirGroupID) *((__s32*) p)++ = htonl(params->groupID);
if (wantParams & afpGetDirAccessRights) *((__s32*) p)++ = htonl(params->access);
if (wantParams & afpGetDirProDOSInfo) p = memcpy(p, &params->cat.proDOSInfo, sizeof params->cat.proDOSInfo) + sizeof params->cat.proDOSInfo;

/* pack the requested variable-length parameters */
if (dirNameOffset) {
	*dirNameOffset = p - pstart;
	p = c2pstrncpy(p, params->cat.name, sizeof params->cat.name - 1);
	}
if (dirShortNameOffset) {
	*dirShortNameOffset = p - pstart;
	p = c2pstrncpy(p, params->cat.shortName, sizeof params->cat.shortName - 1);
	}

/* check for buffer overruns ***** the damage has already been done */
if (p > pend) { errno = aspSizeErr; return NULL; }

return p;
}


/*	UnpackFileParams
	Unpack the file parameters returned by OpenFork or GetFileDirParms
*/
static AFPError UnpackFileParams(
	void		*const pstart,	/* file parameters */
	const void	*const pend,
	AFPFileParmsSelect wantFileParams,
	AFPFileParms	*params
	)
{
register void *p = pstart;

/* *** The handling of the attributes and finderInfo.flags fields is not
   portable, because it depends upon the storage order of bitfields
   which is specifically implementation-dependent. */
if (wantFileParams & afpGetFileAttributes) *(__s16*) &params->attributes = ntohs(*((__s16*) p)++);
if (wantFileParams & afpGetFileParDirID) params->cat.parID = ntohl(*((__s32*) p)++);
if (wantFileParams & afpGetFileCreationDate) params->cat.creationDate = afp2unixdate(ntohl(*((__s32*) p)++));
if (wantFileParams & afpGetFileModificationDate) params->cat.modificationDate = afp2unixdate(ntohl(*((__s32*) p)++));
if (wantFileParams & afpGetFileBackupDate) params->cat.backupDate = afp2unixdate(ntohl(*((__s32*) p)++));
if (wantFileParams & afpGetFileFinderInfo) {
	/* The contents of the finderInfo bytes are not defined by AFP.
	   Our handling of it is based on the definition of the Macintosh
	   native file system HFS, which is probably (but not necessarily)
	   what it is.  Even under HFS most of these fields are not
	   well-defined or their definitions are holdovers from MFS and
	   now meaningless.  The reason why I pack/unpack all the fields
	   (even `unused') is so that a file's Finder information will
	   be preserved across a Get/Set.  In addition, I pack all the
	   data into proper corresponding fields on the off chance that
	   that information may actually be useful to someone. */
	params->finderInfo.type = ntohl(*((__s32*) p)++);
	params->finderInfo.creator = ntohl(*((__s32*) p)++);
	*(__s16*) &params->finderInfo.flags = ntohs(*((__s16*) p)++);
	params->finderInfo.location[0] = ntohs(*((__s16*) p)++);
	params->finderInfo.location[1] = ntohs(*((__s16*) p)++);
	params->finderInfo.folder = ntohs(*((__s16*) p)++);
	params->finderInfo.window = ntohs(*((__s16*) p)++);
	params->finderInfo.icon = ntohs(*((__s16*) p)++);
	memcpy(&params->finderInfo.unused, p, sizeof params->finderInfo.unused), p += 8;
	params->finderInfo.putAway = ntohl(*((__s32*) p)++);
	}
if (wantFileParams & afpGetFileName) {
	char *s = pstart + (__u16) ntohs(*((__s16*) p)++);
	params->cat.name = (void*) s < pend ? (p2cstr(s), s) : NULL;
	}
if (wantFileParams & afpGetFileShortName) {
	char *s = pstart + (__u16) ntohs(*((__s16*) p)++);
	params->cat.shortName = (void*) s < pend ? (p2cstr(s), s) : NULL;
	}
if (wantFileParams & afpGetFileID) params->cat.id = ntohl(*((__s32*) p)++);
if (wantFileParams & afpGetFileDataForkLength) params->dataForkLength = ntohl(*((__s32*) p)++);
if (wantFileParams & afpGetFileRsrcForkLength) params->rsrcForkLength = ntohl(*((__s32*) p)++);

/* check for buffer overruns ***** the damage has already been done */
if (p > pend) return aspSizeErr;

return 0;
}


/*	PackFileParams
	Write file parameters acceptable to SetFileParms
	Return the first byte after the written parameters
NOTE
	Note that not all these parameters can actually be written,
	I just included them for symmetry
BUGS
	Check buffer overrun
*/
static char *PackFileParams(
	void		*const pstart,
	const void	*const pend,
	AFPFileParmsSelect wantParams,
	const AFPFileParms *params
	)
{
/* see the notes in UnpackFileParams */
register void *p = pstart;
__s16 *nameOffset = NULL, *shortNameOffset = NULL;

/* pack the requested fixed-length parameters */
if (wantParams & afpGetFileAttributes) *((__s16*) p)++ = htons(*(__s16*) &params->attributes);
if (wantParams & afpGetFileParDirID) *((__s32*) p)++ = htonl(params->cat.parID);
if (wantParams & afpGetFileCreationDate) *((__s32*) p)++ = htonl(unix2afpdate(params->cat.creationDate));
if (wantParams & afpGetFileModificationDate) *((__s32*) p)++ = htonl(unix2afpdate(params->cat.modificationDate));
if (wantParams & afpGetFileBackupDate) *((__s32*) p)++ = htonl(unix2afpdate(params->cat.backupDate));
if (wantParams & afpGetFileFinderInfo) {
	*((__s32*) p)++ = htonl(params->finderInfo.type);
	*((__s32*) p)++ = htonl(params->finderInfo.creator);
	*((__s16*) p)++ = htons(*(__s16*) &params->finderInfo.flags);
	*((__s16*) p)++ = htons(params->finderInfo.location[0]);
	*((__s16*) p)++ = htons(params->finderInfo.location[1]);
	*((__s16*) p)++ = htons(params->finderInfo.folder);
	*((__s16*) p)++ = htons(params->finderInfo.window);
	*((__s16*) p)++ = htons(params->finderInfo.icon);
	p = memcpy(p, &params->finderInfo.unused, 8) + 8;
	*((__s32*) p)++ = htonl(params->finderInfo.putAway);
	}
if (wantParams & afpGetFileName) nameOffset = ((__s16*) p)++;
if (wantParams & afpGetFileShortName) shortNameOffset = ((__s16*) p)++;
if (wantParams & afpGetFileID) *((__s32*) p)++ = htonl(params->cat.id);
if (wantParams & afpGetFileDataForkLength) *((__s32*) p)++ = htonl(params->dataForkLength);
if (wantParams & afpGetFileRsrcForkLength) *((__s32*) p)++ = htonl(params->rsrcForkLength);

/* pack the requested variable-length parameters */
if (nameOffset) {
	*nameOffset = p - pstart;
	p = c2pstrncpy(p, params->cat.name, sizeof params->cat.name - 1);
	}
if (shortNameOffset) {
	*shortNameOffset = p - pstart;
	p = c2pstrncpy(p, params->cat.shortName, sizeof params->cat.shortName - 1);
	}

/* check for buffer overruns ***** the damage has already been done */
if (p > pend) { errno = aspSizeErr; return NULL; }

return p;
}



/*

	server

*/

/*	Attention
	The server sent a request for attention
*/
static void Attention(
	struct ASP	*asp,
	int		attentionCode
	)
{
const union {
	__u16 i;
	struct {
		__u16		shutDown : 1,
				serverCrash : 1,
				serverMessage : 1,
				disconnect : 1,
				minutes : 12;
		} s;
	} afpUserBytes = { attentionCode };
struct AFP *const afp = containerof(struct AFP, asp, asp);

// get the server message
if (afpUserBytes.s.serverMessage) {
	char replyd[256];
	struct iovec replyv = { replyd, sizeof replyd };
	AFPSrvrMsg message;
	int err = AFPGetSrvrMsg(afp, &replyv, afpSrvrMsgServer, afpSelectSrvrMsg, &message);
/*
		#ifdef __KERNEL__
		printk("%s\n", message.message)
		#endif
		;
*/
	printk("afpfs: attention, but couldn't get server message (%d)\n", err);
	}
}


/*	AFPGetSrvrInfo
	Get information about a server not yet connected to
*/
AFPError AFPGetSrvrInfo(
	struct sockaddr_at *server,	/* server address */
	struct iovec	*replyv,	/* returned server information */
	AFPSrvrInfo	*params		/* parsed server information, or NULL */
	)
{
AFPError err;
struct {
	__s8		function;
	} command = { afpGetSrvrInfo };
struct {
	__s16		machineTypeOffset,
			versionCountOffset,
			uamCountOffset,
			iconMaskOffset;
	__s16		flags;
	unsigned char	name[0];
	} *replyp = replyv->iov_base;
const struct iovec commandv = { &command, 5 };

/* send an ASP GetStatus */
if (err = ASPGetStatus(server, &commandv, replyv)) goto Abort;

/* parse the reply */
if (params) {
	*(__s16*) &params->flags = ntohs(replyp->flags);
	(void) p2cstrncpy(params->name, replyp->name, sizeof params->name - 1);
	(void) p2cstrncpy(params->type, (char*) &replyp->machineTypeOffset + ntohs(replyp->machineTypeOffset), sizeof params->type - 1);

	/* copy the version strings */ {
		unsigned char *p = (unsigned char*) &replyp->machineTypeOffset + ntohs(replyp->versionCountOffset);
		unsigned n, i;
		char *c;

		for (
			n = sizeof params->versions / sizeof *params->versions, i = *p++, c = params->versions[0];
			n > 0;
			n--, c += sizeof *params->versions
			)
			if (i > 0) {
				(void) p2cstrncpy(c, p, sizeof *params->versions - 1),
				p += 1 + p[0];
				i--;
				}
			else
				*c = '\0';
		}

	/* copy the authentication methods */ {
		unsigned char *p = (unsigned char*) &replyp->machineTypeOffset + ntohs(replyp->uamCountOffset);
		unsigned n, i;
		char *c;

		for (
			n = sizeof params->uam / sizeof *params->uam, i = *p++, c = params->uam[0];
			n > 0;
			n--, c += sizeof *params->uam
			)
			if (i > 0) {
				(void) p2cstrncpy(c, p, sizeof *params->uam - 1),
				p += 1 + p[0];
				i--;
				}
			else
				*c = '\0';
		}

	/* copy the icon */
	params->icon = replyp->iconMaskOffset ?
		memcpy(
			params->_icon,
			(const char*) &replyp->machineTypeOffset + ntohs(replyp->iconMaskOffset),
			sizeof params->_icon
			) :
		NULL;
	}

Abort:
return err;
}


/*	AFPGetSrvrParms
	Get server parameters
	reply is used to receive the response.
	If volume is not NULL, the routine also parses the response and returns
	numVolumes pointers to character strings (i.e., pointing into reply)
BUGS
	No provision for returning the flags, although currently they are
	in the bytes volumes[][-1]
*/
AFPError AFPGetSrvrParms(
	struct AFP	*afp,
	struct iovec	*replyv,	/* buffer to receive response */
	time_t		*serverTime,	/* (return) server time, or NULL */
	char		*volumes[],	/* returned pointers to volume names, or NULL */
	unsigned	numVolumes	/* number of volume name pointers */
	)
{
AFPError err;
struct {
	__s8		function;
	} command = { afpGetSrvrParms };
struct {
	__s32		serverTime;
	__s8		numVols;
	__s8		parameters[0];
	} *reply = replyv->iov_base;
struct iovec commandv = { &command, 1 };

/* reply buffer large enough to hold the the first fields of the response? */
if (replyv->iov_len < (char*) reply->parameters - (char*) reply) { err = aspBuffTooSmall; goto Abort; }

/* send the command */
if (err = ASPCommand(afp->asp, &commandv, replyv)) goto Abort;

/* parse the response? */
if (volumes) {
	char
		*r = reply->parameters,
		*const rend = replyv->iov_base + replyv->iov_len;
	char **v = volumes;

	while (numVolumes-- > 0)
		/* return the converted volume name, or NULL */
		if (r + r[1] <= rend) { *v++ = r + 1; r = p2cstr(r + 1); }
		else *v++ = NULL;
	}

/* return server time */
if (serverTime)
	*serverTime = afp2unixdate(ntohl(reply->serverTime));

Abort:
return err;
}


/*	SupportedProtocol
	Return the preferred AFP protocol and authentication versions
	supported by the server having the given information,
	or a negative number
*/
static const char *gProtocols[] = {
	"AFPVersion 1.1",
	"AFPVersion 2.0"			// not really supported yet, included because NT doesn't support 1.1
	// "AFPVersion 2.1"
	};
static enum Protocol SupportedProtocol(
	const AFPSrvrInfo *info
	)
{
signed int q = -1, j;
unsigned int i;
const char *v;

/* for all the versions supported by the server */
for (i = sizeof info->versions / sizeof *info->versions, v = *info->versions; i > 0; i--, v += sizeof *info->versions)
	/* do we support it? */
	for (j = q + 1; j < sizeof gProtocols / sizeof *gProtocols; j++)
		if (strcdcmp(v, gProtocols[j]) == 0) q = j;

return q;
}


/*	SupportedAuthentication
	Return the preferred AFP authentication method supported by
	the server having the given information, with the specified
	username and password, or NULL; or return a negative number
BUGS
	The comparisons should not be diacritical-insensitive
*/
static const char *gAuthentications[] = {
	"No User Authent",
	"Cleartxt Passwrd"
	};
static enum Authentication SupportedAuthentication(
	const AFPSrvrInfo *info,
	const char	*username,
	const char	*password
	)
{
signed int q = -1, j;
unsigned int i;
const char *v;

/* for all the methods supported by the server */
for (i = sizeof info->uam / sizeof *info->uam, v = *info->uam; i > 0; i--, v += sizeof *info->versions)
	// unauthenticated log in wanted?
	if (!username) {
		if (strcdcmp(v, gAuthentications[0]) == 0) { q = 0; break; }
		}

	else
		/* do we support it? */
		for (j = q + 1; j < sizeof gAuthentications / sizeof *gAuthentications; j++)
			if (strcdcmp(v, gAuthentications[j]) == 0) q = j;

return q;
}


/*	AFPLogin
	Log in to an AFP server
	`info' is the server information as obtained previously from a
	call to AFPGetSrvrInfo, which is used to choose appropriate
	protocols and authentication methods; or NULL, in which case
	AFP 1.1 and no authentication is used.  `username' and `password'
	are used for authentication, or NULL for unauthenticated (`guest')
	access.
*/
struct AFP *AFPLogin(
	const struct sockaddr_at *server,
	const AFPSrvrInfo *info,	/* server information */
	const char	*username,
	const char	*password
	)
{
AFPError err;
struct AFP *afp;
const enum Protocol version = info ? SupportedProtocol(info) : 0;
const enum Authentication method = info ? SupportedAuthentication(info, username, password) : 0;

/* agree on a protocol and authentication method version */
if ((int) version < 0) return errno = afpBadVersNum, NULL;
if ((int) method < 0) return errno = afpBadUAM, NULL;

/* allocate the session structure */
if (! (afp = (struct AFP*) malloc(sizeof(struct AFP)))) goto Abort;
afp->loggedIn = 0;
afp->protocol = version;

/* open a connection with the server */
if (! (afp->asp = ASPOpenSession(server, &Attention))) goto AbortClose;

/* send the login request */
{
	struct {
		__s8		function;
		unsigned char	protocol[32],
				_method[32];	// actually follows last byte of protocol
		union {
			struct {
				unsigned char	username[32];
				char		_password[8]; // actually 0-byte word-aligned after username
				} cleartext;
			} _authentication;	// actually follows last byte of method
		} command = { afpLogin };
	struct {
		__s16		userID;
		__s16		unused;
		} reply;
	struct iovec
		commandv = { &command },
		replyv = { &reply, sizeof reply };
	char *p = command.protocol;

	/* build the login request *** check overflows */
	p = c2pstrncpy(p, gProtocols[afp->protocol], 16);
	p = c2pstrncpy(p, gAuthentications[method], 16);

	/* authentication */
	switch (method) {
		case kAFPNoAuthentication:
			break;

		case kAFPCleartext: {
			const unsigned char *pe;

			// user name padded to word-alignment
			p = c2pstrncpy(p, username, 32);
			if ((int) p & 1) *p++ = 0;

			// password, zero-padded out
			p = strncpy(p, password, sizeof command._authentication.cleartext._password) +
				sizeof command._authentication.cleartext._password;
			break;
			}
		}

	commandv.iov_len = p - (char*) commandv.iov_base;
	if (err = ASPCommand(afp->asp, &commandv, &replyv)) goto AbortClose;

	afp->loggedIn = 1;
	}

Abort:
return afp;

AbortClose:
AFPLogout(afp);
errno = err;
return NULL;
}


/*	AFPLoginCont
	Secondary login
*/
AFPError AFPLoginCont(
	struct AFP	*afp,
	__s16		userID,
	struct iovec	*uamInfo,	/* user authentication information, or NULL */
	struct iovec	*uamInfoReply	/* returned continuation UAM, or NULL */
	)
{
struct {
	__s8		function;
	__s8		zero;
	__s16		userID;
	} command = { afpLoginCont, 0, htons(userID) };
struct {
	__s16		userID;
	} reply;
struct iovec
	commandv[2] = { { &command, sizeof command } },
	replyv[2] = { { &reply, sizeof reply } };

if (uamInfo || uamInfoReply) {
	#ifndef __KERNEL__
	fprintf(stderr, "AFPLoginCont: sorry, not implemented: UAM\n");
	#endif
	return afpParamErr;
	}

/* caller wants to send UAM information? */
if (uamInfo) commandv[1] = *uamInfo;

/* caller wants to receive continuation UAM information? */
if (uamInfoReply) replyv[1] = *uamInfoReply;

/* send the request */
return ASPCommand(afp->asp, commandv, replyv);
}


/*	AFPLogout
	Log out
*/
void AFPLogout(
	struct AFP	*afp
	)
{
/* connection was opened? */
if (afp->asp) {
	/* logged in? */
	if (afp->loggedIn) {
		/* send the log out request */
		struct {
			__s8		function;
			} command = { afpLogout };
		struct iovec commandv = { &command, 1 };

		(void) ASPCommand(afp->asp, &commandv, NULL);
		}

	/* close the connection */
	ASPCloseSession(afp->asp);
	}

free(afp);
}


/*	AFPGetSrvrMsg
	Get messages from the server
	`replyv' is a buffer in which the message is retrieved.  If `message'
	is not NULL, the returned buffer is parsed and returned in it.
USAGE
	AFP 2.1
*/
AFPError AFPGetSrvrMsg(
	struct AFP	*afp,
	struct iovec	*replyv,		// reply buffer
	AFPSrvrMsgType	type,			// `attention' or `log in' message
	AFPSrvrMsgSelect bitMap,		// parameters requested
	AFPSrvrMsg	*message		// (return) server message, or NULL
	)
{
AFPError err;
struct {
	__u8		function;
	__u8		zero;
	__u16		type;
	__u16		bitMap;
	} command = {
	afpGetSrvrMsg, 0, htons(type), htonl(bitMap)
	};
struct {
	__u16		type;
	__u16		bitMap;
	char		data[0];
	} *reply = replyv->iov_base;
struct iovec commandv = { &command, sizeof command };

// see that there is enough room for at least the fixed fields
if (!replyv || replyv->iov_len < (char*) reply->data - (char*) reply) { err = aspBuffTooSmall; goto Abort; }

// send the command
if (err = ASPCommand(afp->asp, &commandv, replyv)) goto Abort;

// parse the response?
if (message) {
	AFPSrvrMsgSelect bitMap;
	char *r = reply->data, *const rend = replyv->iov_base + replyv->iov_len;
	
	// see that we have at least the fixed fields
	if (rend < reply->data) { err = aspSizeErr; goto Abort; }
	
	// get the message type and field bit map
	message->type = ntohs(reply->type);
	bitMap = ntohs(reply->bitMap);
	
	// get the fields
	if (bitMap & afpSelectSrvrMsg) if ((r = p2cstr(r)) > rend) { err = aspSizeErr; goto Abort; }
	}

Abort:
return err;
}



/*

	volume

*/

/*	AFPOpenVol
	Make a volume available
	Return the volume ID, or a nonzero error code in `errno'
	
	replyv is the buffer used to receive the result, or NULL if no
	volume information is desired (in which case, params must be NULL
	and wantParams must be 0).  If params is not NULL, the response
	is parsed and results returned in it.  wantParams indicates which
	parameters to return as a result of opening the volume.
BUGS
	replyv should probably not need to be a parameter
*/
AFPVolume AFPOpenVol(
	struct AFP	*afp,
	const char	*name,		/* volume name */
	const char	password[8],	/* password, or NULL */
	struct iovec	*replyv,	/* response buffer, or NULL */
	AFPVolParms	*params,	/* parsed parameters, or NULL */
	AFPVolParmsSelect wantParams	/* requested parameters */
	)
{
AFPVolParms defaultParms;
struct {
	__s8		function;
	__s8		zero;
	__s16		bitMap;
	unsigned char	volumeName[256];
	char		_password[8];	// password actually follows 0-padded volumeName
	} command = {
	afpOpenVol, 0,
	htons(wantParams | afpGetVolID) // we need to return the volume ID
	};
struct {
	__s16		bitMap;
	__s16		volumeID;
	} defaultReply;
struct iovec defaultReplyv = { &defaultReply, sizeof defaultReply };
struct {
	__s16		bitMap;
	char		parameters[0];
	} *reply = replyv ? replyv->iov_base : defaultReplyv.iov_base;
char *p;
struct iovec commandv = { &command };

if (!replyv) {
	if (params || wantParams) { errno = afpParamErr; goto Abort; }
	replyv = &defaultReplyv;
	}
if (!params) params = &defaultParms;

// build the command
p = c2pstrncpy(command.volumeName, name, sizeof command.volumeName - 1);
if ((1 + command.volumeName[0]) & 1) *p++ = 0;	// pad string
if (password) p = memcpy(p, password, sizeof password) + sizeof password;
commandv.iov_len = p - (char*) commandv.iov_base;

// send the OpenVol request
if (errno = ASPCommand(afp->asp, &commandv, replyv)) goto Abort;

// always parse the response (we need the volume ID)
UnpackVolParams(
	reply->parameters,
	replyv->iov_base + replyv->iov_len,
	params, ntohs(reply->bitMap)
	);

errno = 0;

Abort:
return params->id;
}


/*	AFPCloseVol
	Close an open volume
*/
AFPError AFPCloseVol(
	struct AFP	*afp,
	AFPVolume	volumeID
	)
{
struct {
	__s8		function;
	__s8		zero;
	__s16		volumeID;
	} command = { afpCloseVol, 0, htons(volumeID) };
struct iovec commandv = { &command, sizeof command };

/* send the command */
return ASPCommand(afp->asp, &commandv, NULL);
}


/*	AFPSetVolParms
	Set volume parameters
*/
AFPError AFPSetVolParms(
	struct AFP	*afp,
	AFPVolume	volume,
	const AFPVolParms *params,
	AFPVolParmsSelect wantParams
	)
{
AFPError err;
struct {
	__s8		function;
	__s8		zero;
	__s16		volume;
	__s16		bitMap;
	unsigned char	parameters[atpCmdSize - 6];
	} command = { afpSetVolParms, 0, htons(volume), htons(wantParams) };
struct iovec commandv = { &command };
void *p;

/* build the packet */
if (! (p = PackVolParams(command.parameters, &command + 1, params, wantParams))) { err = errno; goto Abort; }
commandv.iov_len = p - commandv.iov_base;

/* send the SetVolParms command */
err = ASPCommand(afp->asp, &commandv, NULL);

Abort:
return err;
}


/*	AFPGetVolParms
	Return volume parameters
*/
AFPError AFPGetVolParms(
	struct AFP	*afp,
	AFPVolume	volume,
	struct iovec	*replyv,
	AFPVolParms	*params,	/* (return) parsed volume parameters, or NULL */
	AFPVolParmsSelect wantParams
	)
{
AFPError err;
struct {
	__s8		function;
	__s8		zero;
	__s16		volumeID;
	__s16		bitMap;
	} command = { afpGetVolParms, 0, htons(volume), htons(wantParams) };
struct {
	__s16		bitMap;
	char		parameters[0];
	} *replyp = replyv->iov_base;
struct iovec commandv = { &command, sizeof command };

/* send the command */
if (err = ASPCommand(afp->asp, &commandv, replyv)) goto Abort;

/* parse the reply */
if (params)
	UnpackVolParams(
		replyp->parameters, replyv->iov_base + replyv->iov_len,
		params, ntohs(replyp->bitMap)
		);

Abort:
return err;
}


/*	AFPFlush
	Flush volume to disk
*/
AFPError AFPFlush(
	struct AFP	*afp,
	AFPVolume	volume
	)
{
struct {
	__s8		function;
	__s8		zero;
	__s16		volume;
	} command = { afpFlush, 0, htons(volume) };
struct iovec commandv = { &command, sizeof command };

/* send the Flush command */
return ASPCommand(afp->asp, &commandv, NULL);
}



/*

	directory

*/

/*	AFPCreateDir
	Create a directory
*/
AFPDirectory AFPCreateDir(
	struct AFP	*afp,
	AFPVolume	volume,
	AFPDirectory	directory,
	const char	*name
	)
{
struct {
	__s8		function;
	__s8		zero;
	__s16		volume;
	__s32		directory;
	__s8		pathType;
	unsigned char	path[256];
	} command = {
	afpCreateDir, 0, htons(volume), htonl(directory), kLongPathType
	};
struct { __s32 directory; } reply;
struct iovec
	commandv = { &command },
	replyv = { &reply, sizeof reply };
char *p;

/* build the packet */
p = c2pstrncpy(command.path, name, sizeof command.path - 1);
commandv.iov_len = p - (char*) commandv.iov_base;

/* send the CreateDir command */
if (errno = ASPCommand(afp->asp, &commandv, &replyv)) return 0;

return ntohl(reply.directory);
}


/*	AFPOpenDir
	Open a directory on a volume
	Return the directory ID or 0
*/
AFPDirectory AFPOpenDir(
	struct AFP	*afp,
	AFPVolume	volume,
	AFPDirectory	parDir,
	const char	*path
	)
{
struct {
	__s8		function;
	__s8		zero;
	__s16		volume;
	__s32		parID;
	__s8		pathType;
	unsigned char	path[256];
	} command = {
	afpOpenDir, 0, htons(volume), htonl(parDir), kLongPathType 
	};
struct {
	__u32		dirID;
	} reply;
struct iovec
	commandv = { &command },
	replyv = { &reply, sizeof reply };
char *p;

// build the packet
p = c2pstrncpy(command.path, path, sizeof command.path - 1);
commandv.iov_len = p - (char*) commandv.iov_base;

// send the OpenDir command
if (errno = ASPCommand(afp->asp, &commandv, &replyv)) {
	struct {
		__u16		fileBitMap,
				dirBitMap;
		__s8		flag, zero;
		__u32		dirID;
		} __attribute__ ((packed)) secondary;
	struct iovec secondaryv = { &secondary, sizeof secondary };
	
	// compensate for a bug in the netatalk afpd server
	/* Because they serve fixed-ID volumes, they think they do not have to
	   support OpenDir; however, [IAT 13-114] specifically states that they must.
	   So instead, we get the directory ID some other way and pretend that we
	   succeeded. */
	if (
		errno != afpCallNotSupported ||
		(errno = AFPGetFileDirParms(
			afp, &secondaryv, volume, parDir, path, NULL, 0, NULL, afpGetDirID
			))
		) return 0;
	
	// was really a directory?
	if (secondary.flag >= 0) return errno = afpObjectTypeErr, 0;
	
	// pretend that the original OpenDir succeeded, and continue
	reply.dirID = secondary.dirID;
	}

return ntohl(reply.dirID);
}


/*	AFPCloseDir
	Close a previously opened directory
*/
AFPError AFPCloseDir(
	struct AFP	*afp,
	AFPVolume	volume,
	AFPDirectory	directory
	)
{
AFPError err;
struct {
	__s8		function;
	__s8		zero;
	__s16		volume;
	__s32		directory;
	} command = {
	afpCloseDir, 0, ntohs(volume), ntohl(directory)
	};
struct iovec commandv = { &command, sizeof command };

// issue the CloseDir call
err = ASPCommand(afp->asp, &commandv, NULL);
if (err == afpCallNotSupported) err = 0;		// compensate for the netatalk bug mentioned in AFPOpenDir

return err;
}


/*	AFPEnumerate
	List directory contents
	Return number of offspring enumerated, or 0
*/
unsigned AFPEnumerate(
	struct AFP	*afp,
	struct iovec	*replyv,
	AFPVolume	volume,
	AFPDirectory	directory,
	const char	*path,		/* path to directory to list, or NULL */
	AFPFileParmsSelect wantFileParams,
	AFPDirParmsSelect wantDirParams,
	AFPFileDirParms	offspring[],	/* (return) file/directory parameters, or NULL */
	unsigned	firstOffspring,	/* 0-based index of first offspring to enumerate */
	unsigned	numOffspring	/* number of offspring to enumerate */
	)
{
struct {
	__s8		function;
	__s8		zero;
	__s16		volume;
	__s32		directory;
	__s16		fileBitMap,
			directoryBitMap;
	__s16		reqCount,
			startIndex;
	__s16		maxReplySize;
	__s8		pathType;
	unsigned char	path[256];
	} command = {
	afpEnumerate, 0,
	htons(volume), htonl(directory),
	htons(wantFileParams), htons(wantDirParams),
	htons(numOffspring), htons(1 + firstOffspring),
	htons(replyv->iov_len), kLongPathType
	};
struct {
	__s16		fileBitMap,
			directoryBitMap;
	__s16		actCount;
	} __attribute__ ((packed)) *reply = replyv->iov_base;
struct iovec commandv = { &command };
void *p;
unsigned n;

if (replyv->iov_len < sizeof *reply) { errno = aspSizeErr; return 0; }

/* build the command packet */
p = c2pstrncpy(command.path, path ? path : "", sizeof command.path - 1);
commandv.iov_len = (char*) p - (char*) commandv.iov_base;

/* send the Enumerate command */
if (errno = ASPCommand(afp->asp, &commandv, replyv)) return 0;

/* get reply parameters */
n = ntohs(reply->actCount);
wantFileParams = ntohs(reply->fileBitMap);
wantDirParams = ntohs(reply->directoryBitMap);

/* parse returned directory entries */
if (offspring) {
	struct {
		__s8		length;
		__s8		flags;
		char		parameters[0];
		} *o = (void*) (reply + 1);
	unsigned i;

	/* for each of the returned offspring */
	if (n > numOffspring) n = numOffspring; /* actually, n should never be greater than the reqCount we requested */
	for (i = 0; i < n; i++, (char*) o += o->length)
		/* is a directory? */
		/* If there is an error parsing one of the entries, return the number of
		   offspring correctly parsed.  If 0, this will cause the caller to take
		   note of the error code in errno.  Else, the caller will ignore the error
		   and use the valid returned offspring.
		   */
		if (o->flags & (1 << 7)) {
			/* parse the returned directory parameters */
			if (UnpackDirParams(
				o->parameters, (char*) o + o->length,
				wantDirParams, &offspring[i].dir
				)) return i;
			}

		/* is a file */
		else {
			/* parse the returned file parameters */
			if (UnpackFileParams(
				o->parameters, (char*) o + o->length,
				wantFileParams, &offspring[i].file
				)) return i;
			}
	}

return n;
}


/*	AFPSetDirParms
	Set directory parameters
*/
AFPError AFPSetDirParms(
	struct AFP	*afp,
	AFPVolume	volume,
	AFPDirectory	directory,
	const char	*pathName,	/* pathName of directory, or NULL */
	const AFPDirParms *params,
	AFPDirParmsSelect wantParams
	)
{
AFPError err;
struct {
	__s8		function;
	__s8		zero;
	__s16		volume;
	__s32		directory;
	__s16		bitMap;
	__s8		pathNameType;
	unsigned char	pathName[256];
	unsigned char	_parameters[atpCmdSize - sizeof pathName - 11]; /* actually follows padded last byte of pathName */
	} command = {
	afpSetDirParms, 0, htons(volume), htonl(directory),
	htons(wantParams), kLongPathType
	};
struct iovec commandv = { &command };
void *p;

/* build the packet */
p = c2pstrncpy(command.pathName, pathName ? pathName : "", sizeof command.pathName - 1);
if ((int) p & 1) *((unsigned char*) p)++ = '\0';
if (! (p = PackDirParams(p, &command + 1, wantParams, params))) { err = errno; goto Abort; }
commandv.iov_len = p - commandv.iov_base;

/* send the SetDirParms command */
err = ASPCommand(afp->asp, &commandv, NULL);

Abort:
return err;
}



/*

	file

*/

/*	AFPGetFileDirParms
	Return information about a file or directory

	wantFileParams and wantDirParams specify the parameters to be
	returned in the replyv buffer in case the specified object is a
	file or directory, respectively.  In addition, if fileParams or
	dirParams is not NULL, then the parameters in the buffer are
	parsed and returned there also.

	If name is NULL, parameters of the directory are returned.
NOTE
	This is not specified in IAT but appears to work.
*/
AFPError AFPGetFileDirParms(
	struct AFP	*afp,
	struct iovec	*replyv,		/* reply buffer */
	AFPVolume	volume,
	AFPDirectory	directory,
	const char	*name,			/* file or directory name, or NULL */
	AFPFileParms	*fileParams,		/* (return) file parameters, or NULL */
	AFPFileParmsSelect wantFileParams,
	AFPDirParms	*dirParams,		/* (return) directory parameters, or NULL */
	AFPDirParmsSelect wantDirParams
	)
{
AFPError err;
struct {
	__s8		function;
	__s8		zero;
	__s16		volume;
	__s32		directory;
	__s16		fileBitmap,
			directoryBitmap;
	__s8		pathType;
	unsigned char	path[256];
	} command = {
	afpGetFileDirParms, 0, htons(volume), htonl(directory),
	htons(wantFileParams), htons(wantDirParams),
	kLongPathType
	};
struct {
	__s16		fileBitMap,
			directoryBitMap;
	__s8		fileDir;
	__s8		zero;
	char		parameters[0];
	} *reply = replyv->iov_base;
struct iovec commandv = { &command };
char *p;

/* check that there is enough room in the buffer for the reply */
if (replyv->iov_len < sizeof *reply) { err = aspSizeErr; goto Abort; }

/* build the packet */
p = c2pstrncpy(command.path, name ? name : "", sizeof command.path - 1);
commandv.iov_len = p - (char*) commandv.iov_base;

/* send the FPGetFileDirParms command */
if (err = ASPCommand(afp->asp, &commandv, replyv)) goto Abort;

/* was a directory? */
if (reply->fileDir & (1 << 7)) {
	if (dirParams)
		/* parse the returned directory parameters into the result */
		if (err = UnpackDirParams(
			reply->parameters, replyv->iov_base + replyv->iov_len,
			ntohs(reply->directoryBitMap), dirParams
			))
			goto Abort;
	}

else {
	if (fileParams)
		/* parse the returned file parameters into the result */
		if (err = UnpackFileParams(
			reply->parameters, replyv->iov_base + replyv->iov_len,
			ntohs(reply->fileBitMap), fileParams
			))
			goto Abort;
	}

Abort:
return err;
}


/*	AFPSetFileParms
	Set file parameters
*/
AFPError AFPSetFileParms(
	struct AFP	*afp,
	AFPVolume	volume,
	AFPDirectory	directory,
	const char	*pathName,
	const AFPFileParms *params,
	AFPFileParmsSelect wantParams
	)
{
AFPError err;
struct {
	__s8		function;
	__s8		zero;
	__s16		volume;
	__s32		directory;
	__s16		bitMap;
	__s8		pathNameType;
	unsigned char	pathName[256];
	unsigned char	_parameters[atpCmdSize - sizeof pathName - 11]; /* actually follows padded last byte of pathName */
	} command = {
	afpSetFileParms, 0, htons(volume), htonl(directory), htons(wantParams), kLongPathType
	};
struct iovec commandv = { &command };
void *p;

/* build the packet */
p = c2pstrncpy(command.pathName, pathName, sizeof command.pathName - 1);
if ((int) p & 1) *((unsigned char*) p)++ = '\0';
if (! (p = PackFileParams(p, &command + 1, wantParams, params))) { err = errno; goto Abort; }
commandv.iov_len = p - commandv.iov_base;

/* send the SetFileParms command */
err = ASPCommand(afp->asp, &commandv, NULL);

Abort:
return err;
}


/*	AFPCreateFile
	Create a file
*/
AFPError AFPCreateFile(
	struct AFP	*afp,
	AFPVolume	volume,
	AFPDirectory	directory,
	int		mayDelete,	/* delete already existing file */
	const char	*path
	)
{
struct {
	__s8		function;
	__s8		flags;
	__s16		volume;
	__s32		directory;
	__s8		pathType;
	unsigned char	path[256];
	} command = {
	afpCreateFile, (mayDelete != 0) << 7, htons(volume), htonl(directory), kLongPathType
	};
struct iovec commandv = { &command };
char *p;

p = c2pstrncpy(command.path, path, 255);
commandv.iov_len = p - (char*) commandv.iov_base;

/* send the CreateFile command */
return ASPCommand(afp->asp, &commandv, NULL);
}


/*	AFPDelete
	Delete a file or directory
*/
AFPError AFPDelete(
	struct AFP	*afp,
	AFPVolume	volume,
	AFPDirectory	directory,
	const char	*name		/* name of file to delete, or NULL for the directory */
	)
{
struct {
	__s8		function;
	__s8		zero;
	__s16		volume;
	__s32		directory;
	__s8		pathType;
	unsigned char	path[256];
	} command = {
	afpDelete, 0, htons(volume), htonl(directory), kLongPathType
	};
struct iovec commandv = { &command };
char *p;

/* build the packet */
p = c2pstrncpy(command.path, name ? name : "", sizeof command.path - 1);
commandv.iov_len = p - (char*) commandv.iov_base;

/* send the Delete command */
return ASPCommand(afp->asp, &commandv, NULL);
}


/*	AFPCopyFile
	Copy a file
*/
AFPError AFPCopyFile(
	struct AFP	*afp,
	AFPVolume	sourceVolume,
	AFPDirectory	sourceDirectory,
	const char	*sourceName,
	AFPVolume	destinationVolume,
	AFPDirectory	destinationDirectory,
	const char	*destinationPath,	/* path to destination directory, or NULL for source directory */
	const char	*destinationName	/* destination file name, or NULL for source file name */
	)
{
/* *** this uses non-ANSI GNU C extension '__attribute__' */
struct {
	__s8		function;
	__s8		zero;
	__s16		sourceVolume;
	__s32		sourceDirectory;
	__s16		destinationVolume;
	__s32		destinationDirectory;
	__s8		sourceNameType;
	unsigned char	sourceName[32];
	__s8		_destinationPathType;
	unsigned char	_destinationPath[256];
	__s8		_destinationNameType;
	unsigned char	_destinationName[32];
	} __attribute__ ((packed)) command = {
	afpCopyFile, 0,
	htons(sourceVolume), htonl(sourceDirectory),
	htons(destinationVolume), htonl(destinationDirectory),
	kLongPathType
	};
struct iovec commandv = { &command };
char *p;

/* construct the command */
p = c2pstrncpy(command.sourceName, sourceName, sizeof command.sourceName - 1);
*((__s8*) p)++ = kLongPathType;
p = c2pstrncpy(p, destinationPath ? destinationPath : "", sizeof command._destinationPath - 1);
*((__s8*) p)++ = kLongPathType;
p = c2pstrncpy(p, destinationName ? destinationName : "", sizeof command._destinationName - 1);

/* send the CopyFile command */
commandv.iov_len = p - (char*) commandv.iov_base;
return ASPCommand(afp->asp, &commandv, NULL);
}


/*	AFPRename
	Rename a file or directory
*/
AFPError AFPRename(
	struct AFP	*afp,
	AFPVolume	volume,
	AFPDirectory	directory,
	const char	*pathName,	/* path and name of file to rename, or NULL for directory */
	const char	*newName
	)
{
struct {
	__s8		function;
	__s8		zero;
	__s16		volume;
	__s32		directory;
	__s8		pathNameType;
	unsigned char	pathName[256];
	__s8		_newNameType;	/* actually follows last byte of pathName */
	unsigned char	_newName[32];
	} command = {
	afpRename, 0, htons(volume), htonl(directory), kLongPathType
	};
struct iovec commandv = { &command };
char *p;

/* build the packet */
p = c2pstrncpy(command.pathName, pathName ? pathName : "", sizeof command.pathName - 1);
*((__s8*) p)++ = kLongPathType;
p = c2pstrncpy(p, newName, sizeof command._newName - 1);
commandv.iov_len = p - (char*) commandv.iov_base;

/* send the FPRename command */
return ASPCommand(afp->asp, &commandv, NULL);
}


/*	AFPMoveAndRename
	Move and/or rename a file or directory
*/
AFPError AFPMoveAndRename(
	struct AFP	*afp,
	AFPVolume	volume,
	AFPDirectory	sourceDirectory,
	const char	*sourcePathName,	/* name of source, or NULL for sourceDirectory */
	AFPDirectory	destinationDirectory,
	const char	*destinationPath,	/* path to destination, or NULL for source path */
	const char	*destinationName	/* name of destination, or NULL for source name */
	)
{
struct {
	__s8		function;
	__s8		zero;
	__s16		volume;
	__s32		sourceDirectory,
			destinationDirectory;
	__s8		sourcePathNameType;
	unsigned char	sourcePathName[256];
	__s8		_desinationPathType;	/* actually follows last byte of sourcePathName */
	unsigned char	_destinationPath[256];
	__s8		_destinationnameType;
	unsigned char	_destinationName[32];
	} command = {
	afpMoveAndRename, 0, htons(volume),
	htonl(sourceDirectory), htonl(destinationDirectory),
	kLongPathType
	};
struct iovec commandv = { &command };
char *p;

/* construct the packet */
p = c2pstrncpy(command.sourcePathName, sourcePathName ? sourcePathName : "", sizeof command.sourcePathName - 1);
*((__s8*) p)++ = kLongPathType;
p = c2pstrncpy(p, destinationPath ? destinationPath : "", sizeof command._destinationPath - 1);
*((__s8*) p)++ = kLongPathType;
p = c2pstrncpy(p, destinationName ? destinationName : "", sizeof command._destinationName - 1);
commandv.iov_len = p - (char*) commandv.iov_base;

/* send the FPMoveAndRename command */
return ASPCommand(afp->asp, &commandv, NULL);
}



/*

	fork

*/

/*	AFPOpenFork
	Open the fork of a file
	Return the fork reference number, or a nonzero error code in `errno'

	replyv is used to receive the file parameters, or NULL if none
	are required, in which case wantParams must be 0 and params must
	be NULL.  wantParams indicates which parameters to return as a
	result of opening the fork.  If params is not NULL, the response
	is parsed and results returned in it.
*/
AFPFork AFPOpenFork(
	struct AFP	*afp,
	struct iovec	*replyv,	/* response buffer, or NULL */
	AFPVolume	volume,
	AFPDirectory	directory,
	AFPAccessMode	mode,
	const char	*path,
	int		resourceFork,	/* data or resource fork */
	AFPFileParms	*params,	/* parsed parameters, or NULL */
	AFPFileParmsSelect wantParams
	)
{
AFPFork fork;
struct {
	__s8		function;
	__s8		fork;
	__s16		volumeID;
	__s32		directoryID;
	__s16		bitMap;
	__s16		accessMode;
	__s8		pathType;
	unsigned char	pathName[256];
	} command = {
	afpOpenFork, (resourceFork != 0) << 7,
	htons(volume), htonl(directory), htons(wantParams),
	htons(mode), kLongPathType
	};
struct {
	__s16		bitMap;
	__s16		refNum;
	char		parameters[0];
	} defaultReply, *reply = replyv ? replyv->iov_base : &defaultReply;
struct iovec
	commandv = { &command, sizeof command },
	defaultReplyv = { &defaultReply, sizeof defaultReply };
__s8 *c;

/* if no response buffer was given, then no parameters may be returned */
if (!replyv) {
	if (params || wantParams) { errno = afpParamErr; goto Abort; }
	replyv = &defaultReplyv;
	}

/* construct the packet */
if (!path) { errno = afpParamErr; goto Abort; }
c = c2pstrncpy(command.pathName, path, sizeof command.pathName - 1);
commandv.iov_len = c - (__s8*) commandv.iov_base;

/* send the command */
if (errno = ASPCommand(afp->asp, &commandv, replyv)) goto Abort;

/* get the fork reference number */
fork = ntohs(reply->refNum);

/* parse the file parameters */
if (params)
	errno = UnpackFileParams(
		reply->parameters, replyv->iov_base + replyv->iov_len,
		ntohs(reply->bitMap), params
		);

Abort:
return fork;
}


/*	AFPCloseFork
	Close an open fork
*/
AFPError AFPCloseFork(
	struct AFP	*afp,
	AFPFork		fork
	)
{
struct {
	__s8		function;
	__s8		zero;
	__s16		fork;
	} command = { afpCloseFork, 0, htons(fork) };
struct iovec commandv = { &command, sizeof command };

/* send the command */
return ASPCommand(afp->asp, &commandv, NULL);
}


/*	AFPFlushFork
	Flush fork to disk
*/
AFPError AFPFlushFork(
	struct AFP	*afp,
	AFPFork		fork
	)
{
struct {
	__s8		function;
	__s8		zero;
	__s16		fork;
	} command = { afpFlushFork, 0, htons(fork) };
struct iovec commandv = { &command, sizeof command };

/* send the FlushFork command */
return ASPCommand(afp->asp, &commandv, NULL);
}


/*	AFPByteRangeLock
	Lock a byte range of a fork
	Return the offset from the beginning of the fork of the first byte
	locked, or 0
*/
unsigned long AFPByteRangeLock(
	struct AFP	*afp,
	AFPFork		fork,
	unsigned long	offset,
	unsigned long	length,
	int		lock,		/* lock or unlock */
	int		end		/* relative to end or beginning of fork */
	)
{
struct {
	__s8		function;
	__s8		flags;
	__s16		fork;
	__s32		offset,
			length;
	} command = {
	afpByteRangeLock, ((end != 0) << 7) | ((lock != 0) << 0),
	htons(fork), htonl(offset), htonl(length)
	};
struct { __s32 rangeStart; } reply;
struct iovec
	commandv = { &command, sizeof command },
	replyv = { &reply, sizeof reply };

/* send the ByteRangeLock command */
if (errno = ASPCommand(afp->asp, &commandv, &replyv)) return 0;

return ntohl(reply.rangeStart);
}


/*	AFPSetForkParms
	Set parameters of the fork's file
*/
AFPError AFPSetForkParms(
	struct AFP	*afp,
	AFPFork		fork,
	const AFPFileParms *params,
	AFPFileParmsSelect wantParams
	)
{
AFPError err;
struct {
	__s8		function;
	__s8		zero;
	__s16		fork;
	__s16		bitMap;
	unsigned char	parameters[atpCmdSize - 6];
	} command = { afpSetForkParms, 0, htons(fork), htons(wantParams) };
struct iovec commandv = { &command };
void *p;

/* build the packet */
if (! (p = PackFileParams(command.parameters, &command + 1, wantParams, params))) { err = errno; goto Abort; }
commandv.iov_len = p - commandv.iov_base;

/* send the SetForkParms command */
err = ASPCommand(afp->asp, &commandv, NULL);

Abort:
return err;
}


/*	AFPGetForkParms
	Return parameters of the fork's file
*/
AFPError AFPGetForkParms(
	struct AFP	*afp,
	struct iovec	*replyv,	/* returned parameters */
	AFPFork		fork,
	AFPFileParms	*params,	/* (return) parsed parameters, or NULL */
	AFPFileParmsSelect wantParams
	)
{
AFPError err;
struct {
	__s8		function;
	__s8		zero;
	__s16		fork;
	__s16		bitMap;
	} command = { afpGetForkParms, 0, htons(fork), htons(wantParams) };
struct {
	__s16		bitMap;
	char		parameters[0];
	} *reply = replyv->iov_base;
struct iovec commandv = { &command, sizeof command };

/* make sure the buffer is large enough for the reply */
if (replyv->iov_len < sizeof *reply) { err = aspSizeErr; goto Abort; }

/* send the FPGetForkParms command */
if (err = ASPCommand(afp->asp, &commandv, replyv)) goto Abort;

if (params)
	/* parse the returned file parameters */
	err = UnpackFileParams(
		reply->parameters, replyv->iov_base + replyv->iov_len,
		ntohs(reply->bitMap), params
		);

Abort:
return err;
}


/*	AFPWrite
	Write bytes into a fork
*/
AFPError AFPWrite(
	struct AFP	*afp,
	AFPFork		fork,
	struct iovec	*buffer,	/* data to write */
	signed long	offset,
	int		atEnd		/* offset is relative to end of file */
	)
{
struct {
	__s8		function;
	__s8		startEnd;
	__s16		fork;
	__s32		offset;
	__s32		count;
	} command = {
	afpWrite, (atEnd != 0) << 7, htons(fork), htonl(offset), htonl(buffer->iov_len)
	};
struct iovec commandv = { &command, sizeof command };

/* send the Write command */
return ASPWrite(afp->asp, &commandv, buffer);
}


/*	AFPRead
	Read bytes from a fork
*/
AFPError AFPRead(
	struct AFP	*afp,
	AFPFork		fork,
	struct iovec	*buffer,
	unsigned long	offset
	)
{
struct {
	__s8		function;
	__s8		zero;
	__s16		refNum;
	__s32		offset,
			reqCount;
	__s8		newLineMask,
			newLineChar;
	} command = {
	afpRead, 0, htons(fork),
	htonl(offset),
	htonl(buffer->iov_len < atpQuantumSize ? buffer->iov_len : atpQuantumSize),
	'\0', '\0'
	};
struct iovec commandv = { &command, sizeof command };

/* send the Read command */
return ASPCommand(afp->asp, &commandv, buffer);
}



/*

	desktop data base

*/

/*	AFPMapID
	Return the name of the specified user or group ID
*/
AFPError AFPMapID(
	struct AFP	*afp,
	int		isGroup,
	AFPUserGroup	id,
	char		name[32]
	)
{
AFPError err;
struct {
	__s8		function;
	__s8		subfunction;
	__s32		id;
	} command = { afpMapID, isGroup ? 2 : 1, ntohl(id) };
struct {
	char		name[31];
	} reply;
struct iovec
	commandv = { &command, sizeof command },
	replyv = { &reply, sizeof reply };

/* send the MapID command */
if (err = ASPCommand(afp->asp, &commandv, &replyv)) goto Abort;

Abort:
return err;
}


/*	AFPMapName
	Return the ID of the specified user or group name
	If `isGroup' is nonzero, `name' specifies the putative name of an
	AppleShare group; otherwise, it is an AppleShare user name.
*/
AFPError AFPMapName(
	struct AFP	*afp,
	int		isGroup,	/* group or user */
	const char	*name,		/* AppleShare user or group name */
	AFPUserGroup	*id		/* (return) corresponding AppleShare user or group ID */
	)
{
AFPError err;
struct {
	__s8		function;
	__s8		subfunction;
	char		name[31];	/* [IAT1 13-31] */
	} command = { afpMapID, isGroup ? 4 : 3 };
struct {
	__s32		id;
	} reply;
struct iovec
	commandv = { &command },
	replyv = { &reply, sizeof reply };
const unsigned nameLength = strlen(name);

if (nameLength > sizeof command.name) return afpParamErr;
(void) memcpy(command.name, name, nameLength);

/* send the MapName command */
commandv.iov_len = sizeof command - sizeof command.name + nameLength;
if (err = ASPCommand(afp->asp, &commandv, &replyv)) goto Abort;

/* return the user/group ID */
if (id) *id = ntohl(reply.id);

Abort:
return err;
}
