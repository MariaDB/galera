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

#include "trx.h"
#include "log.h"

#include <assert.h>
#include <errno.h>  // ENOMEM, etc.
#include <stdbool.h>

wsrep_status_t
node_trx_execute(node_store_t*   const store,
                 wsrep_t*        const wsrep,
                 wsrep_conn_id_t const conn_id,
                 int                   ops_num)
{
    wsrep_status_t cert = WSREP_OK; // for cleanup

    static unsigned int const ws_flags =
        WSREP_FLAG_TRX_START | WSREP_FLAG_TRX_END; // atomic trx
    wsrep_trx_meta_t ws_meta;
    wsrep_status_t ret = WSREP_OK;

    /* prepare simple transaction and obtain a writeset handle for it */
    wsrep_ws_handle_t ws_handle = { 0, NULL };
    while (ops_num--)
    {
        if (0 != (ret = node_store_execute(store, wsrep, &ws_handle)))
        {
#if 0
            NODE_INFO("master [%d]: node_store_execute() returned %d",
                      conn_id, ret);
#endif
            ret = WSREP_TRX_FAIL;
            goto cleanup;
        }
    }

    /* REPLICATION: (replicate and) certify the writeset (pointed to by
     *              ws_handle) with the cluster */
    cert = wsrep->certify(wsrep, conn_id, &ws_handle, ws_flags, &ws_meta);

    if (WSREP_BF_ABORT == cert)
    {
        /* REPLICATION: transaction was signaled to abort due to multi-master
         *              conflict. It must rollback immediately: it blocks
         *              transaction that was ordered earlier and will never
         *              be able to enter commit order. */
        node_store_rollback(store, ws_handle.trx_id);
    }

    /* REPLICATION: writeset was totally ordered, need to enter commit order */
    if (ws_meta.gtid.seqno > 0)
    {
        ret = wsrep->commit_order_enter(wsrep, &ws_handle, &ws_meta);
        if (ret)
        {
            NODE_ERROR("master [%d]: wsrep::commit_order_enter(%lld) failed: "
                       "%d", (long long)(ws_meta.gtid.seqno), ret);
            goto cleanup;
        }

        /* REPLICATION: inside commit monitor
         * Note: we commit transaction only if certification succeded */
        if (WSREP_OK == cert)
            node_store_commit(store, ws_handle.trx_id, &ws_meta.gtid);
        else
            node_store_update_gtid(store, &ws_meta.gtid);

        ret = wsrep->commit_order_leave(wsrep, &ws_handle, &ws_meta, NULL);
        if (ret)
        {
            NODE_ERROR("master [%d]: wsrep::commit_order_leave(%lld) failed: "
                       "%d", (long long)(ws_meta.gtid.seqno), ret);
            goto cleanup;
        }
    }
    else
    {
        assert(cert);
    }

cleanup:
    /* REPLICATION: if wsrep->certify() returned anything else but WSREP_OK
     *              transaction must roll back. BF aborted trx already did it. */
    if (cert && WSREP_BF_ABORT != cert)
        node_store_rollback(store, ws_handle.trx_id);

    /* NOTE: this application follows the approach that resources must be freed
     *       at the same level where they were allocated, so it is assumed that
     *       ws_key and ws were deallocated in either commit or rollback calls.*/

    /* REPLICATION: release provider resources associated with the trx */
    wsrep->release(wsrep, &ws_handle);

    return ret ? ret : cert;
}

wsrep_status_t
node_trx_apply(node_store_t*            const store,
               wsrep_t*                 const wsrep,
               const wsrep_ws_handle_t* const ws_handle,
               const wsrep_trx_meta_t*  const ws_meta,
               const wsrep_buf_t*       const ws)
{
    /* no business being here if event was not ordered */
    assert(ws_meta->gtid.seqno > 0);

    wsrep_trx_id_t trx_id;
    wsrep_buf_t err_buf = { NULL, 0 };
    int app_err;
    if (ws)
    {
        app_err = node_store_apply(store, &trx_id, ws);
        if (app_err)
        {
            /* REPLICATION: if applying failed, prepare an error buffer with
             *              sufficient error specification */
            err_buf.ptr = &app_err; // suppose error code is enough
            err_buf.len = sizeof(app_err);
        }
    }
    else /* ws failed certification and should be skipped */
    {
        /* just some non-0 code to choose node_store_update_gtid() below */
        app_err = 1;
    }

    wsrep_status_t ret;
    ret = wsrep->commit_order_enter(wsrep, ws_handle, ws_meta);
    if (ret) {
        node_store_rollback(store, trx_id);
        return ret;
    }

    if (!app_err) node_store_commit(store, trx_id, &ws_meta->gtid);
    else          node_store_update_gtid(store, &ws_meta->gtid);

    ret = wsrep->commit_order_leave(wsrep, ws_handle, ws_meta, &err_buf);

    return ret;
}
