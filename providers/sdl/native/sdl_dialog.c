#include "sdl_ui_internal.h"

foundation_ui_file_dialog* foundation_ui_file_dialog_create(void) {
    foundation_ui_file_dialog* dialog = SDL_calloc(1, sizeof(*dialog));
    if (dialog == NULL)
        return NULL;
    dialog->mutex = SDL_CreateMutex();
    if (dialog->mutex == NULL) {
        SDL_free(dialog);
        return NULL;
    }
    (void)SDL_SetAtomicInt(&dialog->references, 1);
    dialog->state = FOUNDATION_UI_FILE_DIALOG_PENDING;
    return dialog;
}

void foundation_ui_file_dialog_retain(foundation_ui_file_dialog* dialog) {
    if (dialog != NULL)
        (void)SDL_AtomicIncRef(&dialog->references);
}

void foundation_ui_file_dialog_release(foundation_ui_file_dialog* dialog) {
    if (dialog == NULL || !SDL_AtomicDecRef(&dialog->references))
        return;
    SDL_DestroyMutex(dialog->mutex);
    SDL_free(dialog->path);
    SDL_free(dialog);
}

void foundation_ui_file_dialog_complete(foundation_ui_file_dialog* dialog,
                                        const char* const* filelist) {
    char* path = NULL;
    uint64_t state = FOUNDATION_UI_FILE_DIALOG_CANCELLED;
    size_t length = 0;
    if (dialog == NULL)
        return;
    if (filelist == NULL) {
        state = UINT64_MAX;
    } else if (filelist[0] != NULL) {
        length = SDL_strlen(filelist[0]);
        if (length == 0) {
            state = UINT64_MAX;
        } else {
            path = SDL_strdup(filelist[0]);
            state = path == NULL ? UINT64_MAX : FOUNDATION_UI_FILE_DIALOG_SELECTED;
        }
    }
    SDL_LockMutex(dialog->mutex);
    if (dialog->state == FOUNDATION_UI_FILE_DIALOG_PENDING) {
        dialog->path = path;
        dialog->path_length = path == NULL ? 0 : length;
        dialog->state = state;
        path = NULL;
    }
    SDL_UnlockMutex(dialog->mutex);
    SDL_free(path);
}

int32_t foundation_ui_file_dialog_read(foundation_ui_file_dialog* dialog, uint64_t* state,
                                       fdn_string* path) {
    if (dialog == NULL || state == NULL || path == NULL)
        return FOUNDATION_UI_INVALID;
    fdn_string_drop(path);
    *path = fdn_string_static("", 0);
    SDL_LockMutex(dialog->mutex);
    *state = dialog->state;
    if (dialog->state == FOUNDATION_UI_FILE_DIALOG_SELECTED) {
        *path = foundation_runtime_string_copy(&(fdn_string){dialog->path, dialog->path_length, 0});
    }
    SDL_UnlockMutex(dialog->mutex);
    return *state == UINT64_MAX ? FOUNDATION_UI_FAILED : FOUNDATION_UI_OK;
}

static void SDLCALL foundation_ui_file_dialog_callback(void* userdata, const char* const* filelist,
                                                       int filter) {
    foundation_ui_file_dialog* dialog = userdata;
    (void)filter;
    foundation_ui_file_dialog_complete(dialog, filelist);
    foundation_ui_file_dialog_release(dialog);
}

int32_t foundation_ui_request_open_file_dialog(uint64_t handle) {
    foundation_ui* ui = foundation_ui_from(handle);
    foundation_ui_file_dialog* dialog;
    if (ui == NULL)
        return FOUNDATION_UI_INVALID;
    if (ui->file_dialog != NULL)
        return FOUNDATION_UI_INVALID;
    dialog = foundation_ui_file_dialog_create();
    if (dialog == NULL)
        return FOUNDATION_UI_FAILED;
    ui->file_dialog = dialog;
    foundation_ui_file_dialog_retain(dialog);
    SDL_ShowOpenFileDialog(foundation_ui_file_dialog_callback, dialog, ui->window, NULL, 0, NULL,
                           false);
    return FOUNDATION_UI_OK;
}

int32_t foundation_ui_request_open_folder_dialog(uint64_t handle) {
    foundation_ui* ui = foundation_ui_from(handle);
    foundation_ui_file_dialog* dialog;
    if (ui == NULL)
        return FOUNDATION_UI_INVALID;
    if (ui->file_dialog != NULL)
        return FOUNDATION_UI_INVALID;
    dialog = foundation_ui_file_dialog_create();
    if (dialog == NULL)
        return FOUNDATION_UI_FAILED;
    ui->file_dialog = dialog;
    foundation_ui_file_dialog_retain(dialog);
    SDL_ShowOpenFolderDialog(foundation_ui_file_dialog_callback, dialog, ui->window, NULL, false);
    return FOUNDATION_UI_OK;
}

int32_t foundation_ui_poll_open_file_dialog(uint64_t handle, uint64_t* state, fdn_string* path) {
    foundation_ui* ui = foundation_ui_from(handle);
    int32_t result;
    if (ui == NULL || state == NULL || path == NULL)
        return FOUNDATION_UI_INVALID;
    if (ui->file_dialog == NULL) {
        fdn_string_drop(path);
        *path = fdn_string_static("", 0);
        *state = FOUNDATION_UI_FILE_DIALOG_IDLE;
        return FOUNDATION_UI_OK;
    }
    result = foundation_ui_file_dialog_read(ui->file_dialog, state, path);
    if (*state != FOUNDATION_UI_FILE_DIALOG_PENDING) {
        foundation_ui_file_dialog_release(ui->file_dialog);
        ui->file_dialog = NULL;
    }
    return result;
}
