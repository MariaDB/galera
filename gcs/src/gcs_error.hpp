/*
 * Copyright (C) 2024 Codership Oy <info@codership.com>
 */

/*! @file gcs_error.hpp
 *
 * Error code to error string translation according to GCS conventions.
 */

#ifndef GCS_ERROR_HPP
#define GCS_ERROR_HPP

/*!
 * Return an error string associated with a system error code for gcs calls
 * where the error code does not come from system call. As a fallback,
 * error string for unhandled error codes are obtained by strerror()
 * system call.
 *
 * This function follows the following conventions for system error
 * codes for group communication errors:
 *
 * EAGAIN - Operation failed temporarily due to group configuration
 *          change or flow control.
 * ENOTCONN, EPERM - Not in primary component.
 * ECONNABORTED - Connection was closed while the operation was in progress.
 * ETIMEDOUT - Operation timed out.
 * EBADF - Connection was not initialized.
 *
 * @param err System error code.
 * @return Error string describing the error condition.
 */
const char* gcs_error_str(int err);

/*!
 * Return and errorstring associated with a system error code for
 * state transfer requests. As a fallback, error string for unhandled
 * error codes are obtained by strerror() system call.
 *
 * The function follows the following conventions for system error codes
 * for state transfer request errors (for details, see donor selection in
 * gcs_group.cpp):
 *
 * EAGAIN - No donors available in suitable state.
 * EHOSTUNREACH - Requested donor is not avaialble.
 * EHOSTDOWN - Joiner and donor can't be the same node.
 *
 * @param err System error code.
 * @return Error string describing state transfer error condition.
 */
const char* gcs_state_transfer_error_str(int err);


#endif /* GCS_ERROR_HPP */
