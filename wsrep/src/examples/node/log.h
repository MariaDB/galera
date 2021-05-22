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
 * @file This unit defines logging macros for the application and
 *       a logger callback for the wsrep provider.
 */

#ifndef NODE_LOG_H
#define NODE_LOG_H

#include "../../wsrep_api.h"

/**
 * REPLICATION: a logger callback for wsrep provider
 */
extern void
node_log_cb(wsrep_log_level_t severity, const char* message);

/**
 * Applicaton log function intended to be used through the macros defined below.
 * For simplicity it uses log levels defined by wsrep API, but it does not have
 * to. */
extern void
node_log (wsrep_log_level_t level,
          const char*       file,
          const char*       function,
          const int         line,
          ...);

/**
 * This variable made global to avoid calling node_log() when debug logging
 * is disabled. */
extern wsrep_log_level_t node_log_max_level;
#define NODE_DO_LOG_DEBUG (WSREP_LOG_DEBUG <= node_log_max_level)

/**
 * Base logging macro that records current file, function and line number */
#define NODE_LOG(level, ...)\
        node_log(level, __FILE__, __func__, __LINE__, __VA_ARGS__, NULL)

/**
 * @name Logging macros.
 * Must be implemented as macros to report the location of the code where
 * they are called.
 */
/*@{*/
#define NODE_FATAL(...) NODE_LOG(WSREP_LOG_FATAL, __VA_ARGS__, NULL)
#define NODE_ERROR(...) NODE_LOG(WSREP_LOG_ERROR, __VA_ARGS__, NULL)
#define NODE_WARN(...)  NODE_LOG(WSREP_LOG_WARN,  __VA_ARGS__, NULL)
#define NODE_INFO(...)  NODE_LOG(WSREP_LOG_INFO,  __VA_ARGS__, NULL)
#define NODE_DEBUG(...) if (NODE_DO_LOG_DEBUG) \
    { NODE_LOG(WSREP_LOG_DEBUG, __VA_ARGS__, NULL); }
/*@}*/

#endif /* NODE_LOG_H */
