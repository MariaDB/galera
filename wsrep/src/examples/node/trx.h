/* Copyright (c) 2019-2020, Codership Oy. All rights reserved.
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
 * @file This unit defines "transaction" interface
 */

#ifndef NODE_TRX_H
#define NODE_TRX_H

#include "store.h"

#include "../../wsrep_api.h"

/**
 * executes and replicates local transaction
 */
extern wsrep_status_t
node_trx_execute(node_store_t*   store,
                 wsrep_t*        wsrep,
                 wsrep_conn_id_t conn_id,
                 int             ops_num);

/**
 * applies and commits slave write set
 *
 * @param ws replicated event writeset. NULL if it failed certification (and so
 *           must be skipped, but it was ordered, so store GTID must be updated)
 */
extern wsrep_status_t
node_trx_apply(node_store_t*            store,
               wsrep_t*                 wsrep,
               const wsrep_ws_handle_t* ws_handle,
               const wsrep_trx_meta_t*  ws_meta,
               const wsrep_buf_t*       ws);

#endif /* NODE_TRX_H */
