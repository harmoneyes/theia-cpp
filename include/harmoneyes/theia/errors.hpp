#pragma once

#include <stdexcept>
#include <string>

#include "harmoneyes/theia/detail/c_api.hpp"

namespace harmoneyes::theia {

/// Why a call failed.
///
/// Mirrors theia_error_code_t from the C ABI. Declared independently so no C
/// type appears in a public signature; the values must stay in step with
/// vendor/include/harmoneyes/theia_export.h.
enum class ErrorCode : int {
    None                = THEIA_ERROR_NONE,
    Unknown             = THEIA_ERROR_UNKNOWN,
    ModelUnavailable    = THEIA_ERROR_MODEL_UNAVAILABLE,
    LeaseMalformed      = THEIA_ERROR_LEASE_MALFORMED,
    LeaseUnknownKey     = THEIA_ERROR_LEASE_UNKNOWN_KEY,
    LeaseBadSignature   = THEIA_ERROR_LEASE_BAD_SIGNATURE,
    LeaseExpired        = THEIA_ERROR_LEASE_EXPIRED,
    LeaseDeviceMismatch = THEIA_ERROR_LEASE_DEVICE_MISMATCH,
    LeaseTenantMismatch = THEIA_ERROR_LEASE_TENANT_MISMATCH,
};

/// Base of every exception this SDK throws.
///
/// Derives from std::runtime_error, so `catch (const std::exception&)` in
/// existing code keeps working.
class Error : public std::runtime_error {
public:
    Error(ErrorCode code, const std::string& what)
        : std::runtime_error(what), code_(code) {}

    [[nodiscard]] ErrorCode code() const noexcept { return code_; }

private:
    ErrorCode code_;
};

/// The call failed and the library gave no further detail.
class UnknownError : public Error {
    using Error::Error;
};

/// A model artifact could not be loaded: absent from the cache and not
/// retrievable from the CDN, or present but not decryptable.
class ModelUnavailableError : public Error {
    using Error::Error;
};

/// Base of the device-lease failures.
///
/// Catch this to handle uniformly any lease failure the C ABI distinguishes;
/// catch a subclass to tell an expired lease from a malformed one.
///
/// A lease failure the C ABI has no specific code for is reported as
/// UnknownError, which does NOT derive from this, so a base catch will not see
/// it. The six subclasses below are exhaustive against the codes that exist
/// today.
class LeaseVerificationError : public Error {
    using Error::Error;
};

/// The lease is not a well-formed token.
class LeaseMalformedError : public LeaseVerificationError {
    using LeaseVerificationError::LeaseVerificationError;
};

/// The lease was signed with a key this build does not know.
class LeaseUnknownKeyError : public LeaseVerificationError {
    using LeaseVerificationError::LeaseVerificationError;
};

/// The lease signature did not verify.
///
/// Named for the Python SDK's LeaseSignatureError; the underlying code is
/// THEIA_ERROR_LEASE_BAD_SIGNATURE.
class LeaseSignatureError : public LeaseVerificationError {
    using LeaseVerificationError::LeaseVerificationError;
};

/// The lease is past its expiry. Mint a new one; a long-running process can
/// swap it in without restarting.
class LeaseExpiredError : public LeaseVerificationError {
    using LeaseVerificationError::LeaseVerificationError;
};

/// The lease was issued for a different device.
class LeaseDeviceMismatchError : public LeaseVerificationError {
    using LeaseVerificationError::LeaseVerificationError;
};

/// The lease was issued for a different tenant.
class LeaseTenantMismatchError : public LeaseVerificationError {
    using LeaseVerificationError::LeaseVerificationError;
};

}  // namespace harmoneyes::theia
