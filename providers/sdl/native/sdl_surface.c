#include "sdl_ui_internal.h"

static foundation_ui_surface* foundation_ui_surface_for(foundation_ui* ui, uint64_t id) {
    uint64_t index;
    for (index = 0; index < FOUNDATION_UI_SURFACE_CAPACITY; index++) {
        if (ui->surfaces[index].used && ui->surfaces[index].id == id)
            return &ui->surfaces[index];
    }
    return NULL;
}

static bool foundation_ui_enqueue_input(foundation_ui_surface* surface, foundation_ui_input input) {
    uint64_t index;
    if (input.kind == FOUNDATION_UI_INPUT_MOUSE_MOVE && surface->input_count != 0) {
        index = (surface->input_head + surface->input_count - 1) % FOUNDATION_UI_INPUT_CAPACITY;
        if (surface->input_queue[index].kind == FOUNDATION_UI_INPUT_MOUSE_MOVE) {
            surface->input_queue[index] = input;
            return true;
        }
    }
    if (surface->input_count == FOUNDATION_UI_INPUT_CAPACITY) {
        surface->input_overflow = true;
        return false;
    }
    index = (surface->input_head + surface->input_count) % FOUNDATION_UI_INPUT_CAPACITY;
    surface->input_queue[index] = input;
    surface->input_count++;
    return true;
}

static bool foundation_ui_surface_point(const foundation_ui_surface* surface, float x, float y,
                                        uint64_t* surface_x, uint64_t* surface_y) {
    float normalized_x;
    float normalized_y;
    if (!surface->bounds_valid || surface->bounds.w <= 0.0f || surface->bounds.h <= 0.0f) {
        return false;
    }
    normalized_x = (x - surface->bounds.x) / surface->bounds.w;
    normalized_y = (y - surface->bounds.y) / surface->bounds.h;
    if (normalized_x < 0.0f)
        normalized_x = 0.0f;
    if (normalized_x > 1.0f)
        normalized_x = 1.0f;
    if (normalized_y < 0.0f)
        normalized_y = 0.0f;
    if (normalized_y > 1.0f)
        normalized_y = 1.0f;
    *surface_x = (uint64_t)(normalized_x * 32767.0f + 0.5f);
    *surface_y = (uint64_t)(normalized_y * 32767.0f + 0.5f);
    return true;
}

static bool foundation_ui_inside_surface(const foundation_ui_surface* surface, float x, float y) {
    return surface->bounds_valid && x >= surface->bounds.x && y >= surface->bounds.y &&
           x < surface->bounds.x + surface->bounds.w && y < surface->bounds.y + surface->bounds.h;
}

foundation_ui_surface* foundation_ui_surface_at(foundation_ui* ui, float x, float y) {
    foundation_ui_surface* selected = NULL;
    uint64_t index;
    for (index = 0; index < FOUNDATION_UI_SURFACE_CAPACITY; index++) {
        foundation_ui_surface* surface = &ui->surfaces[index];
        if (!surface->used || !foundation_ui_inside_surface(surface, x, y))
            continue;
        if (selected == NULL || surface->draw_order > selected->draw_order)
            selected = surface;
    }
    return selected;
}

static uint64_t foundation_ui_linux_key(SDL_Scancode scancode) {
    switch (scancode) {
    case SDL_SCANCODE_A:
        return 30;
    case SDL_SCANCODE_B:
        return 48;
    case SDL_SCANCODE_C:
        return 46;
    case SDL_SCANCODE_D:
        return 32;
    case SDL_SCANCODE_E:
        return 18;
    case SDL_SCANCODE_F:
        return 33;
    case SDL_SCANCODE_G:
        return 34;
    case SDL_SCANCODE_H:
        return 35;
    case SDL_SCANCODE_I:
        return 23;
    case SDL_SCANCODE_J:
        return 36;
    case SDL_SCANCODE_K:
        return 37;
    case SDL_SCANCODE_L:
        return 38;
    case SDL_SCANCODE_M:
        return 50;
    case SDL_SCANCODE_N:
        return 49;
    case SDL_SCANCODE_O:
        return 24;
    case SDL_SCANCODE_P:
        return 25;
    case SDL_SCANCODE_Q:
        return 16;
    case SDL_SCANCODE_R:
        return 19;
    case SDL_SCANCODE_S:
        return 31;
    case SDL_SCANCODE_T:
        return 20;
    case SDL_SCANCODE_U:
        return 22;
    case SDL_SCANCODE_V:
        return 47;
    case SDL_SCANCODE_W:
        return 17;
    case SDL_SCANCODE_X:
        return 45;
    case SDL_SCANCODE_Y:
        return 21;
    case SDL_SCANCODE_Z:
        return 44;
    case SDL_SCANCODE_1:
        return 2;
    case SDL_SCANCODE_2:
        return 3;
    case SDL_SCANCODE_3:
        return 4;
    case SDL_SCANCODE_4:
        return 5;
    case SDL_SCANCODE_5:
        return 6;
    case SDL_SCANCODE_6:
        return 7;
    case SDL_SCANCODE_7:
        return 8;
    case SDL_SCANCODE_8:
        return 9;
    case SDL_SCANCODE_9:
        return 10;
    case SDL_SCANCODE_0:
        return 11;
    case SDL_SCANCODE_RETURN:
        return 28;
    case SDL_SCANCODE_ESCAPE:
        return 1;
    case SDL_SCANCODE_BACKSPACE:
        return 14;
    case SDL_SCANCODE_TAB:
        return 15;
    case SDL_SCANCODE_SPACE:
        return 57;
    case SDL_SCANCODE_MINUS:
        return 12;
    case SDL_SCANCODE_EQUALS:
        return 13;
    case SDL_SCANCODE_LEFTBRACKET:
        return 26;
    case SDL_SCANCODE_RIGHTBRACKET:
        return 27;
    case SDL_SCANCODE_BACKSLASH:
        return 43;
    case SDL_SCANCODE_NONUSBACKSLASH:
        return 86;
    case SDL_SCANCODE_SEMICOLON:
        return 39;
    case SDL_SCANCODE_APOSTROPHE:
        return 40;
    case SDL_SCANCODE_GRAVE:
        return 41;
    case SDL_SCANCODE_COMMA:
        return 51;
    case SDL_SCANCODE_PERIOD:
        return 52;
    case SDL_SCANCODE_SLASH:
        return 53;
    case SDL_SCANCODE_CAPSLOCK:
        return 58;
    case SDL_SCANCODE_F1:
        return 59;
    case SDL_SCANCODE_F2:
        return 60;
    case SDL_SCANCODE_F3:
        return 61;
    case SDL_SCANCODE_F4:
        return 62;
    case SDL_SCANCODE_F5:
        return 63;
    case SDL_SCANCODE_F6:
        return 64;
    case SDL_SCANCODE_F7:
        return 65;
    case SDL_SCANCODE_F8:
        return 66;
    case SDL_SCANCODE_F9:
        return 67;
    case SDL_SCANCODE_F10:
        return 68;
    case SDL_SCANCODE_F11:
        return 87;
    case SDL_SCANCODE_F12:
        return 88;
    case SDL_SCANCODE_INSERT:
        return 110;
    case SDL_SCANCODE_HOME:
        return 102;
    case SDL_SCANCODE_PAGEUP:
        return 104;
    case SDL_SCANCODE_DELETE:
        return 111;
    case SDL_SCANCODE_END:
        return 107;
    case SDL_SCANCODE_PAGEDOWN:
        return 109;
    case SDL_SCANCODE_RIGHT:
        return 106;
    case SDL_SCANCODE_LEFT:
        return 105;
    case SDL_SCANCODE_DOWN:
        return 108;
    case SDL_SCANCODE_UP:
        return 103;
    case SDL_SCANCODE_KP_DIVIDE:
        return 98;
    case SDL_SCANCODE_KP_MULTIPLY:
        return 55;
    case SDL_SCANCODE_KP_MINUS:
        return 74;
    case SDL_SCANCODE_KP_PLUS:
        return 78;
    case SDL_SCANCODE_KP_ENTER:
        return 96;
    case SDL_SCANCODE_LCTRL:
        return 29;
    case SDL_SCANCODE_LSHIFT:
        return 42;
    case SDL_SCANCODE_LALT:
        return 56;
    case SDL_SCANCODE_LGUI:
        return 125;
    case SDL_SCANCODE_RCTRL:
        return 97;
    case SDL_SCANCODE_RSHIFT:
        return 54;
    case SDL_SCANCODE_RALT:
        return 100;
    case SDL_SCANCODE_RGUI:
        return 126;
    default:
        return 0;
    }
}

static uint64_t foundation_ui_linux_button(Uint8 button) {
    if (button == SDL_BUTTON_LEFT)
        return 272;
    if (button == SDL_BUTTON_RIGHT)
        return 273;
    if (button == SDL_BUTTON_MIDDLE)
        return 274;
    return 0;
}

void foundation_ui_release_surface_input(foundation_ui* ui, foundation_ui_surface* surface) {
    uint64_t key;
    uint64_t button;
    const uint64_t buttons[] = {272, 273, 274};
    if (surface == NULL || ui->captured_surface != surface)
        return;
    for (key = 1; key < 256; key++) {
        if (!surface->keys[key])
            continue;
        (void)foundation_ui_enqueue_input(
            surface,
            (foundation_ui_input){.kind = FOUNDATION_UI_INPUT_KEY, .key = key, .down = false});
        surface->keys[key] = false;
    }
    for (button = 0; button < 3; button++) {
        if (!surface->buttons[button])
            continue;
        (void)foundation_ui_enqueue_input(
            surface, (foundation_ui_input){.kind = FOUNDATION_UI_INPUT_MOUSE_BUTTON,
                                           .button = buttons[button],
                                           .down = false});
        surface->buttons[button] = false;
    }
    ui->captured_surface = NULL;
    (void)SDL_CaptureMouse(false);
}

bool foundation_ui_handle_surface_event(foundation_ui* ui, const SDL_Event* event) {
    foundation_ui_surface* surface = ui->captured_surface;
    uint64_t x;
    uint64_t y;
    uint64_t key;
    uint64_t button;
    uint64_t button_index;
    int64_t delta;
    if (event->type == SDL_EVENT_KEY_DOWN && surface != NULL &&
        event->key.scancode == SDL_SCANCODE_F8) {
        foundation_ui_release_surface_input(ui, surface);
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && surface == NULL) {
        surface = foundation_ui_surface_at(ui, event->button.x, event->button.y);
        if (surface != NULL) {
            ui->captured_surface = surface;
            (void)SDL_CaptureMouse(true);
        }
    }
    if (surface == NULL)
        return false;
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        if (foundation_ui_surface_point(surface, event->motion.x, event->motion.y, &x, &y)) {
            (void)foundation_ui_enqueue_input(
                surface,
                (foundation_ui_input){.kind = FOUNDATION_UI_INPUT_MOUSE_MOVE, .x = x, .y = y});
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        button = foundation_ui_linux_button(event->button.button);
        if (button == 0)
            return true;
        button_index = button - 272;
        surface->buttons[button_index] = event->button.down;
        if (foundation_ui_surface_point(surface, event->button.x, event->button.y, &x, &y)) {
            (void)foundation_ui_enqueue_input(
                surface,
                (foundation_ui_input){.kind = FOUNDATION_UI_INPUT_MOUSE_MOVE, .x = x, .y = y});
        }
        (void)foundation_ui_enqueue_input(
            surface, (foundation_ui_input){.kind = FOUNDATION_UI_INPUT_MOUSE_BUTTON,
                                           .button = button,
                                           .down = event->button.down});
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_WHEEL) {
        delta = event->wheel.integer_y;
        if (delta == 0 && event->wheel.y != 0.0f) {
            delta = event->wheel.y > 0.0f ? 1 : -1;
        }
        if (delta != 0) {
            (void)foundation_ui_enqueue_input(
                surface, (foundation_ui_input){.kind = FOUNDATION_UI_INPUT_WHEEL, .delta = delta});
        }
        return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) {
        if (event->key.repeat)
            return true;
        key = foundation_ui_linux_key(event->key.scancode);
        if (key == 0)
            return true;
        surface->keys[key] = event->key.down;
        (void)foundation_ui_enqueue_input(
            surface, (foundation_ui_input){
                         .kind = FOUNDATION_UI_INPUT_KEY, .key = key, .down = event->key.down});
        return true;
    }
    return false;
}

int32_t foundation_ui_create_surface(uint64_t handle, uint64_t* surface_id) {
    foundation_ui* ui = foundation_ui_from(handle);
    uint64_t index;
    if (ui == NULL || surface_id == NULL)
        return FOUNDATION_UI_INVALID;
    for (index = 0; index < FOUNDATION_UI_SURFACE_CAPACITY; index++) {
        foundation_ui_surface* surface = &ui->surfaces[index];
        uint64_t id;
        if (surface->used)
            continue;
        id = foundation_ui_new_surface_id();
        if (id == 0)
            return FOUNDATION_UI_FAILED;
        SDL_memset(surface, 0, sizeof(*surface));
        surface->id = id;
        surface->used = true;
        *surface_id = surface->id;
        return FOUNDATION_UI_OK;
    }
    return FOUNDATION_UI_FAILED;
}

int32_t foundation_ui_destroy_surface(uint64_t handle, uint64_t surface_id) {
    foundation_ui* ui = foundation_ui_from(handle);
    foundation_ui_surface* surface;
    if (ui == NULL)
        return FOUNDATION_UI_INVALID;
    surface = foundation_ui_surface_for(ui, surface_id);
    if (surface == NULL)
        return FOUNDATION_UI_INVALID;
    foundation_ui_release_surface_input(ui, surface);
    SDL_free(surface->image.pixels);
    SDL_DestroyTexture(surface->image.texture);
    SDL_memset(surface, 0, sizeof(*surface));
    return FOUNDATION_UI_OK;
}

uint8_t* foundation_ui_image_buffer(uint64_t handle, uint64_t surface_id, uint64_t width,
                                    uint64_t height, uint64_t* capacity) {
    foundation_ui* ui = foundation_ui_from(handle);
    foundation_ui_surface* surface;
    if (ui == NULL)
        return NULL;
    surface = foundation_ui_surface_for(ui, surface_id);
    if (surface == NULL)
        return NULL;
    return foundation_ui_reserve_image(ui, &surface->image, width, height, capacity);
}

int32_t foundation_ui_image_commit(uint64_t handle, uint64_t surface_id) {
    foundation_ui* ui = foundation_ui_from(handle);
    foundation_ui_surface* surface;
    if (ui == NULL)
        return FOUNDATION_UI_INVALID;
    surface = foundation_ui_surface_for(ui, surface_id);
    if (surface == NULL)
        return FOUNDATION_UI_INVALID;
    return foundation_ui_commit_image(&surface->image);
}

int32_t foundation_ui_image(uint64_t handle, uint64_t surface_id) {
    foundation_ui* ui = foundation_ui_from(handle);
    foundation_ui_surface* surface;
    struct nk_rect bounds;
    struct nk_rect target;
    struct nk_command_buffer* canvas;
    struct nk_image image;
    float source_ratio;
    float target_ratio;
    if (ui == NULL)
        return FOUNDATION_UI_INVALID;
    surface = foundation_ui_surface_for(ui, surface_id);
    if (surface == NULL || surface->image.texture == NULL)
        return FOUNDATION_UI_INVALID;
    if (nk_widget(&bounds, ui->context) == NK_WIDGET_INVALID)
        return FOUNDATION_UI_FAILED;
    if (!isfinite(bounds.w) || !isfinite(bounds.h) || bounds.w <= 0.0f || bounds.h <= 0.0f)
        return FOUNDATION_UI_FAILED;
    target = bounds;
    source_ratio = (float)surface->image.width / (float)surface->image.height;
    target_ratio = bounds.w / bounds.h;
    if (target_ratio > source_ratio) {
        target.w = bounds.h * source_ratio;
        target.x += (bounds.w - target.w) * 0.5f;
    } else {
        target.h = bounds.w / source_ratio;
        target.y += (bounds.h - target.h) * 0.5f;
    }
    canvas = nk_window_get_canvas(ui->context);
    image = nk_image_ptr(surface->image.texture);
    nk_draw_image(canvas, target, &image, nk_rgb(255, 255, 255));
    surface->bounds = target;
    surface->bounds_valid = true;
    ui->surface_draw_sequence++;
    surface->draw_order = ui->surface_draw_sequence;
    return FOUNDATION_UI_OK;
}

int32_t foundation_ui_poll_surface_input(uint64_t handle, uint64_t surface_id, uint64_t* kind,
                                         uint64_t* x, uint64_t* y, uint64_t* button, uint64_t* key,
                                         bool* down, int64_t* delta) {
    foundation_ui* ui = foundation_ui_from(handle);
    foundation_ui_surface* surface;
    foundation_ui_input input;
    if (ui == NULL || kind == NULL || x == NULL || y == NULL || button == NULL || key == NULL ||
        down == NULL || delta == NULL) {
        return FOUNDATION_UI_POLL_FAILED;
    }
    surface = foundation_ui_surface_for(ui, surface_id);
    if (surface == NULL)
        return FOUNDATION_UI_POLL_FAILED;
    if (surface->input_overflow) {
        surface->input_overflow = false;
        surface->input_head = 0;
        surface->input_count = 0;
        foundation_ui_release_surface_input(ui, surface);
        return FOUNDATION_UI_POLL_FAILED;
    }
    if (surface->input_count == 0)
        return FOUNDATION_UI_POLL_EMPTY;
    input = surface->input_queue[surface->input_head];
    surface->input_head = (surface->input_head + 1) % FOUNDATION_UI_INPUT_CAPACITY;
    surface->input_count--;
    *kind = input.kind;
    *x = input.x;
    *y = input.y;
    *button = input.button;
    *key = input.key;
    *down = input.down;
    *delta = input.delta;
    return FOUNDATION_UI_POLL_EVENT;
}

bool foundation_ui_surface_captured(uint64_t handle, uint64_t surface_id) {
    foundation_ui* ui = foundation_ui_from(handle);
    foundation_ui_surface* surface;
    if (ui == NULL)
        return false;
    surface = foundation_ui_surface_for(ui, surface_id);
    return surface != NULL && ui->captured_surface == surface;
}

int32_t foundation_ui_release_surface(uint64_t handle, uint64_t surface_id) {
    foundation_ui* ui = foundation_ui_from(handle);
    foundation_ui_surface* surface;
    if (ui == NULL)
        return FOUNDATION_UI_INVALID;
    surface = foundation_ui_surface_for(ui, surface_id);
    if (surface == NULL)
        return FOUNDATION_UI_INVALID;
    foundation_ui_release_surface_input(ui, surface);
    return FOUNDATION_UI_OK;
}
