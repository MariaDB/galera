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

#include "ctx.h"
#include "log.h"
#include "options.h"
#include "stats.h"
#include "worker.h"
#include "wsrep.h"

#include <errno.h>
#include <signal.h> // sigaction()
#include <string.h> // strerror()

static void
signal_handler(int const signum)
{
    NODE_INFO("Got signal %d. Terminating.", signum);
}

static void
install_signal_handler(void)
{
    sigset_t sa_mask;
    sigemptyset(&sa_mask);

    struct sigaction const act =
    {
        .sa_handler = signal_handler,
        .sa_mask    = sa_mask,
        .sa_flags   = (int)SA_RESETHAND
    };

    if (sigaction(SIGINT /* Ctrl-C */, &act, NULL))
    {
        NODE_INFO("sigaction() failed: %d (%s)", errno, strerror(errno));
        abort();
    }
}

int main(int argc, char* argv[])
{
    install_signal_handler();

    struct node_options opts;
    int err = node_options_read(argc, argv, &opts);
    if (err)
    {
        NODE_FATAL("Failed to read command line opritons: %d (%s)",
                   err, strerror(err));
        return err;
    }

    struct node_ctx node;
    node.opts = &opts;

    /* REPLICATION: before connecting to cluster we need to initialize our
     *              storage to know our current position (GTID) */
    node.store = node_store_open(&opts);
    if (!node.store)
    {
        NODE_FATAL("Failed to open node store");
        return 1;
    }

    wsrep_gtid_t current_gtid;
    node_store_gtid(node.store, &current_gtid);

    /* REPLICATION: complete initialization of application context
     *              (including provider itself) */
    node.wsrep = node_wsrep_init(&opts, &current_gtid, &node);
    if (!node.wsrep)
    {
        NODE_FATAL("Failed to initialize wsrep provider");
        return 1;
    }

    /* REPLICATION: now we can connect to the cluster and start receiving
     *              replication events */
    if (node_wsrep_connect(node.wsrep, opts.address, opts.bootstrap) !=
        WSREP_OK)
    {
        NODE_FATAL("Failed to connect to primary component");
        return 1;
    }

    /* REPLICATION: and start processing replicaiton events */
    struct node_worker_pool* slave_pool =
        node_worker_start(&node, NODE_WORKER_SLAVE, (size_t)opts.slaves);
    if (!slave_pool)
    {
        NODE_FATAL("Failed to create slave worker pool");
        return 1;
    }

    /* REPLICATION: now that replicaton events are being processed we can
     *              wait to sync with the cluster */
    if (!node_wsrep_wait_synced(node.wsrep))
    {
        NODE_ERROR("Failed to wait fir SYNCED event");
        return 1;
    }

    NODE_INFO("Synced with cluster");

    /* REPLICATION: now we can start replicate own events */
    struct node_worker_pool* master_pool =
        node_worker_start(&node, NODE_WORKER_MASTER, (size_t)opts.masters);
    if (opts.masters > 0 && !master_pool)
    {
        NODE_FATAL("Failed to create master worker pool");
        return 1;
    }

    node_stats_loop(&node, (int)opts.period);

    /* REPLICATON: to shut down we go in the opposite order:
     *             first  - disconnect from the cluster to signal master threads
     *                      to exit loop,
     *             second - join master and slave threads,
     *             third  - close provider once not in use */
    node_wsrep_disconnect(node.wsrep);

    node_worker_stop(master_pool);
    node_worker_stop(slave_pool);

    node_wsrep_close(node.wsrep);

    /* and finally, when the storage can no longer be disturbed, close it */
    node_store_close(node.store);

    return 0;
}
