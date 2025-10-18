#include "foundation/runtime.h"

#include <assert.h>

int main(void) {
    const uint64_t source = foundation_runtime_cancellation_open();
    const uint64_t token = foundation_runtime_cancellation_retain(source);

    assert(foundation_runtime_cancellation_live_states() == 1);
    assert(!foundation_runtime_cancellation_requested(token));
    foundation_runtime_cancellation_request(source);
    assert(foundation_runtime_cancellation_requested(token));

    foundation_runtime_cancellation_release(token);
    assert(foundation_runtime_cancellation_live_states() == 1);
    foundation_runtime_cancellation_release(source);
    assert(foundation_runtime_cancellation_live_states() == 0);
    assert(fdn_live_allocations() == 0);

    const uint64_t inherited = foundation_runtime_cancellation_open();
    const bool previous = fdn_task_cancellation_enter(true);
    assert(foundation_runtime_cancellation_requested(inherited));
    fdn_task_cancellation_leave(previous);
    assert(!foundation_runtime_cancellation_requested(inherited));
    foundation_runtime_cancellation_release(inherited);
    assert(foundation_runtime_cancellation_live_states() == 0);
    assert(fdn_live_allocations() == 0);
    return 0;
}
