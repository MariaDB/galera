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

#include "log.h"

#include <stdio.h>    // fprintf(), fflush()
#include <sys/time.h> // gettimeofday()
#include <time.h>     // localtime_r()
#include <stdarg.h>   // va_start(), va_end()

wsrep_log_level_t node_log_max_level = WSREP_LOG_INFO;

static const char* log_level_str[WSREP_LOG_DEBUG + 2] =
{
    "FATAL: ",
    "ERROR: ",
    " WARN: ",
    " INFO: ",
    "DEBUG: ",
    "XXXXX: "
};

static inline void
log_timestamp_and_log(const char* const prefix, // source of msg
                      int         const severity,
                      const char* const msg)
{
    struct tm      date;
    struct timeval time;

    gettimeofday(&time, NULL);
    localtime_r (&time.tv_sec, &date);

    FILE* log_file = stderr;
    fprintf(log_file,
            "%04d-%02d-%02d %02d:%02d:%02d.%03d " /* timestamp fmt */
            "[%s] %s%s\n",                        /* [prefix] severity msg */
            date.tm_year + 1900, date.tm_mon + 1, date.tm_mday,
            date.tm_hour, date.tm_min, date.tm_sec,
            (int)time.tv_usec / 1000,
            prefix, log_level_str[severity], msg
        );

    fflush (log_file);
}

void
node_log_cb(wsrep_log_level_t const severity, const char* const msg)
{
    /* REPLICATION: let provider log messages be prefixed with 'wsrep'*/
    log_timestamp_and_log("wsrep", severity, msg);
}

void
node_log(wsrep_log_level_t const severity,
         const char*       const file,
         const char*       const function,
         int               const line,
         ...)
{
    va_list ap;

    char   string[2048];
    int    max_string = sizeof(string);
    char*  str = string;

    /* provide file:func():line info only if debug logging is on */
    if (NODE_DO_LOG_DEBUG) {
        int const len = snprintf(str, (size_t)max_string, "%s:%s():%d: ",
                                 file, function, line);
        str += len;
        max_string -= len;
    }

    va_start(ap, line);
    {
        const char* format = va_arg (ap, const char*);

        if (max_string > 0 && NULL != format) {
            vsnprintf (str, (size_t)max_string, format, ap);
        }
    }
    va_end(ap);

    /* actual logging */
    log_timestamp_and_log(" node", severity, string);
}
