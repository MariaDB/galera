/* Copyright (c) 2019, Codership Oy. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

/**
 * @file This unit implements auxiliary networking functions (for SST purposes)
 *       It has nothing wsrep related and is not of general purpose.
 */

#ifndef NODE_SOCKET_H
#define NODE_SOCKET_H

#include <stddef.h> // size_t
#include <stdint.h> // uint16_t

typedef struct node_socket node_socket_t;

/**
 * Open listening socket at a given address
 *
 * @return listening socket
 */
extern node_socket_t*
node_socket_listen(const char* host, uint16_t port);

/**
 * Connect to a given address.
 *
 * @return connected socket
 */
extern node_socket_t*
node_socket_connect(const char* addr);

/**
 * Wait for connection on a listening socket
 * @return connected socket
 */
extern node_socket_t*
node_socket_accept(node_socket_t* s);

/**
 * Send a given number of bytes
 * @return 0 or a negative error code
 */
extern int
node_socket_send_bytes(node_socket_t* s, const void* buf, size_t len);

/**
 * Receive a given number of bytes
 * @return 0 or a negative error code
 */
extern int
node_socket_recv_bytes(node_socket_t* s, void* buf, size_t len);

/**
 * Release all recources associated with the socket */
extern void
node_socket_close(node_socket_t* s);

#endif /* NODE_SOCKET_H */
