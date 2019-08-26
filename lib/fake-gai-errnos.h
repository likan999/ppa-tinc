/*
 * fake library for ssh
 *
 * This file is included in getaddrinfo.c and getnameinfo.c.
 * See getaddrinfo.c and getnameinfo.c.
 */

/* $Id: fake-gai-errnos.h 1489 2006-12-18 11:41:53Z guus $ */

/* for old netdb.h */
#ifndef EAI_NODATA
#define EAI_NODATA	1
#endif

#ifndef EAI_MEMORY
#define EAI_MEMORY	2
#endif

#ifndef EAI_FAMILY
#define EAI_FAMILY	3
#endif
