/*
 * Copyright (C) 2021 Codership Oy <info@codership.com>
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

/** @file wsrep_allowlist_service.h
 *
 * This file defines interface for connection allowlist checks.
 * 
 * The provider which is capable of using the service interface v1 must
 * export the following functions.
 *
 * int wsrep_init_allowlist_service_v1(wsrep_allowlist_service_v1_t*)
 * void wsrep_deinit_allowlist_service_v1()
 *
 * which can be probed by the application.
 *
 * The application must initialize the service via above init function
 * before the provider is initialized via wsrep->init(). The deinit
 * function must be called after the provider side resources have been
 * released via wsrep->free().
 */

#ifndef WSREP_ALLOWLIST_SERVICE_H
#define WSREP_ALLOWLIST_SERVICE_H

#include "wsrep_api.h"

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/**
 * Type tag for application defined allowlist processing context.
 *
 * Application may pass pointer to the context when initializing
 * the allowlist service. This pointer is passed a first parameter for
 * each service call.
 */
typedef struct wsrep_allowlist_context wsrep_allowlist_context_t;

typedef enum
{
    WSREP_ALLOWLIST_KEY_IP = 0, // IP allowlist check
    WSREP_ALLOWLIST_KEY_SSL     // SSL certificate allowlist check
} wsrep_allowlist_key_t;

/*
 * Allowlist connection check callback. 
 *
 * @retval WSREP_OK          connection allowed
 * @retval WSREP_NOT_ALLOWED connection not allowed
 */
typedef wsrep_status_t (*wsrep_allowlist_cb_t)(
    wsrep_allowlist_context_t*, 
    wsrep_allowlist_key_t key, 
    const wsrep_buf_t* value);

/**
 * Allowlist service struct.
 *
 * A pointer to this struct must be passed to the call to
 * wsrep_init_allowlist_service_v1.
 *
 * The application must provide implementation to all functions defined
 * in this struct.
 */
typedef struct wsrep_allowlist_service_v1_st
{
    /* Allowlist check callback */
    wsrep_allowlist_cb_t allowlist_cb;
    /* Pointer to application defined allowlist context. */
    wsrep_allowlist_context_t* context;
} wsrep_allowlist_service_v1_t;

#ifdef __cplusplus
}
#endif /* __cplusplus */


#define WSREP_ALLOWLIST_SERVICE_INIT_FUNC_V1 "wsrep_init_allowlist_service_v1"
#define WSREP_ALLOWLIST_SERVICE_DEINIT_FUNC_V1 "wsrep_deinit_allowlist_service_v1"

#endif /* WSREP_ALLOWLIST_SERVICE_H */

