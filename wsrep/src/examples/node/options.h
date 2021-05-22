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
 * @file This unit defines options interface
 */

#ifndef NODE_OPTIONS_H
#define NODE_OPTIONS_H

#include <stdbool.h>

struct node_options
{
    const char* provider; // path to wsrep provider
    const char* address;  // wsrep cluster address string
    const char* options;  // wsrep option string
    const char* name;     // node name (for logging purposes)
    const char* data_dir; // name of the storage file
    const char* base_host;// host own address
    long        masters;  // number of master threads
    long        slaves;   // number of slave threads
    long        ws_size;  // desired writeset size
    long        records;  // total number of records
    long        delay;    // delay between commits
    long        base_port;// base port to use
    long        period;   // statistics output interval
    long        operations;// number of "statements" in a "transaction"
    bool        bootstrap;// bootstrap the cluster with this node
};

extern int
node_options_read(int argc, char* argv[], struct node_options* opts);

#endif /* NODE_OPTIONS_H */
