#include "foundation/runtime.h"

#include <assert.h>
#include <stdint.h>

int main(void) {
    uint64_t capture = 0;
    uint64_t width = 0;
    uint64_t height = 0;
    uint64_t frame = 0;
    uint64_t length = 0;
    uint64_t input = 0;
    int32_t status;

    status = foundation_runtime_desktop_capture_open(&capture, &width, &height);
    if (status == 0) {
        assert(capture != 0);
        assert(width != 0);
        assert(height != 0);
        status = foundation_runtime_desktop_capture_frame(capture, &frame);
        if (status == 0) {
            assert(frame != 0);
            status = foundation_runtime_bytes_length(frame, &length);
            assert(status == 0);
            assert(length == width * height * 4);
            foundation_runtime_bytes_close(&frame);
        } else {
            assert(status == 2 || status == 4);
        }
        foundation_runtime_desktop_capture_close(&capture);
    } else {
        assert(status == 1 || status == 2);
    }
    assert(foundation_runtime_desktop_capture_live_handles() == 0);

    status = foundation_runtime_desktop_input_open(&input);
    if (status == 0) {
        assert(input != 0);
        assert(foundation_runtime_desktop_input_move(input, 32768, 0) == 3);
        foundation_runtime_desktop_input_close(&input);
    } else {
        assert(status == 1 || status == 2 || status == 4);
    }
    assert(foundation_runtime_desktop_input_live_handles() == 0);
    return 0;
}
