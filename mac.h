/*
	mac.h
	
	Macintosh data types
	
	Copyright (c) 1996-2026 by: Ben Hekster <heksterb@acm.org>
	Licensed under the MIT License - see LICENSE file for details.
	SPDX-License-Identifier: MIT
	
	Thu Oct 24 17:11:47 IST 1996
*/

#ifndef MAC_H
#define MAC_H

#include <linux/types.h>
#include <asm/types.h>


#ifndef __KERNEL__
#define printk printf
#endif


typedef enum { false, true } bool;


/*	asizeof
	Given a pointer to an array, return the number of elements in the array
*/
#define asizeof(p) ((sizeof p) / sizeof(*(p)))


/*	containerof
	Given a type T containing a field f and a pointer p to this field, return
	a pointer to the container
*/
#define containerof(T,f,p) ((T*) ((char*) (p) - offsetof(T,f)))


/* string conversions */
unsigned char *p2cstr(unsigned char *c);
unsigned char *c2pstrncpy(unsigned char *p, const char *c, unsigned char n);
char *p2cstrncpy(char *c, const unsigned char *p, unsigned char n);

/* string comparison */
int strcdcmp(const char*, const char*);

#endif
