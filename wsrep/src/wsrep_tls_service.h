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

/** @file wsrep_tls_service.h
 *
 * This file defines interface for TLS services provided by the application,
 * used by the provider.
 *
 * In order to support both synchronous and asynchronous IO operations,
 * the interface is designed to work with sockets in both blocking
 * and non-blockig mode.
 *
 * The provider is in charge of opening and closing file
 * descriptors and connecting transport. After the connection has
 * been established, all further IO operations will be delegated
 * to the TLS service implementation which is provided by the application.
 *
 * The provider which is capable of using the service interface v1 must
 * export the following functions.
 *
 * int wsrep_init_tls_service_v1(wsrep_tls_service_v1_t*)
 * void wsrep_deinit_tls_service_v1()
 *
 * which can be probed by the application.
 *
 * The application must initialize the service via above init function
 * before the provider is initialized via wsrep->init(). The deinit
 * function must be called after the provider side resources have been
 * released via wsrep->free().
 */

#ifndef WSREP_TLS_SERVICE_H
#define WSREP_TLS_SERVICE_H

#include <sys/types.h> /* posix size_t */

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/**
 * Type tag for application defined TLS context.
 *
 * Application may pass pointer to the context when initializing
 * TLS service. This pointer is passed a first parameter for
 * each service call.
 */
typedef struct wsrep_tls_context wsrep_tls_context_t;

/**
 * TLS stream structure.
 */
typedef struct wsrep_tls_stream_st
{
    /**
     * File descriptor corresponding to the stream. The provider is
     * responsible in opening and closing the socket.
     */
    int fd;
    /**
     * Opaque pointer reserved for application use.
     */
    void* opaque;
} wsrep_tls_stream_t;

/**
 * Enumeration for return codes.
 */
enum wsrep_tls_result
{
    /**
     * The operation completed successfully, no further actions
     * are necessary.
     */
    wsrep_tls_result_success = 0,
    /**
     * The operation completed successfully, but the application side wants
     * to make further reads. The provider must wait until the stream
     * becomes readable and then try the same operation again.
     */
    wsrep_tls_result_want_read,
    /**
     * The operation completed successfully, but the application side wants
     * to make further writes. The provider must wait until the stream
     * becomes writable and then try the same operation again.
     */
    wsrep_tls_result_want_write,
    /**
     * End of file was read from the stream. This result is needed to
     * make difference between graceful stream shutdown and zero length
     * reads which result from errors.
     */
    wsrep_tls_result_eof,
    /**
     * An error occurred. The specific error reason must be
     * queried with wsrep_tls_stream_get_error_number and
     * wsrep_tls_stream_get_error_category.
     */
    wsrep_tls_result_error
};

/**
 * Initialize a new TLS stream.
 *
 * Initialize the stream for IO operations. During this call the
 * application must set up all of the data structures needed for
 * IO, but must not do any reads or writes into the stream yet.
 *
 * @param stream TLS stream to be initialized.
 *
 * @return Zero on success, system error number on error.
 */
typedef int (*wsrep_tls_stream_init_t)(wsrep_tls_context_t*,
                                       wsrep_tls_stream_t* stream);

/**
 * Deinitialize the TLS stream.
 *
 * Deinitialize the TLS stream and free all allocated resources.
 * Note that this function must not close the socket file descriptor
 * associated the the stream.
 *
 * @param stream Stream to be deinitialized.
 */
typedef void (*wsrep_tls_stream_deinit_t)(wsrep_tls_context_t*,
                                          wsrep_tls_stream_t* stream);

/**
 * Get error number of the last stream error. The error numbers are
 * defined by the application and must be integral type. By the convention
 * zero value must denote success.
 *
 * For managing errors other than system errors, the application may
 * provide several error categories via wsrep_tls_stream_get_error_category_t.
 *
 * @param stream TLS stream to get the last error from.
 *
 * @return Error number.
 */
typedef int (*wsrep_tls_stream_get_error_number_t)(
    wsrep_tls_context_t*,
    const wsrep_tls_stream_t* stream);

/**
 * Get the error category of the last stream error.
 *
 * The category is represented via a const void pointer to the provider.
 * If the category is NULL pointer, the error number is assumed to be
 * system error.
 *
 * @param stream Stream to get last error category from.
 *
 * @return Pointer to error category.
 */
typedef const void* (*wsrep_tls_stream_get_error_category_t)(
    wsrep_tls_context_t*,
    const wsrep_tls_stream_t* stream);

/**
 * Return human readable error message by error number and error
 * category.
 *
 * The message string returned by the application must contain only
 * printable characters and must be null terminated.
 *
 * @param error_number Error number returned by
 *                     wsrep_tls_stream_get_error_number_t.
 * @param category Error category returned by
 *                 wsrep_tls_stream_get_error_category_t.
 *
 * @return Human readable message string.
 */
typedef const char* (*wsrep_tls_error_message_get_t)(
    wsrep_tls_context_t*,
    const wsrep_tls_stream_t* stream,
    int error_number, const void* category);

/**
 * Initiate TLS client side handshake. This function is called for the
 * stream sockets which have been connected by the provider.
 *
 * If the stream socket is in non-blocking mode, the call should return
 * immediately with appropriate result indicating if more actions are needed
 * in the case the operation would block. The provider will call this function
 * again until either a success or an error is returned.
 *
 * @param stream TLS stream.
 *
 * @return Enum wsrep_tls_result.
 */
typedef enum wsrep_tls_result (*wsrep_tls_stream_client_handshake_t)(
    wsrep_tls_context_t*,
    wsrep_tls_stream_t* stream);

/**
 * Initiate TLS server side handshake. This function is called for stream
 * sockets which have been accepted by the provider.
 *
 * If the stream socket is in non-blocking mode, the call should return
 * immediately with appropriate result indicating if more actions are needed
 * in the case the operation would block. The provider will call this function
 * again until either a success or an error is returned.
 *
 * @param stream TLS stream.
 *
 * @return Enum wsrep_tls_result.
 */
typedef enum wsrep_tls_result (*wsrep_tls_stream_server_handshake_t)(
    wsrep_tls_context_t*,
    wsrep_tls_stream_t* stream);

/**
 * Perform a read from the stream. If the file descriptor associated
 * to the stream is in non-blocking mode, the call must return immediately
 * with appropriate result if the stream processing would block.
 *
 * @param[in] stream TLS stream.
 * @param[in] buf Buffer to read the data into.
 * @param[in] max_count Maximum number of bytes to read.
 * @param[out] bytes_transferred Number of bytes read into the buffer during
 *             the operation.
 *
 * @return Enum wsrep_tls_result.
 */
typedef enum wsrep_tls_result (*wsrep_tls_stream_read_t)(
    wsrep_tls_context_t*,
    wsrep_tls_stream_t* stream,
    void* buf,
    size_t max_count,
    size_t* bytes_transferred);

/**
 * Perform a write to the stream. If the file descriptor asociated to
 * te stream is in non-blocking mode, the call must return immediately
 * with appropriate result if the stream processing would block.
 *
 * @param[in] stream TLS stream.
 * @param[in] buf Buffer which contains the data to write.
 * @param[in] count Number of bytes to be written.
 * @param[out] bytes_transferred Number of bytes written into the stream
 *             during the opration.
 *
 * @return Enum wsrep_tls_result.
 */
typedef enum wsrep_tls_result (*wsrep_tls_stream_write_t)(
    wsrep_tls_context_t*,
    wsrep_tls_stream_t* stream,
    const void* buf,
    size_t count,
    size_t* bytes_transferred);

/**
 * Shutdown the TLS stream.
 *
 * Note that the implementation must not close the associated stream
 * socket, just shut down the protocol.
 *
 * If the shutdown call returns either wsrep_result_want_read or
 * wsrep_result_want_write, the provider must wait until the socket
 * becomes readable or writable and then call the function again
 * until the return status is either success or an error occurs.
 *
 * @param stream TLS stream to be shut down.
 *
 * @return Enum wsrep_tls_result code.
 *
 */
typedef enum wsrep_tls_result (*wsrep_tls_stream_shutdown_t)(
    wsrep_tls_context_t*,
    wsrep_tls_stream_t* stream);

/**
 * TLS service struct.
 *
 * A pointer to this struct must be passed to the call to
 * wsrep_init_tls_service_v1.
 *
 * The application must provide implementation to all functions defined
 * in this struct.
 */
typedef struct wsrep_tls_service_v1_st
{
    /* Stream */
    wsrep_tls_stream_init_t stream_init;
    wsrep_tls_stream_deinit_t stream_deinit;
    wsrep_tls_stream_get_error_number_t stream_get_error_number;
    wsrep_tls_stream_get_error_category_t stream_get_error_category;
    wsrep_tls_stream_client_handshake_t stream_client_handshake;
    wsrep_tls_stream_server_handshake_t stream_server_handshake;
    wsrep_tls_stream_read_t stream_read;
    wsrep_tls_stream_write_t stream_write;
    wsrep_tls_stream_shutdown_t stream_shutdown;
    /* Error */
    wsrep_tls_error_message_get_t error_message_get;
    /* Pointer to application defined TLS context. */
    wsrep_tls_context_t* context;
} wsrep_tls_service_v1_t;


#ifdef __cplusplus
}
#endif /* __cplusplus */


#define WSREP_TLS_SERVICE_INIT_FUNC_V1 "wsrep_init_tls_service_v1"
#define WSREP_TLS_SERVICE_DEINIT_FUNC_V1 "wsrep_deinit_tls_service_v1"

#endif /* WSREP_TLS_SERVICE_H */

