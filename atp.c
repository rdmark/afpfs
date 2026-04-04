/*
	atp.c

	AppleTalk Transaction Protocol
	Copyright (c) 1996-1997 by: Ben Hekster <heksterb@acm.org>

	Sun Oct 27 14:14:19 IST 1996

NOTE
	I might have used netatalk's ATP implementation but decided to
	roll my own.  First of all, there were a number of things I
	didn't like about the implementation.  For example, it greatly
	simplifies processing in higher layers if ATP reassembles the
	response packets.  Also, protocol layers above our direct users
	should not have to be confronted with ATP implementation details
	such as user bytes [IAT 9-14].  Also, I would need to do substantial
	reworking on it anyway as it relies on libc which I cannot use
	in kernel code.  Also, since only the server sides of ASP and
	AFP are implemented, they are useless and it seemed a bit too
	much to support it for just their implementation of ATP.

NOTE
	Because the user bytes are passed as an integer parameter, the
	caller does not have to do endian-conversion on them.

	"Children waiting for the day the feel good
	`Happy birthday, happy birthday'
	Made to feel the way that every child should
	`Sit and listen, sit and listen'"
	--Mad World, Tears for Fears;  The Hurting
*/

#include <asm/byteorder.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>

#ifdef __KERNEL__
// #include <asm/errno.h>
#include <sys/errno.h>

struct sk_buff;
#include <linux/if_ether.h>
#endif
#include <linux/atalk.h>

#include <stdio.h>
#include "ddp.h"
#include "atp.h"
#include "timer.h"


extern int errno;


/* ATP packet */
struct Packet {
	__s8		protocol;	/* DDP protocol type */
	__s8		control;
	union {
		__s8		bitMap;		/* transaction bit map */
		__s8		sequence;	/* transaction sequence */
		} bs;
	__s16		tid;		/* transaction ID */
	__s32		userBytes;
	__s8		data[atpCmdSize];
	} __attribute__ ((packed));


/* ATP socket */
struct ATP {
	int		ddp;		/* socket descriptor */
	__u16		tid;		/* next request transaction ID */

	Timer		timer;
	ATPTransaction	*queue;		/* pending transactions */
	};



/*	TransactionQueueDump
	Dump the transaction status
*/
static void TransactionQueueDump(
	struct ATP	*atp
	)
{
ATPTransaction *tcb;

printk("ATP queued transactions, socket: %lx:\n", atp);
if (atp) for (tcb = atp->queue; tcb; tcb = tcb->next)
	printk(
		"  %lx: %x %u %d\n",
		tcb, tcb->flags, tcb->tid, tcb->err
		);
}


/*	TransactionSendRequest
	Send outgoing Request packet
*/
static void TransactionSendRequest(
	ATPTransaction	*tcb
	)
{
/* construct the packet header */
/* Note that we do this on each transmission of the packet.  Instead of
   copying and saving the constructed packet or header and wasting
   time and space in a perfectly functioning network, we waste time only
   when the network is not completely efficient, which seems reasonable. */
struct Packet packet = {
	kDDPATP, atpTReq | tcb->flags & atpXO, tcb->bitMap,
	htons(tcb->tid), htonl(tcb->requestUserBytes)
	};
int n;

/* abort transaction that reached retransmission limit */
if (tcb->tries == 0) { tcb->err = atpReqFailed; return; }

/* copy the request into the packet */
if (tcb->request) {
	if (tcb->request->iov_len > sizeof packet.data) { tcb->err = atpSizeErr; return; }
	(void) memcpy(packet.data, tcb->request->iov_base, tcb->request->iov_len);
	}

/* send the request *** should this be nonblocking? */
if ((n = sendto(
	tcb->atp->ddp, &packet,
	sizeof packet - sizeof packet.data + (tcb->request ? tcb->request->iov_len : 0),
	0, (struct sockaddr*) tcb->address, sizeof *tcb->address
	)) < 0)
	tcb->err = n;

/* decrement retransmission count */
/* Note that we also decrement the retransmission counter, even if we were
   called in response to a responder STS-- I think this makes sense. */
if (tcb->tries + 1 != 0) tcb->tries--;	/* don't decrement infinite retransmission counter */

/* reset the timer ***** reschedule */
tcb->expire = time(NULL) + tcb->interval;
}


/*	TransactionSendResponse
	Send outgoing Response packet
*/
static void TransactionSendResponse(
	ATPTransaction	*tcb
	)
{
unsigned int segment;
unsigned int packetLength;
__u8 bitMap;
size_t offset, length;
struct Packet packet = { kDDPATP, (__s8) atpTResp, 0, htons(tcb->tid), 0 };

// abort transaction that reached retransmission limit
/* Actually, there is not really a response retransmission timer.
   If the Release Timer expired, we release the transaction even though
   we didn't receive the requester's Release */
/* *** should we return an error, since the transaction is still considered completed? */
if (tcb->tries == 0) { tcb->err = atpNoRelErr; return; }

/* for each of the segments in the response */
for (
	segment = 0, bitMap = tcb->bitMap,
	offset = 0,
	length = tcb->response ? tcb->response->iov_len : 0;

	/* always a first packet in the response */
	packetLength = length > atpCmdSize ? atpCmdSize : length,
	segment < 8 && (segment == 0 || packetLength > 0);

	segment++, bitMap >>= 1,
	offset += atpCmdSize,
	length -= packetLength
	)
	/* segment requested? */
	if (bitMap & 1) {
		packet.bs.sequence = segment;

		/* put the user bytes in the first segment only */
		packet.userBytes = (segment == 0) ? ntohl(tcb->responseUserBytes) : 0;

		/* set the EOM bit on the last packet */
		if (length < offset + atpCmdSize) packet.control |= atpEOM;

		/* copy the response data into the segment */
		if (tcb->response) (void) memcpy(packet.data, tcb->response->iov_base + offset, packetLength);

		/* send the segment */
		/* ignore any errors [IAT]-- we'll succeed on the next retransmission or time-out */
		(void) sendto(
			tcb->atp->ddp,
			&packet, sizeof packet - sizeof packet.data + packetLength,
			0, (struct sockaddr*) tcb->address, sizeof *tcb->address
			);
		}

/* reset the release timer */
tcb->expire = time(NULL) + tcb->interval;

/* XO transactions pend until the Release is received, ALO completes */
tcb->err = tcb->flags & atpXO ? atpPending : 0;
}


/*	TransactionSendRelease
	Send transaction release packet
*/
static void TransactionSendRelease(
	ATPTransaction	*tcb
	)
{
struct Packet packet = { kDDPATP, (__s8) atpTRel, 0, htons(tcb->tid), 0 };

/* send the release */
if (sendto(
	tcb->atp->ddp, &packet,
	sizeof packet - sizeof packet.data,
	0, (struct sockaddr*) tcb->address, sizeof *tcb->address
	) < 0)
	tcb->err = errno;

/* there is no retry or timer on sending a Release */
tcb->err = 0;
}


/*	TransactionReceiveRequest
	Handle incoming Request packet
*/
static void TransactionReceiveRequest(
	ATPTransaction	*tcb,
	const struct sockaddr_at *address,
	const struct Packet *packet,
	unsigned int	length
	)
{
// construct the Response Control Block
if (tcb->address) *tcb->address = *address;
tcb->tid = ntohs(packet->tid);
tcb->bitMap = packet->bs.bitMap;
tcb->flags |= packet->control & atpXO;
tcb->requestUserBytes = ntohl(packet->userBytes);

// copy the request into the buffer
if (tcb->request) {
	const unsigned int dataLength = length - (sizeof *packet - sizeof packet->data);
	
	/* record the actual number of bytes in the request */
	if (dataLength < tcb->request->iov_len) tcb->request->iov_len = dataLength;
	
	/* copy only up until the number of bytes expected */
	(void) memcpy(tcb->request->iov_base, packet->data, tcb->request->iov_len);
	
	/* fail if the buffer was too small to receive the request */
	if (dataLength > tcb->request->iov_len) { tcb->err = atpBufTooSmall; return; }
	}

// request complete
tcb->err = 0;
}


/*	TransactionReceiveRequestAgain
	Handle incoming Request already responded to
*/
static void TransactionReceiveRequestAgain(
	ATPTransaction	*tcb,
	const struct Packet *packet
	)
{
/* copy the new segment request bit map */
tcb->bitMap = packet->bs.bitMap;
tcb->tries = 1;			/* one reponse attempt for each request */

/* send the response */
TransactionSendResponse(tcb);
}


/*	TransactionReceiveResponse
	Handle incoming Response packet
*/
static void TransactionReceiveResponse(
	ATPTransaction	*tcb,
	const struct Packet *packet,
	unsigned int	length
	)
{
const __u8 sequenceBit = 1 << packet->bs.sequence;
const unsigned int
	offset = packet->bs.sequence * atpCmdSize,
	dataLength = length - (sizeof *packet - sizeof packet->data),
	fixLength =
		/* caller expects a response? */
		tcb->response ?
			/* response buffer can fit first byte of this segment? */
			tcb->response->iov_len >= offset ?
				/* response buffer can fit last byte? */
				(tcb->response->iov_len >= offset + dataLength) ?
					dataLength :
					tcb->response->iov_len - offset :
				0 :
			0;

/* want this segment? */
if (tcb->bitMap & sequenceBit) {
	/* copy the packet into the response buffer */
	if (tcb->response)
		(void) memcpy(tcb->response->iov_base + offset, packet->data, fixLength);

	/* update the transaction bit map */
	tcb->bitMap &=
		/* packet was end of message? */
		(packet->control & atpEOM) ?
			/* clear bits for this and higher-numbered segments */
			sequenceBit - 1 :

			/* clear bit for this segment */
			~sequenceBit;

	/* packet was start of message? */
	if (packet->bs.sequence == 0)
		/* remember the user bytes (user bytes from other segments are undefined) */
		tcb->responseUserBytes = ntohl(packet->userBytes);

	/* packet was end of message? */
	if (packet->control & atpEOM)
		if (tcb->response)
			/* calculate the response length */
			/* note that if we don't receive the EOM packet because our
			   buffer is too small, iov_len already has the correct value */
			tcb->response->iov_len =
				/* each lower numbered segment and this one */
				packet->bs.sequence * atpCmdSize + fixLength;
	}

/* received all segments? */
/* This is in slight contradiction with IAT, which states that we should
   strictly ignore any unexpected response.  The question is whether
   this should extend so far as to not complete the transaction.  It
   doesn't seem meaningful for a requester to continue to pretend that
   the transaction is still pending when the responder has completed it
   already. */
if (!tcb->bitMap) {
	/* XO transaction must be released */
	if (tcb->flags & atpXO) TransactionSendRelease(tcb);

	/* transaction is complete */
	tcb->err = 0;
	}

/* packet had Send Transaction Status bit set? */
if (packet->control & atpSTS)
	/* retransmit the request (*** apparently even when packet->bitMap is 0) */
	TransactionSendRequest(tcb);
}


/*	TransactionReceiveRelease
	Receive incoming Release packet
*/
static void TransactionReceiveRelease(
	ATPTransaction	*tcb
	)
{
// transaction completed successfully
tcb->err = 0;
}


/*	Maintain
	Handle transaction timers
*/
static unsigned int Maintain(
	struct Timer	*timer,
	time_t		now
	)
{
struct ATP *atp = (struct ATP*) ((void*) timer - ((void*) &atp->timer - (void*) atp));
ATPTransaction *tcb;
time_t then = now + 15; // ***
int interval;

// check transactions
for (tcb = atp->queue; tcb; tcb = tcb->next) {
	if (tcb->err == atpPending && now > tcb->expire) {
		switch (tcb->flags & atpTRel) {
			// requests
			case atpTReq:	TransactionSendRequest(tcb); break;

			// responses
			case atpTResp:	TransactionSendResponse(tcb); break;
			}

		if (tcb->err == atpPending && tcb->expire < then) then = tcb->expire - now;
		}
	}

interval = then - now;
return interval > 1 ? interval : 1;			// one-second minimum
}


/*	Completed
	Return a completed transaction, or NULL
*/
static ATPTransaction *Completed(
	struct ATP	*atp,
	ATPTransaction	*tcb
	)
{
if (tcb) {
	if (tcb->err == atpPending) tcb = NULL;
	}

else
	// for all transactions
	for (tcb = atp->queue; tcb; tcb = tcb->next)
		// transaction is completed?
		if (tcb->err != atpPending) break;

// return the completed transaction
return tcb;
}


/*	Receive
	Receive a packet and return its transaction, or NULL
*/
static ATPTransaction *Receive(
	struct ATP	*atp,
	const struct sockaddr_at *address,
	const struct Packet *packet,
	unsigned int	length
	)
{
ATPTransaction *tcb = NULL;
const __u16 tid = (__u16) ntohs(packet->tid);

/* find a transaction corresponding to this packet */
switch (packet->control & atpTRel) {
	/* request? */
	case atpTReq:
		/* is it a request we already responded to? */
		for (tcb = atp->queue; tcb; tcb = tcb->next)
			if ((tcb->flags & atpTRel) == atpTResp && tcb->err == atpPending && tid == tcb->tid) {
				TransactionReceiveRequestAgain(tcb, packet); break; }
			
			/* is it a request we received already? */
			else if ((tcb->flags & atpTRel) == atpTRel && tcb->err == 0 && tid == tcb->tid) {
				TransactionReceiveRequestAgain(tcb, packet); break; }
			
			/* is it a request we want to respond to? */
			else if ((tcb->flags & atpTRel) == atpTRel && tcb->err == atpPending) {
				TransactionReceiveRequest(tcb, address, packet, length); break; }
		
		#ifdef DEBUG
		if (!tcb) {
			printk("atp: discarded incoming request, tid: %u, control %x, \n", tid, packet->control);
			TransactionQueueDump(atp);
			}
		#endif
		break;

	/* response? */
	case atpTResp:
		/* is it a response to a request we made? */
		for (tcb = atp->queue; tcb; tcb = tcb->next)
			if ((tcb->flags & atpTRel) == atpTReq && tcb->err == atpPending && tid == tcb->tid) {
				TransactionReceiveResponse(tcb, packet, length); break; }
		
		#ifdef DEBUG
		if (!tcb) {
			printk("atp: discarded unexpected response, tid: %u\n", tid);
			TransactionQueueDump(atp);
			}
		#endif
		break;

	/* release? */
	case atpTRel:
		/* is it a release to a response we sent? */
		for (tcb = atp->queue; tcb; tcb = tcb->next)
			if ((tcb->flags & atpTRel) == atpTResp && tcb->err == atpPending && tid == tcb->tid) {
				TransactionReceiveRelease(tcb); break; }
		
		#ifdef DEBUG
		if (!tcb) {
			printk("atp: discarded unexpected release, tid %u\n", tid);
			TransactionQueueDump(atp);
			}
		#endif
		break;
	}

return tcb;
}


/*	ATPOpen
	Open an ATP socket
NOTE
	Our implementation makes no distinction between a requesting or
	a responding socket-- the same socket may be used both to send
	and to receive requests.
*/
struct ATP *ATPOpen(
	__u8		s		/* socket number *** */
	)
{
struct ATP *atp;

/* allocate the data structure */
if (! (atp = (struct ATP*) malloc(sizeof(struct ATP)))) goto Abort;
atp->ddp = 0;

/* no transactions pending */
atp->queue = NULL;

/* create the timer for this socket *** */
if (!NewTimer(&atp->timer, &Maintain, 15, 15)) goto AbortClose;

/* open the DDP socket */
if ((atp->ddp = socket(AF_APPLETALK, SOCK_DGRAM, 0)) < 0) goto AbortClose;

/* start transaction IDs from some random number to minimize collisions on the same socket */
atp->tid = time(NULL);

Abort:
return atp;

AbortClose:
return ATPClose(atp), NULL;
}


/*	ATPClose
	Close an open ATP socket
*/
void ATPClose(
	struct ATP	*atp
	)
{
/* stop timers */
DisposeTimer(&atp->timer);

/* cancel pending transactions */
while (atp->queue) {
	#ifdef DEBUG
	printk("atp: canceling transaction on closing socket\n");
	#endif
	(void) ATPCancel(atp->queue);
	}

/* close the socket */
if (atp->ddp >= 0) (void) close(atp->ddp);

/* free the data structure */
free(atp);
}


/*	ATPSendRequest
	Requesting socket send request

	tcb may be an ATPTransaction transaction control block allocated
	by the caller, in which case the request is asynchronous and the
	function returns immediately; or NULL, in which case the request
	is synchronous and the function does not return until the
	transaction is complete.
	
	For synchronous requests, the response user bytes are returned
	in responseUserBytesp (if not NULL)
***** asynchronous request must specify responseUserBytesp to get them, but will still get them in the TCB.
*/
ATPError ATPSendRequest(
	struct ATP	*atp,
	ATPTransaction	*tcb,		/* transaction control block for asynchronous request, or NULL */
	const struct sockaddr_at *address, /* destination socket */
	ATPFlags	flags,		/* atpXO or 0 *** can't I derive this from timeout/retries? */
	unsigned int	timeout,	/* (seconds) XO timeout, or 0 for ALO */
	unsigned int	tries,		/* number of attempts, or 0 - 1 for infinite */
	__s32		requestUserBytes, /* request user bytes */
	const struct iovec *request,	/* request data, or NULL */
	__s32		*responseUserBytesp, /* synchronous requests: (return) response user bytes, or NULL */
	struct iovec	*response	/* response data, or NULL */
	)
{
ATPError err = 0;
ATPTransaction synchronous;

/* caller may only specify atpXO */
if (flags & ~atpXO) return atpParamErr;

/* construct the transaction control block */
if (!tcb) tcb = &synchronous;
tcb->atp = atp;
tcb->err = atpPending;
tcb->bitMap =
	/* caller wants response? */
	(response && response->iov_len > 0) ?
		/* is within the amount of data we can request? */
		response->iov_len < 8 * atpCmdSize ?
			/* one bit for each required packet */
			(1 << (
				/* number of return packets required */
				(response->iov_len + atpCmdSize - 1) / atpCmdSize
				)) - 1 :

			/* at most eight response packets */
			(1 << 8) - 1 :

	/* caller at least wants user bytes? */
	responseUserBytesp ?
		/* need one response packet for the user bytes */
		1 :

		/* don't need any response packets */
		/* This is how ASP tickles are sent-- so note that even a
		   transaction which explicitly asks for no response is still
		   not complete until it gets one. */
		0;
tcb->address = (struct sockaddr_at*) address; /* won't modify the address */
tcb->tid = atp->tid;
tcb->flags = flags | atpTReq;
tcb->expire = 0;
tcb->interval = timeout;
tcb->tries = tries;
tcb->requestUserBytes = requestUserBytes;
tcb->request = (struct iovec*) request; /* won't modify this */
tcb->response = response;

/* enqueue the request */
tcb->next = atp->queue; atp->queue = tcb;

/* send the request */
TransactionSendRequest(tcb);
atp->tid++;

/* wait on synchronous requests */
if (tcb == &synchronous)
	if (!ATPWait(atp, tcb)->err)
		/* return the response user bytes */
		if (responseUserBytesp)
			*responseUserBytesp = tcb->responseUserBytes;

return err;
}


/*	ATPReceiveRequest
	Responding socket await request

	tcb may be an ATPTransaction transaction control block allocated
	by the caller, in which case the request is asynchronous and the
	function returns immediately; or NULL, in which case the request
	is synchronous and the function does not return until the
	transaction is complete.
	
	For synchronous requests, the request user bytes are returned
	in requestUserBytesp (if not NULL)
BUGS
	Need to add time-out
*/
ATPError ATPReceiveRequest(
	struct ATP	*atp,
	ATPTransaction	*tcb,		/* request, or NULL */
	struct sockaddr_at *address,	/* (return) requester address, or NULL */
	struct iovec	*request,	/* (return) request data, or NULL */
	__s32		*requestUserBytesp /* (return) request user bytes, or NULL */
	)
{
ATPError err = 0;
ATPTransaction synchronous;

/* for asynchronous requests, the user bytes are returned in the transaction block */
if (tcb && requestUserBytesp) return atpParamErr;

/* construct the transaction control block */
if (!tcb) tcb = &synchronous;
tcb->atp = atp;
tcb->err = atpPending;
tcb->address = address;
tcb->request = request;
tcb->flags = atpTRel;
tcb->expire = 0;
tcb->interval = 0;

/* enqueue the request block */
tcb->next = atp->queue; atp->queue = tcb;

/* wait on synchronous requests */
if (tcb == &synchronous)
	if (!ATPWait(atp, tcb)->err)
		/* return the request user bytes */
		if (requestUserBytesp)
			*requestUserBytesp = tcb->requestUserBytes;

return err;
}


/*	ATPSendResponse
	Responding socket send response

	tcb may be an ATPTransaction transaction control block allocated
	by the caller, in which case the response is asynchronous and the
	function returns immediately; or NULL, in which case the response
	is synchronous and the function does not return until the
	transaction is complete.
***
	but if tcb is NULL, what is the TID we are responding to?  other fields?
	and if tcb exists, shouldn't it be what we got back from ATPReceiveRequest?
*/
ATPError ATPSendResponse(
	struct ATP	*atp,
	ATPTransaction	*tcb,		/* transaction to respond to, or NULL */
	const struct iovec *response,	/* response data, or NULL */
	__s32		userBytes
	)
{
ATPError err = 0;
ATPTransaction synchronous;

// set up the transaction control block
if (!tcb) tcb = &synchronous;
tcb->atp = atp;
tcb->err = atpPending;
tcb->flags = atpTResp | (tcb->flags & atpXO); // XO inherited from request
					// TID inherited from request
tcb->interval = 30;			// [IAT C-9]
tcb->tries = 1;
tcb->response = (struct iovec*) response; // won't modify this
tcb->responseUserBytes = userBytes;

// enqueue the request block
tcb->next = atp->queue; atp->queue = tcb;

// send the response
TransactionSendResponse(tcb);

// wait on synchronous responses
if (tcb == &synchronous) err = ATPWait(atp, tcb)->err;

return err;
}


/*	ATPAbort
	Abort transactions on the specified socket
*/
ATPError ATPAbort(
	struct ATP	*atp,
	ATPTransaction	*const tcb		/* specific transaction to abort, or NULL */
	)
{
ATPTransaction *t;

// for every pending transaction
for (t = atp->queue; t; t = t->next)
	if (
		t->err == atpPending && (
			t == tcb ||
			t->tries != -1
			)
		)
		// abort it
		t->err = atpReqAborted;

return 0;
}


/*	ATPCancel
	Cancel a pending transaction
NOTE
	Every transaction must be canceled, either directly with ATPCancel
	or indirectly, by issuing it synchronously or completing it synchronously
	with ATPWait.
*/
ATPError ATPCancel(
	ATPTransaction	*tcb
	)
{
ATPTransaction **tcbp;


struct ATP *atp = tcb->atp;

// find the request
for (tcbp = &tcb->atp->queue; *tcbp; tcbp = &(*tcbp)->next)
	if (*tcbp == tcb) {
		// dequeue
		*tcbp = tcb->next; tcb->next = NULL;
		tcb->atp = NULL;
		
		break;
		}
if (!*tcbp) return atpCBNotFound;

// return an error if the request was still pending
if (tcb->err == atpPending) tcb->err = atpReqAborted;

return tcb->err;
}


/*	ATPWait
	Wait for a specific or any transaction to complete
	Return the completed transaction, or NULL if no such transactions exist
CAVEAT
	Returns NULL if there was a socket error
*/
ATPTransaction *ATPWait(
	struct ATP	*atp,
	ATPTransaction	*wait		// specific transaction, or NULL
	)
{
ATPTransaction *tcb = NULL;
struct Packet packet;
struct sockaddr_at from;
int length, fromLength = sizeof from;

// make sure the transaction we are waiting for is on the socket
if (wait ? wait->atp != atp : !atp->queue) return NULL;

// while requested transactions are not completed
while (!(tcb = Completed(atp, wait)))
	// read packet from socket
	if ((length = recvfrom(atp->ddp, &packet, sizeof packet, 0, (struct sockaddr*) &from, &fromLength)) < 0) {
		#ifdef __KERNEL__
		// system call interrupted?
		if (length == -ERESTARTSYS)
			// abort all the transactions we're waiting for
			// (hopefully this will cause all the higher-level protocols to abort)
			/* This is nice, since it basically says that interrupting a process waiting on I/O
			   is equivalent to its deadline having been anteponed. */
			(void) ATPAbort(atp, wait);
		#endif
		}
	
	else if (
		// received packet with full header?
		length >= sizeof packet - sizeof packet.data &&
		
		// ATP protocol packet?
		packet.protocol == kDDPATP
		)
		// handle the packet
		(void) Receive(atp, &from, &packet, length);

// complete the transaction
if (tcb) ATPCancel(tcb);

// return completed transaction
return tcb;
}


/*	ATPSocket
	Return the socket's number, or 0
NOTE
	This is the only way to obtain the socket number if 0 was specified
	to ATPOpen
*/
const __u8 ATPSocket(
	const struct ATP *atp
	)
{
/* get the socket address from the file descriptor */
struct sockaddr_at address;
int nameLength = sizeof address;
if (getsockname(atp->ddp, (struct sockaddr*) &address, &nameLength) != 0)
	return 0;

return address.sat_port;
}


/*	ATPIsTransactionPending
	Return whether the specified transaction is still pending
*/
int ATPIsTransactionPending(
	const ATPTransaction	*tcb
	)
{
return tcb->err == atpPending;
}


/*	ATPIsTransactionComplete
	Return whether the specified transaction is complete
NOTE
	A transaction which is no longer `pending' is not `complete' until
	ATPWait is called.
*/
int ATPIsTransactionComplete(
	const ATPTransaction	*tcb
	)
{
// return whether the transaction was dequeued
return tcb->atp == NULL;
}

