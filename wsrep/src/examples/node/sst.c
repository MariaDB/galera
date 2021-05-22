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

#include "sst.h"

#include "ctx.h"
#include "log.h"
#include "socket.h"

#include <arpa/inet.h> // htonl()
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>  // snprintf()
#include <stdlib.h> // abort()
#include <string.h> // strdup()
#include <unistd.h> // usleep()

/**
 * Helper: creates detached thread */
static int
sst_create_thread(void* (*thread_routine) (void*),
                  void* const thread_arg)
{
    pthread_t thr;
    pthread_attr_t attr;
    int ret = pthread_attr_init(&attr);
    ret = ret ? ret : pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
    ret = ret ? ret : pthread_create(&thr, &attr, thread_routine, thread_arg);
    return ret;
}

/**
 * Helper: creates detached thread and waits for it to call
 *         sst_sync_with_parent() */
static void
sst_create_and_sync(const char*      const role,
                    pthread_mutex_t* const mtx,
                    pthread_cond_t*  const cond,
                    void* (*thread_routine) (void*),
                    void*            const thread_arg)
{
    int ret = pthread_mutex_lock(mtx);
    if (ret)
    {
        NODE_FATAL("Failed to lock %s mutex: %d (%s)", role, ret, strerror(ret));
        abort();
    }

    ret = sst_create_thread(thread_routine, thread_arg);
    if (ret)
    {
        NODE_FATAL("Failed to create detached %s thread: %d (%s)",
                   role, ret, strerror(ret));
        abort();
    }

    ret = pthread_cond_wait(cond, mtx);
    if (ret)
    {
        NODE_FATAL("Failed to synchronize with %s thread: %d (%s)",
                   role, ret, strerror(ret));
        abort();
    }

    pthread_mutex_unlock(mtx);
}

/**
 * Helper: syncs with parent thread and allows it to continue and return
 *         asynchronously */
static void
sst_sync_with_parent(const char*      role,
                     pthread_mutex_t* mtx,
                     pthread_cond_t*  cond)
{
    int ret = pthread_mutex_lock(mtx);
    if (ret)
    {
        NODE_FATAL("Failed to lock %s mutex: %d (%s)", role, ret, strerror(ret));
        abort();
    }

    NODE_INFO("Initialized %s thread", role);

    pthread_cond_signal(cond);
    pthread_mutex_unlock(mtx);
}

static pthread_mutex_t sst_joiner_mtx  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  sst_joiner_cond = PTHREAD_COND_INITIALIZER;

struct sst_joiner_ctx
{
    struct node_ctx* node;
    node_socket_t*   socket;
};

/**
 * waits for SST completion and signals the provider to continue */
static void*
sst_joiner_thread(void* ctx)
{
    assert(ctx);

    struct node_ctx* const node   = ((struct sst_joiner_ctx*)ctx)->node;
    node_socket_t* const   listen = ((struct sst_joiner_ctx*)ctx)->socket;
    ctx = NULL; /* may be unusable after next statement */

    /* this allows parent callback to return */
    sst_sync_with_parent("JOINER", &sst_joiner_mtx, &sst_joiner_cond);

    wsrep_gtid_t state_gtid = WSREP_GTID_UNDEFINED;
    int err = -1;

    /* REPLICATION: wait for donor to connect and send the state snapshot */
    node_socket_t* const connected = node_socket_accept(listen);
    if (!connected) goto end;

    uint32_t state_len;
    err = node_socket_recv_bytes(connected, &state_len, sizeof(state_len));
    if (err) goto end;

    state_len = ntohl(state_len);
    if (state_len > 0)
    {
        /* REPLICATION: get the state of state_len size */
        void* state = malloc(state_len);
        if (state)
        {
            err = node_socket_recv_bytes(connected, state, state_len);
            if (err)
            {
                free(state);
                goto end;
            }

            /* REPLICATION: install the newly received state. */
            err = node_store_init_state(node->store, state, state_len);
            free(state);
            if (err) goto end;
        }
        else
        {
            NODE_ERROR("Failed to allocate %zu bytes for state snapshot.",
                       state_len);
            err = -ENOMEM;
            goto end;
        }
    }
    else
    {
        /* REPLICATION: it was a bypass, the node will receive missing data via
         *              IST. It starts with the state it currently has. */
    }

    /* REPLICATION: find gtid of the received state to report to provider */
    node_store_gtid(node->store, &state_gtid);

end:
    assert(err <= 0);
    node_socket_close(connected);
    node_socket_close(listen);

    /* REPLICATION: tell provider that SST is received */
    wsrep_status_t sst_ret;
    wsrep_t* const wsrep = node_wsrep_provider(node->wsrep);
    sst_ret = wsrep->sst_received(wsrep, &state_gtid, NULL, err);

    if (WSREP_OK != sst_ret)
    {
        NODE_FATAL("Failed to report completion of SST: %d", sst_ret);
        abort();
    }

    return NULL;
}

enum wsrep_cb_status
node_sst_request_cb (void*   const app_ctx,
                     void**  const sst_req,
                     size_t* const sst_req_len)
{
    static int const SST_PORT_OFFSET = 2;

    assert(app_ctx);
    struct node_ctx* const node = app_ctx;
    const struct node_options* const opts = node->opts;

    char* sst_str = NULL;

    /* REPLICATION: 1. prepare the node to receive SST */
    uint16_t const sst_port = (uint16_t)(opts->base_port + SST_PORT_OFFSET);
    size_t const sst_len = strlen(opts->base_host)
        + 1 /* ':' */ + 5 /* max port len */ + 1 /* \0 */;
    sst_str = malloc(sst_len);
    if (!sst_str)
    {
        NODE_ERROR("Failed to allocate %zu bytes for SST request", sst_len);
        goto end;
    }

    /* write in request the address at which we listen */
    int ret = snprintf(sst_str, sst_len, "%s:%hu", opts->base_host, sst_port);
    if (ret < 0 || (size_t)ret >= sst_len)
    {
        free(sst_str);
        sst_str = NULL;
        NODE_ERROR("Failed to write a SST request");
        goto end;
    }

    node_socket_t* const socket = node_socket_listen(NULL, sst_port);
    if (!socket)
    {
        free(sst_str);
        sst_str = NULL;
        NODE_ERROR("Failed to listen at %s", sst_str);
        goto end;
    }

    /* REPLICATION 2. start the "joiner" thread that will wait for SST and
     *                report its success to provider, and syncronize with it. */
    struct sst_joiner_ctx ctx =
        {
            .node   = node,
            .socket = socket
        };
    sst_create_and_sync("JOINER", &sst_joiner_mtx, &sst_joiner_cond,
                        sst_joiner_thread, &ctx);

    NODE_INFO("Waiting for SST at %s", sst_str);

end:
    if (sst_str)
    {
        *sst_req     = sst_str;
        *sst_req_len = strlen(sst_str) + 1;
    }
    else
    {
        *sst_req     = NULL;
        *sst_req_len = 0;
        return WSREP_CB_FAILURE;
    }

    /* REPLICATION 3. return SST request to provider */
    return WSREP_CB_SUCCESS;
}

static pthread_mutex_t sst_donor_mtx  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  sst_donor_cond = PTHREAD_COND_INITIALIZER;

struct sst_donor_ctx
{
    wsrep_gtid_t     state;
    struct node_ctx* node;
    node_socket_t*   socket;
    wsrep_bool_t     bypass;
};

/**
 * donates SST and signals provider that it is done. */
static void*
sst_donor_thread(void* const args)
{
    struct sst_donor_ctx const ctx = *(struct sst_donor_ctx*)args;

    int err = 0;
    const void* state;
    size_t state_len;

    if (ctx.bypass)
    {
        /* REPLICATION: if bypass is true, there is no need to send snapshot,
         *              just signal the joiner that snapshot is not needed and
         *              it can proceed to apply IST. We'll do it by sending 0
         *              for the size of snapshot */
        state = NULL;
        state_len = 0;
    }
    else
    {
        /* REPLICATION: if bypass is false, we need to send a full state snapshot
         *              Get hold of the state, which is currently just GTID
         *              NOTICE that while parent is waiting, the store is in a
         *              quiescent state, provider blocking any modifications. */
        err = node_store_acquire_state(ctx.node->store, &state, &state_len);
        if (state_len > UINT32_MAX) err = -ERANGE;
    }

    /* REPLICATION: after getting hold of the state we can allow parent callback
     *              to return and the node to resume its normal operation */
    sst_sync_with_parent("DONOR", &sst_donor_mtx, &sst_donor_cond);

    if (err >= 0)
    {
        uint32_t tmp = htonl((uint32_t)state_len);
        err = node_socket_send_bytes(ctx.socket, &tmp, sizeof(tmp));
    }

    if (state_len != 0)
    {
        if (err >= 0)
        {
            assert(state);
            err = node_socket_send_bytes(ctx.socket, state, state_len);
        }

        node_store_release_state(ctx.node->store);
    }

    node_socket_close(ctx.socket);

    /* REPLICATION: signal provider the success of the operation */
    wsrep_t* const wsrep = node_wsrep_provider(ctx.node->wsrep);
    wsrep->sst_sent(wsrep, &ctx.state, err);

    return NULL;
}

enum wsrep_cb_status
node_sst_donate_cb (void*               const app_ctx,
                    void*               const recv_ctx,
                    const wsrep_buf_t*  const str_msg,
                    const wsrep_gtid_t* const state_id,
                    const wsrep_buf_t*  const state,
                    wsrep_bool_t        const bypass)
{
    (void)recv_ctx;
    (void)state;

    struct sst_donor_ctx ctx =
    {
        .node   = app_ctx,
        .state  = *state_id,
        .bypass = bypass
    };

    /* we are expecting a human-readable 0-terminated string  */
    void* p = memchr(str_msg->ptr, '\0', str_msg->len);
    if (!p)
    {
        NODE_ERROR("Received a badly formed State Transfer Request.");
        /* REPLICATION: in case of a failure we return the status to provider, so
         *              that the joining node can be notified of it by cluster */
        return WSREP_CB_FAILURE;
    }

    const char* addr = str_msg->ptr;
    ctx.socket = node_socket_connect(addr);

    if (!ctx.socket) return WSREP_CB_FAILURE;

    sst_create_and_sync("DONOR", &sst_donor_mtx, &sst_donor_cond,
                        sst_donor_thread, &ctx);

    return WSREP_CB_SUCCESS;
}
