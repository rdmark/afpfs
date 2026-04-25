/*
	nbp.h
	
	AppleTalk Name-Binding Protocol, Workstation
	
	Copyright (c) 1996-2026 by: Ben Hekster <heksterb@acm.org>
	Licensed under the MIT License - see LICENSE file for details.
	SPDX-License-Identifier: MIT
	
	Tue Dec 24 14:16:16 IST 1996
*/

#ifndef NBP_H
#define NBP_H

#ifdef __KERNEL__
struct sk_buff;
#include <linux/if_ether.h>
#endif
#include <asm/types.h>
#include <linux/types.h>
#include <linux/atalk.h>


/*	NBPEntity
	Network-Visible Entity
*/
typedef struct {
	struct sockaddr_at address;
	char		object[33],
			type[33],
			zone[33];
	} NBPEntity;


/*

	routines

*/

unsigned NBPLookup(int s, NBPEntity*, unsigned maxMatches, const NBPEntity *lookup);
NBPEntity *NBPParse(NBPEntity*, const char*, int strict);
char *NBPExpress(char name[99], const NBPEntity*);

#endif
