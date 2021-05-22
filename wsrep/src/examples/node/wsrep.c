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

#include "wsrep.h"

#include "log.h"
#include "sst.h"
#include "store.h"
#include "worker.h"

#include <assert.h>
#include <stdio.h>  // snprintf()
#include <stdlib.h> // abort()
#include <string.h> // strcasecmp()

struct node_wsrep
{
    wsrep_t* instance; // wsrep provider instance

    struct wsrep_view
    {
        pthread_mutex_t      mtx;
        wsrep_gtid_t         state_id;
        wsrep_view_status_t  status;
        wsrep_cap_t          capabilities;
        int                  proto_ver;
        int                  memb_num;
        int                  my_idx;
        wsrep_member_info_t* members;
    }
        view;

    struct
    {
        pthread_mutex_t mtx;
        pthread_cond_t  cond;
        int             value;
    }
            synced;

    bool bootstrap; // shall this node bootstrap a primary view?
};

static struct node_wsrep s_wsrep =
{
    .instance = NULL,
    .view =
    {
        .mtx          = PTHREAD_MUTEX_INITIALIZER,
        .state_id     = {{{ 0, }}, WSREP_SEQNO_UNDEFINED },
        .status       = WSREP_VIEW_DISCONNECTED,
        .capabilities = 0,
        .proto_ver    = -1,
        .memb_num     = 0,
        .my_idx       = -1,
        .members      = NULL
    },
    .synced =
    {
        .mtx   = PTHREAD_MUTEX_INITIALIZER,
        .cond  = PTHREAD_COND_INITIALIZER,
        .value = 0
    },
    .bootstrap = false
};

static const char* wsrep_view_status_str[WSREP_VIEW_MAX] =
{
    "PRIMARY",
    "NON-PRIMARY",
    "DISCONNECTED"
};

#define WSREP_CAPABILITIES_MAX ((int)sizeof(wsrep_cap_t) * 8) // bitmask
static const char* wsrep_capabilities_str[WSREP_CAPABILITIES_MAX] =
{
    "MULTI-MASTER",
    "CERTIFICATION",
    "PA",
    "REPLAY",
    "TOI",
    "PAUSE",
    "CAUSAL-READS",
    "CAUSAL-TRX",
    "INCREMENTAL",
    "SESSION-LOCKS",
    "DISTRIBUTED-LOCKS",
    "CONSISTENCY-CHECK",
    "UNORDERED",
    "ANNOTATION",
    "PREORDERED",
    "STREAMING",
    "SNAPSHOT",
    "NBO",
    NULL,
};

/**
 * REPLICATION: callback is called by provider when the node connects to group.
 *              This happens out-of-order, before the node receives a state
 *              transfer and syncs with the cluster. Unless application requires
 *              it it can be empty. We however want to know the GTID of the
 *              group out of order for SST tricks, so we record it out of order.
 */
static enum wsrep_cb_status
wsrep_connected_cb(void*                    const x,
                   const wsrep_view_info_t* const v)
{
    char gtid_str[WSREP_GTID_STR_LEN + 1];
    wsrep_gtid_print(&v->state_id, gtid_str, sizeof(gtid_str));

    NODE_INFO("connect_cb(): Connected at %s to %s group of %d member(s)",
              gtid_str, wsrep_view_status_str[v->status], v->memb_num);

    struct node_wsrep* const wsrep = ((struct node_ctx*)x)->wsrep;

    if (pthread_mutex_lock(&wsrep->view.mtx))
    {
        NODE_FATAL("Failed to lock VIEW mutex");
        abort();
    }

    wsrep->view.state_id = v->state_id;

    pthread_mutex_unlock(&wsrep->view.mtx);

    return WSREP_CB_SUCCESS;
}

/**
 * logs view data */
static void
wsrep_log_view(const struct wsrep_view* v)
{
    char gtid[WSREP_GTID_STR_LEN + 1];
    wsrep_gtid_print(&v->state_id, gtid, sizeof(gtid));
    gtid[WSREP_GTID_STR_LEN] = '\0';

    char caps[256];
    int written = 0;
    size_t space_left = sizeof(caps);
    int i;
    for (i = 0; i < WSREP_CAPABILITIES_MAX && space_left > 0; i++)
    {
        wsrep_cap_t const f = 1u << i;

        if (!(f & v->capabilities)) continue;

        if (wsrep_capabilities_str[i])
        {
            written += snprintf(&caps[written], space_left, "%s|",
                                wsrep_capabilities_str[i]);
        }
        else
        {
            written += snprintf(&caps[written], space_left, "%d|", i);
        }

        space_left = sizeof(caps) - (size_t)written;
    }
    caps[written ? written - 1 : 0] = '\0'; // overwrite last '|'

    char members_list[1024];
    written = 0;
    space_left = sizeof(members_list);
    for (i = 0; i < v->memb_num && space_left > 0; i++)
    {
        wsrep_member_info_t* m = &v->members[i];
        char uuid[WSREP_UUID_STR_LEN + 1];
        wsrep_uuid_print(&m->id, uuid, sizeof(uuid));
        uuid[WSREP_UUID_STR_LEN] = '\0';

        written += snprintf(&members_list[written], space_left,
                            "%s%d: %s '%s' incoming:'%s'\n",
                            v->my_idx == i ? " * " : "   ", i,
                            uuid, m->name, m->incoming);

        space_left = sizeof(members_list) - (size_t)written;
    }
    members_list[written ? written - 1 : 0] = '\0'; // overwrite the last '\n'

    NODE_INFO(
        "New view received:\n"
        "state: %s (%s)\n"
        "capabilities: %s\n"
        "protocol version: %d\n"
        "members(%d)%s%s",
        gtid, wsrep_view_status_str[v->status],
        caps,
        v->proto_ver,
        v->memb_num, v->memb_num ? ":\n" : "", members_list);
}

/**
 * REPLICATION: callback is called when the node needs to process cluster
 *              view change. The callback is called in "total order isolation",
 *              so all the preceding replication events will be processed
 *              strictly before the call and all subsequent - striclty after.
 */
static enum wsrep_cb_status
wsrep_view_cb(void*                    const x,
              void*                    const r,
              const wsrep_view_info_t* const v,
              const char*              const state,
              size_t                   const state_len)
{
    (void)r;
    (void)state;
    (void)state_len;

    struct node_ctx* const node = x;

    if (WSREP_VIEW_PRIMARY == v->status)
    {
        /* REPLICATION: membership change is a totally ordered event and as such
         *              should be a part of the state, like changes to the
         *              database. */
        int err = node_store_update_membership(node->store, v);
        if (err)
        {
            NODE_FATAL("Failed to update membership in store: %d (%s)",
                       err, strerror(-err));
            abort();
        }
    }

    enum wsrep_cb_status ret = WSREP_CB_SUCCESS;
    struct node_wsrep* const wsrep = ((struct node_ctx*)x)->wsrep;

    if (pthread_mutex_lock(&wsrep->view.mtx))
    {
        NODE_FATAL("Failed to lock VIEW mutex");
        abort();
    }

    /* below we'll just copy the data for future reference (if need be): */

    size_t const memb_size = (size_t)v->memb_num * sizeof(wsrep_member_info_t);
    void* const tmp = realloc(wsrep->view.members, memb_size);
    if (memb_size > 0 && !tmp)
    {
        NODE_ERROR("Could not allocate memory for a new view: %zu bytes",
                   memb_size);
        ret = WSREP_CB_FAILURE;
        goto cleanup;
    }
    else
    {
        wsrep->view.members = tmp;
        if (memb_size) memcpy(wsrep->view.members, &v->members[0], memb_size);
    }

    wsrep->view.state_id     = v->state_id;
    wsrep->view.status       = v->status;
    wsrep->view.capabilities = v->capabilities;
    wsrep->view.proto_ver    = v->proto_ver;
    wsrep->view.memb_num     = v->memb_num;
    wsrep->view.my_idx       = v->my_idx;

    /* and now log the info */

    wsrep_log_view(&wsrep->view);

cleanup:
    pthread_mutex_unlock(&wsrep->view.mtx);

    return ret;
}

/**
 * REPLICATION: callback is called by provider when the node becomes SYNCED */
static enum wsrep_cb_status
wsrep_synced_cb(void* const x)
{
    struct node_wsrep* const wsrep = ((struct node_ctx*)x)->wsrep;

    if (pthread_mutex_lock(&wsrep->synced.mtx))
    {
        NODE_FATAL("Failed to lock SYNCED mutex");
        abort();
    }

    if (wsrep->synced.value == 0)
    {
        NODE_INFO("become SYNCED");
        wsrep->synced.value = 1;
        pthread_cond_broadcast(&wsrep->synced.cond);
    }

    pthread_mutex_unlock(&wsrep->synced.mtx);

    return WSREP_CB_SUCCESS;
}

struct node_wsrep*
node_wsrep_init(const struct node_options* const opts,
                const wsrep_gtid_t*        const current_gtid,
                void*                      const app_ctx)
{
    if (s_wsrep.instance != NULL) return NULL; // already initialized

    wsrep_status_t err;
    err = wsrep_load(opts->provider, &s_wsrep.instance, node_log_cb);
    if (WSREP_OK != err)
    {
        if (strcasecmp(opts->provider, WSREP_NONE))
        {
            NODE_ERROR("wsrep_load(%s) failed: %s (%d).",
                       opts->provider, strerror(err), err);
        }
        else
        {
            NODE_ERROR("Initializing dummy provider failed: %s (%d).",
                       strerror(err), err);
        }
        return NULL;
    }

    char base_addr[256];
    snprintf(base_addr, sizeof(base_addr) - 1, "%s:%ld",
             opts->base_host, opts->base_port);

    struct wsrep_init_args args =
    {
        .app_ctx        = app_ctx,

        .node_name      = opts->name,
        .node_address   = base_addr,
        .node_incoming  = "",   // we don't accept client connections
        .data_dir       = opts->data_dir,
        .options        = opts->options,
        .proto_ver      = 0,    // this is the first version of the application
                                // so the first version of the writeset protocol
        .state_id       = current_gtid,
        .state          = NULL, // unused

        .logger_cb      = node_log_cb,
        .connected_cb   = wsrep_connected_cb,
        .view_cb        = wsrep_view_cb,
        .synced_cb      = wsrep_synced_cb,
        .encrypt_cb     = NULL, // not implemented ATM

        .apply_cb       = node_worker_apply_cb,
        .unordered_cb   = NULL, // not needed now

        .sst_request_cb = node_sst_request_cb,
        .sst_donate_cb  = node_sst_donate_cb
    };

    wsrep_t* wsrep = s_wsrep.instance;

    err = wsrep->init(wsrep, &args);

    if (WSREP_OK != err)
    {
        NODE_ERROR("wsrep::init() failed: %d, must shutdown", err);
        node_wsrep_close(&s_wsrep);
        return NULL;
    }

    return &s_wsrep;
}

wsrep_status_t
node_wsrep_connect(struct node_wsrep* const wsrep,
                   const char*        const address,
                   bool               const bootstrap)
{
    wsrep->bootstrap = bootstrap;
    wsrep_status_t err = wsrep->instance->connect(wsrep->instance,
                                                  "wsrep_cluster",
                                                  address,
                                                  NULL,
                                                  wsrep->bootstrap);

    if (WSREP_OK != err)
    {
        NODE_ERROR("wsrep::connect(%s) failed: %d, must shutdown",
                   address, err);
        node_wsrep_close(wsrep);
    }

    return err;
}

void
node_wsrep_disconnect(struct node_wsrep* const wsrep)
{
    if (pthread_mutex_lock(&wsrep->synced.mtx))
    {
        NODE_FATAL("Failed to lock SYNCED mutex");
        abort();
    }
    wsrep->synced.value = -1; /* this will signal master threads to exit */
    pthread_cond_broadcast(&wsrep->synced.cond);
    pthread_mutex_unlock(&wsrep->synced.mtx);

    wsrep_status_t const err = wsrep->instance->disconnect(wsrep->instance);

    if (err)
    {
        /* REPLICATION: unless connection is not closed, slave threads will
         *              never return. */
        NODE_FATAL("Failed to close wsrep connection: %d", err);
        abort();
    }
}

void
node_wsrep_close(struct node_wsrep* const wsrep)
{
    if (pthread_mutex_lock(&wsrep->view.mtx))
    {
        NODE_FATAL("Failed to lock VIEW mutex");
        abort();
    }
    assert(0    == wsrep->view.memb_num); // the node must be disconneted
    assert(NULL == wsrep->view.members);
    free(wsrep->view.members);
    wsrep->view.members = NULL;
    pthread_mutex_unlock(&wsrep->view.mtx);

    wsrep->instance->free(wsrep->instance);
    wsrep->instance = NULL;
}

bool
node_wsrep_wait_synced(struct node_wsrep* const wsrep)
{
    if (pthread_mutex_lock(&wsrep->synced.mtx))
    {
        NODE_FATAL("Failed to lock SYNCED mutex");
        abort();
    }

    while (wsrep->synced.value == 0)
    {
        pthread_cond_wait(&wsrep->synced.cond, &wsrep->synced.mtx);
    }

    bool const ret = wsrep->synced.value > 0;

    pthread_mutex_unlock(&wsrep->synced.mtx);

    return ret;
}

void
node_wsrep_connected_gtid(struct node_wsrep* wsrep, wsrep_gtid_t* gtid)
{
    if (pthread_mutex_lock(&wsrep->view.mtx))
    {
        NODE_FATAL("Failed to lock VIEW mutex");
        abort();
    }

    *gtid = wsrep->view.state_id;

    pthread_mutex_unlock(&wsrep->view.mtx);
}

wsrep_t*
node_wsrep_provider(struct node_wsrep* wsrep)
{
    return wsrep->instance;
}
