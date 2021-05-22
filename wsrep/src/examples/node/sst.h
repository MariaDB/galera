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
 * @file This unit defines SST interface
 */

#ifndef NODE_SST_H
#define NODE_SST_H

#include "../../wsrep_api.h"

extern enum wsrep_cb_status
node_sst_request_cb (void*   app_ctx,
                     void**  sst_req,
                     size_t* sst_req_len);

extern enum wsrep_cb_status
node_sst_donate_cb (void*               app_ctx,
                    void*               recv_ctx,
                    const wsrep_buf_t*  str_msg,
                    const wsrep_gtid_t* state_id,
                    const wsrep_buf_t*  state,
                    wsrep_bool_t        bypass);

#endif /* NODE_SST_H */
