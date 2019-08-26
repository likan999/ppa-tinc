/*
    meta.c -- handle the meta communication
    Copyright (C) 2000-2013 Guus Sliepen <guus@tinc-vpn.org>,
                  2000-2005 Ivo Timmermans
                  2006      Scott Lamb <slamb@slamb.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "system.h"

#include <openssl/err.h>
#include <openssl/evp.h>

#include "avl_tree.h"
#include "connection.h"
#include "logger.h"
#include "meta.h"
#include "net.h"
#include "protocol.h"
#include "utils.h"
#include "xalloc.h"

bool send_meta(connection_t *c, const char *buffer, int length) {
	int outlen;
	int result;

	if(!c) {
		logger(LOG_ERR, "send_meta() called with NULL pointer!");
		abort();
	}

	ifdebug(META) logger(LOG_DEBUG, "Sending %d bytes of metadata to %s (%s)", length,
			   c->name, c->hostname);

	if(!c->outbuflen)
		c->last_flushed_time = now;

	/* Find room in connection's buffer */
	if(length + c->outbuflen > c->outbufsize) {
		c->outbufsize = length + c->outbuflen;
		c->outbuf = xrealloc(c->outbuf, c->outbufsize);
	}

	if(length + c->outbuflen + c->outbufstart > c->outbufsize) {
		memmove(c->outbuf, c->outbuf + c->outbufstart, c->outbuflen);
		c->outbufstart = 0;
	}

	/* Add our data to buffer */
	if(c->status.encryptout) {
		result = EVP_EncryptUpdate(c->outctx, (unsigned char *)c->outbuf + c->outbufstart + c->outbuflen,
				&outlen, (unsigned char *)buffer, length);
		if(!result || outlen < length) {
			logger(LOG_ERR, "Error while encrypting metadata to %s (%s): %s",
					c->name, c->hostname, ERR_error_string(ERR_get_error(), NULL));
			return false;
		} else if(outlen > length) {
			logger(LOG_EMERG, "Encrypted data too long! Heap corrupted!");
			abort();
		}
		c->outbuflen += outlen;
	} else {
		memcpy(c->outbuf + c->outbufstart + c->outbuflen, buffer, length);
		c->outbuflen += length;
	}

	return true;
}

bool flush_meta(connection_t *c) {
	int result;
	
	ifdebug(META) logger(LOG_DEBUG, "Flushing %d bytes to %s (%s)",
			 c->outbuflen, c->name, c->hostname);

	while(c->outbuflen) {
		result = send(c->socket, c->outbuf + c->outbufstart, c->outbuflen, 0);
		if(result <= 0) {
			if(!errno || errno == EPIPE) {
				ifdebug(CONNECTIONS) logger(LOG_NOTICE, "Connection closed by %s (%s)",
						   c->name, c->hostname);
			} else if(errno == EINTR) {
				continue;
			} else if(sockwouldblock(sockerrno)) {
				ifdebug(CONNECTIONS) logger(LOG_DEBUG, "Flushing %d bytes to %s (%s) would block",
						c->outbuflen, c->name, c->hostname);
				return true;
			} else {
				logger(LOG_ERR, "Flushing meta data to %s (%s) failed: %s", c->name,
					   c->hostname, sockstrerror(sockerrno));
			}

			return false;
		}

		c->outbufstart += result;
		c->outbuflen -= result;
	}

	c->outbufstart = 0; /* avoid unnecessary memmoves */
	return true;
}

void broadcast_meta(connection_t *from, const char *buffer, int length) {
	avl_node_t *node;
	connection_t *c;

	for(node = connection_tree->head; node; node = node->next) {
		c = node->data;

		if(c != from && c->status.active)
			send_meta(c, buffer, length);
	}
}

bool receive_meta(connection_t *c) {
	int oldlen, i, result;
	int lenin, lenout, reqlen;
	bool decrypted = false;
	char inbuf[MAXBUFSIZE];

	/* Strategy:
	   - Read as much as possible from the TCP socket in one go.
	   - Decrypt it.
	   - Check if a full request is in the input buffer.
	   - If yes, process request and remove it from the buffer,
	   then check again.
	   - If not, keep stuff in buffer and exit.
	 */

	lenin = recv(c->socket, c->buffer + c->buflen, MAXBUFSIZE - c->buflen, 0);

	if(lenin <= 0) {
		if(!lenin || !errno) {
			ifdebug(CONNECTIONS) logger(LOG_NOTICE, "Connection closed by %s (%s)",
					   c->name, c->hostname);
		} else if(sockwouldblock(sockerrno))
			return true;
		else
			logger(LOG_ERR, "Metadata socket read error for %s (%s): %s",
				   c->name, c->hostname, sockstrerror(sockerrno));

		return false;
	}

	oldlen = c->buflen;
	c->buflen += lenin;

	while(lenin > 0) {
		/* Decrypt */

		if(c->status.decryptin && !decrypted) {
			result = EVP_DecryptUpdate(c->inctx, (unsigned char *)inbuf, &lenout, (unsigned char *)c->buffer + oldlen, lenin);
			if(!result || lenout != lenin) {
				logger(LOG_ERR, "Error while decrypting metadata from %s (%s): %s",
						c->name, c->hostname, ERR_error_string(ERR_get_error(), NULL));
				return false;
			}
			memcpy(c->buffer + oldlen, inbuf, lenin);
			decrypted = true;
		}

		/* Are we receiving a TCPpacket? */

		if(c->tcplen) {
			if(c->tcplen <= c->buflen) {
				if(!c->node) {
					if(c->outgoing && proxytype == PROXY_SOCKS4 && c->allow_request == ID) {
						if(c->buffer[0] == 0 && c->buffer[1] == 0x5a) {
							logger(LOG_DEBUG, "Proxy request granted");
						} else {
							logger(LOG_ERR, "Proxy request rejected");
							return false;
						}
					} else if(c->outgoing && proxytype == PROXY_SOCKS5 && c->allow_request == ID) {
						if(c->buffer[0] != 5) {
							logger(LOG_ERR, "Invalid response from proxy server");
							return false;
						}
						if(c->buffer[1] == (char)0xff) {
							logger(LOG_ERR, "Proxy request rejected: unsuitable authentication method");
							return false;
						}
						if(c->buffer[2] != 5) {
							logger(LOG_ERR, "Invalid response from proxy server");
							return false;
						}
						if(c->buffer[3] == 0) {
							logger(LOG_DEBUG, "Proxy request granted");
						} else {
							logger(LOG_DEBUG, "Proxy request rejected");
							return false;
						}
					} else {
						logger(LOG_ERR, "c->tcplen set but c->node is NULL!");
						abort();
					}
				} else {
					if(c->allow_request == ALL) {
						receive_tcppacket(c, c->buffer, c->tcplen);
					} else {
						logger(LOG_ERR, "Got unauthorized TCP packet from %s (%s)", c->name, c->hostname);
						return false;
					}
				}

				c->buflen -= c->tcplen;
				lenin -= c->tcplen - oldlen;
				memmove(c->buffer, c->buffer + c->tcplen, c->buflen);
				oldlen = 0;
				c->tcplen = 0;
				continue;
			} else {
				break;
			}
		}

		/* Otherwise we are waiting for a request */

		reqlen = 0;

		for(i = oldlen; i < c->buflen; i++) {
			if(c->buffer[i] == '\n') {
				c->buffer[i] = '\0';	/* replace end-of-line by end-of-string so we can use sscanf */
				reqlen = i + 1;
				break;
			}
		}

		if(reqlen) {
			c->reqlen = reqlen;
			if(!receive_request(c))
				return false;

			c->buflen -= reqlen;
			lenin -= reqlen - oldlen;
			memmove(c->buffer, c->buffer + reqlen, c->buflen);
			oldlen = 0;
			continue;
		} else {
			break;
		}
	}

	if(c->buflen >= MAXBUFSIZE) {
		logger(LOG_ERR, "Metadata read buffer overflow for %s (%s)",
			   c->name, c->hostname);
		return false;
	}

	return true;
}
