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

#include "stats.h"

#include "log.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>  // snprintf()
#include <stdlib.h> // abort()
#include <string.h> // strcmp()
#include <unistd.h> // usleep()

enum
{
    STATS_REPL_BYTE,
    STATS_REPL_WS,
    STATS_RECV_BYTE,
    STATS_RECV_WS,
    STATS_TOTAL_BYTE,
    STATS_TOTAL_WS,
    STATS_CERT_FAILS,
    STATS_STORE_FAILS,
    STATS_FC_PAUSED,
    STATS_MAX
};

static const char* const stats_legend[STATS_MAX] =
{
    " repl(B/s)",
    " repl(W/s)",
    " recv(B/s)",
    " recv(W/s)",
    "total(B/s)",
    "total(W/s)",
    " cert.fail",
    " stor.fail",
    " paused(%)"
};

/* stats IDs in provider output - provider dependent, here we use Galera's */
static const char* const galera_ids[STATS_MAX] =
{
    "replicated_bytes",       /**<  STATS_REPL_BYTE  */
    "replicated",             /**<  STATS_REPL_WS    */
    "received_bytes",         /**<  STATS_RECV_BYTE  */
    "received",               /**<  STATS_RECV_WS    */
    "",                       /**<  STATS_TOTAL_BYTE */
    "",                       /**<  STATS_TOTAL_WS   */
    "local_cert_failures",    /**<  STATS_CERT_FAILS */
    "",                       /**<  STATS_STORE_FAILS */
    "flow_control_paused_ns"  /**<  STATS_FC_PAUSED  */
};

/* maps local stats IDs to provider stat IDs */
static int stats_galera_map[STATS_MAX];

/**
 * Helper to map provider stats to own stats set */
static void
stats_establish_mapping(wsrep_t* const wsrep)
{
    int const magic_map = -1;
    size_t i;
    for (i = 0; i < sizeof(stats_galera_map)/sizeof(stats_galera_map[0]); i++)
    {
        stats_galera_map[i] = magic_map; /* initialize map array */
    }

    struct wsrep_stats_var* const stats = wsrep->stats_get(wsrep);

    /* to compensate for STATS_TOTAL_* and STATS_STORE_FAILS having no
     * counterparts */
    int mapped = 3;

    i = 0;
    while (stats[i].name) /* stats array is terminated by Null name */
    {
        int j;
        for (j = 0; j < STATS_MAX; j++)
        {
            if (magic_map == stats_galera_map[j] /* j-th member still unset */
                &&
                !strcmp(stats[i].name, galera_ids[j]))
            {
                stats_galera_map[j] = (int)i;
                mapped++;
                if (STATS_MAX == mapped) /* all mapped */ goto out;
            }
        }

        i++;
    }

out:
    wsrep->stats_free(wsrep, stats);
}

static void
stats_get(node_store_t* const store, wsrep_t* const wsrep, long long stats[])
{
    stats[STATS_STORE_FAILS] = node_store_read_view_failures(store);

    struct wsrep_stats_var* const ret = wsrep->stats_get(wsrep);
    if (!ret)
    {
        NODE_FATAL("wsrep::stats_get() call failed.");
        abort();
    }

    int i;
    for (i = 0; i < STATS_MAX; i++)
    {
        int j = stats_galera_map[i];
        if (j >= 0)
        {
            assert(WSREP_VAR_INT64 == ret[j].type);
            stats[i] = ret[j].value._int64;
        }
    }

    wsrep->stats_free(wsrep, ret);

    // totals are just sums
    stats[STATS_TOTAL_BYTE] = stats[STATS_REPL_BYTE] + stats[STATS_RECV_BYTE];
    stats[STATS_TOTAL_WS  ] = stats[STATS_REPL_WS  ] + stats[STATS_RECV_WS  ];
}

static void
stats_print(long long bef[], long long aft[], double period)
{
    double rate[STATS_MAX];
    int i;
    for (i = 0; i < STATS_MAX; i++)
    {
        rate[i] = (double)(aft[i] - bef[i])/period;
    }
    rate[STATS_FC_PAUSED] /= 1.0e+07; // nanoseconds to % of seconds

    char   str[256];
    int    written = 0;

    /* first line write legend */
    for (i = 0; i < STATS_MAX; i++)
    {
        size_t const space_left = sizeof(str) - (size_t)written;
        written += snprintf(&str[written], space_left, "%s", stats_legend[i]);
    }

    str[written] = '\n';
    written++;

    /* second line write values */
    for (i = 0; i < STATS_MAX; i++)
    {
        size_t const space_left = sizeof(str) - (size_t)written;
        long long const value   = (long long)rate[i];
        written += snprintf(&str[written], space_left, " %9lld", value);
    }

    str[written] = '\0';

    /* use logging macro for timestamp */
    NODE_INFO("\n%s", str);
}

void
node_stats_loop(const struct node_ctx* const node, int const period)
{
    double     const period_sec  = period;
    useconds_t const period_usec = (useconds_t)period * 1000000;

    wsrep_t* const wsrep = node_wsrep_provider(node->wsrep);
    stats_establish_mapping(wsrep);

    long long stats1[STATS_MAX];
    long long stats2[STATS_MAX];

    stats_get(node->store, wsrep, stats1);

    while (1)
    {
        if (usleep(period_usec)) break;
        stats_get(node->store, wsrep, stats2);
        stats_print(stats1, stats2, period_sec);

        if (usleep(period_usec)) break;
        stats_get(node->store, wsrep, stats1);
        stats_print(stats2, stats1, period_sec);
    }

    if (EINTR != errno)
    {
        NODE_ERROR("Unexpected usleep(%lld) error: %d (%s)",
                   (long long)period_usec, errno, strerror(errno));
    }
    else
    {
        /* interrupted by signal */
    }
}
