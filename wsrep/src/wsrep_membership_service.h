/*
 * Copyright (C) 2020 Codership Oy <info@codership.com>
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

/** @file wsrep_membership_service.h
 *
 * This file defines interface for quering the immediate membership and
 * members' states of the current configuration. The information is provided
 * OUT OF ORDER to facilitate administrative tasks.
 *
 * The provider which is capable of using the service interface v1 must
 * export the following functions.
 *
 * int wsrep_init_membership_service_v1(struct wsrep_membership_service_v1*)
 * void wsrep_deinit_membership_service_v1()
 *
 * which can be probed by the application.
 *
 * The application must initialize the service via above init function
 * before the provider is initialized via wsrep->init(). The deinit
 * function must be called after the provider side resources have been
 * released via wsrep->free().
 */

#ifndef WSREP_MEMBERSHIP_SERVICE_H
#define WSREP_MEMBERSHIP_SERVICE_H

#include "wsrep_api.h"

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/**
 * Member info structure extended to contain member state
 */
struct wsrep_member_info_ext
{
    struct wsrep_member_info base;
    wsrep_seqno_t            last_committed;
    enum wsrep_member_status status;
};

/**
 * Extended membership structure
 */
struct wsrep_membership
{
    /**
     * Epoch of the membership data (last time it was updated)
     */
    wsrep_uuid_t group_uuid;
    /**
     * Sequence number of the last received (not processed) action
     */
    wsrep_seqno_t last_received;
    /**
     * When the members' data was last updated
     */
    wsrep_seqno_t updated;
    /**
     * Current group state
     */
    enum wsrep_view_status state;
    /**
     * Number of members in the array
     */
    size_t num;
    /**
     * Membership array
     */
    struct wsrep_member_info_ext members[1];
};

/**
 * Memory allocation callback for wsrep_get_mmebership_fn() below
 *
 * @param size of buffer to allocate
 * @return allocated buffer pointer or NULL in case of error
 */
typedef void* (*wsrep_allocator_cb) (size_t size);

/**
 * Query membership
 *
 * @param wsrep      provider handle
 * @param allocator  to use for wsrep_membership struct allocation
 * @param membership pointer to pointer to the memebrship structure.
 *                   The structure is allocated by provider and must be freed
 *                   by the caller.
 * @return error code of the call
 */
typedef wsrep_status_t (*wsrep_get_membership_fn) (
    wsrep_t* wsrep,
    wsrep_allocator_cb allocator,
    struct wsrep_membership** membership);

/**
 * Membership service struct.
 * Returned by WSREP_MEMBERSHIP_SERVICE_INIT_FUNC_V1
 */
struct wsrep_membership_service_v1
{
    wsrep_get_membership_fn get_membership;
};

#ifdef __cplusplus
}
#endif /* __cplusplus */

typedef
wsrep_status_t
(*wsrep_membership_service_v1_init_fn) (struct wsrep_membership_service_v1*);
typedef
void
(*wsrep_membership_service_v1_deinit_fn)(void);

/** must be exported by the provider */
#define WSREP_MEMBERSHIP_SERVICE_V1_INIT_FN   "wsrep_init_membership_service_v1"
#define WSREP_MEMBERSHIP_SERVICE_V1_DEINIT_FN "wsrep_deinit_membership_service_v1"

#endif /* WSREP_MEMBERSHIP_SERVICE_H */
