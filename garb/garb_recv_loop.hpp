/* Copyright (C) 2011-2023 Codership Oy <info@codership.com> */

#ifndef _GARB_RECV_LOOP_HPP_
#define _GARB_RECV_LOOP_HPP_

#include "garb_gcs.hpp"
#include "garb_config.hpp"

#include <gu_throw.hpp>
#include <gu_asio.hpp>
#include <common.h> // COMMON_BASE_DIR_KEY

#include <pthread.h>

namespace garb
{

class RecvLoop
{
public:

    RecvLoop (const Config&);

    ~RecvLoop () {}

private:

    bool one_loop();
    void loop();
    void close_connection();

    const Config& config_;
    gu::Config    gconf_;

    struct RegisterParams
    {
        RegisterParams(gu::Config& cnf)
        {
            gu::ssl_register_params(cnf);
            gcs_register_params(cnf);
            cnf.add(COMMON_BASE_DIR_KEY);
        }
    }
        params_;

    struct ParseOptions
    {
        ParseOptions(gu::Config& cnf, const std::string& opt)
        {
            cnf.parse(opt);
            gu::ssl_init_options(cnf);
        }
    }
        parse_;

    Gcs gcs_;

    gu::UUID    uuid_;
    gu::seqno_t seqno_;
    int         proto_;
    bool        closed_;

}; /* RecvLoop */

} /* namespace garb */

#endif /* _GARB_RECV_LOOP_HPP_ */
