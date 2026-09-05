#include "foundation/runtime.h"

#include <stdint.h>

int main(void) {
    fdn_string hostname = fdn_string_static("", 0);
    fdn_string architecture = fdn_string_static("", 0);
    uint64_t logical_cpus = 0;
    int result = 0;
    if (foundation_runtime_system_info(&hostname, &architecture, &logical_cpus) != 0 ||
        hostname.length == 0 || architecture.length == 0 || logical_cpus == 0) {
        result = 1;
    }
    fdn_string_drop(&hostname);
    fdn_string_drop(&architecture);
    if (fdn_live_allocations() != 0) {
        result = 2;
    }
    return result;
}
