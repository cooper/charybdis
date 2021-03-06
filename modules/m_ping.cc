/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_ping.c: Requests that a PONG message be sent back.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002-2005 ircd-ratbox development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 */

#include <ircd/stdinc.h>
#include <ircd/client.h>
#include <ircd/ircd.h>
#include <ircd/numeric.h>
#include <ircd/send.h>
#include <ircd/match.h>
#include <ircd/msg.h>
#include <ircd/parse.h>
#include <ircd/modules.h>
#include <ircd/hash.h>
#include <ircd/s_conf.h>
#include <ircd/s_serv.h>

static const char ping_desc[] =
	"Provides the PING command to ensure a client or server is still alive";

static void m_ping(struct MsgBuf *, struct Client *, struct Client *, int, const char **);
static void ms_ping(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message ping_msgtab = {
	"PING", 0, 0, 0, 0,
	{mg_unreg, {m_ping, 2}, {ms_ping, 2}, {ms_ping, 2}, mg_ignore, {m_ping, 2}}
};

mapi_clist_av1 ping_clist[] = { &ping_msgtab, NULL };

DECLARE_MODULE_AV2(ping, NULL, NULL, ping_clist, NULL, NULL, NULL, NULL, ping_desc);

/*
** m_ping
**      parv[1] = origin
**      parv[2] = destination
*/
static void
m_ping(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;
	const char *destination;

	destination = parv[2];	/* Will get NULL or pointer (parc >= 2!!) */

	if(!EmptyString(destination) && !match(destination, me.name))
	{
		if((target_p = find_server(source_p, destination)))
		{
			sendto_one(target_p, ":%s PING %s :%s",
				   get_id(source_p, target_p),
				   source_p->name, get_id(target_p, target_p));
		}
		else
		{
			sendto_one_numeric(source_p, ERR_NOSUCHSERVER,
					   form_str(ERR_NOSUCHSERVER),
					   destination);
			return;
		}
	}
	else
		sendto_one(source_p, ":%s PONG %s :%s", me.name,
			   (destination) ? destination : me.name, parv[1]);
}

static void
ms_ping(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;
	const char *destination;

	destination = parv[2];	/* Will get NULL or pointer (parc >= 2!!) */

	if(!EmptyString(destination) && irccmp(destination, me.name) &&
	   irccmp(destination, me.id))
	{
		if((target_p = find_client(destination)) && IsServer(target_p))
			sendto_one(target_p, ":%s PING %s :%s",
				   get_id(source_p, target_p), source_p->name,
				   get_id(target_p, target_p));
		/* not directed at an id.. */
		else if(!IsDigit(*destination))
			sendto_one_numeric(source_p, ERR_NOSUCHSERVER,
					   form_str(ERR_NOSUCHSERVER),
					   destination);
	}
	else
		sendto_one(source_p, ":%s PONG %s :%s",
			   get_id(&me, source_p), me.name,
			   get_id(source_p, source_p));
}
