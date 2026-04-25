/*
	kernel.c
	
	Kernel support
	
	Copyright (c) 1996-2026 by: Ben Hekster <heksterb@acm.org>
	Licensed under the MIT License - see LICENSE file for details.
	SPDX-License-Identifier: MIT
	
	Wed Oct 23 17:08:09 IST 1996
	
	These are implementations of kernel and libc functions which are
	not available to kernel code.
	
	Note that separating these functions in this way has the effect
	of making the rest of the code independent of particular kernels
	or even whether it is part of a kernel or not.
*/

#include "kernel.h"

#include <linux/malloc.h>
#include <linux/timer.h>

// why do people still do this?
#ifdef memcpy
#undef memcpy
#endif

#ifdef memset
#undef memset
#endif


/*	errno
	Global error status
	I have long believed that the result of a function should be used
	for the actual function result, and not as a mere indication that
	there was no failure-- that is a waste of precious communication
	bandwidth, and other constructions used to return function results
	through input parameters are awkward at best.
BUGS
	This should not be a systemwide but processwide global, since any
	process may now get any other process' AFP errors.  In practice
	this will not happen since I rarely, if ever, cause the kernel to
	reschedule after signaling an error.  Nevertheless, I will eventually
	change this.
*/
int errno;



// MkLinux does not export these functions, so I copied the source
#if CONFIG_OSFMACH3 && MODULE
unsigned long get_fs(void) { return current->osfmach3.thread->reg_fs; }
void set_fs(unsigned long val) { current->osfmach3.thread->reg_fs = val; }
#endif



#ifdef DEBUG
void dumpk(
	const void	*m,
	unsigned int	l
	)
{
while (l > 0) {
	int i = l > 0x10 ? 0x10 : l;
	l -= i;

	printk("%lx:  ", m);
	while (i-- > 0) printk("%2.2x ", *((unsigned char*) m)++);
	printk("\n");
	}
}
#endif


/*	malloc(3)
	free(3)
*/
inline void *malloc(size_t size) { return kmalloc(size, GFP_KERNEL); }
inline void free(void *p) { kfree(p); }


/*	krealloc
	Like realloc(3), except the size of the existing object has to
	be passed explicitly because I don't know how to get it from the
	kernel.  If old is zero, it is assumed that the new block is not
	smaller than the existing one.
*/
void *krealloc(void *p, size_t new, size_t old)
{
// allocate the new block
void *const r = new ? malloc(new) : NULL;

// old and new blocks exist?
if (p && r) {
	// copy the old into the new block
	memcpy(r, p, old && old < new ? old : new);
	free(p);
	}

return r;
}


/*	memcpy(3)
	memset(3)
	strncpy(3)
*/
void *memcpy(
	void		*dest,
	const void	*src,
	size_t		n
	)
{
register char *d = dest;
register const char *s = src;

while (n-- > 0) *d++ = *s++;

return dest;
}

void *memset(void *dest, int c, size_t n)
{
register char *d = dest;

while (n-- > 0) *d++ = c;

return dest;
}

char *strncpy(char *dest, const char *src, size_t n)
{
register char *d = dest;
register const char *s = src;

// copy n bytes from s, stretching the last null byte out to fill
while (n-- > 0) if (*d++ = *s) s++;

return dest;
}


/*

	signals

*/

static void (*gAlarm)();
static struct timer_list gTimer;

static void Timer(unsigned long data) { if (gAlarm) (*gAlarm)(); }


/*	alarm
	As alarm(2)
BUGS
	Does not return the correct value for the previous timer
*/
long alarm(
	long		seconds
	)
{
/* cancel the previous timer */
if (gTimer.expires)
	(void) del_timer(&gTimer);

gTimer.expires = seconds ? jiffies + seconds * HZ : 0;

/* set the new timer */
if (gTimer.expires) {
	gTimer.next = gTimer.prev = NULL;
	gTimer.function = &Timer;
	add_timer(&gTimer);
	}

return 0;
}


/*	sigaction
	As sigaction(2)
	signum must be SIGALRM
NOTE
	This is not really implemented with signals but with a kernel
	timer, in order that timers may outlast processes and so that
	the process' signal handling is not disturbed.
*/
int sigaction(
	int		signum,
	const struct sigaction *act,
	struct sigaction *oldact
	)
{
void (*const oldAlarm)() = gAlarm;

if (signum != SIGALRM) return -EINVAL;

/* set the new signal handler */
gAlarm = act->sa_handler;

/* initialize the timer */
if (gAlarm && !oldAlarm) gTimer.expires = 0;

/* dispose the timer */
if (!gAlarm && oldAlarm) (void) alarm(0);

/* return the previous handler to the caller */
if (oldact)
	oldact->sa_handler = oldAlarm;

return 0;
}



/*

	sockets

*/

const unsigned int kFirstSocketDescriptor = 3;
static struct file *gSocket[1];

static struct file *file_from_descriptor(
	int		fd
	)
{
struct file *file;

/* find the file associated with the descriptor */
if (
	fd < 0 || fd >= NR_OPEN ||
	!(file = current->files->fd[fd])
	) return errno = -EBADF, NULL;

return file;
}


static struct socket *socket_from_file(
	struct file	*file
	)
{
struct inode *const inode = file->f_inode;
struct socket *const sock = &inode->u.socket_i;

/* find the socket associated with the file */
if (!inode) return errno = -EBADF, NULL;

/* check that it is really a socket */
if (!S_ISSOCK(inode->i_mode) || !inode->i_sock) return errno = -ENOTSOCK, NULL;

return sock;
}


static struct file *file_from_pseudodescriptor(
	int		fd
	)
{
struct file *file;

/* make sure that it is a valid pseudo descriptor */
if (
	fd != kFirstSocketDescriptor ||
	!(file = gSocket[fd - kFirstSocketDescriptor])
	) return errno = -EBADF, NULL;

return file;
}


static struct socket *socket_from_pseudodescriptor(
	int		fd
	)
{
struct file *file = file_from_pseudodescriptor(fd);
if (!file) return NULL;

/* return the associated socket */
return socket_from_file(file);
}


/*	socket(2)
	socket_return
	socket pretends to open the open socket specified to socket_return
	The file descriptor returned by socket is not real
*/
int socket_return(int fd)
{
/* find the file structure */
struct file *const file = file_from_descriptor(fd);
if (!file) return errno;

/* check that it is a socket */
if (!socket_from_file(file)) return errno;

/* remember the file */
gSocket[0] = file;

return 0;
}


int socket(int family, int type, int protocol)
{
struct file *file = gSocket[0];
if (!file) return -ENOSR;

/* *** check the socket based on the parameters */
// gSocket[0] = NULL;

/* increase the socket usage count to hold it open */
file->f_count++;

/* return some fake file descriptor-- noone else but we will see it */
return kFirstSocketDescriptor;
}


/*	close
	Pretend to close the socket returned by socket
*/
int close(int fd)
{
struct file *file = file_from_pseudodescriptor(fd);

/* decrease the usage count ***** is that enough? */
file->f_count--;

gSocket[0] = NULL;

return 0;
}


/*	getsockname
	As getsockname(2)
*/
int getsockname(int fd, struct sockaddr *name, int *namelen)
{
struct socket *const sock = socket_from_pseudodescriptor(fd);
int nl;
int err;
if (!sock) return errno;

/* get the local socket address */
if (!sock->ops || !sock->ops->getname) return -EBADF; /* *** */
if (err = (sock->ops->getname)(sock, name, namelen, 0 /* local */)) return err;
if (namelen) *namelen = nl;

return err;
}


/*	sendto
	As sendto(2)
*/
int sendto(int fd, const void *data, int len, unsigned int flags, const struct sockaddr *to, int tolen)
{
int n;
struct socket *const sock = socket_from_pseudodescriptor(fd);
struct iovec iov = { (void*) data, len };
struct msghdr msg = { (struct sockaddr*) to, tolen, &iov, 1, NULL, 0 };
const unsigned long oldfs = get_fs();	/* *** nfs, what is this? */
if (!sock) return errno;

// check the arguments
if (len < 0) { n = -EINVAL; goto Abort; }
if (!sock->ops || !sock->ops->sendmsg) { n = -EBADF; goto Abort; /* *** */ }

set_fs(get_ds());
/* *** O_NONBLOCK is too severe, but I can't have the init task block on resending ASP session tickles */
/* *** in fact, init should definitely not block because it is our timer. */
n = (sock->ops->sendmsg)(sock, &msg, len, 0/* *** file->f_flags  O_NONBLOCK */, flags);
set_fs(oldfs);

Abort:
return n;
}


/*	recvfrom
	As recvfrom(2)
*/
int recvfrom(int fd, void *data, int len, unsigned int flags, struct sockaddr *from, int *fromlen)
{
int n;
struct socket *const sock = socket_from_pseudodescriptor(fd);
struct iovec iov = { data, len };
struct msghdr msg = { from, 0, &iov, 1, NULL, 0 };
const unsigned long oldfs = get_fs();
if (!sock) return errno;

if (!sock->ops || !sock->ops->sendmsg) return -EBADF; /* *** */
set_fs(get_ds());
n = (sock->ops->recvmsg)(sock, &msg, len, 0 /* file->f_flags O_NONBLOCK */, flags, fromlen);
set_fs(oldfs);

return n;
}


/*	time
	As time(2)
*/
time_t time(time_t *t)
{
struct timeval tv;

do_gettimeofday(&tv);

if (t) *t = tv.tv_sec;		/* *** is this really correct?! */

return tv.tv_sec;
}
