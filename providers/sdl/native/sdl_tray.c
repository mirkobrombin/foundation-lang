#include "sdl_ui_internal.h"

static void SDLCALL foundation_ui_tray_action_callback(void* data, SDL_TrayEntry* entry) {
    foundation_ui_tray_action* action = data;
    foundation_ui* ui;
    uint64_t position;
    (void)entry;
    if (action == NULL || action->owner == NULL)
        return;
    ui = action->owner;
    if (ui->tray_event_count == FOUNDATION_UI_TRAY_EVENT_CAPACITY) {
        ui->tray_event_overflow = true;
        return;
    }
    position = (ui->tray_event_head + ui->tray_event_count) % FOUNDATION_UI_TRAY_EVENT_CAPACITY;
    ui->tray_events[position] = action->id;
    ui->tray_event_count++;
}

static SDL_Surface* foundation_ui_tray_icon(const foundation_ui* ui) {
    SDL_Surface* icon;
    uint64_t row;
    size_t row_length;
    if (ui->application_image.pixels == NULL || ui->application_image.width == 0 ||
        ui->application_image.height == 0 || ui->application_image.width > INT32_MAX ||
        ui->application_image.height > INT32_MAX) {
        return NULL;
    }
    row_length = (size_t)(ui->application_image.width * 4);
    icon = SDL_CreateSurface((int)ui->application_image.width, (int)ui->application_image.height,
                             SDL_PIXELFORMAT_RGBA32);
    if (icon == NULL)
        return NULL;
    for (row = 0; row < ui->application_image.height; row++) {
        SDL_memcpy((uint8_t*)icon->pixels + (size_t)row * (size_t)icon->pitch,
                   ui->application_image.pixels + row * row_length, row_length);
    }
    return icon;
}

int32_t foundation_ui_create_tray(uint64_t handle, const fdn_string* tooltip) {
    foundation_ui* ui = foundation_ui_from(handle);
    char* text;
    if (ui == NULL || !foundation_ui_string_valid(tooltip))
        return FOUNDATION_UI_INVALID;
    if (ui->tray != NULL)
        return FOUNDATION_UI_INVALID;
    text = foundation_ui_text(tooltip);
    if (text == NULL)
        return FOUNDATION_UI_INVALID;
    ui->tray_icon = foundation_ui_tray_icon(ui);
    if (ui->application_image.pixels != NULL && ui->tray_icon == NULL) {
        SDL_free(text);
        return FOUNDATION_UI_FAILED;
    }
    ui->tray = SDL_CreateTray(ui->tray_icon, text);
    SDL_free(text);
    if (ui->tray == NULL) {
        SDL_DestroySurface(ui->tray_icon);
        ui->tray_icon = NULL;
        return FOUNDATION_UI_UNAVAILABLE;
    }
    ui->tray_menu = SDL_CreateTrayMenu(ui->tray);
    if (ui->tray_menu == NULL) {
        foundation_ui_destroy_tray(handle);
        return FOUNDATION_UI_FAILED;
    }
    return FOUNDATION_UI_OK;
}

int32_t foundation_ui_set_tray_tooltip(uint64_t handle, const fdn_string* tooltip) {
    foundation_ui* ui = foundation_ui_from(handle);
    char* text;
    if (ui == NULL || ui->tray == NULL || !foundation_ui_string_valid(tooltip))
        return FOUNDATION_UI_INVALID;
    text = foundation_ui_text(tooltip);
    if (text == NULL)
        return FOUNDATION_UI_INVALID;
    SDL_SetTrayTooltip(ui->tray, text);
    SDL_free(text);
    return FOUNDATION_UI_OK;
}

int32_t foundation_ui_add_tray_action(uint64_t handle, const fdn_string* label,
                                      uint64_t* action_id) {
    foundation_ui* ui = foundation_ui_from(handle);
    foundation_ui_tray_action* action;
    char* text;
    if (ui == NULL || ui->tray_menu == NULL || !foundation_ui_string_valid(label) ||
        label->length == 0 || action_id == NULL) {
        return FOUNDATION_UI_INVALID;
    }
    *action_id = 0;
    if (ui->tray_action_count == FOUNDATION_UI_TRAY_ACTION_CAPACITY ||
        ui->next_tray_action_id == UINT64_MAX) {
        return FOUNDATION_UI_FAILED;
    }
    text = foundation_ui_text(label);
    if (text == NULL)
        return FOUNDATION_UI_INVALID;
    action = &ui->tray_actions[ui->tray_action_count];
    action->entry = SDL_InsertTrayEntryAt(ui->tray_menu, -1, text, SDL_TRAYENTRY_BUTTON);
    SDL_free(text);
    if (action->entry == NULL)
        return FOUNDATION_UI_FAILED;
    ui->next_tray_action_id++;
    action->owner = ui;
    action->id = ui->next_tray_action_id;
    SDL_SetTrayEntryCallback(action->entry, foundation_ui_tray_action_callback, action);
    ui->tray_action_count++;
    *action_id = action->id;
    return FOUNDATION_UI_OK;
}

int32_t foundation_ui_add_tray_separator(uint64_t handle) {
    foundation_ui* ui = foundation_ui_from(handle);
    if (ui == NULL || ui->tray_menu == NULL)
        return FOUNDATION_UI_INVALID;
    if (SDL_InsertTrayEntryAt(ui->tray_menu, -1, NULL, 0) == NULL)
        return FOUNDATION_UI_FAILED;
    return FOUNDATION_UI_OK;
}

int32_t foundation_ui_poll_tray_action(uint64_t handle, uint64_t* action) {
    foundation_ui* ui = foundation_ui_from(handle);
    if (ui == NULL || action == NULL || ui->tray == NULL)
        return FOUNDATION_UI_POLL_FAILED;
    *action = 0;
    if (ui->tray_event_overflow) {
        ui->tray_event_overflow = false;
        return FOUNDATION_UI_POLL_FAILED;
    }
    if (ui->tray_event_count == 0)
        return FOUNDATION_UI_POLL_EMPTY;
    *action = ui->tray_events[ui->tray_event_head];
    ui->tray_event_head = (ui->tray_event_head + 1) % FOUNDATION_UI_TRAY_EVENT_CAPACITY;
    ui->tray_event_count--;
    return FOUNDATION_UI_POLL_EVENT;
}

void foundation_ui_destroy_tray(uint64_t handle) {
    foundation_ui* ui = foundation_ui_from(handle);
    if (ui == NULL || ui->tray == NULL)
        return;
    SDL_DestroyTray(ui->tray);
    SDL_DestroySurface(ui->tray_icon);
    ui->tray = NULL;
    ui->tray_menu = NULL;
    ui->tray_icon = NULL;
    ui->tray_action_count = 0;
    ui->tray_event_head = 0;
    ui->tray_event_count = 0;
    ui->next_tray_action_id = 0;
    ui->tray_event_overflow = false;
    SDL_memset(ui->tray_actions, 0, sizeof(ui->tray_actions));
    SDL_memset(ui->tray_events, 0, sizeof(ui->tray_events));
}
