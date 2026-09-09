#include "sdl_ui_internal.h"

#include <string.h>

static int test_pending_and_cancelled(void) {
    foundation_ui_file_dialog* dialog = foundation_ui_file_dialog_create();
    uint64_t state = 0;
    fdn_string path = {0};
    const char* const cancelled[] = {NULL};
    int result = 0;
    if (dialog == NULL)
        return 1;
    if (foundation_ui_file_dialog_read(dialog, &state, &path) != FOUNDATION_UI_OK ||
        state != FOUNDATION_UI_FILE_DIALOG_PENDING || path.length != 0) {
        result = 2;
    }
    if (result == 0) {
        foundation_ui_file_dialog_complete(dialog, cancelled);
        if (foundation_ui_file_dialog_read(dialog, &state, &path) != FOUNDATION_UI_OK ||
            state != FOUNDATION_UI_FILE_DIALOG_CANCELLED || path.length != 0) {
            result = 3;
        }
    }
    fdn_string_drop(&path);
    foundation_ui_file_dialog_release(dialog);
    return result;
}

static int test_selected(void) {
    foundation_ui_file_dialog* dialog = foundation_ui_file_dialog_create();
    uint64_t state = 0;
    fdn_string path = {0};
    const char* const selected[] = {"/home/example/document.txt", NULL};
    int result = 0;
    if (dialog == NULL)
        return 1;
    foundation_ui_file_dialog_complete(dialog, selected);
    if (foundation_ui_file_dialog_read(dialog, &state, &path) != FOUNDATION_UI_OK ||
        state != FOUNDATION_UI_FILE_DIALOG_SELECTED ||
        path.length != strlen(selected[0]) || memcmp(path.data, selected[0], path.length) != 0) {
        result = 2;
    }
    fdn_string_drop(&path);
    foundation_ui_file_dialog_release(dialog);
    return result;
}

static int test_failure(void) {
    foundation_ui_file_dialog* dialog = foundation_ui_file_dialog_create();
    uint64_t state = 0;
    fdn_string path = {0};
    int result = 0;
    if (dialog == NULL)
        return 1;
    foundation_ui_file_dialog_complete(dialog, NULL);
    if (foundation_ui_file_dialog_read(dialog, &state, &path) != FOUNDATION_UI_FAILED)
        result = 2;
    fdn_string_drop(&path);
    foundation_ui_file_dialog_release(dialog);
    return result;
}

int main(void) {
    const int cancelled = test_pending_and_cancelled();
    const int selected = test_selected();
    const int failed = test_failure();
    return cancelled == 0 && selected == 0 && failed == 0 ? 0 : 1;
}
