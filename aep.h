/*
	aep

	AppleTalk Echo Protocol, client
	Copyright (c) 1996-1997 by: Ben Hekster <heksterb@acm.org>

	Thu Jul 17 20:08:37 IST 1997
*/

#ifndef AEP_H
#define AEP_H

struct sockaddr_at;


int AEPRequest(int s, const struct at_addr *node, struct timeval*);

#endif

