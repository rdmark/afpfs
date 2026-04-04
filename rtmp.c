/*
	rtmp

	AppleTalk Routing Table Maintenance Protocol

	Copyright (c) 1996-1997 by: Ben Hekster <heksterb@acm.org>
	Fri Jul 18 00:15:47 IST 1997
*/

#include <sys/types.h>
#include <sys/socket.h>

#include <linux/atalk.h>

#include "mac.h"
#include "ddp.h"
#include "rtmp.h"

extern int errno;



enum { rtmpRequest = 1 };


/*	RTMPRequest
	Look for routers on the local network
*/
int RTMPRequest(
	int		s
	)
{
const struct Request {
	__s8		protocol;
	__s8		function;
	} request = { kDDPRTMP, rtmpRequest };
struct Response {
	__s8		protocol;
	__s8		function;
	__s16		network;
	__u8		idlength;
	__s8		id[DDP_MAXSZ - 4];
	} response;
struct sockaddr_at listeners, listener;
int listenerlen = sizeof listener;

// broadcast the request
listeners.sat_family = AF_APPLETALK;
listeners.sat_port = kRTMPListenerSocket;
listeners.sat_addr.s_net = ATADDR_ANYNET;
listeners.sat_addr.s_node = ATADDR_BCAST;
if (sendto(s, &request, sizeof request, 0, (struct sockaddr*) &listeners, sizeof listeners) < 0) return errno;

// wait for responses from routers
if (recvfrom(s, &response, sizeof response, 0, (struct sockaddr*) &listener, &listenerlen) < 0) return errno;

return 0;
}

