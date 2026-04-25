/*
	atp.h
	
	AppleTalk Transaction Protocol
	
	Copyright (c) 1996-2026 by: Ben Hekster <heksterb@acm.org>
	Licensed under the MIT License - see LICENSE file for details.
	SPDX-License-Identifier: MIT
	
	Sun Oct 27 14:14:19 IST 1996

BUGS
	Maybe this should be a `protocol', as defined by a struct proto_ops,
	but I wasn't aware of this construct when I began writing this.
*/

#ifndef ATP_H
#define ATP_H

#include "mac.h"
#include "timer.h"

struct sockaddr_at;

struct ATP;


/* ATP result codes */
typedef enum {
	atpReqAborted = -1105, atpCBNotFound = -1102, atpNoRelErr,
	atpTooManySkts = -1098, atpTooManyReqs, atpReqFailed,
	atpBadVersNum = -1066, atpBufTooSmall, atpNoMoreSessions,
	atpNoServers, atpParamErr, atpServerBusy, atpSessClosed,
	atpSizeErr, atpTooManyClients, atpNoAck,
	atpPending = -1
	} ATPError;

/* ATP maximum command and reply size */
enum {
	atpCmdSize = 578,
	atpQuantumSize = 8 * atpCmdSize
	};

/* packet control information (CI) */
typedef enum {
	atpSTS = 1 << 3,		/* used internally */
	atpEOM = 1 << 4,		/* used internally */
	atpXO = 1 << 5,			/* exactly-once transaction */
	atpTReq = 1 << 6,		/* transaction request */
	atpTResp = 2 << 6,		/* transaction response */
	atpTRel = 3 << 6		/* used internally */
	} ATPFlags;


/*	ATPTransaction
	Transaction Control Block
*/
typedef struct ATPTransaction {
	struct ATPTransaction *next;	/* linked list */
	struct ATP	*atp;
	ATPError	err;		/* request error code, or 0 (completed successfully), or atpPending (pending) */
	struct sockaddr_at *address;
	__u16		tid;
	__u8		bitMap;		/* transaction segment bit map */
	ATPFlags	flags;

	time_t		expire,		/* expiration time, if transaction pending */
			interval;	/* retry interval, or 0 */
	unsigned int	tries;		/* tries remaining, or (0 - 1) for infinite */

	__s32		requestUserBytes,
			responseUserBytes;
	struct iovec	*request;
	struct iovec	*response;
	} ATPTransaction;



/*

	functions

*/

struct ATP *ATPOpen(__u8 socket);
void ATPClose(struct ATP*);
ATPError ATPSendRequest(struct ATP*, ATPTransaction*, const struct sockaddr_at*, ATPFlags, unsigned int timeout, unsigned int tries, __s32 requestUserBytes, const struct iovec *request, __s32 *responseUserBytesp, struct iovec *response);
ATPError ATPReceiveRequest(struct ATP*, ATPTransaction*, struct sockaddr_at*, struct iovec *request, __s32 *requestUserBytesp);
ATPError ATPSendResponse(struct ATP*, ATPTransaction*, const struct iovec *response, __s32 userBytes);
ATPError ATPCancel(ATPTransaction*);
ATPError ATPAbort(struct ATP*, ATPTransaction*);
ATPTransaction *ATPWait(struct ATP*, ATPTransaction*);
const __u8 ATPSocket(const struct ATP*);
int ATPIsTransactionPending(const ATPTransaction*);
int ATPIsTransactionComplete(const ATPTransaction*);

#endif
