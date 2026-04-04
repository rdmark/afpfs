/*
	timer.h

	Timers
	Copyright (c) 1996-1997 by: Ben Hekster <heksterb@acm.org>

	Sun Jan  5 12:43:17 IST 1997

	"Tell me what I'm doing here!"
	--Ascend, Nitzer Ebb;  Ebbhead
*/

#ifndef TIMER_H
#define TIMER_H

#include <linux/types.h>


/*	Timer
	Timer object
*/
typedef struct Timer {
	struct Timer	*link;
	unsigned int	(*Proc)(struct Timer*, time_t);
	time_t		when;
	unsigned int	period;
	} Timer;


Timer *NewTimer(Timer*, unsigned int (*)(Timer*, time_t), unsigned int delay, unsigned int period);
int RescheduleTimer(Timer*, unsigned int (*)(Timer*, time_t), unsigned int delay, unsigned int period);
void DisposeTimer(Timer*);
void CallTimers();

#endif
