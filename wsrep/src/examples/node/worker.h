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
 * @file This unit defines worker thread interface
 */

#ifndef NODE_WORKER_H
#define NODE_WORKER_H

#include "ctx.h"

#include "../../wsrep_api.h"

/**
 * REPLICATION: a callback to apply and commit slave replication events */
extern enum wsrep_cb_status
node_worker_apply_cb(void*                    recv_ctx,
                     const wsrep_ws_handle_t* ws_handle,
                     uint32_t                 ws_flags,
                     const wsrep_buf_t*       ws,
                     const wsrep_trx_meta_t*  ws_meta,
                     wsrep_bool_t*            exit_loop);

typedef enum node_worker_type
{
    NODE_WORKER_SLAVE,
    NODE_WORKER_MASTER
}
    node_worker_type_t;

struct node_worker_pool;

/**
 * Starts the required number of workier threads of a given type
 *
 * @param[in] ctx application context
 * @param[in] type of a worker
 * @param[in] number of workers
 *
 * @return worker pool handle
 */
extern struct node_worker_pool*
node_worker_start(struct node_ctx*   ctx,
                  node_worker_type_t type,
                  size_t             number);

/**
 * Stops workers in a pool and deallocates respective resources */
extern void
node_worker_stop(struct node_worker_pool* pool);

#endif /* NODE_WORKER_H */
