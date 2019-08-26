/*
    route.h -- header file for route.c
    Copyright (C) 2000-2005 Ivo Timmermans <zarq@iname.com>
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

    $Id: route.h 1452 2006-04-26 13:52:58Z guus $
*/

#ifndef __TINC_ROUTE_H__
#define __TINC_ROUTE_H__

#include "net.h"
#include "node.h"

typedef enum rmode_t {
	RMODE_HUB = 0,
	RMODE_SWITCH,
	RMODE_ROUTER,
} rmode_t;

extern rmode_t routing_mode;
extern bool overwrite_mac;
extern bool priorityinheritance;
extern int macexpire;

extern mac_t mymac;

extern void age_subnets(void);
extern void route(struct node_t *, struct vpn_packet_t *);

#endif							/* __TINC_ROUTE_H__ */
