/*
    net.h -- generic header for device.c
    Copyright (C) 2001-2005 Ivo Timmermans <zarq@iname.com>
                  2001-2006 Guus Sliepen <guus@tinc-vpn.org>

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

    $Id: device.h 1452 2006-04-26 13:52:58Z guus $
*/

#ifndef __TINC_DEVICE_H__
#define __TINC_DEVICE_H__

#include "net.h"

extern int device_fd;
extern char *device;

extern char *iface;

extern bool setup_device(void);
extern void close_device(void);
extern bool read_packet(struct vpn_packet_t *);
extern bool write_packet(struct vpn_packet_t *);
extern void dump_device_stats(void);

#endif							/* __TINC_DEVICE_H__ */
