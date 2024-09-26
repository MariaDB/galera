/*
 * Copyright (C) 2024 Codership Oy <info@codership.com>
 */

#include "gcs_error.hpp"

#include <cerrno>
#include <cstring>

const char* gcs_error_str(int err)
{
    switch (err)
    {
    case EINTR: return "Operation interrupted";
    case EAGAIN: return "Operation failed temporarily";
    case EPERM:
    case ENOTCONN: return "Not in primary component";
    case ECONNABORTED: return "Connection was closed";
    case EBADF: return "Connection not initialized";
    case ETIMEDOUT: return "Operation timed out";
    default: return strerror(err);
    }
}

const char* gcs_state_transfer_error_str(int err)
{
  switch (err)
  {
  case EAGAIN:
      return "No donor candidates temporarily available in suitable state";
  case EHOSTUNREACH: return "Requested donor is not available";
  case EHOSTDOWN: return "Joiner and donor can't be the same node";
  default: return gcs_error_str(err);
  }
}
