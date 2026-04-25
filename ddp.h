/*
	ddp
	
	AppleTalk Datagram Delivery Protocol
	
	Copyright (c) 1996-2026 by: Ben Hekster <heksterb@acm.org>
	Licensed under the MIT License - see LICENSE file for details.
	SPDX-License-Identifier: MIT
	
	Thu Jul 17 20:30:08 IST 1997

	Not implemented-- relies on kernel DDP support
*/

#ifndef DDP_H
#define DDP_H

// protocol types
enum DDPProtocol {
	kDDPRTMP = 1,			// Routing Table Maintenance Protocol
	kDDPNBP,			// Name Binding Protocol
	kDDPATP,			// AppleTalk Transaction Protocol
	kDDPAEP,			// AppleTalk Echo Protocol
	kDDPRTMPRequest			// Routing Table Maintanance Protocol nonrouter node requests
	};

// well-known sockets
enum {
	kRTMPListenerSocket = 1,
	kAEPListenerSocket = 4
	};

#endif

