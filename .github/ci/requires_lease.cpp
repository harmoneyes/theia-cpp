// CI check: the vendored library must refuse to start without a valid device
// lease.
//
// Both models are disabled. A default-constructed SDK loads them first, and on
// a runner with no model cache that fails with "Required model ... not in
// cache" BEFORE the lease is ever checked — so the check passed or failed on
// model availability rather than on the lease. Disabling them puts the lease
// verification first, which is the only thing this is meant to prove.

#include <harmoneyes/theia.hpp>

#include <cstdio>
#include <cstdlib>

namespace het = harmoneyes::theia;

/// Construct with `lease` and report whether it was refused for a lease reason.
static bool refused(const char* what, const char* lease)
{
    het::Options options;
    options.device               = het::Device::PupilLabsNeon;
    options.enable_cog_load_model = false;
    options.enable_fatigue_model  = false;
    if (lease != nullptr) { options.lease_token = lease; }

    try {
        het::TheiaSDK sdk{options};
    } catch (const het::LeaseVerificationError& e) {
        std::printf("%s: refused (%s)\n", what, e.what());
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s: failed for an unrelated reason: %s\n", what, e.what());
        return false;
    }
    std::fprintf(stderr, "%s: started anyway\n", what);
    return false;
}

int main()
{
    // The harness clears these, but an inherited value would silently satisfy
    // the no-lease case and turn this whole check green for the wrong reason.
    if (std::getenv("THEIA_LEASE") != nullptr || std::getenv("THEIA_LICENSE_KEY") != nullptr) {
        std::fputs("THEIA_LEASE / THEIA_LICENSE_KEY must be unset for this check\n", stderr);
        return 1;
    }

    const bool no_lease  = refused("no lease", nullptr);
    const bool bad_lease = refused("malformed lease", "not-a-lease");
    return (no_lease && bad_lease) ? 0 : 1;
}
