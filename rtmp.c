/*
	rtmp
	
	AppleTalk Routing Table Maintenance Protocol Stub
	
	Copyright (c) 1996-2026 by: Ben Hekster <heksterb@acm.org>
	Licensed under the MIT License - see LICENSE file for details.
	SPDX-License-Identifier: MIT
	
	Fri Jul 18 00:15:47 IST 1997
*/

#include <asm/types.h>
#include <sys/types.h>
#include <asm/byteorder.h>
#include <sys/socket.h>
#include <linux/atalk.h>

#include "mac.h"
#include "ddp.h"
#include "rtmp.h"
#include "timer.h"


extern int errno;



enum { rtmpRequest = 1 };


/*	RTMPRequest
	Look for routers on the local network
*/
int RTMPRequest(
	int		s,				// socket to request through
	unsigned	*network,			// (returned) network number
	struct sockaddr_at *router			// (returned) router address
	)
{
const struct Request {
	__s8		protocol;
	__s8		function;
	} request = { kDDPRTMPRequest, rtmpRequest };
struct Response {
	__s8		protocol;
	__s16		network;
	__u8		idlength;
	__s8		id[DDP_MAXSZ - 4];
	} __attribute__ ((packed)) response;
struct sockaddr_at listeners, listener;
int routerl = sizeof *router;
Timer timer;

// broadcast the request
listeners.sat_family = AF_APPLETALK;
listeners.sat_port = kRTMPListenerSocket;
listeners.sat_addr.s_net = ATADDR_ANYNET;
listeners.sat_addr.s_node = ATADDR_BCAST;
if (sendto(s, &request, sizeof request, 0, (struct sockaddr*) &listeners, sizeof listeners) < 0) goto Abort;

// start the retransmission timer
if (!NewTimer(&timer, NULL, 5, 5)) goto Abort;

// wait for responses from routers
while (
	// received packet
	recvfrom(s, &response, sizeof response, 0, (struct sockaddr*) router, &routerl) >= 0 &&
	
	// is RTMP Response
	response.protocol != kDDPRTMP
	);
if (errno) goto Abort;

DisposeTimer(&timer);

// return the number of the network the router says we're on
if (network) *network = ntohs(response.network);

Abort:
return errno;
}

