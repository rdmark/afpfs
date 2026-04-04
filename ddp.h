/*
	ddp

	AppleTalk Datagram Delivery Protocol
	Copyright (c) 1996-1997 by: Ben Hekster <heksterb@acm.org>

	Thu Jul 17 20:30:08 IST 1997

	Not implemented-- relies on kernel DDP support
*/

#ifndef DDP_H
#define DDP_H

// protocol types
enum DDPProtocol {
	kDDPRTMP = 1, kDDPNBP, kDDPATP, kDDPAEP
	};

// well-known sockets
enum {
	kRTMPListenerSocket = 1,
	kAEPListenerSocket = 4
	};

#endif

