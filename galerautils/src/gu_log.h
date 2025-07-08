// Copyright (C) 2007-2014 Codership Oy <info@codership.com>

/**
 * @file Logging API
 *
 * $Id$
 */

#ifndef _gu_log_h_
#define _gu_log_h_

#include "gu_macros.h"
#include <stdlib.h> /* For NULL */
#include "wsrep_api.h"

#if defined(__cplusplus)
extern "C"
{
#endif

/**
 * @typedef
 * Defines severity classes for log messages:
 * FATAL - is a fatal malfunction of the library which cannot be recovered
 *         from. Application must close.
 * error - error condition in the library which prevents further normal
 *         operation but can be recovered from by the application. E.g. EAGAIN.
 * warn  - some abnormal condition which library can recover from by itself.
 *
 * info  - just an informative log message.
 *
 * debug - debugging message.
 */


/**
 * @typedef
 * Defines a type of callback function that application can provide
 * to do the logging
 */
typedef void (*gu_log_cb_t) (wsrep_log_level_t, const char* msg);

/** Helper for macros defined below. Should not be called directly. */
extern int
gu_log (wsrep_log_level_t severity,
        const char*       file,
        const char*       function,
        const int         line,
        const char*       fmt,
        ...) __attribute__((format(printf, 5, 6)));

/** This variable is made global only for the purpose of using it in
 *  gu_debug() macro and avoid calling gu_log() when debug is off.
 *  Don't use it directly! */
extern wsrep_log_level_t gu_log_max_level;

#define gu_log_debug (WSREP_LOG_DEBUG == gu_log_max_level)

#if defined(__cplusplus)
}
#endif

#if !defined(__cplusplus) || defined(GALERA_LOG_H_ENABLE_CXX)
/**
 * @name Logging macros.
 * Must be implemented as macros to report the location of the code where
 * they are called.
 */
/*@{*/
#define gu_fatal(...)                                                          \
    gu_log(WSREP_LOG_FATAL, __FILE__, __func__, __LINE__, __VA_ARGS__);
#define gu_error(...)                                                          \
    gu_log(WSREP_LOG_ERROR, __FILE__, __func__, __LINE__, __VA_ARGS__);
#define gu_warn(...)                                                           \
    gu_log(WSREP_LOG_WARN, __FILE__, __func__, __LINE__, __VA_ARGS__);
#define gu_info(...)                                                           \
    gu_log(WSREP_LOG_INFO, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define gu_debug(...)                                                          \
    if (gu_unlikely(gu_log_debug))                                             \
    {                                                                          \
        gu_log(WSREP_LOG_DEBUG, __FILE__, __func__, __LINE__, __VA_ARGS__);       \
    }
/*@}*/

#endif /* __cplusplus */
#endif /* _gu_log_h_ */

#ifdef __GU_LOGGER__ // C++ logger should use the same stuff, so export it
#ifndef _gu_log_extra_
#define _gu_log_extra_
extern "C"
{
extern bool        gu_log_self_tstamp;
extern gu_log_cb_t gu_log_cb;
extern void        gu_log_cb_default (wsrep_log_level_t, const char*);
extern const char* gu_log_level_str[];
}
#endif /* _gu_log_extra_ */
#endif /* __GU_LOGGER__ */

