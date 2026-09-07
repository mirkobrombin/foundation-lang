#include <foundation/ui.h>

#include <stdint.h>

_Static_assert(FOUNDATION_UI_UNAVAILABLE < 0, "provider errors must be negative");
_Static_assert(FOUNDATION_UI_INVALID < 0, "provider errors must be negative");
_Static_assert(FOUNDATION_UI_FAILED < 0, "provider errors must be negative");
_Static_assert(FOUNDATION_UI_FRAME_ACTIVE >= 0, "frame states must be nonnegative");
_Static_assert(FOUNDATION_UI_FRAME_CLOSING >= 0, "frame states must be nonnegative");
_Static_assert(FOUNDATION_UI_FRAME_IDLE >= 0, "frame states must be nonnegative");

int main(void) {
    uint64_t invalid = UINT64_MAX;
    if (foundation_ui_provider_abi() != FOUNDATION_UI_ABI_CURRENT)
        return 1;
    if (FOUNDATION_UI_ABI_MAJOR_OF(foundation_ui_provider_abi()) != FOUNDATION_UI_ABI_MAJOR)
        return 2;
    if (FOUNDATION_UI_ABI_MINOR_OF(foundation_ui_provider_abi()) != FOUNDATION_UI_ABI_MINOR)
        return 3;
    if (foundation_ui_begin_frame(invalid) != FOUNDATION_UI_INVALID)
        return 4;
    foundation_ui_close(&invalid);
    if (invalid != 0)
        return 5;
    return 0;
}
