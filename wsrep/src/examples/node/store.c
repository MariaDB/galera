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

#include "store.h"

#include "log.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>   // ptrdiff_t
#include <stdint.h>   // uintptr_t
#include <stdlib.h>   // abort()
#include <string.h>   // memset()

#define DECLARE_SERIALIZE_INT(INTTYPE)                                  \
    static inline size_t                                                \
    store_serialize_##INTTYPE(void* const to, INTTYPE##_t const from)   \
    {                                                                   \
        memcpy(to, &from, sizeof(from)); /* for simplicity ignore endianness */ \
        return sizeof(from);                                            \
    }

DECLARE_SERIALIZE_INT(uint32);
DECLARE_SERIALIZE_INT(int64);

#define DECLARE_DESERIALIZE_INT(INTTYPE)                                \
    static inline size_t                                                \
    store_deserialize_##INTTYPE(INTTYPE##_t* const to, const void* const from) \
    {                                                                   \
        memcpy(to, from, sizeof(*to)); /* for simplicity ignore endianness */ \
        return sizeof(*to);                                             \
    }

DECLARE_DESERIALIZE_INT(uint32);
DECLARE_DESERIALIZE_INT(int64);

typedef struct record
{
    wsrep_seqno_t version;
    uint32_t      value;
    /* this order ensures that there is no padding between the members */
}
record_t;

#define STORE_RECORD_SIZE \
    (sizeof(((record_t*)(NULL))->version) + sizeof(((record_t*)(NULL))->value))

static inline size_t
store_record_set(void*           const base,
                 size_t          const index,
                 const record_t* const record)
{
    char* const position = (char*)base + index*STORE_RECORD_SIZE;
    memcpy(position, record, STORE_RECORD_SIZE);
    return STORE_RECORD_SIZE;
}

static inline size_t
store_record_get(const void*     const base,
                 size_t          const index,
                 record_t*       const record)
{
    const char* const position = (const char*)base + index*STORE_RECORD_SIZE;
    memcpy(record, position, STORE_RECORD_SIZE);
    return STORE_RECORD_SIZE;
}

static inline bool
store_record_equal(const record_t* const lhs, const record_t* const rhs)
{
    return (lhs->version == rhs->version) && (lhs->value == rhs->value);
}

/* transaction context */
struct store_trx_op
{
    /* Normally what we'd need for transaction context is the record index and
     * new record value. Here we also save read view snapshot (rec_from & rec_to)
     * to
     * 1. test provider certification correctness if provider supports read view
     * 2. if not, detect conflicts at a store level. */
    record_t rec_from;
    record_t rec_to;
    uint32_t idx_from;
    uint32_t idx_to;
    uint32_t new_value;
    uint32_t size; /* nominal "size" of operation to manipulate on-the-wire
                    * writeset size. */
};

#define STORE_OP_SIZE (STORE_RECORD_SIZE + STORE_RECORD_SIZE +           \
                       sizeof(((struct store_trx_op*)NULL)->idx_from) +  \
                       sizeof(((struct store_trx_op*)NULL)->idx_to) +    \
                       sizeof(((struct store_trx_op*)NULL)->new_value) + \
                       sizeof(((struct store_trx_op*)NULL)->size))

struct store_trx_ctx
{
    wsrep_gtid_t         rv_gtid;
    size_t               ops_num;
    struct store_trx_op* ops;
};

static inline bool
store_trx_add_op(struct store_trx_ctx* const trx)
{
    struct store_trx_op* const new_ops =
        realloc(trx->ops, sizeof(struct store_trx_op)*(trx->ops_num + 1));

    if (new_ops)
    {
        trx->ops = new_ops;
#ifndef NDEBUG
        memset(&trx->ops[trx->ops_num], 0, sizeof(*trx->ops));
#endif
        trx->ops_num++;
    }

    return (NULL == new_ops);
}

struct store_trx_entry
{
    bool                 used;
    struct store_trx_ctx ctx;
};

typedef wsrep_uuid_t member_t;

struct node_store
{
    wsrep_gtid_t    gtid;
    pthread_mutex_t gtid_mtx;
    wsrep_trx_id_t  trx_id;
    pthread_mutex_t trx_id_mtx;
    char*           snapshot;
    member_t*       members;
    void*           records;
    size_t          op_size;
    long            read_view_fails;
    uint32_t        members_num;
    uint32_t        records_num;
    uint32_t        entries_mask;
    bool            read_view_support; // read view support by cluster
    /* trx pool piggybacked */
};

node_store_t*
node_store_open(const struct node_options* const opts)
{
    /* make the size of trx pool the next highest power of 2 over the total
     * number of workers */
    uint32_t trx_pool_mask = (uint32_t)(opts->masters + opts->slaves);
    if (trx_pool_mask > 0)
    {
        trx_pool_mask -= 1;
        trx_pool_mask |= trx_pool_mask >> 1;
        trx_pool_mask |= trx_pool_mask >> 2;
        trx_pool_mask |= trx_pool_mask >> 4;
        trx_pool_mask |= trx_pool_mask >> 8;
        trx_pool_mask |= trx_pool_mask >> 16;
    }
    assert(((trx_pool_mask + 1) & trx_pool_mask) == 0); // 2^n - 1

    size_t const desired_op_size = (size_t)(opts->ws_size/opts->operations);
    size_t const op_size = (desired_op_size > STORE_OP_SIZE ?
                            desired_op_size : STORE_OP_SIZE);

    /* since the number of workers will never change, we can allocate trx pool
     * together with the main store struc */
    size_t const store_alloc_size = sizeof(struct node_store) +
        /* op_size - additional buffer for op serialization per trx */
        (sizeof(struct store_trx_entry) + op_size)*(trx_pool_mask + 1);

    struct node_store* const ret = malloc(store_alloc_size);

    if (ret)
    {
        memset(ret, 0, store_alloc_size);
        ret->records = malloc((size_t)opts->records * STORE_RECORD_SIZE);

        if (ret->records)
        {
            ret->gtid = WSREP_GTID_UNDEFINED;
            pthread_mutex_init(&ret->gtid_mtx, NULL);
            pthread_mutex_init(&ret->trx_id_mtx, NULL);
            ret->op_size      = op_size;
            ret->records_num  = (uint32_t)opts->records;
            ret->entries_mask = trx_pool_mask;

            uint32_t i;
            for (i = 0; i < ret->records_num; i++)
            {
                /* keep state in serialized form for easy snapshotting */
                struct record const record = { WSREP_SEQNO_UNDEFINED, i };
                store_record_set(ret->records, i, &record);
            }

            return ret;
        }
        else
        {
            free(ret);
        }
    }

    return NULL;
}

void
node_store_close(struct node_store* const store)
{
    assert(store);
    assert(store->records);
    pthread_mutex_destroy(&store->gtid_mtx);
    pthread_mutex_destroy(&store->trx_id_mtx);
    free(store->records);
    free(store->members);
    free(store);
}

#define STORE_MUTEX_LOCK(mtx)                              \
    {                                                      \
        int err = pthread_mutex_lock(mtx);                 \
        if (err)                                           \
        {                                                  \
            NODE_FATAL("Failed to lock " #mtx ": %d (%s)", \
                       err, strerror(err));                \
            abort();                                       \
        }                                                  \
    }

static inline struct store_trx_entry*
store_get_trx_entry(struct node_store* const store, wsrep_trx_id_t const trx_id)
{
    return (struct store_trx_entry*)
        ((char*)(store + 1) + (trx_id & store->entries_mask)*
         (sizeof(struct store_trx_entry) + store->op_size));
}

static inline struct store_trx_ctx*
store_get_trx_ctx(struct node_store* const store, wsrep_trx_id_t const trx_id)
{
    return &(store_get_trx_entry(store, trx_id)->ctx);
}

static inline wsrep_trx_id_t
store_new_trx_id(struct node_store* const store)
{
    wsrep_trx_id_t ret;
    struct store_trx_entry* trx;

    STORE_MUTEX_LOCK(&store->trx_id_mtx);

    do
    {
        store->trx_id++;
        trx = store_get_trx_entry(store, store->trx_id);
    }
    while (trx->used);
    trx->used = true;
    ret = store->trx_id;

    pthread_mutex_unlock(&store->trx_id_mtx);

    memset(&trx->ctx, 0, sizeof(trx->ctx));

    return ret;
}

static inline void
store_free_trx_id(struct node_store* const store, wsrep_trx_id_t const trx_id)
{
    struct store_trx_entry* const trx = store_get_trx_entry(store, trx_id);
    assert(trx->used);
    free(trx->ctx.ops);

    STORE_MUTEX_LOCK(&store->trx_id_mtx);

    trx->used = false;

    pthread_mutex_unlock(&store->trx_id_mtx);
}

/**
 * deserializes membership from snapshot */
static int
store_new_members(const char* ptr, const char* const endptr,
                  uint32_t* const num, member_t** const memb)
{
    ptr += store_deserialize_uint32(num, ptr);

    if (*num < 2)
    {
        NODE_ERROR("Bogus number of members %u", *num);
        return -1;
    }

    int ret = (int)sizeof(*num);

    size_t const msize = sizeof(member_t) * *num;
    if ((endptr - ptr) < (ptrdiff_t)msize)
    {
        NODE_ERROR("State snapshot does not contain all membership: "
                   "%zd < %zu", endptr - ptr, msize);
        return -1;
    }

    *memb = calloc(*num, sizeof(member_t));
    if (!*memb)
    {
        NODE_ERROR("Could not allocate new membership");
        return -ENOMEM;
    }

    memcpy(*memb, ptr, msize);

    return ret + (int)msize;
}

/**
 * deserializes records from snapshot */
static int
store_new_records(const char* ptr, const char* const endptr,
                  uint32_t* const num, void** const rec)
{
    ptr += store_deserialize_uint32(num, ptr);

    int ret = (int)sizeof(*num);
    if (!*num)
    {
        *rec = NULL;
        return ret;
    }

    size_t const rsize = STORE_RECORD_SIZE * *num;
    if ((endptr - ptr) < (ptrdiff_t)rsize)
    {
        NODE_ERROR("State snapshot does not contain all records: "
                   "%zu < %zu", endptr - ptr, rsize);
        return -1;
    }

    *rec = malloc(rsize);
    if (!*rec)
    {
        NODE_ERROR("Could not allocate new records");
        return -ENOMEM;
    }

    memcpy(*rec, ptr, rsize);

    return ret + (int)rsize;
}

int
node_store_init_state(struct node_store*  const store,
                      const void*         const state,
                      size_t              const state_len)
{
    /* First, deserialize and prepare new state */
    if (state_len <= sizeof(member_t)*2 /* at least two members */ +
        WSREP_UUID_STR_LEN + 1 /* : */ + 1 /* seqno */ + 1 /* \0 */)
    {
        NODE_ERROR("State snapshot too short: %zu", state_len);
        return -1;
    }

    wsrep_gtid_t state_gtid;
    int ret;
    ret = wsrep_gtid_scan(state, state_len, &state_gtid);
    if (ret < 0)
    {
        char state_str[WSREP_GTID_STR_LEN + 1] = { 0, };
        memcpy(state_str, state, sizeof(state_str) - 1);
        NODE_ERROR("Could not find valid GTID in the received data: %s",
                    state_str);
        return -1;
    }

    ret++; /* \0 */
    if ((state_len - (size_t)ret) < sizeof(uint32_t))
    {
        NODE_ERROR("State snapshot does not contain the number of members");
        return -1;
    }

    const char* ptr = ((char*)state);
    const char* const endptr = ptr + state_len;
    ptr += ret;

    uint32_t m_num;
    member_t* new_members;
    ret = store_new_members(ptr, endptr, &m_num, &new_members);
    if (ret < 0)
    {
        return ret;
    }
    ptr += ret;

    bool const read_view_support = ptr[0];
    ptr += 1;

    uint32_t r_num;
    void* new_records;
    ret = store_new_records(ptr, endptr, &r_num, &new_records);
    if (ret < 0)
    {
        free(new_members);
        return ret;
    }
    ptr += ret;

    STORE_MUTEX_LOCK(&store->gtid_mtx);

    /* just a sanity check */
    if (0 == wsrep_uuid_compare(&state_gtid.uuid, &store->gtid.uuid) &&
        state_gtid.seqno < store->gtid.seqno)
    {
        NODE_ERROR("Received snapshot that is in the past: my seqno %lld,"
                   " received seqno: %lld",
                   (long long)store->gtid.seqno, (long long)state_gtid.seqno);
        free(new_members);
        free(new_records);
        ret = -1;
    }
    else
    {
        free(store->members);
        store->members_num = m_num;
        store->members     = new_members;
        free(store->records);
        store->records_num = r_num;
        store->records     = new_records;
        store->gtid        = state_gtid;
        store->read_view_support = read_view_support;
        ret = 0;
    }

    pthread_mutex_unlock(&store->gtid_mtx);

    return ret;
}

int
node_store_acquire_state(node_store_t* const store,
                         const void**  const state,
                         size_t*       const state_len)
{
    int ret = 0;

    STORE_MUTEX_LOCK(&store->gtid_mtx);

    if (!store->snapshot)
    {
        size_t const memb_len = store->members_num * sizeof(member_t);
        size_t const rec_len  = store->records_num * STORE_RECORD_SIZE;
        size_t const buf_len  = WSREP_GTID_STR_LEN + 1
            + sizeof(uint32_t) + memb_len
            + 1 /* read view support */
            + sizeof(uint32_t) + rec_len;

        store->snapshot = malloc(buf_len);

        if (store->snapshot)
        {
            char* ptr = store->snapshot;

            /* state GTID */
            ret = wsrep_gtid_print(&store->gtid, ptr, buf_len);
            if (ret > 0)
            {
                NODE_INFO("");
                assert((size_t)ret < buf_len);

                ptr[ret] = '\0';
                ret++;
                ptr += ret;
                assert((size_t)ret < buf_len);

                /* membership */
                ptr += store_serialize_uint32(ptr, store->members_num);
                ret += (int)sizeof(uint32_t);
                assert((size_t)ret + memb_len < buf_len);
                memcpy(ptr, store->members, memb_len);
                ptr += memb_len;
                ret += (int)memb_len;
                assert((size_t)ret + sizeof(uint32_t) <= buf_len);

                /* read view support */
                ptr[0] = store->read_view_support;
                ptr += 1;
                ret += 1;

                /* records */
                ptr += store_serialize_uint32(ptr, store->records_num);
                ret += (int)sizeof(uint32_t);
                assert((size_t)ret + rec_len < buf_len);
                memcpy(ptr, store->records, rec_len);
                ret += (int)rec_len;
                assert((size_t)ret <= buf_len);
            }
            else
            {
                NODE_ERROR("Failed to record GTID: %d (%s)", ret,strerror(-ret));
                free(store->snapshot);
                store->snapshot = 0;
            }
        }
        else
        {
            NODE_ERROR("Failed to allocate snapshot buffer of size %zu",buf_len);
            ret = -ENOMEM;
        }
    }
    else
    {
        assert(0); /* provider should prevent such situation */
        ret = -EAGAIN;
    }

    pthread_mutex_unlock(&store->gtid_mtx);

    if (ret > 0)
    {
        NODE_INFO("\n\nPrepared snapshot of %u records\n\n", store->records_num);
        *state     = store->snapshot;
        *state_len = (size_t)ret;
        ret        = 0;
    }

    return ret;
}

void
node_store_release_state(node_store_t* const store)
{
    STORE_MUTEX_LOCK(&store->gtid_mtx);

    assert(store->snapshot);
    free(store->snapshot);
    store->snapshot = 0;

    pthread_mutex_unlock(&store->gtid_mtx);
}

int
node_store_update_membership(struct node_store*       const store,
                             const wsrep_view_info_t* const v)
{
    assert(store);
    assert(WSREP_VIEW_PRIMARY == v->status);
        assert(v->memb_num > 0);

    STORE_MUTEX_LOCK(&store->gtid_mtx);

    bool const continuation = v->state_id.seqno == store->gtid.seqno + 1 &&
        0 == wsrep_uuid_compare(&v->state_id.uuid, &store->gtid.uuid);

    bool const initialization = WSREP_SEQNO_UNDEFINED == store->gtid.seqno &&
        0 == wsrep_uuid_compare(&WSREP_UUID_UNDEFINED, &store->gtid.uuid);

    if (!(continuation || initialization))
    {
        char store_str[WSREP_GTID_STR_LEN + 1] = { 0, };
        wsrep_gtid_print(&store->gtid, store_str, sizeof(store_str));
        char view_str[WSREP_GTID_STR_LEN + 1] = { 0, };
        wsrep_gtid_print(&v->state_id, view_str, sizeof(view_str));

        NODE_FATAL("Attempt to initialize store GTID from incompatible view:\n"
                   "\tstore: %s\n"
                   "\tview:  %s",
                   store_str, view_str);
        abort();
    }

    wsrep_uuid_t* const new_members = calloc(sizeof(wsrep_uuid_t),
                                             (size_t)v->memb_num);
    if (!new_members)
    {
        NODE_FATAL("Could not allocate new members array");
        abort();
    }

    int i;
    for (i = 0; i < v->memb_num; i++)
    {
        new_members[i] = v->members[i].id;
    }

    /* REPLICATION: at this point we should compare old and new memberships and
     *              rollback all streaming transactions from the partitioned
     *              members, if any. But we don't support it in this program yet.
     */

    free(store->members);

    store->members     = new_members;
    store->members_num = (uint32_t)v->memb_num;
    store->gtid        = v->state_id;
    store->read_view_support = (v->capabilities & WSREP_CAP_SNAPSHOT);

    pthread_mutex_unlock(&store->gtid_mtx);

    return 0;
}

void
node_store_gtid(struct node_store* const store,
                wsrep_gtid_t*      const gtid)
{
    assert(store);

    STORE_MUTEX_LOCK(&store->gtid_mtx);

    *gtid = store->gtid;

    pthread_mutex_unlock(&store->gtid_mtx);
}


static inline void
store_serialize_op(void* const buf, const struct store_trx_op* const op)
{
    char* ptr = buf;
    ptr += store_record_set(ptr, 0, &op->rec_from);
    ptr += store_record_set(ptr, 0, &op->rec_to);
    ptr += store_serialize_uint32(ptr, op->idx_from);
    ptr += store_serialize_uint32(ptr, op->idx_to);
    ptr += store_serialize_uint32(ptr, op->new_value);
    store_serialize_uint32(ptr, op->size);
}

static inline void
store_deserialize_op(struct store_trx_op* const op, const void* const buf)
{
    const char* ptr = buf;
    ptr += store_record_get(ptr, 0, &op->rec_from);
    ptr += store_record_get(ptr, 0, &op->rec_to);
    ptr += store_deserialize_uint32(&op->idx_from, ptr);
    ptr += store_deserialize_uint32(&op->idx_to, ptr);
    ptr += store_deserialize_uint32(&op->new_value, ptr);
    store_deserialize_uint32(&op->size, ptr);
}

static inline void
store_serialize_gtid(void* const buf, const wsrep_gtid_t* const gtid)
{
    char* ptr = buf;
    memcpy(ptr, &gtid->uuid, sizeof(gtid->uuid));
    ptr += sizeof(gtid->uuid);
    store_serialize_int64(ptr, gtid->seqno);
}

static inline void
store_deserialize_gtid(wsrep_gtid_t* const gtid, const void* const buf)
{
    const char* ptr = buf;
    memcpy(&gtid->uuid, ptr, sizeof(gtid->uuid));
    ptr += sizeof(gtid->uuid);
    store_deserialize_int64(&gtid->seqno, ptr);
}

#define STORE_GTID_SIZE (sizeof(((wsrep_gtid_t*)(NULL))->uuid) + sizeof(int64_t))

int
node_store_execute(node_store_t*      const store,
                   wsrep_t*           const wsrep,
                   wsrep_ws_handle_t* const ws_handle)
{
    assert(store);

    if (0 == ws_handle->trx_id)
    {
        assert(sizeof(ws_handle->trx_id) >= sizeof(uintptr_t));
        ws_handle->trx_id = store_new_trx_id(store);
    }

    struct store_trx_ctx* trx = store_get_trx_ctx(store, ws_handle->trx_id);
    if (store_trx_add_op(trx)) return -ENOMEM;
    struct store_trx_op* const op = &trx->ops[trx->ops_num - 1];

    STORE_MUTEX_LOCK(&store->gtid_mtx);

    if (1 == trx->ops_num)
    {
        /* First operation, save ID of the read view of the transaction */
        trx->rv_gtid = store->gtid;
    }

    /* Transaction op: copy value from one random record to another... */
    op->idx_from = (uint32_t)rand() % store->records_num;
    op->idx_to   = (uint32_t)rand() % store->records_num;
    store_record_get(store->records, op->idx_from, &op->rec_from);
    store_record_get(store->records, op->idx_to,   &op->rec_to);

    pthread_mutex_unlock(&store->gtid_mtx);

    wsrep_status_t ret = WSREP_TRX_FAIL;

    if (op->rec_from.version > trx->rv_gtid.seqno ||
        op->rec_to.version   > trx->rv_gtid.seqno)
    {
        /* transaction read view changed, trx needs to be restarted */
#if 0
        NODE_INFO("Transaction read view changed: %lld -> %lld, returning %d",
                  (long long)trx->rv_gtid.seqno,
                  (long long)(op->rec_from.version > op->rec_to.version ?
                              op->rec_from.version : op->rec_to.version),
                  ret);
#endif
        goto error;
    }

    /* Transaction op: ... and modify it somehow, e.g. increment by 1 */
    op->new_value = op->rec_from.value + 1;

    if (1 == trx->ops_num) // first trx operation
    {
        /* REPLICATION: Since this application does not implement record locks,
         *              it needs to establish read view for each transaction for
         *              a proper conflict detection and transaction isolation.
         *              Otherwose we'll need to implement record versioning */
        if (store->read_view_support)
        {
            ret = wsrep->assign_read_view(wsrep, ws_handle, &trx->rv_gtid);
            if (ret)
            {
                NODE_ERROR("wsrep::assign_read_view(%lld) failed: %d",
                           trx->rv_gtid.seqno, ret);
                goto error;
            }
        }

        /* Record read view in the writeset for debugging purposes */
        assert(store->op_size > STORE_GTID_SIZE);
        store_serialize_gtid(trx + 1, &trx->rv_gtid);
        wsrep_buf_t ws = { .ptr = trx + 1, .len = STORE_GTID_SIZE };
        ret = wsrep->append_data(wsrep, ws_handle, &ws, 1, WSREP_DATA_ORDERED,
                                 true);
        if (ret)
        {
            NODE_ERROR("wsrep::append_data(rv_gtid) failed: %d", ret);
            goto error;
        }
    }

    /* REPLICATION: append keys touched by the operation
     *
     * NOTE: depending on data access granularity some applications may require
     *       multipart keys, e.g. <schema>:<table>:<row> in a SQL database.
     *       Single part keys match hashtables and key-value stores.
     *       Below we have two different single-part keys which reference two
     *       different records. */
    uint32_t    key_val;
    wsrep_buf_t key_part = { .ptr = &key_val, .len = sizeof(key_val) };
    wsrep_key_t ws_key   = { .key_parts = &key_part, .key_parts_num = 1 };

    /* REPLICATION: Key 1 - the key of the source, unchanged record */
    store_serialize_uint32(&key_val, op->idx_from);
    ret = wsrep->append_key(wsrep, ws_handle,
                            &ws_key,
                            1,   /* single key */
                            WSREP_KEY_REFERENCE,
                            true /* provider shall make a copy of the key */);
    if (ret)
    {
        NODE_ERROR("wsrep::append_key(REFERENCE) failed: %d", ret);
        goto error;
    }

    /* REPLICATION: Key 2 - the key of the record we want to update */
    store_serialize_uint32(&key_val, op->idx_to);
    ret = wsrep->append_key(wsrep, ws_handle,
                            &ws_key,
                            1,   /* single key */
                            WSREP_KEY_UPDATE,
                            true /* provider shall make a copy of the key */);
    if (ret)
    {
        NODE_ERROR("wsrep::append_key(UPDATE) failed: %d", ret);
        goto error;
    }

    /* REPLICATION: append transaction operation to the "writeset"
     *              (WS buffer was allocated together with trx context above) */
    assert(store->op_size >= STORE_OP_SIZE);
    assert(store->op_size == (uint32_t)store->op_size);
    op->size = (uint32_t)store->op_size;
    store_serialize_op(trx + 1, op);
    wsrep_buf_t ws = { .ptr = trx + 1, .len = store->op_size };
    ret = wsrep->append_data(wsrep, ws_handle, &ws, 1, WSREP_DATA_ORDERED, true);

    if (!ret) return 0;

    NODE_ERROR("wsrep::append_data(op) failed: %d", ret);

error:
    store_free_trx_id(store, ws_handle->trx_id);

    return ret;
}

int
node_store_apply(node_store_t*      const store,
                 wsrep_trx_id_t*    const trx_id,
                 const wsrep_buf_t* const ws)
{
    assert(store);
    (void)store;

    *trx_id = store_new_trx_id(store);
    struct store_trx_ctx* const trx = store_get_trx_ctx(store, *trx_id);

    /* prepare trx context for commit */
    const char* ptr = ws->ptr;
    size_t left     = ws->len;

    /* at least one operation should be there */
    assert(left >= STORE_GTID_SIZE + STORE_OP_SIZE);

    if (left >= STORE_GTID_SIZE)
    {
        store_deserialize_gtid(&trx->rv_gtid, ptr);
        left -= STORE_GTID_SIZE;
        ptr  += STORE_GTID_SIZE;
    }

    while (left >= STORE_OP_SIZE)
    {
        if (store_trx_add_op(trx))
        {
            store_free_trx_id(store,*trx_id); /* "rollback": release resources */
            return -ENOMEM;
        }
        struct store_trx_op* const op = &trx->ops[trx->ops_num - 1];

        store_deserialize_op(op, ptr);
        assert(op->idx_to <= store->records_num);

        left -= op->size;
        ptr  += op->size;
    }

    if (left != 0)
    {
        NODE_FATAL("Failed to process last (%d/%zu) bytes of the writeset.",
                   (int)left, ws->len);
        abort();
    }

    return 0;
}

static uint32_t const store_fnv32_seed  = 2166136261;

static inline uint32_t
store_fnv32a(const void* buf, size_t const len, uint32_t seed)
{
    static uint32_t const fnv32_prime = 16777619;
    const uint8_t* bp = (const uint8_t*)buf;
    const uint8_t* const be = bp + len;

    while (bp < be)
    {
        seed ^= *bp++;
        seed *= fnv32_prime;
    }

    return seed;
}


static void
store_checksum_state(node_store_t* store)
{
    uint32_t res = store_fnv32_seed;
    uint32_t i;

    for (i = 0; i < store->members_num; i++)
    {
        res = store_fnv32a(&store->members[i], sizeof(*store->members), res);
    }

    res = store_fnv32a(store->records, store->records_num * STORE_RECORD_SIZE,
                       res);

    res = store_fnv32a(&store->gtid.uuid, sizeof(store->gtid.uuid), res);

    wsrep_seqno_t s;
    store_serialize_int64(&s, store->gtid.seqno);
    res = store_fnv32a(&s, sizeof(s), res);

    NODE_INFO("\n\n\tSeqno: %lld; state hash: %#010x\n",
              (long long)store->gtid.seqno, res);
}

static inline void
store_update_gtid(node_store_t* const store, const wsrep_gtid_t* ws_gtid)
{
    assert(0 == wsrep_uuid_compare(&store->gtid.uuid, &ws_gtid->uuid));

    store->gtid.seqno++;

    if (store->gtid.seqno != ws_gtid->seqno)
    {
        NODE_FATAL("Out of order commit: expected %lld, got %lld",
                   store->gtid.seqno, ws_gtid->seqno);
        abort();
    }

    static wsrep_seqno_t const period = 0x000fffff; /* ~1M */
    if (0 == (store->gtid.seqno & period))
    {
        store_checksum_state(store);
    }
}

void
node_store_commit(node_store_t*       const store,
                  wsrep_trx_id_t      const trx_id,
                  const wsrep_gtid_t* const ws_gtid)
{
    assert(store);
    assert(trx_id);

    struct store_trx_ctx* const trx = store_get_trx_ctx(store, trx_id);

    bool const check_read_view_snapshot =
#ifdef NDEBUG
        !store->read_view_support;
#else
    1;
#endif /* NDEBUG */

    STORE_MUTEX_LOCK(&store->gtid_mtx);

    store_update_gtid(store, ws_gtid);

    /* First loop is to check if we can commit all operations if provider
     * does not support read view or for debugging puposes */
    size_t i;
    if (check_read_view_snapshot)
    {
        for (i = 0; i < trx->ops_num; i++)
        {
            struct store_trx_op* const op = &trx->ops[i];

            record_t from, to;
            store_record_get(store->records, op->idx_from, &from);
            store_record_get(store->records, op->idx_to,   &to);

            if (!store_record_equal(&op->rec_from, &from) ||
                !store_record_equal(&op->rec_to,   &to))
            {
                /* read view changed since transaction was executed,
                 * can't commit */
                assert(op->rec_from.version <= from.version);
                assert(op->rec_to.version <= to.version);
                if (op->rec_from.version == from.version)
                    assert(op->rec_from.value == from.value);
                if (op->rec_to.version == to.version)
                    assert(op->rec_to.value == to.value);
                if (store->read_view_support) abort();

                store->read_view_fails++;

                NODE_INFO("Read view changed at commit time, rollback trx");

                goto error;
            }
        }
    }

    /* Second loop is to actually modify the dataset */
    for (i = 0; i < trx->ops_num; i++)
    {
        struct store_trx_op* const op = &trx->ops[i];

        record_t const new_record =
            { .version = ws_gtid->seqno, .value = op->new_value };

        store_record_set(store->records, op->idx_to, &new_record);
    }

error:
    pthread_mutex_unlock(&store->gtid_mtx);

    store_free_trx_id(store, trx_id);
}

void
node_store_rollback(node_store_t*  const store,
                    wsrep_trx_id_t const trx_id)
{
    assert(store);
    assert(trx_id);

    store_free_trx_id(store, trx_id);
}

void
node_store_update_gtid(node_store_t*       const store,
                       const wsrep_gtid_t* const ws_gtid)
{
    assert(store);

    STORE_MUTEX_LOCK(&store->gtid_mtx);

    store_update_gtid(store, ws_gtid);

    pthread_mutex_unlock(&store->gtid_mtx);
}

long
node_store_read_view_failures(node_store_t* const store)
{
    assert(store);

    long ret;

    STORE_MUTEX_LOCK(&store->gtid_mtx);

    ret = store->read_view_fails;;

    pthread_mutex_unlock(&store->gtid_mtx);

    return ret;
}
