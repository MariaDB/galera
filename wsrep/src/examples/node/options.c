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

#include "options.h"

#include <ctype.h>  // isspace()
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h> // strtol()
#include <string.h> // strcmp()

/*
 * getopt_long() declarations begin
 */

#define OPTS_NA no_argument
#define OPTS_RA required_argument
#define OPTS_OA optional_argument

typedef enum opt
{
    OPTS_NOOPT     = 0,
    OPTS_ADDRESS   = 'a',
    OPTS_BOOTSTRAP = 'b',
    OPTS_DELAY     = 'd',
    OPTS_DATA_DIR  = 'f',
    OPTS_HELP      = 'h',
    OPTS_PERIOD    = 'i',
    OPTS_MASTERS   = 'm',
    OPTS_NAME      = 'n',
    OPTS_OPTIONS   = 'o',
    OPTS_BASE_PORT = 'p',
    OPTS_RECORDS   = 'r',
    OPTS_SLAVES    = 's',
    OPTS_BASE_HOST = 't',
    OPTS_PROVIDER  = 'v',
    OPTS_WS_SIZE   = 'w',
    OPTS_OPS       = 'x'
}
    opt_t;

static struct option s_opts[] =
{
    { "address",   OPTS_RA, NULL, OPTS_ADDRESS   },
    { "bootstrap", OPTS_NA, NULL, OPTS_BOOTSTRAP },
    { "delay",     OPTS_RA, NULL, OPTS_DELAY     },
    { "storage",   OPTS_RA, NULL, OPTS_DATA_DIR  },
    { "help",      OPTS_NA, NULL, OPTS_HELP      },
    { "period",    OPTS_RA, NULL, OPTS_PERIOD    },
    { "masters",   OPTS_RA, NULL, OPTS_MASTERS   },
    { "name",      OPTS_RA, NULL, OPTS_NAME      },
    { "options",   OPTS_RA, NULL, OPTS_OPTIONS,  },
    { "base-port", OPTS_RA, NULL, OPTS_BASE_PORT },
    { "records",   OPTS_RA, NULL, OPTS_RECORDS   },
    { "slaves",    OPTS_RA, NULL, OPTS_SLAVES    },
    { "base-host", OPTS_RA, NULL, OPTS_BASE_HOST },
    { "provider",  OPTS_RA, NULL, OPTS_PROVIDER  },
    { "size",      OPTS_RA, NULL, OPTS_WS_SIZE   },
    { "ops",       OPTS_RA, NULL, OPTS_OPS       },
    { NULL, 0, NULL, 0 }
};

static const char* opts_string = "a:d:f:hi:m:n:o:p:r:s:t:v:w:x:";

/*
 * getopt_long() declarations end
 */

static const struct node_options opts_defaults =
{
    .provider  = "none",
    .address   = "",
    .options   = "",
    .name      = "unnamed",
    .data_dir  = ".",
    .base_host = "localhost",
    .masters   = 0,
    .slaves    = 1,
    .ws_size   = 1024,
    .records   = 1024*1024,
    .delay     = 0,
    .base_port = 4567,
    .period    = 10,
    .operations= 1,
    .bootstrap = true
};

static void
opts_print_help(FILE* out, const char* prog_name)
{
    fprintf(
        out,
        "Usage: %s [OPTION...]\n"
        "\n"
        "  -h, --help                 this thing.\n"
        "  -v, --provider=PATH        a path to wsrep provider library file.\n"
        "  -a, --address=STRING       list of node addresses in the group.\n"
        "                             If not set the node assumes that it is the first\n"
        "                             node in the group (default)\n"
        "  -o, --options=STRING       a string of wsrep provider options.\n"
        "  -n, --name=STRING          human-readable node name.\n"
        "  -f, --data-dir=PATH        a directory to save working data in.\n"
        "                             Should be private to the process.\n"
        "  -t, --base-host=ADDRESS    address of this node at which other members can\n"
        "                             connect to it\n"
        "  -p, --base-port=NUM        base port which the node shall listen for\n"
        "                             connections from other members. This port will be\n"
        "                             used for replication, port+1 for IST and port+2\n"
        "                             for SST. Default: 4567\n"
        "  -m, --masters=NUM          number of concurrent master workers.\n"
        "  -s, --slaves=NUM           number of concurrent slave workers.\n"
        "                             (can't be less than 1)\n"
        "  -w, --size=NUM             desirable size of the resulting writesets\n"
        "                             (approximate lower boundary). Default: 1K\n"
        "  -r, --records=NUM          number of records in the store. Default: 1M\n"
        "  -x, --ops=NUM              number of operations per transaction. Default: 1\n"
        "  -d, --delay=NUM            delay in milliseconds between \"commits\"\n"
        "                             (per master thread).\n"
        "  -b, --bootstrap            bootstrap the cluster with this node.\n"
        "                             Default: 'Yes' if --address is not given, 'No'\n"
        "                             otherwise.\n"
        "  -i, --period               period in seconds between performance stats output\n"
        "\n"
        , prog_name);
}

static void
opts_print_config(FILE* out, const struct node_options* opts)
{
    fprintf(
        out,
        "Continuing with the following configuration:\n"
        "provider:      %s\n"
        "address:       %s\n"
        "options:       %s\n"
        "name:          %s\n"
        "data dir:      %s\n"
        "base addr:     %s:%ld\n"
        "masters:       %ld\n"
        "slaves:        %ld\n"
        "writeset size: %ld bytes\n"
        "records:       %ld\n"
        "operations:    %ld\n"
        "commit delay:  %ld ms\n"
        "stats period:  %ld s\n"
        "bootstrap:     %s\n"
        ,
        opts->provider, opts->address, opts->options, opts->name, opts->data_dir,
        opts->base_host, opts->base_port,
        opts->masters, opts->slaves, opts->ws_size, opts->records,
        opts->operations,
        opts->delay, opts->period, opts->bootstrap ? "Yes" : "No"
        );
}

static int
opts_check_conversion(int cond, const char* ptr, int idx)
{
    if (!cond || errno || (*ptr != '\0' && !isspace(*ptr)))
    {
        fprintf(stderr, "Bad value for %s option.\n", s_opts[idx].name);
        return EINVAL;
    }
    return 0;
}

int
node_options_read(int argc, char* argv[], struct node_options* opts)
{
    *opts = opts_defaults;

    int   opt = 0;
    int   opt_idx = 0;
    char* endptr;
    int   ret = 0;

    bool address_given   = false;
    bool bootstrap_given = false;

    while ((opt = getopt_long(argc, argv, opts_string, s_opts, &opt_idx)) != -1)
    {
        switch (opt)
        {
        case OPTS_ADDRESS:
            address_given = strcmp(opts->address, optarg);
            opts->address = optarg;
            break;
        case OPTS_BOOTSTRAP:
            bootstrap_given = true;
            opts->bootstrap = true;
            break;
        case OPTS_DELAY:
            opts->delay = strtol(optarg, &endptr, 10);
            if ((ret = opts_check_conversion(opts->delay >= 0, endptr, opt_idx)))
                goto err;
            break;
        case OPTS_DATA_DIR:
            opts->data_dir = optarg;
            break;
        case OPTS_HELP:
            ret = 1;
            goto help;
        case OPTS_PERIOD:
            opts->period = strtol(optarg, &endptr, 10);
            if ((ret = opts_check_conversion(opts->period > 0, endptr, opt_idx)))
                goto err;
            break;
        case OPTS_MASTERS:
            opts->masters = strtol(optarg, &endptr, 10);
            if ((ret = opts_check_conversion(opts->masters >= 0, endptr,
                                             opt_idx)))
                goto err;
            break;
        case OPTS_NAME:
            opts->name = optarg;
            break;
        case OPTS_OPTIONS:
            opts->options = optarg;
            break;
        case OPTS_BASE_PORT:
            opts->base_port = strtol(optarg, &endptr, 10);
            if ((ret = opts_check_conversion(
                     opts->base_port > 0 && opts->base_port < 65536,
                     endptr, opt_idx)))
                goto err;
            break;
        case OPTS_RECORDS:
            opts->records = strtol(optarg, &endptr, 10);
            if ((ret = opts_check_conversion(opts->records >= 0, endptr,
                                             opt_idx)))
                goto err;
            break;
        case OPTS_SLAVES:
            opts->slaves = strtol(optarg, &endptr, 10);
            if ((ret = opts_check_conversion(opts->slaves > 0, endptr, opt_idx)))
                goto err;
            break;
        case OPTS_BASE_HOST:
            opts->base_host = optarg;
            break;
        case OPTS_PROVIDER:
            opts->provider = optarg;
            break;
        case OPTS_WS_SIZE:
            opts->ws_size = strtol(optarg, &endptr, 10);
            if ((ret = opts_check_conversion(opts->ws_size > 0, endptr,
                                             opt_idx)))
                goto err;
            break;
        case OPTS_OPS:
            opts->operations = strtol(optarg, &endptr, 10);
            if ((ret = opts_check_conversion(opts->operations >= 1, endptr,
                                             opt_idx)))
                goto err;
            break;
        default:
            ret = EINVAL;
        }
    }

help:
    if (ret) {
        opts_print_help(stderr, argv[0]);
    }
    else
    {
        if (!bootstrap_given)
        {
            opts->bootstrap = !address_given;
        }
        opts_print_config(stdout, opts);
        opts->delay  *= 1000;    /* convert to microseconds for usleep() */
    }

err:
    return ret;
}
