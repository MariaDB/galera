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

#include "socket.h"

#include "log.h"

#include <assert.h>
#include <ctype.h>      // isspace()
#include <errno.h>
#include <limits.h>     // USHRT_MAX
#include <netdb.h>      // struct addrinfo
#include <stdio.h>      // snprintf()
#include <string.h>     // strerror()
#include <sys/socket.h> // bind(), connect(), accept(), send(), recv()

struct node_socket
{
    int fd;
};

/**
 * Initializes addrinfo from the separate host address and port arguments
 *
 * Requires calling freeaddrinfo() later
 *
 * @param[in] host - if NULL, will be initialized for listening
 * @param[in] port
 *
 * @return struct addrinfo* or NULL in case of error
 */
static struct addrinfo*
socket_get_addrinfo2(const char* const host,
                     uint16_t    const port)
{
    struct addrinfo const hints =
    {
        .ai_flags     = AI_PASSIVE |   /** will be ignored if host is not NULL */
                        AI_NUMERICSERV, /** service is a numeric port */
        .ai_family    = AF_UNSPEC,      /** either IPv4 or IPv6 */
        .ai_socktype  = SOCK_STREAM,    /** STREAM or DGRAM */
        .ai_protocol  = 0,
        .ai_addrlen   = 0,
        .ai_addr      = NULL,
        .ai_canonname = NULL,
        .ai_next      = NULL
    };

    char service[6];
    snprintf(service, sizeof(service), "%hu", port);

    struct addrinfo* info;
    int err = getaddrinfo(host, service, &hints, &info);
    if (err)
    {
        NODE_ERROR("Failed to resolve '%s': %d (%s)",
                   host, err, gai_strerror(err));
        return NULL;
    }

    return info;
}

/**
 * Initializes addrinfo from single address and port string
 * The port is expected to be in numerical form and appended to the host address
 * via colon.
 *
 * Requires calling freeaddrinfo() later
 *
 * @param[in] addr full address specification, including port
 *
 * @return struct addrinfo* or NULL in case of error
 */
static struct addrinfo*
socket_get_addrinfo1(const char* const addr)
{
    int   const addr_len = (int)strlen(addr);
    char* const addr_buf = strdup(addr);
    if (!addr_buf)
    {
        NODE_ERROR("strdup(%s) failed: %d (%s)", addr, errno, strerror(errno));
        return NULL;
    }

    struct addrinfo* res = NULL;
    long port;
    char* endptr;

    int i;
    for (i = addr_len - 1; i >= 0; i--)
    {
        if (addr_buf[i] == ':') break;
    }

    if (addr_buf[i] != ':')
    {
        NODE_ERROR("Malformed address:port string: '%s'", addr);
        goto end;
    }

    addr_buf[i] = '\0';
    port = strtol(addr_buf + i + 1, &endptr, 10);

    if (port <= 0 || port > USHRT_MAX || errno ||
        (*endptr != '\0' && !isspace(*endptr)))
    {
        NODE_ERROR("Malformed/invalid port: '%s'. Errno: %d (%s)",
                   addr_buf + i + 1, errno, strerror(errno));
        goto end;
    }

    res = socket_get_addrinfo2(strlen(addr_buf) > 0 ? addr_buf : NULL,
                               (uint16_t)port);
end:
    free(addr_buf);
    return res;
}

static struct node_socket*
socket_create(int const fd)
{
    assert(fd > 0);

    struct node_socket* res = calloc(1, sizeof(struct node_socket));
    if (res)
    {
        res->fd = fd;
    }
    else
    {
        NODE_ERROR("Failed to allocate struct node_socket: %d (%s)",
                   errno, strerror(errno));
        close(fd);
    }

    return res;
}

/**
 * Definition of function type with the signature of bind() and connect()
 */
typedef int (*socket_act_fun_t) (int                    sfd,
                                 const struct sockaddr* addr,
                                 socklen_t              addrlen);

static int
socket_bind_and_listen(int                    const sfd,
                       const struct sockaddr* const addr,
                       socklen_t              const addrlen)
{
    int ret = bind(sfd, addr, addrlen);

    if (!ret)
        ret = listen(sfd, SOMAXCONN);

    return ret;
}

/**
 * A "template" method to do the "right thing" with the addrinfo and create a
 * socket from it. The "right thing" would normally be bind and listen for
 * a server socket OR connect for a client socket.
 *
 * @param[in] info       addrinfo list, swallowed and deallocated
 * @param[in] action_fun the "right thing" to do on socket and struct sockaddr
 * @param[in] action_str action description to be printed in the error message
 * @param[in] orig_host  host address to be pronted in the error message
 * @param[in] orig_port  port to be printed in the error message, if orig_host
 *                       string contains the port, this parameter should be 0
 *
 * The last three parameters are for diagnostic puposes only. orig_host and
 * orig_port are supposed to be what were used to obtain addrinfo.
 *
 * @return new struct node_socket.
 */
static struct node_socket*
socket_from_addrinfo(struct addrinfo* const info,
                     socket_act_fun_t const action_fun,
                     const char*      const action_str,
                     const char*      const orig_host,
                     uint16_t         const orig_port)
{
    int sfd;
    int err = 0;

    /* Iterate over addrinfo list and try to apply action_fun on the resulting
     * socket. Once successful, break loop. */
    struct addrinfo* addr;
    for (addr = info; addr != NULL; addr = addr->ai_next)
    {
        sfd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (sfd == -1)
        {
            err = errno;
            continue;
        }

        if (action_fun(sfd, addr->ai_addr, addr->ai_addrlen) == 0) break;

        err = errno;
        close(sfd);
    }

    freeaddrinfo(info); /* no longer needed */

    if (!addr)
    {
        NODE_ERROR("Failed to %s to '%s%s%.0hu': %d (%s)",
                   action_str,
                   orig_host ? orig_host : "", orig_port > 0 ? ":" : "",
                   orig_port > 0 ? orig_port : 0, /* won't be printed if 0 */
                   err, strerror(err));
        return NULL;
    }

    assert(sfd > 0);
    return socket_create(sfd);
}

struct node_socket*
node_socket_listen(const char* const host, uint16_t const port)
{
    struct addrinfo* const info = socket_get_addrinfo2(host, port);
    if (!info) return NULL;

    return socket_from_addrinfo(info, socket_bind_and_listen,
                                "bind a listening socket", host, port);
}

struct node_socket*
node_socket_connect(const char* const addr_str)
{
    struct addrinfo* const info = socket_get_addrinfo1(addr_str);
    if (!info) return NULL;

    return socket_from_addrinfo(info, connect, "connect", addr_str, 0);
}

struct node_socket*
node_socket_accept(struct node_socket* socket)
{
    int sfd = accept(socket->fd, NULL, NULL);

    if (sfd < 0)
    {
        NODE_ERROR("Failed to accept connection: %d (%s)",
                   errno, strerror(errno));
        return NULL;
    }

    return socket_create(sfd);
}

int
node_socket_send_bytes(node_socket_t* socket, const void* buf, size_t len)
{
    ssize_t const ret = send(socket->fd, buf, len, MSG_NOSIGNAL);

    if (ret != (ssize_t)len)
    {
        NODE_ERROR("Failed to send %zu bytes: %d (%s)", errno, strerror(errno));
        return -1;
    }

    return 0;
}

int
node_socket_recv_bytes(node_socket_t* socket, void* buf, size_t len)
{
    ssize_t const ret = recv(socket->fd, buf, len, MSG_WAITALL);

    if (ret != (ssize_t)len)
    {
        NODE_ERROR("Failed to recv %zu bytes: %d (%s)", errno, strerror(errno));
        return -1;
    }

    return 0;
}

void
node_socket_close(node_socket_t* socket)
{
    if (!socket) return;

    if (socket->fd > 0) close(socket->fd);

    free(socket);
}
