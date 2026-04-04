/*
	afpmount

	Mount afpfs volumes
	Copyright (c) 1996-1997 by: Ben Hekster <heksterb@acm.org>

	Thu Jan  9 16:11:29 IST 1997

	"I'm just trying not to die like a dog
	End up alone, wheezing from the smog
	I'm just trying not to die in vain
	Got to leave my mark, stake my claim"
	--Die Like a Dog, Curve;  Cherry EP
*/

#include <asm/types.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <linux/atalk.h>

// following needed `ben-linux' for MS_MGC_VAL, not on `winblowz'
#include <linux/fs.h>

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include "afpfs.h"
#include "nbp.h"
#include "atp.h"
#include "mac.h"


extern int errno;

const char gUsage[] =
	"usage: afpmount [-u username [-p password]] mount-point server[@zone] volume\n"
	"	mount-point	where to mount the volume\n"
	"	server		name of AppleShare server\n"
	"	zone		AppleShare zone where server is located, else default zone\n"
	"	volume		name of volume to mount\n"
	/* "	-s		don't mount, but list available servers\n" */
	/* "	-v		don't mount, but list available volumes\n" */
	"	-u		mount as specified AppleShare user, else as `Guest'\n"
	"	-p		with '-u', password to log in under (will prompt if not specified)\n";


/*	main
	Command-line interface
	afpmount [-v] [-u username] mount-point server[@zone] [volume]
*/
int main(
	int		argc,
	const char	*argv[]
	)
{
int err;
FILE *mtab;
struct AFPFSMount params = { sizeof params };
NBPEntity search, entity;
int addresslen = sizeof entity.address;
bool
	doMount = true,				// mount volume
	doListServers = false,			// list servers on network
	doListVolumes = false;			// list volumes on server
const char
	*mountp = NULL,
	*server = NULL, *volume = NULL,
	*username = NULL, *password = NULL;

// parse options
while (++argv, --argc > 0)
	if ((*argv)[0] == '-') switch ((*argv)[1]) {
		/* mount under specific user name */
		case 'u':	if (argc <= 1) return fprintf(stderr, "afpmount: '-u' requires user name\n"), 1;
				if (username) return fprintf(stderr, "afpmount: '-u' can only be specified once\n"), 1;
				username = (--argc, *++argv);
				break;
		
		case 'p':	if (argc <= 1) return fprintf(stderr, "afpmount: '-p' requires password\n"), 1;
				if (password) return fprintf(stderr, "afpmount: '-p' can only be specified once\n"), 1;
				password = (--argc, *++argv);
				break;
		
		#if 0
		/* get list of servers */
		case 's':	doMount = false; doListServers = true; break;
		
		/* get list of volumes */
		case 'v':	doMount = false; doListVolumes = true; break;
		#endif
		
		default:	return fprintf(stderr, "afpmount: unknown option \"%s\"\n", *argv), 1;
		}
	
	else
		break;

/* the next three arguments should be the mount point, the server name, and the volume name */
if (argc > 0) mountp = (--argc, *argv++);
if (argc > 0) server = (--argc, *argv++);
if (argc > 0) volume = (--argc, *argv++);

/* parse the server argument into an address */
if (!server) return fprintf(stderr, gUsage), 1;
if (!NBPParse(&search, server, 0))
	return fprintf(stderr,
		"afpmount: can't parse \"%s\"\n"
		"\tIt should be of the form server[@zone]\n",
		server
		), 1;

// don't allow user to specify type
if (search.type[0])
	return fprintf(stderr, "afpmount: entity type specification \"%s\" not permitted\n", search.type), 1;
(void) strcpy(search.type, "AFPServer");

// if a user name was specified, get a password
if (username && !password && doMount) password = (char*) getpass("Enter the server password: ");

// open the network socket
if ((params.fd = socket(AF_APPLETALK, SOCK_DGRAM, 0)) < 0) {
	fprintf(stderr, "afpmount: can't create socket (%d)\n", params.fd);
	return 1;
	}

// find the AppleShare server
if (NBPLookup(params.fd, &entity, 1, &search) < 1) {
	fprintf(stderr, "afpmount: can't find server (%d)\n", errno);
	goto AbortClose;
	}
params.server = entity.address;

/* get the server information */ {
	char data[atpCmdSize];
	struct iovec buffer = { data, sizeof data };

	if (err = AFPGetSrvrInfo(&params.server, &buffer, &params.info)) {
		fprintf(stderr, "afpmount: can't get server information (%d)\n", err);
		goto AbortClose;
		}
	if (username) strncpy(params.username, username, sizeof params.username); else params.username[0] = '\0';
	if (password) strncpy(params.password, password, sizeof params.password); else params.password[0] = '\0';
	}

// list available volumes
if (doListVolumes) {
	}

// mount the file system
if (doMount) {
	/* return specific mount errors */
	AFPFSError mountErr = 0;
	params.error = &mountErr;
	
	if (!mountp || !volume) { return fprintf(stderr, gUsage), 1; }
	strncpy(params.volume, volume, sizeof params.volume);
	if ((err = mount(volume, mountp, "afpfs", MS_MGC_VAL, &params)) < 0) {
		/* try to find out what really went wrong */
		const char *message = NULL;
		char messages[128];
		switch (mountErr) {
			/* never got to mount? */
			case 0:
				/* are we root? */
				/* mount apparently returns -EPERM whenever afpfs can't read the super block */
				if (err == -EPERM && geteuid() != 0)
					message = "probably need to be user `root' to mount volumes";
				else
					sprintf(messages, "can't mount file system (%d) (is the kernel module installed?)", err);
				
				break;
			
			/* mount-specific error */
			case afpfsNoSuchVolume:
				sprintf(messages, "probably volume \"%s\" not available", params.volume);
				break;
			case afpfsNoSuchUser:
				sprintf(messages, "unknown user \"%s\"", params.username);
				break;
			case afpfsNoSuchPassword:
				message = "incorrect password";
				break;
			
			/* generic AppleTalk error */
			default:
				sprintf(messages, "unexpected AppleTalk error (%d)", mountErr);
				break;
			}
		
		fprintf(stderr, "afpmount: %s\n", message ? message : messages);
		goto AbortClose;
		}
	
	/* record the mounted volume in mtab */
	if (mtab = fopen("/etc/mtab", "a")) {
		fprintf(mtab, "none\t%s\tafpfs\trw\t0\t0\n", mountp);
		fclose(mtab);
		}
	}

/* close the socket */
AbortClose:
(void) close(params.fd);

return err;
}

