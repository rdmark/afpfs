/*
	aep

	AppleTalk Echo Protocol, client
	Copyright (c) 1996-1997 by: Ben Hekster <heksterb@acm.org>

	Thu Jul 17 20:08:37 IST 1997

	"When they say ignorance is bliss
	they make it sound too good to miss
	And the problem with success
	is you become what you detest"
	--The Right Decision, Jesus Jones;  Perverse
*/

#include <asm/types.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <linux/atalk.h>

#include "mac.h"
#include "ddp.h"
#include "aep.h"

extern int errno;


enum Function { aepRequest = 1, aepReply };

struct Packet {
	__s8		protocol;
	__s8		function;
	__s8		data[0];		// variable-length data
	};


/*	AEPRequest
	Request an echo
	`tries' is the number of times the request is sent before a reply
	is received.  If not NULL, time points to a structure in which
	the time is returned it took for the response to arrive
*/
int AEPRequest(
	int		s,		/* socket to request through */
	const struct at_addr *node,
	struct timeval	*time
	)
{
struct Packet request = { kDDPAEP, aepRequest }, reply;
struct sockaddr_at listener;
struct timeval start;
struct timezone tz;

if (time) (void) gettimeofday(&start, &tz);

// send the request
listener.sat_family = AF_APPLETALK;
listener.sat_port = kAEPListenerSocket;
listener.sat_addr = *node;
if (sendto(
	s, &request, sizeof request,
	0, (struct sockaddr*) &listener, sizeof listener
	) < 0)
	return errno;

// wait for the response
do {
	int listenerlen = sizeof listener;
	if (recvfrom(s, &reply, sizeof reply, 0, (struct sockaddr*) &listener, &listenerlen) < 0)
		return errno;
	} while (! (
		// is an AEP Reply
		reply.protocol == kDDPAEP &&
		reply.function == aepReply
		));

if (time) {
	(void) gettimeofday(time, &tz);
	if ((time->tv_usec -= start.tv_usec) > start.tv_usec) time->tv_sec--;
	time->tv_sec -= start.tv_sec;
	}

return 0;
}

