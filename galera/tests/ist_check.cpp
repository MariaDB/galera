//
// Copyright (C) 2011-2019 Codership Oy <info@codership.com>
//

#include "galera_test_env.hpp"

#include "ist.hpp"
#include "ist_proto.hpp"
#include "trx_handle.hpp"
#include "monitor.hpp"
#include "replicator_smm.hpp"

#include <GCache.hpp>
#include <gu_arch.h>
#include <check.h>

using namespace galera;

// Message tests

START_TEST(test_ist_message)
{

    using namespace galera::ist;

#if 0 /* This is a check for the old (broken) format */
    Message m3(3, Message::T_HANDSHAKE, 0x2, 3, 1001);

#if GU_WORDSIZE == 32
    fail_unless(serial_size(m3) == 20, "serial size %zu != 20",
                serial_size(m3));
#elif GU_WORDSIZE == 64
    fail_unless(serial_size(m3) == 24, "serial size %zu != 24",
                serial_size(m3));
#endif

    gu::Buffer buf(m3.serial_size());
    m3.serialize(&buf[0], buf.size(), 0);
    Message mu3(3);
    mu3.unserialize(&buf[0], buf.size(), 0);

    fail_unless(mu3.version() == 3);
    fail_unless(mu3.type()    == Message::T_HANDSHAKE);
    fail_unless(mu3.flags()   == 0x2);
    fail_unless(mu3.ctrl()    == 3);
    fail_unless(mu3.len()     == 1001);
#endif /* 0 */

    Message const m2(VER21, Message::T_HANDSHAKE, 0x2, 3, 1001);
    size_t const s2(12);
    fail_unless(m2.serial_size() == s2,
                "Expected m2.serial_size() = %zd, got %zd", s2,m2.serial_size());

    gu::Buffer buf2(m2.serial_size());
    m2.serialize(&buf2[0], buf2.size(), 0);

    Message mu2(VER21);
    mu2.unserialize(&buf2[0], buf2.size(), 0);
    fail_unless(mu2.version() == VER21);
    fail_unless(mu2.type()    == Message::T_HANDSHAKE);
    fail_unless(mu2.flags()   == 0x2);
    fail_unless(mu2.ctrl()    == 3);
    fail_unless(mu2.len()     == 1001);

    Message const m4(VER40, Message::T_HANDSHAKE, 0x2, 3, 1001);
    size_t const s4(16 + sizeof(uint64_t /* Message::checksum_t */));
    fail_unless(m4.serial_size() == s4,
                "Expected m3.serial_size() = %zd, got %zd", s4,m4.serial_size());

    gu::Buffer buf4(m4.serial_size());
    m4.serialize(&buf4[0], buf4.size(), 0);

    Message mu4(VER40);
    mu4.unserialize(&buf4[0], buf4.size(), 0);
    fail_unless(mu4.version() == VER40);
    fail_unless(mu4.type()    == Message::T_HANDSHAKE);
    fail_unless(mu4.flags()   == 0x2);
    fail_unless(mu4.ctrl()    == 3);
    fail_unless(mu4.len()     == 1001);
}
END_TEST

// IST tests

static gu_barrier_t start_barrier;

class TestOrder
{
public:
    TestOrder(galera::TrxHandleSlave& trx) : trx_(trx) { }
    void lock() { }
    void unlock() { }
    wsrep_seqno_t seqno() const { return trx_.global_seqno(); }
    bool condition(wsrep_seqno_t last_entered,
                   wsrep_seqno_t last_left) const
    {
        return (last_left >= trx_.depends_seqno());
    }
#ifdef GU_DBUG_ON
    void debug_sync(gu::Mutex&) { }
#endif // GU_DBUG_ON
private:
    galera::TrxHandleSlave& trx_;
};

struct sender_args
{
    gcache::GCache& gcache_;
    const std::string& peer_;
    wsrep_seqno_t first_;
    wsrep_seqno_t last_;
    int version_;
    sender_args(gcache::GCache& gcache,
                const std::string& peer,
                wsrep_seqno_t first, wsrep_seqno_t last,
                int version)
        :
        gcache_(gcache),
        peer_  (peer),
        first_ (first),
        last_  (last),
        version_(version)
    { }
};


struct receiver_args
{
    std::string   listen_addr_;
    wsrep_seqno_t first_;
    wsrep_seqno_t last_;
    TrxHandleSlave::Pool& trx_pool_;
    gcache::GCache& gcache_;
    int           version_;

    receiver_args(const std::string listen_addr,
                  wsrep_seqno_t first, wsrep_seqno_t last,
                  TrxHandleSlave::Pool& sp,
                  gcache::GCache& gc, int version)
        :
        listen_addr_(listen_addr),
        first_      (first),
        last_       (last),
        trx_pool_   (sp),
        gcache_     (gc),
        version_    (version)
    { }
};


extern "C" void* sender_thd(void* arg)
{
    mark_point();

    const sender_args* sargs(reinterpret_cast<const sender_args*>(arg));

    gu::Config conf;
    galera::ReplicatorSMM::InitConfig(conf, NULL, NULL);
    gu_barrier_wait(&start_barrier);
    galera::ist::Sender sender(conf, sargs->gcache_, sargs->peer_,
                               sargs->version_);
    mark_point();
    sender.send(sargs->first_, sargs->last_, sargs->first_);
    mark_point();
    return 0;
}


namespace
{
    class ISTHandler : public galera::ist::EventHandler
    {
    public:
        ISTHandler() :
            mutex_(),
            cond_(),
            seqno_(0),
            eof_(false),
            error_(0)
        { }

        ~ISTHandler() {}

        void ist_trx(const TrxHandleSlavePtr& ts, bool must_apply, bool preload)
        {
            assert(ts != 0);
            ts->verify_checksum();

            if (ts->state() == TrxHandle::S_ABORTING)
            {
                log_info << "ist_trx: aborting: " << ts->global_seqno();
            }
            else
            {
                log_info << "ist_trx: " << *ts;
                ts->set_state(TrxHandle::S_CERTIFYING);
            }

            if (preload == false)
            {
                assert(seqno_ + 1 == ts->global_seqno());
            }
            else
            {
                assert(seqno_ < ts->global_seqno());
            }
            seqno_ = ts->global_seqno();
        }

        void ist_cc(const gcs_act_cchange& cc, const gcs_action& act,
                    bool must_apply, bool preload)
        {
            assert(act.seqno_g == cc.seqno);

            log_info << "ist_cc" << cc.seqno;
            if (preload == false)
            {
                assert(seqno_ + 1 == cc.seqno);
            }
            else
            {
                assert(seqno_ < cc.seqno);
            }
            seqno_ = cc.seqno;
        }

        void ist_end(int error)
        {
            log_info << "IST ended with status: " << error;
            gu::Lock lock(mutex_);
            error_ = error;
            eof_ = true;
            cond_.signal();
        }

        int wait()
        {
            gu::Lock lock(mutex_);
            while (eof_ == false)
            {
                lock.wait(cond_);
            }
            return error_;
        }

        wsrep_seqno_t seqno() const { return seqno_; }

    private:
        gu::Mutex mutex_;
        gu::Cond  cond_;
        wsrep_seqno_t seqno_;
        bool eof_;
        int error_;
    };
}

extern "C" void* receiver_thd(void* arg)
{
    mark_point();

    receiver_args* rargs(reinterpret_cast<receiver_args*>(arg));

    gu::Config conf;
    TrxHandleSlave::Pool slave_pool(sizeof(TrxHandleSlave), 1024,
                                    "TrxHandleSlave");
    galera::ReplicatorSMM::InitConfig(conf, NULL, NULL);

    mark_point();

    conf.set(galera::ist::Receiver::RECV_ADDR, rargs->listen_addr_);
    ISTHandler isth;
    galera::ist::Receiver receiver(conf, rargs->gcache_, slave_pool,
                                   isth, 0);

    // Prepare starts IST receiver thread
    rargs->listen_addr_ = receiver.prepare(rargs->first_, rargs->last_,
                                           rargs->version_,
                                           WSREP_UUID_UNDEFINED);

    gu_barrier_wait(&start_barrier);
    mark_point();

    receiver.ready(rargs->first_);

    int ist_error(isth.wait());
    log_info << "IST wait finished with status: " << ist_error;
    assert(0 == ist_error);
    fail_if(0 != ist_error, "Receiver exits with error: %d", ist_error);

    receiver.finished();

    // gcache cleanup like what would result after purge_trxs_upto()
    rargs->gcache_.seqno_release(isth.seqno());

    return 0;
}


static int select_trx_version(int protocol_version)
{
    // see protocol version table in replicator_smm.hpp
    switch (protocol_version)
    {
    case 1:
    case 2:
        return 1;
    case 3:
    case 4:
        return 2;
    case 5:
    case 6:
    case 7:
    case 8:
        return 3;
    case 9:
        return 4;
    case 10:
        return 5;
    default:
        fail("unsupported replicator protocol version: %n", protocol_version);
    }

    return -1;
}

static void store_trx(gcache::GCache* const gcache,
                      TrxHandleMaster::Pool& lp,
                      const TrxHandleMaster::Params& trx_params,
                      const wsrep_uuid_t& uuid,
                      int const i)
{
    TrxHandleMasterPtr trx(TrxHandleMaster::New(lp, trx_params, uuid, 1234+i,
                                                5678+i),
                           TrxHandleMasterDeleter());

    const wsrep_buf_t key[3] = {
        {"key1", 4},
        {"key2", 4},
        {"key3", 4}
    };

    trx->append_key(KeyData(trx_params.version_, key, 3, WSREP_KEY_EXCLUSIVE,
                            true));
    trx->append_data("bar", 3, WSREP_DATA_ORDERED, true);
    assert (i > 0);
    int last_seen(i - 1);
    int pa_range(i);

    gu::byte_t* ptr(0);

    if (trx_params.version_ < 3)
    {
        fail("WS version %d not supported any more", trx_params.version_);
    }
    else
    {
        galera::WriteSetNG::GatherVector bufs;
        ssize_t trx_size(trx->gather(bufs));
        mark_point();
        trx->finalize(last_seen);
        void* ptx;
        ptr = static_cast<gu::byte_t*>(gcache->malloc(trx_size, ptx));

        /* concatenate buffer vector */
        gu::byte_t* p(static_cast<gu::byte_t*>(ptx));
        for (size_t k(0); k < bufs->size(); ++k)
        {
            ::memcpy(p, bufs[k].ptr, bufs[k].size); p += bufs[k].size;
        }
        assert ((p - static_cast<gu::byte_t*>(ptx)) == trx_size);

        gu::Buf ws_buf = { ptx, trx_size };
        mark_point();
        galera::WriteSetIn wsi(ws_buf);
        assert (wsi.last_seen() == last_seen);
        assert (wsi.pa_range()  == (wsi.version() < WriteSetNG::VER5 ?
                                    0 : WriteSetNG::MAX_PA_RANGE));
        wsi.set_seqno(i, pa_range);
        assert (wsi.seqno()     == int64_t(i));
        assert (wsi.pa_range()  == pa_range);

        gcache->seqno_assign(ptr, i, GCS_ACT_WRITESET, (i - pa_range) <= 0);
        gcache->free(ptr);
    }
}

static void store_cc(gcache::GCache* const gcache,
                      const wsrep_uuid_t& uuid,
                     int const i)
{
    static int conf_id(0);

    gcs_act_cchange cc;

    ::memcpy(&cc.uuid, &uuid, sizeof(uuid));

    cc.seqno = i;
    cc.conf_id = conf_id++;

    void* tmp;
    int   const cc_size(cc.write(&tmp));
    void* ptx;
    void* const cc_ptr(gcache->malloc(cc_size, ptx));

    fail_if(NULL == cc_ptr);
    memcpy(ptx, tmp, cc_size);

    gcache->seqno_assign(cc_ptr, i, GCS_ACT_CCHANGE, i > 0);
    gcache->free(cc_ptr);
}

void log_test_name(int const v, bool const send_enc, bool const recv_enc)
{
    log_info << "\n\n"
        "##########################\n"
        "##                      ##\n"
        "##      IST v" << v << ' ' << (send_enc ? 'E' : 'P')
             << (recv_enc ? 'E' : 'P') << "     ##\n"
        "##                      ##\n"
        "##########################\n";
}

static void test_ist_common(int  const version,
                            bool const sender_enc,
                            bool const receiver_enc)
{
    using galera::KeyData;
    using galera::TrxHandle;
    using galera::KeyOS;

    log_test_name(version, sender_enc, receiver_enc);

    TrxHandleMaster::Pool lp(TrxHandleMaster::LOCAL_STORAGE_SIZE(), 4,
                             "ist_common");
    TrxHandleSlave::Pool sp(sizeof(TrxHandleSlave), 4, "ist_common");

    int const trx_version(select_trx_version(version));
    TrxHandleMaster::Params const trx_params("", trx_version,
                                       galera::KeySet::MAX_VERSION);

    TestEnv sender_env("ist_sender", sender_enc);
    gcache::GCache* gcache_sender = &sender_env.gcache();
    if (sender_enc) gcache_sender->param_set("gcache.keep_pages_size", "1M");

    TestEnv receiver_env("ist_receiver", receiver_enc);
    gcache::GCache* gcache_receiver = &receiver_env.gcache();

    std::string receiver_addr("tcp://127.0.0.1:0");
    wsrep_uuid_t uuid;
    gu_uuid_generate(reinterpret_cast<gu_uuid_t*>(&uuid), 0, 0);

    mark_point();

    // populate gcache
    for (size_t i(1); i <= 10; ++i)
    {
        if (i % 3)
        {
            store_trx(gcache_sender, lp, trx_params, uuid, i);
        }
        else
        {
            store_cc(gcache_sender, uuid, i);
        }
    }

    mark_point();

    receiver_args rargs(receiver_addr, 1, 10, sp, *gcache_receiver, version);
    sender_args sargs(*gcache_sender, rargs.listen_addr_, 1, 10, version);

    gu_barrier_init(&start_barrier, 0, 2);

    gu_thread_t sender_thread, receiver_thread;

    gu_thread_create(&sender_thread, 0, &sender_thd, &sargs);
    mark_point();
    usleep(100000);
    gu_thread_create(&receiver_thread, 0, &receiver_thd, &rargs);
    mark_point();

    gu_thread_join(sender_thread, 0);
    gu_thread_join(receiver_thread, 0);

    mark_point();
}

/* REPL proto 7 tests: trx ver: 3, STR ver: 2, alignment: - */
START_TEST(test_ist_v7PP)
{
    test_ist_common(7, false, false);
}
END_TEST

START_TEST(test_ist_v7PE)
{
    test_ist_common(7, false, true);
}
END_TEST

START_TEST(test_ist_v7EP)
{
    test_ist_common(7, true, false);
}
END_TEST

START_TEST(test_ist_v7EE)
{
    test_ist_common(7, true, true);
}
END_TEST

/* REPL proto 8 tests: trx ver: 3, STR ver: 2, alignment: 8 */
START_TEST(test_ist_v8PP)
{
    test_ist_common(8, false, false);
}
END_TEST

START_TEST(test_ist_v8PE)
{
    test_ist_common(8, false, true);
}
END_TEST

START_TEST(test_ist_v8EP)
{
    test_ist_common(8, true, false);
}
END_TEST

START_TEST(test_ist_v8EE)
{
    test_ist_common(8, true, true);
}
END_TEST

/* REPL proto 9 tests: trx ver: 4, STR ver: 2, alignment: 8 */
START_TEST(test_ist_v9PP)
{
    test_ist_common(9, false, false);
}
END_TEST

START_TEST(test_ist_v9PE)
{
    test_ist_common(9, false, true);
}
END_TEST

START_TEST(test_ist_v9EP)
{
    test_ist_common(9, true, false);
}
END_TEST

START_TEST(test_ist_v9EE)
{
    test_ist_common(9, true, true);
}
END_TEST

/* REPL proto 10 (Galera 4.0) tests: trx ver: 5, STR ver: 3, alignment: 8 */
START_TEST(test_ist_v10PP)
{
    test_ist_common(10, false, false);
}
END_TEST

START_TEST(test_ist_v10PE)
{
    test_ist_common(10, false, true);
}
END_TEST

START_TEST(test_ist_v10EP)
{
    test_ist_common(10, true, false);
}
END_TEST

START_TEST(test_ist_v10EE)
{
    test_ist_common(10, true, true);
}
END_TEST

Suite* ist_suite()
{
    Suite* s  = suite_create("ist");
    TCase* tc;

    tc = tcase_create("test_ist_message");
    tcase_add_test(tc, test_ist_message);
    suite_add_tcase(s, tc);
    tc = tcase_create("test_ist_v7");
    tcase_set_timeout(tc, 60);
    tcase_add_test(tc, test_ist_v7PP);
    tcase_add_test(tc, test_ist_v7PE);
    tcase_add_test(tc, test_ist_v7EP);
    tcase_add_test(tc, test_ist_v7EE);
    suite_add_tcase(s, tc);
    tc = tcase_create("test_ist_v8");
    tcase_set_timeout(tc, 60);
    tcase_add_test(tc, test_ist_v8PP);
    tcase_add_test(tc, test_ist_v8PE);
    tcase_add_test(tc, test_ist_v8EP);
    tcase_add_test(tc, test_ist_v8EE);
    suite_add_tcase(s, tc);
    tc = tcase_create("test_ist_v9");
    tcase_set_timeout(tc, 60);
    tcase_add_test(tc, test_ist_v9PP);
    tcase_add_test(tc, test_ist_v9PE);
    tcase_add_test(tc, test_ist_v9EP);
    tcase_add_test(tc, test_ist_v9EE);
    suite_add_tcase(s, tc);
    tc = tcase_create("test_ist_v10");
    tcase_set_timeout(tc, 60);
    tcase_add_test(tc, test_ist_v10PP);
    tcase_add_test(tc, test_ist_v10PE);
    tcase_add_test(tc, test_ist_v10EP);
    tcase_add_test(tc, test_ist_v10EE);
    suite_add_tcase(s, tc);

    return s;
}
