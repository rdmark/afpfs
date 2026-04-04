/*
	asp.c
	
	AppleTalk Session Protocol, Workstation
	Copyright (c) 1996-1997 by: Ben Hekster <heksterb@acm.org>
	
	Tue Oct 22 11:52:08 IST 1996
	
	"Damcha hit'arev badami
	Veani bechalomcha overet
	Eifoh atah matchil
	veeifoh ani nigmeret?"
	--Ha'im atah meushar, Rita
*/

#include <errno.h>

#include <asm/byteorder.h>

#include <sys/types.h>
#include <linux/types.h>
#ifdef __KERNEL__
struct sk_buff;
#include <linux/if_ether.h>
#endif
#include <linux/atalk.h>
#include <sys/uio.h>

#include <stdio.h>

#include "atp.h"
#include "asp.h"
#include "mac.h"


extern int errno;


/* ASP function codes */
enum ASPRequest {
	aspCloseSession = 1, aspCommand, aspGetStatus, aspOpenSess,
	aspTickle, aspWrite, aspWriteContinue, aspAttention
	};


/*	ASP
	ASP session data structure
*/
struct ASP {
	struct ATP	*atp;		// ATP Workstation Session Socket
	
	struct sockaddr_at
			sls,		// Server Listening Socket
			sss;		// Server Session Socket
					// if the sat_port field is 0, the session is not open
	__s8		sessionID;	// server session ID
	__s16		sequence;	// command sequence number
	
	ATPTransaction	tickle;		// session tickle
	time_t		lastTickle;	// when last tickle was received from server
	
	/* An ASP workstation session needs to receive four different server
	   requests: Tickle, CloseSess, Attention, and WriteContinue.  Only the
	   latter returns any ATP data, so this is all we need to accept. */
	ATPTransaction	listen;		// listen to server transactions
	struct sockaddr_at listenAddress;
	struct iovec	listenv;
	__s16		listenData;	// two bytes of packet received on listen
	void		(*Attention)(struct ASP*, int attentionCode);
	};


/*	ReceiveSessionRequest
	Handle server-initiated session requests
*/
static ASPError ReceiveSessionRequest(
	struct ASP	*asp
	)
{
int err = 0;

// *** should I also check the sessionID, address, sequence number?
if (!asp->listen.err) switch (asp->listen.requestUserBytes >> 24) {
	case aspCloseSession:
		// reply session is closed
		if (err = ATPSendResponse(asp->atp, &asp->listen, NULL, 0));
			(void) ATPWait(asp->atp, &asp->listen);
		
		// abort all pending transactions
		(void) ATPAbort(asp->atp, NULL);
		
		// mark the session closed
		asp->sss.sat_port = 0;
		
		break;
	
	case aspTickle:
		asp->lastTickle = time(NULL);
		break;
	
	case aspWriteContinue:
		/* Hopefully, this is being called only after ASPWrite already handled the
		   request.  *** Therefore, this case shouldn't be here but in ASPWrite. */
		break;
	
	case aspAttention:
		// acknowledge
		if (err = ATPSendResponse(asp->atp, &asp->listen, NULL, 0));
			(void) ATPWait(asp->atp, &asp->listen);
		
		// notify our user
		if (asp->Attention) (*asp->Attention)(asp, (__s16) asp->listen.requestUserBytes);
		
		break;
	
	#ifdef DEBUG
	default:
		printk("asp: ignoring server request %d\n", asp->listen.requestUserBytes);
		break;
	#endif
	}

// listen for the next request
asp->listenv.iov_base = &asp->listenData;
asp->listenv.iov_len = sizeof asp->listenData;
return ATPReceiveRequest(asp->atp, &asp->listen, &asp->listenAddress, &asp->listenv, NULL);
}


/*	ASPGetParms
	Return ASP protocol parameters
*/
void ASPGetParms(
	unsigned int	*maxCmdSize,	// maximum size of a command block
	unsigned int	*quantumSize	// maximum size of a command or write
	)
{
/* this implementation of ASP is built on top of ATP */
if (maxCmdSize) *maxCmdSize = atpCmdSize;
if (quantumSize) *quantumSize = atpQuantumSize;
}


/*	ASPGetStatus
	Get server status information
*/
ASPError ASPGetStatus(
	const struct sockaddr_at *server, /* server address */
	const struct iovec *request,	/* request, or NULL */
	struct iovec	*response	/* returned information */
	)
{
ASPError err = 0;

/* open an ATP port */
struct ATP *const atp = ATPOpen(0);
if (!atp) goto Abort;

/* send the server information request (user bytes are always zero in the reply)*/
if (err = ATPSendRequest(
	atp, NULL, server, 0, 16, 4,
	aspGetStatus << 24, request, NULL, response
	)) goto AbortClose;

/* close the ATP port */
AbortClose:
ATPClose(atp);

Abort:
return err;
}


/*	ASPOpenSession
	Open an ASP session
	Returns the opened ASP session, or NULL
*/
struct ASP *ASPOpenSession(
	const struct sockaddr_at *server, /* Server Listening Socket */
	void		(*Attention)(struct ASP*, int attentionCode)
	)
{
__s32 reply;

// create the ASP session struct
struct ASP *const asp = (struct ASP*) malloc(sizeof(struct ASP)); if (!asp) goto Abort;

// open an ATP socket for the session
if (! (asp->atp = ATPOpen(0))) goto AbortClose;

asp->sss.sat_port = 0;			// didn't open session yet
asp->listen.atp = NULL;			// didn't start listening yet
asp->tickle.atp = NULL;			// didn't start tickling yet

// send the OpenSess request and wait for the OpenSessReply response
asp->sls = *server;
if (errno = ATPSendRequest(
	asp->atp, NULL, &asp->sls, atpXO, 16, 4,
	(aspOpenSess << 24) | (ATPSocket(asp->atp) << 16) | 0x0100,
	NULL, &reply, NULL
	)) goto AbortClose;
if (errno = (__s16) reply) goto AbortClose;

asp->sss = *server;			// remember the Server Session Socket
	asp->sss.sat_port = (__u8) (reply >> 24);
asp->sessionID = (__u8) (reply >> 16);
asp->sequence = 0;

// listen to server-initiated transactions
asp->listen.err = 1;			// ugly trick to cause ReceiveSessionRequest to ignore a request we haven't received yet
if (errno = ReceiveSessionRequest(asp)) goto AbortClose;

// start the session tickler
if (errno = ATPSendRequest(
	asp->atp, &asp->tickle, &asp->sls, 0, 30, -1,
	(aspTickle << 24) | (asp->sessionID << 16),
	NULL, NULL, NULL
	)) goto AbortClose;

// notify user of server attention requests
asp->Attention = Attention;

Abort:
return asp;

AbortClose:
ASPCloseSession(asp);
return NULL;
}


/*	ASPCloseSession
	Close an ASP session
*/
void ASPCloseSession(
	struct ASP	*asp		/* session to close */
	)
{
/* socket is open? */
if (asp->atp) {
	/* session is open? */
	if (asp->sss.sat_port) {
		ATPTransaction close;
		
		/* send the CloseSess request */
		if (!ATPSendRequest(
			asp->atp, &close, &asp->sss, 0, 16, 4,
			(aspCloseSession << 24) | (asp->sessionID << 16),
			NULL, NULL, NULL
			))
			/* await the CloseSessReply response */
			while (close.atp)
				// handle server requests
				if (ATPWait(asp->atp, NULL) == &asp->listen)
					/* We still need to do this, because the server may have initiated
					   a Close before we did, in which case it won't reply to ours. */
					(void) ReceiveSessionRequest(asp);
		}
	
	/* stop tickling the session */
	if (asp->tickle.atp) (void) ATPCancel(&asp->tickle);
	
	/* stop listening to server commands */
	if (asp->listen.atp) (void) ATPCancel(&asp->listen);
	
	/* close the ATP socket */
	ATPClose(asp->atp);
	}

/* free the ASP session structure */
free(asp);
}


/*	ASPCommand
	Send a command to a server

	command is the command to send.  Note that because the ASP
	request is sent in a single ATP packet, its size may not exceed
	atpCmdSize (as returned by ASPGetParms).

	reply is the buffer in which the command reply is returned, or
	NULL.  Note that at most atpQuantumSize (as returned by
	ASPGetParms) bytes are returned.
*/
ASPError ASPCommand(
	struct ASP	*asp,
	const struct iovec *command,
	struct iovec	*reply		/* reply, or NULL */
	)
{
ATPTransaction request;

// session not open?
if (!asp->sss.sat_port) return aspSessClosed;

/* send the Command request */
if (ATPSendRequest(
	asp->atp, &request, &asp->sss, atpXO, 16, 4,
	(aspCommand << 24) | (asp->sessionID << 16) | asp->sequence,
	command, &request.responseUserBytes, reply
	))
	return request.err;

// while the request is not complete
while (request.atp)
	// handle server-initiated requests
	if (ATPWait(asp->atp, NULL) == &asp->listen) ReceiveSessionRequest(asp);
if (request.err) return request.err;

/* transaction was successful */
asp->sequence++;

/* return the ASP command result */
return request.responseUserBytes;
}


/*	ASPWrite
	Write data to a server
	The number of bytes actually written is returned in data->iov_len
NOTE
	There is no provision to receive the server WriteReply
	command reply data-- there doesn't seem to be a need for it
*/
ASPError ASPWrite(
	struct ASP	*asp,
	struct iovec	*command,	/* command */
	struct iovec	*data		/* data to write */
	)
{
ASPError err;
ATPTransaction request;
struct sockaddr_at address;

// session not open?
if (!asp->sss.sat_port) return aspSessClosed;

// send the Write request
if (err = ATPSendRequest(
	asp->atp, &request, &asp->sss, atpXO, 16, 4,
	(aspWrite << 24) | (asp->sessionID << 16) | asp->sequence,
	command,
	&request.responseUserBytes,		// *** ATP API bug, need to put something here to get the response
	NULL
	)) return err;

// while the Write request is not complete
while (request.atp)
	// received a request?
	if (ATPWait(asp->atp, NULL) == &asp->listen) {
		// was WriteContinue?
		if (
			!asp->listen.err &&
			(asp->listen.requestUserBytes >> 24) == aspWriteContinue
			) {
			// get the server available buffer size
			const unsigned int bufferSize =
				// received the available buffer size in the WriteContinue request?
				asp->listenv.iov_len >= sizeof(__s16) ?
					ntohs(asp->listenData) :
					/* If we received a malformed WriteContinue, send an empty reply since
					   there is no way to signal an error. */
					0;
			
			// send only up to the amount of data requested
			if (bufferSize < data->iov_len) data->iov_len = bufferSize;
			
			// send the data to write in the response
			// *** a deficiency in the ATP API makes it impossible to respond synchronously
			if (
				// couldn't queue the response?
				(err = ATPSendResponse(asp->atp, &asp->listen, data, 0)) ||
				
				// couldn't complete the response?
				(ATPWait(asp->atp, &asp->listen), err = asp->listen.err)
				)
				// abort the Write request
				ATPAbort(asp->atp, &request);
			}
		
		ReceiveSessionRequest(asp);
		}

// transaction completed
asp->sequence++;

/* note we don't try to get the WriteReply command reply data, mainly because AFP doesn't need it */
Abort:
if (
	err ||					// error in WriteContinueReply?
	(err = request.err) ||			// error in Write transaction?
	(err = request.responseUserBytes)	// error in Write request?
	) return err;

return 0;
}


/*	ASPIsSessionOpen
	Return whether the session is open
	A session may be closed asynchronously by the server
*/
int ASPIsSessionOpen(
	struct ASP	*asp
	)
{
return asp->sss.sat_port != 0;
}

