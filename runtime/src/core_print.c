#include "foundation/runtime_core.h"
#include "foundation/freestanding.h"

/* A separate archive member, so that only libraries that print require fdn_hook_write. Hosted
   printing stays in runtime.c. */
#if !defined(FOUNDATION_FREESTANDING)
#error "core_print.c belongs to the freestanding runtime"
#endif

void fdn_println(fdn_string value) {
    if (value.data != NULL && value.length != 0) {
        fdn_hook_write(value.data, value.length);
    }
    fdn_hook_write("\n", 1);
}

void fdn_abi_println(const fdn_string *value) { fdn_println(*value); }
