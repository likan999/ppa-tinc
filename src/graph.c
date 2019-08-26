/*
    graph.c -- graph algorithms
    Copyright (C) 2001-2005 Guus Sliepen <guus@tinc-vpn.org>,
                  2001-2005 Ivo Timmermans <ivo@tinc-vpn.org>

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

    $Id: graph.c 1439 2005-05-04 18:09:30Z guus $
*/

/* We need to generate two trees from the graph:

   1. A minimum spanning tree for broadcasts,
   2. A single-source shortest path tree for unicasts.

   Actually, the first one alone would suffice but would make unicast packets
   take longer routes than necessary.

   For the MST algorithm we can choose from Prim's or Kruskal's. I personally
   favour Kruskal's, because we make an extra AVL tree of edges sorted on
   weights (metric). That tree only has to be updated when an edge is added or
   removed, and during the MST algorithm we just have go linearly through that
   tree, adding safe edges until #edges = #nodes - 1. The implementation here
   however is not so fast, because I tried to avoid having to make a forest and
   merge trees.

   For the SSSP algorithm Dijkstra's seems to be a nice choice. Currently a
   simple breadth-first search is presented here.

   The SSSP algorithm will also be used to determine whether nodes are directly,
   indirectly or not reachable from the source. It will also set the correct
   destination address and port of a node if possible.
*/

#include "system.h"

#include "avl_tree.h"
#include "connection.h"
#include "device.h"
#include "edge.h"
#include "logger.h"
#include "netutl.h"
#include "node.h"
#include "process.h"
#include "subnet.h"
#include "utils.h"

/* Implementation of Kruskal's algorithm.
   Running time: O(EN)
   Please note that sorting on weight is already done by add_edge().
*/

void mst_kruskal(void)
{
	avl_node_t *node, *next;
	edge_t *e;
	node_t *n;
	connection_t *c;
	int nodes = 0;
	int safe_edges = 0;
	bool skipped;

	cp();
	
	/* Clear MST status on connections */

	for(node = connection_tree->head; node; node = node->next) {
		c = node->data;
		c->status.mst = false;
	}

	/* Do we have something to do at all? */

	if(!edge_weight_tree->head)
		return;

	ifdebug(SCARY_THINGS) logger(LOG_DEBUG, "Running Kruskal's algorithm:");

	/* Clear visited status on nodes */

	for(node = node_tree->head; node; node = node->next) {
		n = node->data;
		n->status.visited = false;
		nodes++;
	}

	/* Starting point */

	((edge_t *) edge_weight_tree->head->data)->from->status.visited = true;

	/* Add safe edges */

	for(skipped = false, node = edge_weight_tree->head; node; node = next) {
		next = node->next;
		e = node->data;

		if(!e->reverse || e->from->status.visited == e->to->status.visited) {
			skipped = true;
			continue;
		}

		e->from->status.visited = true;
		e->to->status.visited = true;

		if(e->connection)
			e->connection->status.mst = true;

		if(e->reverse->connection)
			e->reverse->connection->status.mst = true;

		safe_edges++;

		ifdebug(SCARY_THINGS) logger(LOG_DEBUG, " Adding edge %s - %s weight %d", e->from->name,
				   e->to->name, e->weight);

		if(skipped) {
			skipped = false;
			next = edge_weight_tree->head;
			continue;
		}
	}

	ifdebug(SCARY_THINGS) logger(LOG_DEBUG, "Done, counted %d nodes and %d safe edges.", nodes,
			   safe_edges);
}

/* Implementation of a simple breadth-first search algorithm.
   Running time: O(E)
*/

void sssp_bfs(void)
{
	avl_node_t *node, *next, *to;
	edge_t *e;
	node_t *n;
	list_t *todo_list;
	list_node_t *from, *todonext;
	bool indirect;
	char *name;
	char *address, *port;
	char *envp[7];
	int i;

	cp();

	todo_list = list_alloc(NULL);

	/* Clear visited status on nodes */

	for(node = node_tree->head; node; node = node->next) {
		n = node->data;
		n->status.visited = false;
		n->status.indirect = true;
	}

	/* Begin with myself */

	myself->status.visited = true;
	myself->status.indirect = false;
	myself->nexthop = myself;
	myself->via = myself;
	list_insert_head(todo_list, myself);

	/* Loop while todo_list is filled */

	for(from = todo_list->head; from; from = todonext) {	/* "from" is the node from which we start */
		n = from->data;

		for(to = n->edge_tree->head; to; to = to->next) {	/* "to" is the edge connected to "from" */
			e = to->data;

			if(!e->reverse)
				continue;

			/* Situation:

				   /
				  /
			   ----->(n)---e-->(e->to)
				  \
				   \

			   Where e is an edge, (n) and (e->to) are nodes.
			   n->address is set to the e->address of the edge left of n to n.
			   We are currently examining the edge e right of n from n:

			   - If e->reverse->address != n->address, then e->to is probably
			     not reachable for the nodes left of n. We do as if the indirectdata
			     flag is set on edge e.
			   - If edge e provides for better reachability of e->to, update
			     e->to and (re)add it to the todo_list to (re)examine the reachability
			     of nodes behind it.
			 */

			indirect = n->status.indirect || e->options & OPTION_INDIRECT
				|| ((n != myself) && sockaddrcmp(&n->address, &e->reverse->address));

			if(e->to->status.visited
			   && (!e->to->status.indirect || indirect))
				continue;

			e->to->status.visited = true;
			e->to->status.indirect = indirect;
			e->to->nexthop = (n->nexthop == myself) ? e->to : n->nexthop;
			e->to->via = indirect ? n->via : e->to;
			e->to->options = e->options;

			if(sockaddrcmp(&e->to->address, &e->address)) {
				node = avl_unlink(node_udp_tree, e->to);
				sockaddrfree(&e->to->address);
				sockaddrcpy(&e->to->address, &e->address);

				if(e->to->hostname)
					free(e->to->hostname);

				e->to->hostname = sockaddr2hostname(&e->to->address);

				if(node)
					avl_insert_node(node_udp_tree, node);

				if(e->to->options & OPTION_PMTU_DISCOVERY) {
					e->to->mtuprobes = 0;
					e->to->minmtu = 0;
					e->to->maxmtu = MTU;
					if(e->to->status.validkey)
						send_mtu_probe(e->to);
				}
			}

			list_insert_tail(todo_list, e->to);
		}

		todonext = from->next;
		list_delete_node(todo_list, from);
	}

	list_free(todo_list);

	/* Check reachability status. */

	for(node = node_tree->head; node; node = next) {
		next = node->next;
		n = node->data;

		if(n->status.visited != n->status.reachable) {
			n->status.reachable = !n->status.reachable;

			if(n->status.reachable) {
				ifdebug(TRAFFIC) logger(LOG_DEBUG, _("Node %s (%s) became reachable"),
					   n->name, n->hostname);
				avl_insert(node_udp_tree, n);
			} else {
				ifdebug(TRAFFIC) logger(LOG_DEBUG, _("Node %s (%s) became unreachable"),
					   n->name, n->hostname);
				avl_delete(node_udp_tree, n);
			}

			n->status.validkey = false;
			n->status.waitingforkey = false;

			n->maxmtu = MTU;
			n->minmtu = 0;
			n->mtuprobes = 0;

			asprintf(&envp[0], "NETNAME=%s", netname ? : "");
			asprintf(&envp[1], "DEVICE=%s", device ? : "");
			asprintf(&envp[2], "INTERFACE=%s", iface ? : "");
			asprintf(&envp[3], "NODE=%s", n->name);
			sockaddr2str(&n->address, &address, &port);
			asprintf(&envp[4], "REMOTEADDRESS=%s", address);
			asprintf(&envp[5], "REMOTEPORT=%s", port);
			envp[6] = NULL;

			asprintf(&name,
					 n->status.reachable ? "hosts/%s-up" : "hosts/%s-down",
					 n->name);
			execute_script(name, envp);

			free(name);
			free(address);
			free(port);

			for(i = 0; i < 6; i++)
				free(envp[i]);

			subnet_update(n, NULL, n->status.reachable);
		}
	}
}

void graph(void)
{
	mst_kruskal();
	sssp_bfs();
}
