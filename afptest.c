/*
	afptest.c
	
	Test suite
	
	Copyright (c) 1996-2026 by: Ben Hekster <heksterb@acm.org>
	Licensed under the MIT License - see LICENSE file for details.
	SPDX-License-Identifier: MIT
	
	Wed Aug 28 19:21:58 IST 1996
*/

#include <asm/byteorder.h>
#include <asm/errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <pwd.h>
#include <unistd.h>
#include <stdio.h>

#include "nbp.h"
#include "rtmp.h"
#include "afp.h"
#include "atp.h"


extern int errno;


static char *strtime(
	time_t		*time
	)
{
char *s = ctime(time);

/* clip off the trailing '\n' */
if (s) s[strlen(s) - 1] = '\0';

return s;
}


/*	main
	Entry point

	afptest [-u userName] [server]
*/
int main(
	int		argc,
	char		*argv[]
	)
{
AFPError err;
char data[atpCmdSize];
struct iovec buffer = { data, sizeof data };
struct AFP *afp;
char *volumes[2];
NBPEntity filter, entity;
AFPSrvrInfo server;
AFPVolume volume;
AFPDirectory directory, subDirectory;
AFPFork fork;
unsigned long dataForkLength;
const char *serverName = NULL, *userName = NULL, *password = NULL;

/* parse the options */
while (argv++, --argc > 0) {
	if ((*argv)[0] == '-') switch ((*argv)[1]) {
		case 'u':	if (argc <= 1) return fprintf(stderr, "afptest: -u option requires user name\n"), 1;
				userName = *++argv; --argc;
				break;
		default:	return fprintf(stderr, "afptest: unknown option \"%s\"\n", *argv);
		}

	/* must be the name of the server */
	else {
		if (serverName) return fprintf(stderr, "afptest: can only specify one server\n"), 1;
		serverName = *argv;
		}
	}

/* if a user name was specified, get a password */
if (userName) password = (const char*) getpass("Enter the server password: ");

/* parse the argument into an address */
if (!NBPParse(&filter, serverName, 0))
	return fprintf(stderr,
		"afptest: can't parse \"%s\"\n"
		"\tIt should be of the form server:AFPServer\n",
		serverName), 1;

/* type must be AFP server */
if (!filter.type[0])
	(void) strcpy(filter.type, "AFPServer");
else if (strcmp(filter.type, "AFPServer") != 0)
	return fprintf(stderr, "afptest: can't connect to entity of type \"%s\" other than \"AFPServer\"\n", filter.type), 1;

{
	int s;
	char found[99];
	struct timeval time, timeout;
	struct sockaddr_at local;

	/* open a bound socket */
	if ((s = socket(PF_APPLETALK, SOCK_DGRAM, 0)) < 0)
		return fprintf(stderr, "afptest: can't open socket (%d)\n", errno), 1;
	local.sat_family = AF_APPLETALK;
	local.sat_addr.s_net = ATADDR_ANYNET;
	local.sat_addr.s_node = ATADDR_ANYNODE;
	local.sat_port = ATADDR_ANYPORT;
	if (bind(s, (struct sockaddr*) &local, sizeof local) < 0) {
		fprintf(stderr, "afptest: can't bind socket (%d)\n", errno);
		
		/* I think I'm close to figuring out what the dependency on netatalk is.
		   EADDRNOTAVAIL is returned if `bind' finds no `AppleTalk interfaces'.
		   There apparently is an ioctl to do that, and that's probably what atalkd
		   does for me now.  So I just need to do it myself and then I will be rid
		   of netatalk altogether. */
		if (errno == EADDRNOTAVAIL) fprintf(stderr, "  Is netatalk installed?\n");
		return 1;
		}

	/* see if there is a router */ {
		unsigned network;
		struct sockaddr_at router;
		if (RTMPRequest(s, &network, &router))
			fprintf(stderr, "no router found\n");
		
		else {
			fprintf(stderr, "found router (%hu@%hu) declaring our network %u\n",
				router.sat_addr.s_node,
				ntohs(router.sat_addr.s_net),
				network
				);
			
			/* AppleTalk network numbers in nonrouter nodes are supposed to be obtained
			   dynamically by an RTMP stub.  Unfortunately, netatalk apparently has no
			   way of obtaining its own network number other than by hard-coding in its
			   configuration.  If the network number it picks is not the network number
			   our router says we are on, routing does not work.  I can't imagine why
			   there should be any network-theoretical objection to this working, though I
			   can hypothesize some.  In any case, it is clear empirically that it doesn't,
			   so the user should probably fix this. */
			}
		}

	/* look up a server */
	if (NBPLookup(s, &entity, 1, &filter) < 1)
		return fprintf(stderr, "afptest: can't find entity (%d)\n", errno), 1;
	(void) NBPExpress(found, &entity);
	fprintf(stderr, "found \"%s\" at %hu@%hu\n",
		found,
		entity.address.sat_addr.s_node,
		ntohs(entity.address.sat_addr.s_net)
		);

	/* ping the node */
	if (AEPRequest(s, &entity.address.sat_addr, &time))
		return fprintf(stderr, "afptest: no response (%d)\n", errno), 1;
	fprintf(stderr, "responded in %u ms\n", time.tv_usec / 1000 + time.tv_sec * 1000);

	close(s);
	}

/* get server information without a connection */ {
	unsigned i;
	char *v;

	if (err = AFPGetSrvrInfo(&entity.address, &buffer, &server)) {
		fprintf(stderr, "main: can't get server information %d\n", err);
		return 1;
		}

	fprintf(stderr, "server \"%s\", type \"%s\", %s\n",
		server.name, server.type, server.icon ? "has icon" : "no icon"
		);

	/* print the supported versions */
	if (server.versions[0][0]) fputc('\t', stderr);
	for (i = sizeof server.versions / sizeof *server.versions, v = *server.versions; i > 0; i--, v += sizeof *server.versions)
		if (*v) fprintf(stderr, "%s%s", v, (v + sizeof *server.versions)[0] ? ", " : "\n");
		else break;

	/* print the supported authentication methods */
	if (server.versions[0][0]) fputc('\t', stderr);
	for (i = sizeof server.uam / sizeof *server.uam, v = *server.uam; i > 0; i--, v += sizeof *server.uam)
		if (*v) fprintf(stderr, "%s%s", v, (v + sizeof *server.uam)[0] ? ", " : "\n");
		else break;
	}

/* log in */
if (! (afp = AFPLogin(&entity.address, &server, userName, password))) {
	switch (errno) {
		case afpBadUAM:	fprintf(stderr, "afptest: can't log in because server disallows guest access\n"); break;
		default:	fprintf(stderr, "afptest: can't log in (%d)\n", errno); break;
		}
	return 1;
	}

/* get the volume names */ {
	int i;
	char **v = volumes;
	time_t serverTime;
	char *serverTimeString;

	buffer.iov_len = sizeof data;
	if (err = AFPGetSrvrParms(afp, &buffer, &serverTime, volumes, sizeof volumes / sizeof *volumes)) {
		fprintf(stderr, "main: can't get AFP server parameters (%d)\n", err);
		return 1;
		}
	
	/* print the volume names */
	fprintf(stderr, "server %s, available volumes: ", strtime(&serverTime));
	for (i = 0; i < sizeof volumes / sizeof *volumes; i++, v++)
		if (*v) fprintf(stderr, *v);
		else break;
	fputc('\n', stderr);
	}

/* open the first volume */ {
	AFPVolParms params;
	buffer.iov_len = sizeof data;
	if (volume = AFPOpenVol(
		afp, volumes[0], NULL, &buffer, &params,
		afpGetVolID |
		afpGetVolAttributes | afpGetVolSignature |
		afpGetVolCreationDate | afpGetVolModificationDate | afpGetVolBackupDate |
		afpGetVolBytesFree | afpGetVolBytesTotal |
		afpGetVolName
		), errno)
		return fprintf(stderr, "can't open volume (%d)\n", errno), 1;
	
	fprintf(stderr, "opened volume ID %d\n"
		"\tattributes %d, signature %d\n"
		"\tcreation %#lx, modification %#lx, backup %#lx\n"
		"\tbytes free %lu, total %lu\n",
		params.id,
		params.attributes, params.signature,
		params.creationDate, params.modificationDate, params.backupDate,
		params.bytesFree, params.bytesTotal
		);
	}

/* open a directory */ {
	if (! (directory = AFPOpenDir(afp, volume, 2, "Playground"))) {
		if (errno == afpObjectNotFound)
			return fprintf(stderr, "main: Please create a folder named `Playground' at the root of the shared volume for this test\n"), 0;

		else
			return fprintf(stderr, "main: can't open directory (%d)\n", errno), 1;
		}

	fprintf(stderr, "opened directory %lu\n", directory);
	}

/* create a directory */
if (! (subDirectory = AFPCreateDir(afp, volume, directory, "subdirectory")))
	if (errno != afpObjectExists) {
		fprintf(stderr, "main: can't create directory (%d)\n", errno);
		return 1;
		}

/* get directory parameters */ {
	AFPDirParms params;
	unsigned n;
	AFPFileDirParms offspring[4];

	buffer.iov_len = sizeof data;
	if (err = AFPGetFileDirParms(
		afp, &buffer, volume, directory, NULL,
		NULL, 0, &params,
		afpGetDirID |
		afpGetDirAttributes | afpGetDirParDirID | afpGetDirOffspring |
		afpGetDirCreationDate | afpGetDirModificationDate | afpGetDirBackupDate |
		afpGetDirName | afpGetDirShortName |
		afpGetDirOwnerID | afpGetDirGroupID | afpGetDirAccessRights |
		afpGetDirFinderInfo
		)) {
		fprintf(stderr, "main: can't get directory parameters (%d)\n", err);
		return 1;
		}

	fprintf(stderr, "got directory parameters for %ld\n"
		"\tattributes %d, parent %ld, valence %d\n"
		"\tcreation %#lx, modification %#lx, backup %#lx\n"
		"\tname \"%s\", short name \"%s\"\n"
		"\towner %ld, group %ld, access %d\n",
		params.cat.id,
		params.attributes, params.cat.parID, params.offspring,
		params.cat.creationDate, params.cat.modificationDate, params.cat.backupDate,
		params.cat.name, params.cat.shortName,
		params.ownerID, params.groupID, params.access
		);

	/* list directory contents */
	buffer.iov_len = sizeof data;
	if (!
		(n = AFPEnumerate(
			afp, &buffer, volume, directory, NULL,
			afpGetFileName | afpGetFileCreationDate | afpGetFileDataForkLength,
			afpGetDirName | afpGetDirCreationDate | afpGetDirOffspring,
			offspring, 0,
			(params.offspring < sizeof offspring / sizeof *offspring) ? params.offspring : (sizeof offspring / sizeof *offspring)
			)) &&
		errno
		)
		fprintf(stderr, "main: can't enumerate directory (%d)\n", errno);

	else {
		unsigned i;
		fprintf(stderr, "enumerated %u items:\n", n);
		for (i = 0; i < n; i++)
			fprintf(stderr, "\t%-16s %s %u\n",
				offspring[i].cat.name,
				strtime(&offspring[i].cat.creationDate),
				offspring[i].file.dataForkLength
				);
		}
	}

/* open a file */
{
	AFPFileParms params;
	buffer.iov_len = sizeof data;
	if (fork = AFPOpenFork(
		afp, &buffer,
		volume, directory, afpAccessRead, "hello", 0,
		&params,
		afpGetFileAttributes | afpGetFileID | afpGetFileParDirID |
		afpGetFileCreationDate | afpGetFileModificationDate | afpGetFileBackupDate |
		afpGetFileFinderInfo |
		afpGetFileName | afpGetFileShortName |
		afpGetFileDataForkLength
		), errno) {
		if (errno == afpObjectNotFound)
			return fprintf(stderr, "please create a text file named `hello' in the `Playground' directory\n"), 0;
		else
			return fprintf(stderr, "main: can't open fork (%d)\n", errno), 1;
		}
	
	fprintf(stderr, "opened fork refnum %d\n"
		"\tattributes %d, fileID %lu, parID %lu\n"
		"\tcreation %#lx, modification %#lx, backup %#lx\n"
		"\tname \"%s\", short name \"%s\"\n"
		"\tlength %lu\n",
		fork,
		params.attributes, params.cat.id, params.cat.parID,
		params.cat.creationDate, params.cat.modificationDate, params.cat.backupDate,
		params.cat.name, params.cat.shortName, params.dataForkLength
		);

	dataForkLength = params.dataForkLength;
	}

/* read from the file */
{
	buffer.iov_len = dataForkLength < sizeof data ? dataForkLength : sizeof data;
	if (err = AFPRead(afp, fork, &buffer, 0))
		fprintf(stderr, "main: can't read from file (%d)\n", err);
	
	else {
		fprintf(stderr, "read %lu bytes from file:\n", buffer.iov_len);
		fwrite(buffer.iov_base, 1, buffer.iov_len, stderr);
		fputc('\n', stderr);
		}
	}

/* close the file */
if (err = AFPCloseFork(afp, fork))
	fprintf(stderr, "main: can't close fork (%d)\n", err);

/* create a file */
if (err = AFPCreateFile(afp, volume, directory, 1, "file")) {
	fprintf(stderr, "main: can't create file (%d)\n", err);
	return 1;
	}

/* copy it */
if (err = AFPCopyFile(afp,
	volume, directory, "file",
	volume, directory, "", "copy"
	))
	if (err != afpObjectExists) {
		fprintf(stderr, "main: can't copy file (%d)\n", err);
		return 1;
		}

/* open it for writing */
if (fork = AFPOpenFork(afp, &buffer, volume, directory, afpAccessRead | afpAccessWrite, "file", 0, NULL, 0), errno) {
	fprintf(stderr, "main: can't open file for write access (%d)\n", errno);
	return 1;
	}

/* set its parameters */
{
	AFPFileParms params;

	buffer.iov_len = sizeof data;
	if (err = AFPGetForkParms(afp, &buffer, fork, &params, afpGetFileFinderInfo))
		fprintf(stderr, "main: can't get fork parameters (%d)\n", err);

	else {
		params.cat.creationDate = 0;
		params.finderInfo.type = (('T' << 8 | 'E') << 8 | 'X') << 8 | 'T'; // text file
		params.finderInfo.creator = (('t' << 8 | 't') << 8 | 'x') << 8 | 't'; // creator TeachText
		if (err = AFPSetFileParms(afp, volume, directory, "file", &params, afpGetFileCreationDate | afpGetFileFinderInfo))
			fprintf(stderr, "main: can't set fork parameters (%d)\n", err);
		}
	}

/* write to the file */
{
	sprintf(buffer.iov_base, "I can write!");
	buffer.iov_len = strlen(buffer.iov_base);
	if (err = AFPWrite(afp, fork, &buffer, 0, 0))
		fprintf(stderr, "main: can't write to file (%d)\n", err);

	else
		fprintf(stderr, "wrote %d bytes to fork\n", buffer.iov_len);
	}

/* move it to the subdirectory */
if (err = AFPMoveAndRename(
	afp, volume, directory, "copy", subDirectory, NULL, "move"
	))
	fprintf(stderr, "main: can't move/rename file (%d)\n", err);

/* rename it */
if (err = AFPRename(afp, volume, subDirectory, "move", "rename"))
	fprintf(stderr, "main: can't rename file (%d)\n", err);

/* flush the file */
if (err = AFPFlushFork(afp, fork))
	fprintf(stderr, "main: can't flush fork (%d)\n", err);

/* close the file */
if (err = AFPCloseFork(afp, fork))
	fprintf(stderr, "main: can't close fork (%d)\n", err);

/* delete the file */
if (err = AFPDelete(afp, volume, subDirectory, "rename"))
	fprintf(stderr, "main: can't delete file (%d)\n", err);

/* delete the directory */
if (subDirectory)
	if (err = AFPDelete(afp, volume, subDirectory, NULL))
		fprintf(stderr, "main: can't delete directory (%d)\n", err);

/* close the directory */
if (err = AFPCloseDir(afp, volume, directory))
	fprintf(stderr, "main: can't close directory (%d)\n", err);

/* flush the volume */
if (err = AFPFlush(afp, volume))
	fprintf(stderr, "main: can't flush volume (%d)\n", err);

/* close the volume */
if (err = AFPCloseVol(afp, volume))
	fprintf(stderr, "main: can't unmount volume (%d)\n", err);

/* log out */
AFPLogout(afp);

return 0;
}
