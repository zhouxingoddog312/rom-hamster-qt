#include "rh-err.h"

const char *rh_err_str(RhErr err)
{
    switch (err) {
    case RH_OK:                 return "ok";
    case RH_ERR_INVALID:        return "invalid argument";
    case RH_ERR_NOMEM:          return "out of memory";
    case RH_ERR_IO:             return "i/o error";
    case RH_ERR_DB:             return "database error";
    case RH_ERR_SCHEMA_TOO_NEW: return "database schema is newer than this build";
    case RH_ERR_MIGRATE:        return "schema migration failed";
    case RH_ERR_UNSUPPORTED:    return "unsupported";
    case RH_ERR_ENCRYPTED:      return "archive is encrypted";
    case RH_ERR_NOT_FOUND:      return "not found";
    }
    return "unknown error";
}
