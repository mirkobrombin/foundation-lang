#include "foundation/runtime.h"

int main(void) {
    fdn_frame caller;
    fdn_frame boundary;
    fdn_frame_enter(&caller, "main", "caller", "ffi.fdn", 3, 5);
    fdn_frame_enter_native(&boundary, "native_bridge", "bridge.c", 12, 7);
    fdn_panic_cstr("native failure");
}
