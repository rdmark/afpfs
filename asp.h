/*
	asp.h

	AppleTalk Session Protocol, Workstation
	Copyright (c) 1996-1997 by: Ben Hekster <heksterb@acm.org>

	Mon Oct 21 17:52:49 IST 1996

	"It's opening time down on Fascination Street
	So let's cut the conversation and get out a little bit
	'Cause I feel it all fading and paling
	and I'm begging to drag you down with me"
	--Fascination Street, The Cure;  Disintegration
*/

#ifndef ASP_H
#define ASP_H

struct iovec;
struct sockaddr_at;

/* ASP session data structure */
struct ASP;


/* ASP error codes */
typedef enum {
	aspNoAck = -1075,
	aspSizeErr = -1073, aspSessClosed,
	aspBuffTooSmall = -1067
	} ASPError;


/* functions */

void ASPGetParms(unsigned int *maxCmdSize, unsigned int *quantumSize);
ASPError ASPGetStatus(const struct sockaddr_at*, const struct iovec *request, struct iovec *reply);
struct ASP *ASPOpenSession(const struct sockaddr_at*, void (*Attention)(struct ASP*, int attentionCode));
void ASPCloseSession(struct ASP*);
ASPError ASPCommand(struct ASP*, const struct iovec *command, struct iovec *reply);
ASPError ASPWrite(struct ASP*, struct iovec *command, struct iovec *data);
int ASPIsSessionOpen(struct ASP*);

#endif
