/*
 * Copyright (C) 2022 Codership Oy <info@codership.com>
 *
 * This file is part of wsrep-API.
 *
 * Wsrep-API is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * Wsrep-API is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with wsrep-API.  If not, see <https://www.gnu.org/licenses/>.
 */

/** @file wsrep_config_service.h
 *
 * This file defines interface to retrieve a complete list of configuration
 * parameters accepted by the provider.
 * *
 * The provider which is capable of using the service interface v1 must
 * export the following functions:
 *
 * int wsrep_init_config_service_v1(wsrep_config_service_v1_t*)
 * void wsrep_deinit_config_service_v1()
 *
 * which can be probed by the application.
 *
 */

#ifndef WSREP_CONFIG_SERVICE_H
#define WSREP_CONFIG_SERVICE_H

#include "wsrep_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Flags to describe parameters.
 * By default, a parameter is dynamic and of type string,
 * unless flagged otherwise.
 */
#define WSREP_PARAM_DEPRECATED    (1 << 0)
#define WSREP_PARAM_READONLY      (1 << 1)
#define WSREP_PARAM_TYPE_BOOL     (1 << 2)
#define WSREP_PARAM_TYPE_INTEGER  (1 << 3)
#define WSREP_PARAM_TYPE_DOUBLE   (1 << 4)

#define WSREP_PARAM_TYPE_MASK ( \
    WSREP_PARAM_TYPE_BOOL     | \
    WSREP_PARAM_TYPE_INTEGER  | \
    WSREP_PARAM_TYPE_DOUBLE     \
    )

typedef struct wsrep_parameter
{
  int flags;
  const char* name;
  union {
    bool as_bool;
    int64_t as_integer;
    double as_double;
    const char* as_string;
  } value;
} wsrep_parameter_t;

/**
 * Callback called once for each parameter exposed by provider.
 * The callback should return WSREP_OK on success. Any other
 * return value causes get_parameters() to return WSREP_FATAL.
 *
 * @param p        parameter
 * @param context  application context
 *
 * @return WSREP_OK on success, otherwise application failure
 */
typedef wsrep_status_t (*wsrep_get_parameters_cb) (const wsrep_parameter_t* p,
                                                   void* context);

/**
 * Get configuration parameters exposed by the provider.
 *
 * @param wsrep    pointer to provider handle
 * @param cb       function pointer for callback
 * @param context  application context passed to callback
 *
 * @return WSREP_OK on success, WSREP_FATAL on failure
 */
typedef wsrep_status_t (*wsrep_get_parameters_fn) (wsrep_t* wsrep,
                                                   wsrep_get_parameters_cb cb,
                                                   void* context);

/**
 * Config service struct.
 *
 * A pointer to this struct must be passed to the call to
 * wsrep_init_config_service_v1.
 */
typedef struct wsrep_config_service_v1_st {
  wsrep_get_parameters_fn get_parameters;
} wsrep_config_service_v1_t;

#ifdef __cplusplus
}
#endif

#define WSREP_CONFIG_SERVICE_INIT_FUNC_V1 "wsrep_init_config_service_v1"
#define WSREP_CONFIG_SERVICE_DEINIT_FUNC_V1 "wsrep_deinit_config_service_v1"

#endif /* WSREP_CONFIG_SERVICE */
