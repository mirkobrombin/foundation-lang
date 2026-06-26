#include "foundation/runtime.h"

#include <stdint.h>
#include <stdlib.h>

static void require(bool condition) {
    if (!condition) {
        abort();
    }
}

int main(void) {
    const size_t baseline = fdn_live_allocations();
    const fdn_string host = fdn_string_static("127.0.0.1", 9);
    const fdn_string invalid = fdn_string_static("", 0);
    const fdn_string invalid_utf8 = fdn_string_static("\xc3\x28", 2);
    uint64_t addresses = 0;

    require(foundation_runtime_net_resolve(&invalid, UINT64_C(80), &addresses) == 1);
    require(addresses == 0);
    require(foundation_runtime_net_resolve(&invalid_utf8, UINT64_C(80), &addresses) == 1);
    require(addresses == 0);
    require(foundation_runtime_net_resolve(&host, UINT64_C(0), &addresses) == 1);
    require(addresses == 0);
    require(foundation_runtime_net_resolve(&host, UINT64_C(65536), &addresses) == 1);
    require(addresses == 0);
    require(foundation_runtime_net_resolve(&host, UINT64_C(80), &addresses) == 0);
    require(addresses != 0);
    require(foundation_runtime_net_live_addresses() == 1);
    foundation_runtime_net_addresses_close(addresses);
    require(foundation_runtime_net_live_addresses() == 0);
    require(foundation_runtime_net_live_connections() == 0);
    require(foundation_runtime_net_live_requests() == 0);
    require(foundation_runtime_net_live_services() == 0);
    require(fdn_live_allocations() == baseline);
    return 0;
}
