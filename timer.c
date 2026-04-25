/*
	timer.c
	
	Timers
	
	Copyright (c) 1996-2026 by: Ben Hekster <heksterb@acm.org>
	Licensed under the MIT License - see LICENSE file for details.
	SPDX-License-Identifier: MIT
	
	Sun Jan  5 12:43:17 IST 1997

BUGS
	We do our best to stay out of our client's way by not installing
	the signal handler unless we need to, but beyond that we do not
	handle the client's alarm nor restore the alarm period when we
	are done.  Installing another SIGALRM handler obviously wholly
	negates handling of these timers.
*/

// following needed `ben-linux', not `winblowz'
#include <asm/signal.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

#include "timer.h"


extern int errno;

static Timer *gTimers;			/* list of timers *** could maintain sort order */
static struct sigaction gOldAction, gAction = { &CallTimers, 0, 0, 0 };



/*	Schedule
	Reschedule the next alarm
*/
static void Schedule()
{
Timer *t;
time_t next = 0;
unsigned int s;

/* for all timers */
for (t = gTimers; t; t = t->link)
	/* timer active? */
	if (t->when)
		/* find the first timer to go off */
		if (t->when < next || !next) next = t->when;

if (next) {
	signed int t = next - time(NULL);
	s = t > 0 ? t : 1;		/* *** maybe call ourselves right now */
	}

else
	s = 0;

alarm(s);
}


/*	CallTimers
	Call expired timers

	On the off chance that you are using these timers in kernel code
	you must somehow yourself call this function periodically to
	service timers.

NOTE
	There is actually a working mechanism whereby kernel.c implements
	sigaction by means of a kernel timer, but those timers run at
	interrupt time and are thus very limited.  Since I needed
	interrupt-safe timers for afpfs I disabled that mechanism
	(cf. NewTimer and DisposeTimer), but it is still there.
*/
void CallTimers()
{
/* call all the expired timers */
time_t now = time(NULL);
Timer *t;

for (t = gTimers; t; t = t->link)
	if (t->when && now >= t->when) {
		unsigned int period = t->Proc ? (*t->Proc)(t, now) : t->period;
		t->when = period ? t->when + period : 0;
		}

/* reschedule the next alarm */
Schedule();
}


/*	NewTimer
	Start a new timer

	Proc is a function that is called when the timer expires, or
	NULL. delay specifies the number of seconds until the timer
	expires, or 0.  period specifies the number of seconds between
	subsequent reschedulings, or 0.

	If Proc is not NULL, then its return value specifies the period,
	and the interpretation of parameter period is at its discretion.

	Return the timer, or NULL
*/
Timer *NewTimer(
	Timer		*timer,
	unsigned int	(*Proc)(struct Timer*, time_t),
	unsigned int	delay,
	unsigned int	period
	)
{
/* install our signal handler */
if (!gTimers) {
	#ifndef __KERNEL__
	if (sigaction(SIGALRM, &gAction, &gOldAction) < 0) return NULL;
	#endif
	}

/* insert the timer into the list */
timer->when = 0;			/* until RescheduleTimer initializes it */
timer->link = gTimers;
gTimers = timer;

/* reschedule */
if (errno = RescheduleTimer(timer, Proc, delay, period))
	return DisposeTimer(timer), NULL;

return timer;
}


/*	DisposeTimer
	Delete a timer object
*/
void DisposeTimer(
	Timer		*timer
	)
{
Timer **t;

/* remove the timer from the list */
for (t = &gTimers; *t; t = &(*t)->link)
	if (*t == timer) {
		*t = timer->link;
		timer->link = NULL;
		break;
		}

/* reschedule the timer */
Schedule();

/* restore the signal handler */
if (!gTimers) {
	#ifndef __KERNEL__
	(void) sigaction(SIGALRM, &gOldAction, NULL);
	#endif
	}
}


/*	RescheduleTimer
	Reschedule an existing timer (cf. NewTimer)
*/
int RescheduleTimer(
	Timer		*timer,
	unsigned int	(*Proc)(struct Timer*, time_t),
	unsigned int	delay,
	unsigned int	period
	)
{
/* reschedule the specific timer */
timer->Proc = Proc;
timer->when = delay ? time(NULL) + delay : 0;
timer->period = period;

/* reschedule the system timer */
Schedule();

return 0;
}
