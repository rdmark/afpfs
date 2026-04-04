/*
	nbp.c

	AppleTalk Name-Binding Protocol, Workstation
	Copyright (c) 1996-1997 by: Ben Hekster <heksterb@acm.org>

	Tue Dec 24 14:16:16 IST 1996

NOTE
	The `server' or registry functions are not yet implemented.
	These belong in a daemon together with server implementations
	of ASP and AFP.

	"And on the days that followed
	I listened to his words
	I strained to understand him
	I chased his thoughts like birds"
	--Darkness, The Police;  Ghost in the Machine
*/

#include <bytesex.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <linux/atalk.h>

#include <stdio.h>
#include "mac.h"
#include "ddp.h"
#include "nbp.h"
#include "timer.h"


extern int errno;
static __u16 gID;			/* lookup ID */


static const unsigned int nbpnisSocket = 2; /* NBP Names Information Socket */

enum Function { nbpBrRq = 1, nbpLkUp, nbpLkUpReply };

struct Tuple {
	__s16		network;
	__s8		node;
	__s8		socket;
	__s8		enumerator;
	__s8		nameLength;
	char		name[32];
	__s8		_typeLength;	/* actually follows last byte of name */
	char		_type[32];
	__s8		_zoneLength;	/* actually follows last byte of type */
	char		_zone[32];
	};

struct Packet {
	__s8		protocol;
	#if __BYTE_ORDER == 1234
	unsigned	count : 4,
			function : 4;
	#else
	unsigned	function : 4,
			count : 4;
	#endif
	__s8		id;
	struct Tuple	tuples[0];
	} __attribute__ ((packed));



/*	InsertLookupReplyEntity
	Insert an entity returned by a LookupReply into the table
	Return a pointer to the first byte following the tuple, or NULL
*/
static const struct Tuple *InsertLookupReplyEntity(
	NBPEntity	*entities,
	unsigned int	*numEntities,
	const struct Tuple *tuple,
	const void	*const endTuple
	)
{
/* ***** check that we don't have the entity already */

/* insert the entry */
NBPEntity *e = entities + *numEntities;
unsigned int l;
const void *p = &tuple->nameLength;

/* copy address */
if (p + 4 > endTuple) return NULL;
e->address.sat_family = AF_APPLETALK;
e->address.sat_addr.s_net = tuple->network;
e->address.sat_addr.s_node = tuple->node;
e->address.sat_port = tuple->socket;

/* copy object, type, and zone */
if (p + (l = *((__s8*) p)++) > endTuple || l > sizeof e->object - 1) return NULL;
(void) memcpy(e->object, p, l); e->object[l] = '\0'; p += l;
if (p + (l = *((__s8*) p)++) > endTuple || l > sizeof e->type - 1) return NULL;
(void) memcpy(e->type, p, l); e->type[l] = '\0'; p += l;
if (p + (l = *((__s8*) p)++) > endTuple || l > sizeof e->zone - 1) return NULL;
(void) memcpy(e->zone, p, l); e->zone[l] = '\0'; p += l;

/* one more entry in the table */
(*numEntities)++;

return (const struct Tuple*) p;
}


/*	NBPLookup
	Look up matching Network-Visible Entities
	Return the number of entities found, or 0

	`search' specifies the entities to look for:
	*	an empty `object' or equalling `=' signifies any host,
		and any other string the host matching that name;
	*	an empty `type' signifies any type, or else an entity
		having that type;
	*	a `zone' equalling `*' signifies the local zone,
		an empty `zone' any zone, and any other string the
		zone matching that name; and,
	*	nonzero fields in `address' signifying a specific
		port, network, and node.
	Entities are returned matching in the symbolic and numeric
	object, type, and zone.
*/
unsigned int NBPLookup(
	int		s,		/* bound socket to look up through */
	NBPEntity	*entities,	/* (return) found entities */
	unsigned	maxReceive,	/* maximum number of entities to return */
	const NBPEntity	*search		/* entities to look for */
	)
{
Timer timer;
unsigned numReceived = 0;
unsigned int tries = 4;			/* *** arbitrary.  Maybe a better criterion is whether numReceived went up since the last retransmission */
struct sockaddr_at local, address;
struct {
	struct Packet	header;
	struct Tuple	_tuple;		/* actually follows last byte of packet */
	} lookup = {
	{ kDDPNBP,
		#if __BYTE_ORDER == 1234
		1, nbpLkUp,
		#else
		nbpLkUp, 1,
		#endif
		htons(gID++)
		}
	};
char *l = &lookup.header.tuples[0].nameLength;

/* ***** can't look up in zones yet */
if (search->zone[0] != '\0' && strcmp(search->zone, "*") != 0) return 1;

/* assemble the lookup tuple */
{
	/* get the local socket address */
	int namelen = sizeof local;
	if (getsockname(s, (struct sockaddr*) &local, &namelen) < 0) goto Abort;

	/* specify our address in the look-up request */
	lookup.header.tuples[0].network = local.sat_addr.s_net;
	lookup.header.tuples[0].node = local.sat_addr.s_node;
	lookup.header.tuples[0].socket = local.sat_port;
	lookup.header.tuples[0].enumerator = 0;	/* ignored */

	/* assemble the lookup tuple entity */
	l = c2pstrncpy(l, search->object[0] != '\0' ? search->object : "=", 32);
	l = c2pstrncpy(l, search->type[0] != '\0' ? search->type : "=", 32);
	l = c2pstrncpy(l, search->zone[0] != '\0' ? search->zone : "*", 32);
	}

/* who to send the look-up request to */
address.sat_family = AF_APPLETALK;
address.sat_addr.s_net =
	search->address.sat_addr.s_net ? search->address.sat_addr.s_net :
	!strcmp(search->zone, "*") ? local.sat_addr.s_net :
	ATADDR_ANYNET;
address.sat_addr.s_node =
	search->address.sat_addr.s_node ? search->address.sat_addr.s_node :
	!strcmp(search->object, "=") ? local.sat_addr.s_node :
	ATADDR_BCAST;
address.sat_port = nbpnisSocket;

/* look up */
while (numReceived < maxReceive && tries-- > 0) {
	struct {
		struct Packet	header;
		struct Tuple	_tuple[4];
		} reply;
	signed int length;

	/* broadcast the request */
	if (!NewTimer(&timer, NULL, 5, 5)) goto Abort;
	if (sendto(s, &lookup, (void*) l - (void*) &lookup, 0, (struct sockaddr*) &address, sizeof address) < 0) goto AbortStop;

	while (
		/* need more replies? */
		numReceived < maxReceive &&

		/* no error on the socket? */
		(length = recvfrom(s, &reply, sizeof reply, 0, NULL, NULL)) >= 0
		) if (
		/* received at least a header? */
		length >= sizeof reply.header &&

		/* NBP LookupReply to our request? */
		reply.header.protocol == kDDPNBP &&
		reply.header.function == nbpLkUpReply &&
		reply.header.id == lookup.header.id
		) {
		/* for each of the received tuples */
		unsigned int tuples;
		const struct Tuple *t;
		for (
			t = reply.header.tuples, tuples = reply.header.count;
			t && tuples > 0 && numReceived < maxReceive;
			tuples--
			) 
			t = InsertLookupReplyEntity(entities, &numReceived, t, (void*) &reply + length);
		}

	AbortStop:
	DisposeTimer(&timer);
	}

Abort:
return numReceived;
}


/*	NBPParse
	Return the entity or NULL described by a string of the form
	
		[object][:type][@zone]

	If strict is zero, then fields consisting only of digits are
	interpreted as numbers rather than names.  That is, a numeric
	object represents the node number, type the socket number, and
	zone the network number.

	If the entity name is NULL, the returned address specifies any
	and all network-visible entities.

NOTE
	This function does not actually perform any look-ups; it just
	returns the entity corresponding to the given string.
*/
NBPEntity *NBPParse(
	NBPEntity	*entity,	// (return) parsed entity
	const char	*name,		// entity string, or NULL
	int		strict
	)
{
char *a, *aend;
int numeric;
unsigned i;

entity->address.sat_family = AF_APPLETALK;

// copy the object
a = entity->object; aend = a + 32; numeric = !strict;
if (name) {
	while (*name && *name != ':' && *name != '@' && a < aend)
		numeric &= isdigit(*a++ = *name++);
	}
if (a >= aend) return NULL;
*a = '\0';
entity->address.sat_addr.s_node = numeric ? atoi(entity->object) : 0;
if (numeric) entity->object[0] = '\0';

// copy the type
a = entity->type; aend = a + 32; numeric = !strict;
if (name && *name == ':' && name++)
	while (*name && *name != '@' && a < aend)
		numeric &= isdigit(*a++ = *name++);
if (a >= aend) return NULL;
*a = '\0';
entity->address.sat_port = numeric ? atoi(entity->type) : 0;
if (numeric) entity->type[0] = '\0';

// copy the zone
a = entity->zone; aend = a + 32; numeric = !strict;
if (name && *name == '@' && name++)
	while (*name && a < aend)
		numeric &= isdigit(*a++ = *name++);
if (a >= aend) return NULL;
*a = '\0';
entity->address.sat_addr.s_net = htons(numeric ? atoi(entity->zone) : 0);
if (numeric) entity->zone[0] = '\0';

return entity;
}


static char *ExpressField(
	char		*e,
	char		separator,
	const char	*f,
	unsigned	i
	)
{
// determinate field?
if (f[0] != '\0' || i != 0) {
	// have separator?
	if (separator != '\0') *e++ = separator;

	// have symbol?
	if (f[0] != '\0')
		e += sprintf(e, "%s", f);

	// have number
	else
		e += sprintf(e, "%u", i);
	}

return e;
}


/*	NBPExpress
	Express the specified entity in a human-readable form, suitable
	for parsing by NBPParse
NOTE
	If both numeric and symbolic forms are present, the latter takes
	precedence
BUGS
	Should return metacharacters
*/
char *NBPExpress(
	char		name[99],
	const NBPEntity	*entity
	)
{
name = ExpressField(name, '\0', entity->object, entity->address.sat_addr.s_node);
name = ExpressField(name, ':', entity->type, entity->address.sat_port);
name = ExpressField(name, '@', entity->zone, entity->address.sat_addr.s_net);

return name;
}

