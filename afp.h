/*
	afp.h

	AppleTalk Filing Protocol, Workstation
	Copyright (c) 1996-1997 by: Ben Hekster <heksterb@acm.org>

	Mon Oct 21 17:39:38 IST 1996

	"It's just a little too late
	It's never enough to swallow those pills
	Now I'm sick and always will be"
	--Coast is Clear, Curve;  Frozen EP
*/

#ifndef AFP_H
#define AFP_H

struct iovec;
struct sockaddr_at;

struct AFP;


typedef short AFPVolume;
typedef long AFPDirectory;
typedef short AFPFork;
typedef char AFPName[32];

typedef struct { char data[6]; } AFPProDOSInfo;

typedef unsigned long AFPUserGroup;


/* AFP result codes */

typedef enum {
	afpAccessDenied = -5000, afpAuthContinue = -5001, afpBadUAM = -5002,
	afpBadVersNum = -5003, afpBitmapErr = -5004, afpCantMove = -5005,
	afpDenyConflict = -5006, afpDirNotEmpty = -5007, afpDiskFull = -5008,
	afpEOFErr = -5009, afpFileBusy = -5010, afpFlatVol = -5011,
	afpItemNotFound = -5012, afpLockErr = -5013, afpMiscErr = -5014,
	afpNoMoreLocks = -5015, afpNoServer = -5016, afpObjectExists = -5017,
	afpObjectNotFound = -5018, afpParamErr = -5019, afpRangeNotLocked = -5020,
	afpRangeOverlap = -5021, afpSessClosed = -5022, afpUserNotAuth = -5023,
	afpCallNotSupported = -5024, afpObjectTypeErr = -5025,
	afpTooManyFilesOpen = -5026, afpServerGoingDown = -5027,
	afpCantRename = -5028, afpDirNotFound = -5029, afpIconTypeErr = -5030,
	afpVolLocked = -5031, afpObjectLocked = -5032,
	} AFPError;


/* parameters for AFPGetSrvrInfo */

typedef struct {
	struct {
		unsigned int	supportsCopyFile : 1,
				supportsChangePassword : 1,
				unused : 14;
		} flags;
	char		name[33],	/* server machine name */
			type[17],	/* server machine type */
			versions[16][17], /* supported AFP versions */
			uam[16][17];	/* supported User Authentication Methods */
	char		*icon;		/* pointer to _icon or NULL */
	char		_icon[2][128];	/* server volume and mask */
	} AFPSrvrInfo;


/* parameters for AFPGetVolParms and AFPOpenVol */

typedef enum {
	afpGetVolAttributes = 1 << 0,
	afpGetVolSignature = 1 << 1,
	afpGetVolCreationDate = 1 << 2,
	afpGetVolModificationDate = 1 << 3,
	afpGetVolBackupDate = 1 << 4,
	afpGetVolID = 1 << 5,
	afpGetVolBytesFree = 1 << 6,
	afpGetVolBytesTotal = 1 << 7,
	afpGetVolName = 1 << 8
	} AFPVolParmsSelect;

typedef struct {
	struct {
		unsigned int	readOnly : 1,
				unused : 15;
		} attributes;
	short		signature;
	time_t		creationDate,
			modificationDate,
			backupDate;
	AFPVolume	id;
	unsigned long	bytesFree,
			bytesTotal;
	char		*name;
	} AFPVolParms;


/* parameters for AFPGetFileDirParms and AFPOpenFork */

typedef enum {
	afpAccessRead = 1 << 0,
	afpAccessWrite = 1 << 1,
	afpAccessDenyRead = 1 << 4,
	afpAccessDenyWrite = 1 << 5
	} AFPAccessMode;

typedef enum {
	afpAccessOwnerSearch = 1 << 0,
	afpAccessOwnerRead = 1 << 1,
	afpAccessOwnerWrite = 1 << 2,
	afpAccessGroupSearch = 1 << 8,
	afpAccessGroupRead = 1 << 9,
	afpAccessGroupWrite = 1 << 10,
	afpAccessWorldSearch = 1 << 16,
	afpAccessWorldRead = 1 << 17,
	afpAccessWorldWrite = 1 << 18,
	afpAccessUserSearch = 1 << 24,
	afpAccessUserRead = 1 << 25,
	afpAccessUserWrite = 1 << 26,
	afpAccessUserIsOwner = 1 << 31
	} AFPAccess;

typedef enum {
	afpGetDirAttributes = 1 << 0,
	afpGetDirParDirID = 1 << 1,
	afpGetDirCreationDate = 1 << 2,
	afpGetDirModificationDate = 1 << 3,
	afpGetDirBackupDate = 1 << 4,
	afpGetDirFinderInfo = 1 << 5,
	afpGetDirName = 1 << 6,
	afpGetDirShortName = 1 << 7,
	afpGetDirID = 1 << 8,
	afpGetDirOffspring = 1 << 9,
	afpGetDirOwnerID = 1 << 10,
	afpGetDirGroupID = 1 << 11,
	afpGetDirAccessRights = 1 << 12,
	afpGetDirProDOSInfo = 1 << 13
	} AFPDirParmsSelect;

typedef enum {
	afpGetFileAttributes = 1 << 0,
	afpGetFileParDirID = 1 << 1,
	afpGetFileCreationDate = 1 << 2,
	afpGetFileModificationDate = 1 << 3,
	afpGetFileBackupDate = 1 << 4,
	afpGetFileFinderInfo = 1 << 5,
	afpGetFileName = 1 << 6,
	afpGetFileShortName = 1 << 7,
	afpGetFileID = 1 << 8,
	afpGetFileDataForkLength = 1 << 9,
	afpGetFileRsrcForkLength = 1 << 10,
	afpGetFileProDOSInfo = 1 << 13
	} AFPFileParmsSelect;

typedef struct {
	AFPDirectory	id,
			parID;
	time_t		creationDate,
			modificationDate,
			backupDate;
	char		*name,
			*shortName;
	AFPProDOSInfo	proDOSInfo;
	} AFPCatalogParms;

typedef struct {
	AFPCatalogParms	cat;
	unsigned long	dataForkLength,
			rsrcForkLength;
	struct {
		unsigned int	invisible : 1,
				multiUser : 1,
				system : 1,
				dataAlreadyOpen : 1,
				rsrcAlreadyOpen : 1,
				readOnly : 1,
				backupNeeded : 1,
				renameInhibit : 1,
				deleteInhibit : 1,
				unused0 : 1,
				copyProtect : 1,
				unused1 : 4,
				setClear : 1;
		} attributes;

	/* The definition of the finderInfo is not specified by AFP.
	   In particular, it is conceivable that the directory ID in putAway
	   is not really an AFPDirectory.  Most of these should probably
	   not be touched even under HFS. */
	struct {
		long		type,
				creator;
		struct {
			unsigned int	unused0 : 1,
					invisible : 1,
					bundle : 1,
					unused1 : 12,
					desktop : 1;
			} flags;
		signed int	location[2];
		int		folder;
		int		window;
		int		icon;
		char		unused[8];
		AFPDirectory	putAway;
		} finderInfo;
	} AFPFileParms;

typedef struct {
	AFPCatalogParms	cat;
	unsigned	offspring;
	AFPUserGroup	ownerID, groupID;
	AFPAccess	access;
	struct {
		unsigned int	invisible : 1,
				unused0 : 1,
				system : 1,
				unused1 : 3,
				backupNeeded : 1,
				renameInhibit : 1,
				deleteInhibit : 1,
				unused2 : 7;
		} attributes;
	struct {
		signed int	extent[4];
		struct {
			unsigned int	unused0 : 1,
					invisible : 1,
					bundle : 1,
					unused1 : 12,
					desktop : 1;
			} flags;
		signed int	location[2];
		int		view;
		signed int	scroll[2];
		long		openChain;
		int		unused;
		int		comment;
		AFPDirectory	putAway;
		} finderInfo;
	} AFPDirParms;

typedef union {
	AFPCatalogParms	cat;
	AFPFileParms	file;
	AFPDirParms	dir;
	} AFPFileDirParms;


// parameters for AFPGetSrvrMsg
typedef enum {
	afpSrvrMsgLogIn,
	afpSrvrMsgServer
	} AFPSrvrMsgType;

typedef enum {
	afpSelectSrvrMsg = 1 << 8
	} AFPSrvrMsgSelect;

typedef struct {
	AFPSrvrMsgType	type;
	char		*message;
	} AFPSrvrMsg;


/*

	functions

*/

// server

AFPError AFPGetSrvrInfo(struct sockaddr_at *server, struct iovec *reply, AFPSrvrInfo*);
AFPError AFPGetSrvrParms(struct AFP*, struct iovec *reply, time_t *serverTime, char *volume[], unsigned numVolumes);
struct AFP *AFPLogin(const struct sockaddr_at *server, const AFPSrvrInfo*, const char *username, const char *password);
void AFPLogout(struct AFP*);
AFPError AFPGetSrvrMsg(struct AFP*, struct iovec *replyv, AFPSrvrMsgType, AFPSrvrMsgSelect, AFPSrvrMsg*);


/* volume */

AFPVolume AFPOpenVol(struct AFP*, const char *name, const char password[8], struct iovec *response, AFPVolParms*, AFPVolParmsSelect);
AFPError AFPCloseVol(struct AFP*, AFPVolume);
AFPError AFPFlush(struct AFP*, AFPVolume);
AFPError AFPGetVolParms(struct AFP*, AFPVolume, struct iovec *replyv, AFPVolParms*, AFPVolParmsSelect);
AFPError AFPSetVolParms(struct AFP*, AFPVolume, const AFPVolParms *, AFPVolParmsSelect);


/* directory */

AFPDirectory AFPCreateDir(struct AFP*, AFPVolume, AFPDirectory, const char *name);
AFPDirectory AFPOpenDir(struct AFP*, AFPVolume, AFPDirectory, const char *path);
AFPError AFPCloseDir(struct AFP*, AFPVolume, AFPDirectory);
unsigned AFPEnumerate(struct AFP*, struct iovec *replyv, AFPVolume, AFPDirectory, const char *path, AFPFileParmsSelect, AFPDirParmsSelect, AFPFileDirParms[], unsigned firstOffspring, unsigned numOffspring);
AFPError AFPSetDirParms(struct AFP*, AFPVolume, AFPDirectory, const char *pathName, const AFPDirParms*, AFPDirParmsSelect);


/* file */

AFPError AFPCreateFile(struct AFP*, AFPVolume, AFPDirectory, int mayDelete, const char *path);
AFPError AFPDelete(struct AFP*, AFPVolume, AFPDirectory, const char *name);
AFPError AFPGetFileDirParms(struct AFP*, struct iovec *replyv, AFPVolume, AFPDirectory, const char *name, AFPFileParms*, AFPFileParmsSelect, AFPDirParms*, AFPDirParmsSelect);
AFPError AFPSetFileParms(struct AFP*, AFPVolume, AFPDirectory, const char *pathName, const AFPFileParms*, AFPFileParmsSelect);
AFPError AFPCopyFile(struct AFP*, AFPVolume sourceVolume, AFPDirectory sourceDirectory, const char *sourceName, AFPVolume destinationVolume, AFPDirectory destinationDirectory, const char *destinationPath, const char *destinationName);
AFPError AFPRename(struct AFP*, AFPVolume, AFPDirectory, const char *pathName, const char *newName);
AFPError AFPMoveAndRename(struct AFP*, AFPVolume, AFPDirectory, const char *sourcePathName, AFPDirectory destinationDirectory, const char *destinationPath, const char *destinationName);


/* fork */

AFPFork AFPOpenFork(struct AFP*, struct iovec *responsev, AFPVolume, AFPDirectory, AFPAccessMode, const char *path, int resourceFork, AFPFileParms*, AFPFileParmsSelect);
AFPError AFPCloseFork(struct AFP*, AFPFork);
AFPError AFPFlushFork(struct AFP*, AFPFork);
AFPError AFPGetForkParms(struct AFP*, struct iovec *replyv, AFPFork, AFPFileParms*, AFPFileParmsSelect);
AFPError AFPSetForkParms(struct AFP*, AFPFork, const AFPFileParms*, AFPFileParmsSelect);
AFPError AFPRead(struct AFP*, AFPFork, struct iovec *buffer, unsigned long offset);
AFPError AFPWrite(struct AFP*, AFPFork, struct iovec *buffer, signed long offset, int atEnd);

#endif
