#include <foundation/ui.h>

#include <cstdint>

static_assert(FOUNDATION_UI_ABI_MAJOR_OF(FOUNDATION_UI_ABI_CURRENT) == FOUNDATION_UI_ABI_MAJOR);
static_assert(FOUNDATION_UI_ABI_MINOR_OF(FOUNDATION_UI_ABI_CURRENT) == FOUNDATION_UI_ABI_MINOR);

int main() {
    const std::uint64_t version = foundation_ui_provider_abi();
    return version == FOUNDATION_UI_ABI_CURRENT ? 0 : 1;
}
