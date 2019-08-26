/*
    protocol_subnet.c -- handle the meta-protocol, subnets
    Copyright (C) 1999-2005 Ivo Timmermans,
                  2000-2006 Guus Sliepen <guus@tinc-vpn.org>

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

    $Id: protocol_subnet.c 1452 2006-04-26 13:52:58Z guus $
*/

#include "system.h"

#include "conf.h"
#include "connection.h"
#include "logger.h"
#include "net.h"
#include "netutl.h"
#include "node.h"
#include "protocol.h"
#include "subnet.h"
#include "utils.h"
#include "xalloc.h"

bool send_add_subnet(connection_t *c, const subnet_t *subnet)
{
	char netstr[MAXNETSTR];

	cp();

	if(!net2str(netstr, sizeof netstr, subnet))
		return false;

	return send_request(c, "%d %lx %s %s", ADD_SUBNET, random(), subnet->owner->name, netstr);
}

bool add_subnet_h(connection_t *c)
{
	char subnetstr[MAX_STRING_SIZE];
	char name[MAX_STRING_SIZE];
	node_t *owner;
	subnet_t s = {0}, *new;

	cp();

	if(sscanf(c->buffer, "%*d %*x " MAX_STRING " " MAX_STRING, name, subnetstr) != 2) {
		logger(LOG_ERR, _("Got bad %s from %s (%s)"), "ADD_SUBNET", c->name,
			   c->hostname);
		return false;
	}

	/* Check if owner name is a valid */

	if(!check_id(name)) {
		logger(LOG_ERR, _("Got bad %s from %s (%s): %s"), "ADD_SUBNET", c->name,
			   c->hostname, _("invalid name"));
		return false;
	}

	/* Check if subnet string is valid */

	if(!str2net(&s, subnetstr)) {
		logger(LOG_ERR, _("Got bad %s from %s (%s): %s"), "ADD_SUBNET", c->name,
			   c->hostname, _("invalid subnet string"));
		return false;
	}

	if(seen_request(c->buffer))
		return true;

	/* Check if the owner of the new subnet is in the connection list */

	owner = lookup_node(name);

	if(!owner) {
		owner = new_node();
		owner->name = xstrdup(name);
		node_add(owner);
	}

	if(tunnelserver && owner != myself && owner != c->node)
		return false;

	/* Check if we already know this subnet */

	if(lookup_subnet(owner, &s))
		return true;

	/* If we don't know this subnet, but we are the owner, retaliate with a DEL_SUBNET */

	if(owner == myself) {
		ifdebug(PROTOCOL) logger(LOG_WARNING, _("Got %s from %s (%s) for ourself"),
				   "ADD_SUBNET", c->name, c->hostname);
		s.owner = myself;
		send_del_subnet(c, &s);
		return true;
	}

	/* In tunnel server mode, check if the subnet matches one in the config file of this node */

	if(tunnelserver) {
		config_t *cfg;
		subnet_t *allowed;

		for(cfg = lookup_config(c->config_tree, "Subnet"); cfg; cfg = lookup_config_next(c->config_tree, cfg)) {
			if(!get_config_subnet(cfg, &allowed))
				return false;

			if(!subnet_compare(&s, allowed))
				break;

			free_subnet(allowed);
		}

		if(!cfg)
			return false;

		free_subnet(allowed);
	}

	/* If everything is correct, add the subnet to the list of the owner */

	*(new = new_subnet()) = s;
	subnet_add(owner, new);

	if(owner->status.reachable)
		subnet_update(owner, new, true);

	/* Tell the rest */

	if(!tunnelserver)
		forward_request(c);

	return true;
}

bool send_del_subnet(connection_t *c, const subnet_t *s)
{
	char netstr[MAXNETSTR];

	cp();

	if(!net2str(netstr, sizeof netstr, s))
		return false;

	return send_request(c, "%d %lx %s %s", DEL_SUBNET, random(), s->owner->name, netstr);
}

bool del_subnet_h(connection_t *c)
{
	char subnetstr[MAX_STRING_SIZE];
	char name[MAX_STRING_SIZE];
	node_t *owner;
	subnet_t s = {0}, *find;

	cp();

	if(sscanf(c->buffer, "%*d %*x " MAX_STRING " " MAX_STRING, name, subnetstr) != 2) {
		logger(LOG_ERR, _("Got bad %s from %s (%s)"), "DEL_SUBNET", c->name,
			   c->hostname);
		return false;
	}

	/* Check if owner name is a valid */

	if(!check_id(name)) {
		logger(LOG_ERR, _("Got bad %s from %s (%s): %s"), "DEL_SUBNET", c->name,
			   c->hostname, _("invalid name"));
		return false;
	}

	/* Check if the owner of the new subnet is in the connection list */

	owner = lookup_node(name);

	if(!owner) {
		ifdebug(PROTOCOL) logger(LOG_WARNING, _("Got %s from %s (%s) for %s which is not in our node tree"),
				   "DEL_SUBNET", c->name, c->hostname, name);
		return true;
	}

	if(tunnelserver && owner != myself && owner != c->node)
		return false;

	/* Check if subnet string is valid */

	if(!str2net(&s, subnetstr)) {
		logger(LOG_ERR, _("Got bad %s from %s (%s): %s"), "DEL_SUBNET", c->name,
			   c->hostname, _("invalid subnet string"));
		return false;
	}

	if(seen_request(c->buffer))
		return true;

	/* If everything is correct, delete the subnet from the list of the owner */

	s.owner = owner;

	find = lookup_subnet(owner, &s);

	if(!find) {
		ifdebug(PROTOCOL) logger(LOG_WARNING, _("Got %s from %s (%s) for %s which does not appear in his subnet tree"),
				   "DEL_SUBNET", c->name, c->hostname, name);
		return true;
	}

	/* If we are the owner of this subnet, retaliate with an ADD_SUBNET */

	if(owner == myself) {
		ifdebug(PROTOCOL) logger(LOG_WARNING, _("Got %s from %s (%s) for ourself"),
				   "DEL_SUBNET", c->name, c->hostname);
		send_add_subnet(c, find);
		return true;
	}

	/* Tell the rest */

	if(!tunnelserver)
		forward_request(c);

	/* Finally, delete it. */

	if(owner->status.reachable)
		subnet_update(owner, find, false);

	subnet_del(owner, find);

	return true;
}
