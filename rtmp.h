/*
	rtmp
	
	AppleTalk Routing Table Maintenance Protocol Stub
	
	Copyright (c) 1996-2026 by: Ben Hekster <heksterb@acm.org>
	Licensed under the MIT License - see LICENSE file for details.
	SPDX-License-Identifier: MIT
	
	Fri Jul 18 00:15:47 IST 1997
*/

#ifndef RTMP_H
#define RTMP_H

struct sockaddr_at;


int RTMPRequest(int s, unsigned *network, struct sockaddr_at *router);


#endif

