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
 * @file This unit defines various helpers to manage wsrep provider
 */

#ifndef NODE_WSREP_H
#define NODE_WSREP_H

#include "options.h"

#include "../../wsrep_api.h"

#include <pthread.h>
#include <stdbool.h>

typedef struct node_wsrep node_wsrep_t;

/**
 * loads and initializes wsrep provider for further usage
 *
 * @param[in] opts         program options
 * @param[in] current_gtid GTID corresponding to the current node state
 * @param[in] app_ctx      application context to be passed to callbacks
 *
 * @return initialized object pointer
 */
extern node_wsrep_t*
node_wsrep_init(const struct node_options* opts,
                const wsrep_gtid_t*        current_gtid,
                void*                      app_ctx);

/**
 * connects to primary component
 *
 * @param[in] wsrep      wsrep context
 * @param[in] address    address to connect at (provider specific)
 * @param[in] bootsstrap bootstrap primary component if there's none
 *
 * @return wsrep status code
 */
extern wsrep_status_t
node_wsrep_connect(node_wsrep_t* wsrep,
                   const char*   address,
                   bool          bootstrap);

/**
 * disconnects from primary component
 */
extern void
node_wsrep_disconnect(node_wsrep_t* wsrep);

/**
 * deinitializes and unloads wsrep provider
 */
extern void
node_wsrep_close(node_wsrep_t* wsrep);

/**
 * waits for the node to become SYNCED
 *
 * @return true if node is synced, false in any other event.
 */
extern bool
node_wsrep_wait_synced(node_wsrep_t* wsrep);

/**
 * @param[in]  wsrep context
 * @param[out] gtid of the current view */
extern void
node_wsrep_connected_gtid(node_wsrep_t* wsrep, wsrep_gtid_t* gtid);

/**
 * @return wsrep provider instance */
extern wsrep_t*
node_wsrep_provider(node_wsrep_t* wsrep);

#endif /* NODE_WSREP_H */
