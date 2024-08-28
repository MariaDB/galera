//
// Copyright (C) 2020 Codership Oy <info@codership.com>
//

/** @gile gu_asio_socket_util.hpp
 *
 * Common utility functions for asio sockets.
 */

#ifndef GU_ASIO_SOCKET_UTIL_HPP
#define GU_ASIO_SOCKET_UTIL_HPP

#include "gu_throw.hpp"

#ifndef GU_ASIO_IMPL
#error This header should not be included directly.
#endif // GU_ASIO_IMPL

#include "gu_asio_ip_address_impl.hpp"

#include "asio/ip/tcp.hpp"
#include "asio/version.hpp"

template <class S>
int native_socket_handle(S& socket)
{
#if ASIO_VERSION < 101401
    return socket.native();
#else
    return socket.native_handle();
#endif

}

template <class S>
static void set_fd_options(S& socket)
{
    long flags(FD_CLOEXEC);
    if (fcntl(native_socket_handle(socket), F_SETFD, flags) == -1)
    {
        gu_throw_error(errno) << "failed to set FD_CLOEXEC";
    }
}

template <class Socket>
static void set_socket_options(Socket& socket)
{
    set_fd_options(socket);
    socket.set_option(asio::ip::tcp::no_delay(true));
}

template <class Socket>
static void set_receive_buffer_size(Socket& socket, size_t size)
{
    try
    {
        socket.set_option(asio::socket_base::receive_buffer_size(size));
    }
    catch (const asio::system_error& e)
    {
        gu_throw_error(e.code().value())
            << "Failed to set receive buffer size: "
            << e.what();
    }
}

template <class Socket>
static size_t get_receive_buffer_size(Socket& socket)
{
    try
    {
        asio::socket_base::receive_buffer_size option;
        socket.get_option(option);
        return option.value();
    }
    catch (const asio::system_error& e)
    {
        gu_throw_error(e.code().value())
            << "Failed to get receive buffer size: "
            << e.what();
    }
}

template <class Socket>
static void set_send_buffer_size(Socket& socket, size_t size)
{
    try
    {
        socket.set_option(asio::socket_base::send_buffer_size(size));
    }
    catch (const asio::system_error& e)
    {
        gu_throw_error(e.code().value())
            << "Failed to set send buffer size: "
            << e.what();
    }
}

template <class Socket>
static size_t get_send_buffer_size(Socket& socket)
{
    try
    {
        asio::socket_base::send_buffer_size option;
        socket.get_option(option);
        return option.value();
    }
    catch (const asio::system_error& e)
    {
        gu_throw_error(e.code().value())
            << "Failed to get send buffer size: "
            << e.what();
    }
}

static inline asio::ip::tcp::resolver::iterator resolve_tcp(
    asio::io_service& io_service,
    const gu::URI& uri)
{
    asio::ip::tcp::resolver resolver(io_service);
    // Give query flags explicitly to avoid having AI_ADDRCONFIG in
    // underlying getaddrinfo() hint flags.
    asio::ip::tcp::resolver::query
        query(gu::unescape_addr(uri.get_host()),
              uri.get_port(),
              asio::ip::tcp::resolver::query::flags(0));
    return resolver.resolve(query);
}

template <class Socket>
static void bind(Socket& socket, const gu::AsioIpAddress& addr)
{
    try
    {
        asio::ip::tcp::endpoint endpoint(addr.impl().native(), 0);
        socket.bind(endpoint);
    }
    catch (const asio::system_error& e)
    {
        gu_throw_error(e.code().value())
            << "Failed bind socket to address: "
            << e.what();
    }
}

#ifdef __APPLE__

template <class Socket>
static struct tcp_info get_tcp_info(Socket& socket)
{
    struct tcp_info tcpi;
    memset(&tcpi, 0, sizeof(tcpi));
    // struct tcp_connection_info
    // https://developer.apple.com/documentation/kernel/tcp_connection_info
    // http://git.haproxy.org/?p=haproxy-2.6.git;a=commitdiff_plain;h=7747d465d54a1e367e9bf9c07c263d7f1f7fd481;hp=5c83e3a1563cd7face299bf08037e51f976eb5e3
    // TODO needed fields:
    //  ret.rtt            = tcpi.tcpi_rtt;
    //  ret.rttvar         = tcpi.tcpi_rttvar;
    //  ret.rto            = tcpi.tcpi_rto;
#if defined(__linux__)
    //  ret.lost           = tcpi.tcpi_lost;
#else
    //  ret.lost           = 0;
#endif /* __linux__ */
    //  ret.last_data_recv = tcpi.tcpi_last_data_recv;
    //  ret.cwnd           = tcpi.tcpi_snd_cwnd;
    //  gu::datetime::Date now(gu::datetime::Date::monotonic());
    //  Critical<AsioProtonet> crit(net_);
    //  ret.last_queued_since = (now - last_queued_tstamp_).get_nsecs();
    //  ret.last_delivered_since = (now - last_delivered_tstamp_).get_nsecs();
    //  ret.send_queue_length = send_q_.size();
    //  ret.send_queue_bytes = send_q_.queued_bytes();
    //  ret.send_queue_segments = send_q_.segments();

    return tcpi;
}

#else

template <class Socket>
static struct tcp_info get_tcp_info(Socket& socket)
{
    struct tcp_info tcpi;
    memset(&tcpi, 0, sizeof(tcpi));
#if defined(__linux__) || defined(__FreeBSD__)
#if defined(__linux__)
    static int const level(SOL_TCP);
#else /* FreeBSD */
    static int const level(IPPROTO_TCP);
#endif
    socklen_t tcpi_len(sizeof(tcpi));
    int native_fd(native_socket_handle(socket));
    if (getsockopt(native_fd, level, TCP_INFO, &tcpi, &tcpi_len))
    {
        int err(errno);
        gu_throw_error(err) << "Failed to read TCP info from socket: "
                            << strerror(err);
    }
#endif /* __linux__ || __FreeBSD__ */
    return tcpi;
}
#endif

static inline std::string
uri_string (const std::string& scheme, const std::string& addr,
                const std::string& port = std::string(""))
{
    if (port.length() > 0)
        return (scheme + "://" + addr + ':' + port);
    else
        return (scheme + "://" + addr);
}

#endif // GU_ASIO_SOCKET_UTIL_HPP
