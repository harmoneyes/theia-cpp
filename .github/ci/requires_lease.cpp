// CI check: the vendored library must refuse to start without a device lease.

#include <harmoneyes/theia.hpp>

#include <cstdio>

int main()
{
    try {
        harmoneyes::theia::TheiaSDK sdk{harmoneyes::theia::Device::PupilLabsNeon};
    } catch (const harmoneyes::theia::LeaseVerificationError&) {
        std::puts("refused to start without a lease");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "started without a lease, then failed: %s\n", e.what());
        return 1;
    }
    std::fputs("started without a lease\n", stderr);
    return 1;
}
