/*
 * oper-override for charybdis.
 *
 * adds usermode +p and has a timer event that is iterated over to disable
 * usermode +p after a while...
 *
 * you need to have oper:override permission on the opers you want to be
 * able to use this extension.
 */

#include <ircd/stdinc.h>
#include <ircd/modules.h>
#include <ircd/hook.h>
#include <ircd/client.h>
#include <ircd/ircd.h>
#include <ircd/send.h>
#include <ircd/hash.h>
#include <ircd/s_conf.h>
#include <ircd/s_user.h>
#include <ircd/s_serv.h>
#include <ircd/numeric.h>
#include <ircd/privilege.h>
#include <ircd/s_newconf.h>

static const char override_desc[] =
	"Adds user mode +p, an operator-only user mode that grants temporary privileges to override anything";

static void check_umode_change(void *data);
static void hack_channel_access(void *data);
static void hack_can_join(void *data);
static void hack_can_kick(void *data);
static void hack_can_send(void *data);
static void handle_client_exit(void *data);

mapi_hfn_list_av1 override_hfnlist[] = {
	{ "umode_changed", (hookfn) check_umode_change },
	{ "get_channel_access", (hookfn) hack_channel_access },
	{ "can_join", (hookfn) hack_can_join },
	{ "can_kick", (hookfn) hack_can_kick },
	{ "can_send", (hookfn) hack_can_send },
	{ "client_exit", (hookfn) handle_client_exit },
	{ NULL, NULL }
};

#define CHFL_OVERRIDE		0x0004
#define IsOperOverride(x)	(HasPrivilege((x), "oper:override"))

struct OverrideSession {
	rb_dlink_node node;

	struct Client *client;
	time_t deadline;
};

rb_dlink_list overriding_opers = { NULL, NULL, 0 };

static void
update_session_deadline(struct Client *source_p, struct OverrideSession *session_p)
{
	if (session_p == NULL)
	{
		rb_dlink_node *n;

		RB_DLINK_FOREACH(n, overriding_opers.head)
		{
			struct OverrideSession *s = (OverrideSession *)n->data;

			if (s->client == source_p)
			{
				session_p = s;
				break;
			}
		}
	}

	if (session_p == NULL)
	{
		session_p = (OverrideSession *)rb_malloc(sizeof(struct OverrideSession));
		session_p->client = source_p;
	}

	session_p->deadline = rb_current_time() + 1800;

	rb_dlinkDelete(&session_p->node, &overriding_opers);
	rb_dlinkAdd(session_p, &session_p->node, &overriding_opers);
}

static void
expire_override_deadlines(void *unused)
{
	rb_dlink_node *n, *tn;

	RB_DLINK_FOREACH_SAFE(n, tn, overriding_opers.head)
	{
		struct OverrideSession *session_p = (OverrideSession *)n->data;

		if (session_p->deadline > rb_current_time())
			break;
		else if (session_p->deadline < rb_current_time())
		{
		        const char *parv[4] = {session_p->client->name, session_p->client->name, "-p", NULL};
			user_mode(session_p->client, session_p->client, 3, parv);
		}
	}
}

static void
check_umode_change(void *vdata)
{
	hook_data_umode_changed *data = (hook_data_umode_changed *)vdata;
	struct Client *source_p = data->client;

	if (!MyClient(source_p))
		return;

	if (data->oldumodes & UMODE_OPER && !IsOper(source_p))
		source_p->umodes &= ~user_modes['p'];

	/* didn't change +p umode, we don't need to do anything */
	if (!((data->oldumodes ^ source_p->umodes) & user_modes['p']))
		return;

	if (source_p->umodes & user_modes['p'])
	{
		if (!IsOperOverride(source_p))
		{
			sendto_one_notice(source_p, ":*** You need oper:override privilege for +p");
			source_p->umodes &= ~user_modes['p'];
			return;
		}

		update_session_deadline(source_p, NULL);

		sendto_realops_snomask(SNO_GENERAL, L_NETWIDE, "%s has enabled oper-override (+p)",
				       get_oper_name(source_p));
	}
	else if (!(source_p->umodes & user_modes['p']))
	{
		rb_dlink_node *n, *tn;

		RB_DLINK_FOREACH_SAFE(n, tn, overriding_opers.head)
		{
			struct OverrideSession *session_p = (OverrideSession *)n->data;

			if (session_p->client != source_p)
				continue;

			sendto_realops_snomask(SNO_GENERAL, L_NETWIDE, "%s has disabled oper-override (+p)",
					       get_oper_name(session_p->client));

			rb_dlinkDelete(n, &overriding_opers);
			rb_free(session_p);
		}
	}
}

static void
hack_channel_access(void *vdata)
{
	hook_data_channel_approval *data = (hook_data_channel_approval *) vdata;

	if (data->dir == MODE_QUERY)
		return;

	if (data->approved == CHFL_CHANOP)
		return;

	if (data->client->umodes & user_modes['p'])
	{
		update_session_deadline(data->client, NULL);
		data->approved = CHFL_OVERRIDE;

		/* we only want to report modehacks, which are always non-NULL */
		if (data->modestr)
			sendto_realops_snomask(SNO_GENERAL, L_NETWIDE, "%s is using oper-override on %s (modehacking: %s)",
					       get_oper_name(data->client), data->chptr->chname, data->modestr);
	}
}

static void
hack_can_join(void *vdata)
{
	hook_data_channel *data = (hook_data_channel *) vdata;

	if (data->approved == 0)
		return;

	if (data->client->umodes & user_modes['p'])
	{
		update_session_deadline(data->client, NULL);
		data->approved = 0;

		sendto_realops_snomask(SNO_GENERAL, L_NETWIDE, "%s is using oper-override on %s (banwalking)",
				       get_oper_name(data->client), data->chptr->chname);
	}
}

static void
hack_can_kick(void *vdata)
{
	hook_data_channel_approval *data = (hook_data_channel_approval *) vdata;
	int alevel;

	alevel = get_channel_access(data->client, data->chptr, data->msptr, data->dir, NULL);
	if (alevel != CHFL_OVERRIDE)
		return;

	if (data->client->umodes & user_modes['p'])
	{
		update_session_deadline(data->client, NULL);
		sendto_realops_snomask(SNO_GENERAL, L_NETWIDE, "%s is using oper-override on %s (KICK %s)",
				       get_oper_name(data->client), data->chptr->chname, data->target->name);
	}
}

static void
hack_can_send(void *vdata)
{
	hook_data_channel_approval *data = (hook_data_channel_approval *) vdata;

	if (data->dir == MODE_QUERY)
		return;

	if (data->approved == CAN_SEND_NONOP || data->approved == CAN_SEND_OPV)
		return;

	if (data->client->umodes & user_modes['p'])
	{
		data->approved = CAN_SEND_NONOP;

		if (MyClient(data->client))
		{
			update_session_deadline(data->client, NULL);
			sendto_realops_snomask(SNO_GENERAL, L_NETWIDE, "%s is using oper-override on %s (forcing message)",
					       get_oper_name(data->client), data->chptr->chname);
		}
	}
}

static void
handle_client_exit(void *vdata)
{
	hook_data_client_exit *data = (hook_data_client_exit *) vdata;
	rb_dlink_node *n, *tn;
	struct Client *source_p = data->target;

	RB_DLINK_FOREACH_SAFE(n, tn, overriding_opers.head)
	{
		struct OverrideSession *session_p = (OverrideSession *)n->data;

		if (session_p->client != source_p)
			continue;

		rb_dlinkDelete(n, &overriding_opers);
		rb_free(session_p);
	}
}

struct ev_entry *expire_override_deadlines_ev = NULL;

static int
_modinit(void)
{
	/* add the usermode to the available slot */
	user_modes['p'] = find_umode_slot();
	construct_umodebuf();

        expire_override_deadlines_ev = rb_event_add("expire_override_deadlines", expire_override_deadlines, NULL, 60);

	return 0;
}

static void
_moddeinit(void)
{
	/* disable the umode and remove it from the available list */
	user_modes['p'] = 0;
	construct_umodebuf();

	rb_event_delete(expire_override_deadlines_ev);
}

DECLARE_MODULE_AV2(override, _modinit, _moddeinit, NULL, NULL,
			override_hfnlist, NULL, NULL, override_desc);
