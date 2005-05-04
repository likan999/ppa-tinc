/*
    protocol.c -- handle the meta-protocol, basic functions
    Copyright (C) 1999-2005 Ivo Timmermans <ivo@tinc-vpn.org>,
                  2000-2005 Guus Sliepen <guus@tinc-vpn.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    $Id: protocol.c 1439 2005-05-04 18:09:30Z guus $
*/

#include "system.h"

#include "conf.h"
#include "connection.h"
#include "logger.h"
#include "meta.h"
#include "protocol.h"
#include "utils.h"
#include "xalloc.h"

bool tunnelserver = false;

/* Jumptable for the request handlers */

static bool (*request_handlers[])(connection_t *) = {
		id_h, metakey_h, challenge_h, chal_reply_h, ack_h,
		status_h, error_h, termreq_h,
		ping_h, pong_h,
		add_subnet_h, del_subnet_h,
		add_edge_h, del_edge_h,
		key_changed_h, req_key_h, ans_key_h, tcppacket_h,
};

/* Request names */

static char (*request_name[]) = {
		"ID", "METAKEY", "CHALLENGE", "CHAL_REPLY", "ACK",
		"STATUS", "ERROR", "TERMREQ",
		"PING", "PONG",
		"ADD_SUBNET", "DEL_SUBNET",
		"ADD_EDGE", "DEL_EDGE", "KEY_CHANGED", "REQ_KEY", "ANS_KEY", "PACKET",
};

static avl_tree_t *past_request_tree;

bool check_id(const char *id)
{
	for(; *id; id++)
		if(!isalnum(*id) && *id != '_')
			return false;

	return true;
}

/* Generic request routines - takes care of logging and error
   detection as well */

bool send_request(connection_t *c, const char *format, ...)
{
	va_list args;
	char buffer[MAXBUFSIZE];
	int len, request;

	cp();

	/* Use vsnprintf instead of vasprintf: faster, no memory
	   fragmentation, cleanup is automatic, and there is a limit on the
	   input buffer anyway */

	va_start(args, format);
	len = vsnprintf(buffer, MAXBUFSIZE, format, args);
	va_end(args);

	if(len < 0 || len > MAXBUFSIZE - 1) {
		logger(LOG_ERR, _("Output buffer overflow while sending request to %s (%s)"),
			   c->name, c->hostname);
		return false;
	}

	ifdebug(PROTOCOL) {
		sscanf(buffer, "%d", &request);
		ifdebug(META)
			logger(LOG_DEBUG, _("Sending %s to %s (%s): %s"),
				   request_name[request], c->name, c->hostname, buffer);
		else
			logger(LOG_DEBUG, _("Sending %s to %s (%s)"), request_name[request],
				   c->name, c->hostname);
	}

	buffer[len++] = '\n';

	if(c == broadcast) {
		broadcast_meta(NULL, buffer, len);
		return true;
	} else
		return send_meta(c, buffer, len);
}

void forward_request(connection_t *from)
{
	int request;

	cp();

	ifdebug(PROTOCOL) {
		sscanf(from->buffer, "%d", &request);
		ifdebug(META)
			logger(LOG_DEBUG, _("Forwarding %s from %s (%s): %s"),
				   request_name[request], from->name, from->hostname,
				   from->buffer);
		else
			logger(LOG_DEBUG, _("Forwarding %s from %s (%s)"),
				   request_name[request], from->name, from->hostname);
	}

	from->buffer[from->reqlen - 1] = '\n';

	broadcast_meta(from, from->buffer, from->reqlen);
}

bool receive_request(connection_t *c)
{
	int request;

	cp();

	if(sscanf(c->buffer, "%d", &request) == 1) {
		if((request < 0) || (request >= LAST) || !request_handlers[request]) {
			ifdebug(META)
				logger(LOG_DEBUG, _("Unknown request from %s (%s): %s"),
					   c->name, c->hostname, c->buffer);
			else
				logger(LOG_ERR, _("Unknown request from %s (%s)"),
					   c->name, c->hostname);

			return false;
		} else {
			ifdebug(PROTOCOL) {
				ifdebug(META)
					logger(LOG_DEBUG, _("Got %s from %s (%s): %s"),
						   request_name[request], c->name, c->hostname,
						   c->buffer);
				else
					logger(LOG_DEBUG, _("Got %s from %s (%s)"),
						   request_name[request], c->name, c->hostname);
			}
		}

		if((c->allow_request != ALL) && (c->allow_request != request)) {
			logger(LOG_ERR, _("Unauthorized request from %s (%s)"), c->name,
				   c->hostname);
			return false;
		}

		if(!request_handlers[request](c)) {
			/* Something went wrong. Probably scriptkiddies. Terminate. */

			logger(LOG_ERR, _("Error while processing %s from %s (%s)"),
				   request_name[request], c->name, c->hostname);
			return false;
		}
	} else {
		logger(LOG_ERR, _("Bogus data received from %s (%s)"),
			   c->name, c->hostname);
		return false;
	}

	return true;
}

static int past_request_compare(const past_request_t *a, const past_request_t *b)
{
	return strcmp(a->request, b->request);
}

static void free_past_request(past_request_t *r)
{
	cp();

	if(r->request)
		free(r->request);

	free(r);
}

void init_requests(void)
{
	cp();

	past_request_tree = avl_alloc_tree((avl_compare_t) past_request_compare, (avl_action_t) free_past_request);
}

void exit_requests(void)
{
	cp();

	avl_delete_tree(past_request_tree);
}

bool seen_request(char *request)
{
	past_request_t *new, p = {0};

	cp();

	p.request = request;

	if(avl_search(past_request_tree, &p)) {
		ifdebug(SCARY_THINGS) logger(LOG_DEBUG, _("Already seen request"));
		return true;
	} else {
		new = xmalloc(sizeof(*new));
		new->request = xstrdup(request);
		new->firstseen = now;
		avl_insert(past_request_tree, new);
		return false;
	}
}

void age_past_requests(void)
{
	avl_node_t *node, *next;
	past_request_t *p;
	int left = 0, deleted = 0;

	cp();

	for(node = past_request_tree->head; node; node = next) {
		next = node->next;
		p = node->data;

		if(p->firstseen + pingtimeout < now)
			avl_delete_node(past_request_tree, node), deleted++;
		else
			left++;
	}

	if(left || deleted)
		ifdebug(SCARY_THINGS) logger(LOG_DEBUG, _("Aging past requests: deleted %d, left %d"),
			   deleted, left);
}