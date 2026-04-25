/*
	kernel.h
	
	Kernel support
	
	Copyright (c) 1996-2026 by: Ben Hekster <heksterb@acm.org>
	Licensed under the MIT License - see LICENSE file for details.
	SPDX-License-Identifier: MIT
	
	Wed Oct 23 17:08:09 IST 1996
*/

#ifdef __KERNEL__

#ifndef KERNEL_H
#define KERNEL_H

#include <linux/types.h>

struct sigaction;
struct sockaddr;


void *malloc(size_t size);
void free(void *p);
void *krealloc(void*, size_t new, size_t old);

void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *dest, int c, size_t n);
char *strncpy(char *dest, const char *src, size_t n);

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);

int socket_return(int s);
int socket(int family, int type, int protocol);
int getsockname(int s, struct sockaddr *name, int *namelen);
int close(int fd);
int sendto(int s, const void *msg, int len, unsigned int flags, const struct sockaddr *to, int tolen);
int recvfrom(int s, void *buf, int len, unsigned int flags, struct sockaddr *from, int *fromlen);

time_t time(time_t *t);

#endif
#endif
