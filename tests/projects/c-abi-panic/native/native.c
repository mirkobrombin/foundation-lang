#include "foundation_abi.h"

void foundation_native_fail(void) {
    fdn_panic_cstr("C ABI failure");
}
