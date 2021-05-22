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

#include "worker.h"

#include "log.h"
#include "options.h"
#include "trx.h"
#include "wsrep.h"

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>  // strerror()

struct node_worker
{
    struct node_ctx* node;
    pthread_t        thread_id;
    size_t           id;
    bool             exit;
};

enum wsrep_cb_status
node_worker_apply_cb(void*                    const recv_ctx,
                     const wsrep_ws_handle_t* const ws_handle,
                     uint32_t                 const ws_flags,
                     const wsrep_buf_t*       const ws,
                     const wsrep_trx_meta_t*  const ws_meta,
                     wsrep_bool_t*            const exit_loop)
{
    assert(recv_ctx);

    struct node_worker* const worker = recv_ctx;

    wsrep_status_t const ret = node_trx_apply(
        worker->node->store,
        node_wsrep_provider(worker->node->wsrep),
        ws_handle,
        ws_meta,
        ws_flags & WSREP_FLAG_ROLLBACK ? NULL : ws);

    *exit_loop = worker->exit;

    return WSREP_OK == ret ? WSREP_CB_SUCCESS : WSREP_CB_FAILURE;
}


static void*
worker_slave(void* recv_ctx)
{
    struct node_worker* const worker = recv_ctx;
    wsrep_t* const wsrep = node_wsrep_provider(worker->node->wsrep);

    wsrep_status_t const ret = wsrep->recv(wsrep, worker);

    if (WSREP_OK != ret)
    {
        NODE_ERROR("slave worker [%zu] exited with error %d.", worker->id, ret);
    }

    return NULL;
}

static void*
worker_master(void* send_ctx)
{
    struct node_worker* const worker = send_ctx;
    struct node_ctx*    const node   = worker->node;
    wsrep_t*            const wsrep  = node_wsrep_provider(node->wsrep);

    assert(node->opts->ws_size > 0);

    wsrep_status_t ret;

    do
    {
        /* REPLICATION: we should not perform any local writes until the node
         * is synced with the cluster. */
        if (!node_wsrep_wait_synced(node->wsrep))
        {
            NODE_ERROR("master worker [%zu] failed waiting for SYNCED state.",
                       worker->id);
            break;
        }

        /* REPLICATION: the node is now synced */

        do
        {
            ret = node_trx_execute(node->store,
                                   wsrep,
                                   worker->id,
                                   (int)node->opts->operations);
        }
        while(WSREP_OK           == ret // success
              || (WSREP_TRX_FAIL == ret // certification failed, trx rolled back
                  && (usleep(10000),true)) // retry after short sleep
            );
    }
    while (WSREP_CONN_FAIL == ret); // provider in bad state (e.g. non-Primary)

    return NULL;
}

struct node_worker_pool
{
    size_t             size;      // size of the pool (nu,ber of nodes)
    struct node_worker worker[1]; // worker context array;
};

struct node_worker_pool*
node_worker_start(struct node_ctx*   const ctx,
                  node_worker_type_t const type,
                  size_t             const size)
{
    assert(ctx);

    if (0 == size) return NULL;

    const char* const type_str = type == NODE_WORKER_SLAVE ? "slave" : "master";

    size_t const alloc_size =
        sizeof(struct node_worker_pool) +
        sizeof(struct node_worker) * (size - 1);

    struct node_worker_pool* const ret = malloc(alloc_size);

    if (ret)
    {
        void* (* const routine) (void*) =
            type == NODE_WORKER_SLAVE ? worker_slave : worker_master;

        size_t i;
        for (i = 0; i < size; i++)
        {
            struct node_worker* const worker = &ret->worker[i];
            worker->node = ctx;
            worker->id   = i;
            worker->exit = false;

            int const err = pthread_create(&worker->thread_id,
                                           NULL,
                                           routine,
                                           worker);
            if (err)
            {
                NODE_ERROR("Failed to start %s worker[%zu]: %d (%s)",
                           type_str, i, err, strerror(err));
                if (0 == i)
                {
                    free(ret);
                    return NULL;
                }
                else
                {
                    break; // some threads have started,
                           // need to return to close them first
                }
            }
        }

        ret->size = i;
    }
    else
    {
        NODE_ERROR("Failed to allocate %zu bytes for the %s worker pool",
                   alloc_size, type_str);
    }

    return ret;
}

void
node_worker_stop(struct node_worker_pool* pool)
{
    size_t i;
    for (i = 0; pool && i < pool->size; i++)
    {
        pthread_join(pool->worker[i].thread_id, NULL);
    }

    free(pool);
}
