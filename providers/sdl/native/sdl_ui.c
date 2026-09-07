#include <SDL3/SDL.h>

static char* foundation_ui_dtoa(char* destination, double value) {
    (void)SDL_snprintf(destination, 64, "%.17g", value);
    return destination;
}

#define NK_DTOA(str, value) foundation_ui_dtoa(str, value)

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wc23-extensions"
#pragma clang diagnostic ignored "-Wstrict-prototypes"
#endif
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4116 4244 4267 4701 5287)
#endif
#define FOUNDATION_UI_PROVIDER_IMPLEMENTATION
#include "sdl_ui_internal.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "vendor/dejavu_sans_mono.inc"
#include "vendor/inter_regular.inc"
#include "vendor/inter_semibold.inc"

static const nk_rune foundation_ui_terminal_glyph_ranges[] = {
    0x0020, 0x024f, 0x0370, 0x052f, 0x2000, 0x206f, 0x2190,
    0x21ff, 0x2500, 0x27bf, 0x2b00, 0x2bff, 0,
};

uint64_t foundation_ui_provider_abi(void) { return FOUNDATION_UI_ABI_CURRENT; }

bool foundation_ui_string_valid(const fdn_string* value) {
    return value != NULL && (value->length == 0 || value->data != NULL);
}

const char* foundation_ui_string_data(const fdn_string* value) {
    return value->length == 0 ? "" : value->data;
}

char* foundation_ui_text(const fdn_string* value) {
    char* copy;
    if (!foundation_ui_string_valid(value) ||
        (value->length != 0 && memchr(value->data, '\0', value->length) != NULL)) {
        return NULL;
    }
    if (value->length == SIZE_MAX)
        return NULL;
    copy = SDL_malloc(value->length + 1);
    if (copy == NULL)
        return NULL;
    if (value->length != 0)
        SDL_memcpy(copy, value->data, value->length);
    copy[value->length] = '\0';
    return copy;
}

static SDL_SpinLock foundation_ui_registry_lock;
static foundation_ui** foundation_ui_windows;
static size_t foundation_ui_window_count;
static size_t foundation_ui_window_capacity;
static uint64_t foundation_ui_next_window_id;
static uint64_t foundation_ui_next_surface_id;

foundation_ui* foundation_ui_from(uint64_t handle) {
    foundation_ui* ui = NULL;
    size_t index;
    if (handle == 0)
        return NULL;
    SDL_LockSpinlock(&foundation_ui_registry_lock);
    for (index = 0; index < foundation_ui_window_count; index++) {
        if (foundation_ui_windows[index]->id == handle) {
            ui = foundation_ui_windows[index];
            break;
        }
    }
    SDL_UnlockSpinlock(&foundation_ui_registry_lock);
    return ui;
}

uint64_t foundation_ui_new_surface_id(void) {
    uint64_t id = 0;
    SDL_LockSpinlock(&foundation_ui_registry_lock);
    if (foundation_ui_next_surface_id != UINT64_MAX) {
        foundation_ui_next_surface_id++;
        id = foundation_ui_next_surface_id;
    }
    SDL_UnlockSpinlock(&foundation_ui_registry_lock);
    return id;
}

static bool foundation_ui_register(foundation_ui* ui) {
    foundation_ui** windows;
    size_t capacity;
    SDL_LockSpinlock(&foundation_ui_registry_lock);
    if (foundation_ui_next_window_id == UINT64_MAX) {
        SDL_UnlockSpinlock(&foundation_ui_registry_lock);
        return false;
    }
    if (foundation_ui_window_count == foundation_ui_window_capacity) {
        capacity = foundation_ui_window_capacity == 0 ? 4 : foundation_ui_window_capacity * 2;
        if (capacity < foundation_ui_window_capacity ||
            capacity > SIZE_MAX / sizeof(*foundation_ui_windows)) {
            SDL_UnlockSpinlock(&foundation_ui_registry_lock);
            return false;
        }
        windows = SDL_realloc(foundation_ui_windows, capacity * sizeof(*foundation_ui_windows));
        if (windows == NULL) {
            SDL_UnlockSpinlock(&foundation_ui_registry_lock);
            return false;
        }
        foundation_ui_windows = windows;
        foundation_ui_window_capacity = capacity;
    }
    foundation_ui_next_window_id++;
    ui->id = foundation_ui_next_window_id;
    foundation_ui_windows[foundation_ui_window_count++] = ui;
    SDL_UnlockSpinlock(&foundation_ui_registry_lock);
    return true;
}

static void foundation_ui_unregister(foundation_ui* ui) {
    size_t index;
    SDL_LockSpinlock(&foundation_ui_registry_lock);
    for (index = 0; index < foundation_ui_window_count; index++) {
        if (foundation_ui_windows[index] != ui)
            continue;
        foundation_ui_window_count--;
        foundation_ui_windows[index] = foundation_ui_windows[foundation_ui_window_count];
        break;
    }
    if (foundation_ui_window_count == 0) {
        SDL_free(foundation_ui_windows);
        foundation_ui_windows = NULL;
        foundation_ui_window_capacity = 0;
    }
    SDL_UnlockSpinlock(&foundation_ui_registry_lock);
}

static void foundation_ui_route_event(foundation_ui* current, const SDL_Event* event) {
    SDL_Window* window = SDL_GetWindowFromEvent(event);
    size_t index;
    if (window == NULL || window == current->window)
        return;
    SDL_LockSpinlock(&foundation_ui_registry_lock);
    for (index = 0; index < foundation_ui_window_count; index++) {
        foundation_ui* target = foundation_ui_windows[index];
        uint64_t position;
        if (target->window != window)
            continue;
        if (target->event_count == FOUNDATION_UI_EVENT_CAPACITY) {
            target->event_overflow = true;
            break;
        }
        position = (target->event_head + target->event_count) % FOUNDATION_UI_EVENT_CAPACITY;
        target->event_queue[position] = *event;
        target->event_count++;
        break;
    }
    SDL_UnlockSpinlock(&foundation_ui_registry_lock);
}

static bool foundation_ui_take_event(foundation_ui* ui, SDL_Event* event) {
    bool available = false;
    SDL_LockSpinlock(&foundation_ui_registry_lock);
    if (ui->event_count != 0) {
        *event = ui->event_queue[ui->event_head];
        ui->event_head = (ui->event_head + 1) % FOUNDATION_UI_EVENT_CAPACITY;
        ui->event_count--;
        available = true;
    }
    SDL_UnlockSpinlock(&foundation_ui_registry_lock);
    return available;
}

static void foundation_ui_close_all(void) {
    size_t index;
    SDL_LockSpinlock(&foundation_ui_registry_lock);
    for (index = 0; index < foundation_ui_window_count; index++) {
        foundation_ui_windows[index]->closing = true;
    }
    SDL_UnlockSpinlock(&foundation_ui_registry_lock);
}

static void foundation_ui_process_event(foundation_ui* ui, SDL_Event* event, bool* active,
                                        bool* resized) {
    *active = true;
    if (event->type == SDL_EVENT_QUIT) {
        foundation_ui_close_all();
    } else if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        ui->closing = true;
    }
    if (event->type == SDL_EVENT_WINDOW_RESIZED || event->type == SDL_EVENT_WINDOW_MAXIMIZED ||
        event->type == SDL_EVENT_WINDOW_RESTORED) {
        *resized = true;
    }
    if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        foundation_ui_release_surface_input(ui, ui->captured_surface);
        ui->terminal_focus = false;
    }
    if (!foundation_ui_handle_terminal_event(ui, event) &&
        !foundation_ui_handle_surface_event(ui, event)) {
        (void)nk_sdl_handle_event(ui->context, event);
    }
}

static void foundation_ui_apply_theme(foundation_ui* ui, bool light) {
    struct nk_color table[NK_COLOR_COUNT];
    const struct nk_color background = light ? nk_rgb(247, 249, 252) : nk_rgb(15, 24, 40);
    const struct nk_color panel = light ? nk_rgb(255, 255, 255) : nk_rgb(18, 30, 49);
    const struct nk_color raised = light ? nk_rgb(235, 239, 245) : nk_rgb(25, 40, 64);
    const struct nk_color border = light ? nk_rgb(197, 207, 221) : nk_rgb(47, 66, 94);
    const struct nk_color text = light ? nk_rgb(15, 24, 40) : nk_rgb(247, 249, 252);
    const struct nk_color muted = light ? nk_rgb(75, 91, 113) : nk_rgb(156, 172, 195);
    const struct nk_color accent = ui->custom_accent ? ui->accent : nk_rgb(95, 221, 122);

    table[NK_COLOR_TEXT] = text;
    table[NK_COLOR_WINDOW] = background;
    table[NK_COLOR_HEADER] = panel;
    table[NK_COLOR_BORDER] = border;
    table[NK_COLOR_BUTTON] = raised;
    table[NK_COLOR_BUTTON_HOVER] = border;
    table[NK_COLOR_BUTTON_ACTIVE] = accent;
    table[NK_COLOR_TOGGLE] = raised;
    table[NK_COLOR_TOGGLE_HOVER] = border;
    table[NK_COLOR_TOGGLE_CURSOR] = accent;
    table[NK_COLOR_SELECT] = raised;
    table[NK_COLOR_SELECT_ACTIVE] = accent;
    table[NK_COLOR_SLIDER] = raised;
    table[NK_COLOR_SLIDER_CURSOR] = muted;
    table[NK_COLOR_SLIDER_CURSOR_HOVER] = text;
    table[NK_COLOR_SLIDER_CURSOR_ACTIVE] = accent;
    table[NK_COLOR_PROPERTY] = panel;
    table[NK_COLOR_EDIT] = panel;
    table[NK_COLOR_EDIT_CURSOR] = text;
    table[NK_COLOR_COMBO] = panel;
    table[NK_COLOR_CHART] = panel;
    table[NK_COLOR_CHART_COLOR] = muted;
    table[NK_COLOR_CHART_COLOR_HIGHLIGHT] = accent;
    table[NK_COLOR_SCROLLBAR] = background;
    table[NK_COLOR_SCROLLBAR_CURSOR] = raised;
    table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = border;
    table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = muted;
    table[NK_COLOR_TAB_HEADER] = panel;
    table[NK_COLOR_KNOB] = panel;
    table[NK_COLOR_KNOB_CURSOR] = muted;
    table[NK_COLOR_KNOB_CURSOR_HOVER] = text;
    table[NK_COLOR_KNOB_CURSOR_ACTIVE] = accent;
    nk_style_from_table(ui->context, table);

    ui->context->style.window.padding = nk_vec2(24.0f, 0.0f);
    ui->context->style.window.spacing = nk_vec2(8.0f, 8.0f);
    ui->context->style.window.border = 0.0f;
    ui->context->style.window.rounding = 8.0f;
    ui->context->style.window.group_padding = nk_vec2(16.0f, 16.0f);
    ui->context->style.window.contextual_border = 1.0f;
    ui->context->style.window.contextual_border_color = border;
    ui->context->style.window.contextual_padding = nk_vec2(6.0f, 6.0f);
    ui->context->style.button.rounding = 6.0f;
    ui->context->style.button.padding = nk_vec2(12.0f, 8.0f);
    ui->context->style.contextual_button.normal = nk_style_item_color(background);
    ui->context->style.contextual_button.hover = nk_style_item_color(raised);
    ui->context->style.contextual_button.active = nk_style_item_color(accent);
    ui->context->style.contextual_button.border_color = border;
    ui->context->style.contextual_button.text_background = background;
    ui->context->style.contextual_button.text_normal = text;
    ui->context->style.contextual_button.text_hover = text;
    ui->context->style.contextual_button.text_active = background;
    ui->context->style.contextual_button.border = 0.0f;
    ui->context->style.contextual_button.rounding = 5.0f;
    ui->context->style.contextual_button.padding = nk_vec2(10.0f, 7.0f);
    ui->context->style.contextual_button.disabled_factor = 0.48f;
    ui->context->style.edit.rounding = 6.0f;
    ui->context->style.edit.border = 1.0f;
    ui->context->style.edit.padding = nk_vec2(12.0f, 8.0f);
    ui->background = background;
    ui->panel = panel;
    ui->raised = raised;
    ui->text = text;
    ui->muted = muted;
    ui->accent = accent;
    ui->light_theme = light;
}

static bool foundation_ui_set_window_shape(foundation_ui* ui) {
    SDL_Surface* shape;
    const SDL_PixelFormatDetails* format;
    Uint32 transparent;
    Uint32 opaque;
    int width;
    int height;
    int radius;
    const bool maximized = (SDL_GetWindowFlags(ui->window) & SDL_WINDOW_MAXIMIZED) != 0;
    if (!SDL_GetWindowSize(ui->window, &width, &height))
        return false;
    if (width <= 0 || height <= 0)
        return false;
    if (width == ui->shape_width && height == ui->shape_height &&
        maximized == ui->shape_maximized) {
        return true;
    }
    radius = 10;
    if (radius > width / 2)
        radius = width / 2;
    if (radius > height / 2)
        radius = height / 2;
    shape = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    if (shape == NULL)
        return false;
    format = SDL_GetPixelFormatDetails(shape->format);
    if (format == NULL) {
        SDL_DestroySurface(shape);
        return false;
    }
    transparent = SDL_MapRGBA(format, NULL, 0, 0, 0, 0);
    opaque = SDL_MapRGBA(format, NULL, 255, 255, 255, 255);
    if (!SDL_FillSurfaceRect(shape, NULL, opaque) || !SDL_LockSurface(shape)) {
        SDL_DestroySurface(shape);
        return false;
    }
    if (!maximized) {
        for (int y = 0; y < radius; y++) {
            Uint32* top = (Uint32*)((uint8_t*)shape->pixels + y * shape->pitch);
            Uint32* bottom = (Uint32*)((uint8_t*)shape->pixels + (height - 1 - y) * shape->pitch);
            for (int x = 0; x < radius; x++) {
                const int dx = radius - 1 - x;
                const int dy = radius - 1 - y;
                if (dx * dx + dy * dy < radius * radius)
                    continue;
                top[x] = transparent;
                top[width - 1 - x] = transparent;
                bottom[x] = transparent;
                bottom[width - 1 - x] = transparent;
            }
        }
    }
    SDL_UnlockSurface(shape);
    const bool applied = SDL_SetWindowShape(ui->window, shape);
    SDL_DestroySurface(shape);
    if (!applied)
        return false;
    ui->shape_width = width;
    ui->shape_height = height;
    ui->shape_maximized = maximized;
    return true;
}

static SDL_HitTestResult foundation_ui_hit_test(SDL_Window* window, const SDL_Point* area,
                                                void* data) {
    int width;
    int height;
    const int edge = 6;
    int drag_limit;
    (void)data;
    if (!SDL_GetWindowSize(window, &width, &height))
        return SDL_HITTEST_NORMAL;
    drag_limit = width > 108 ? width - 108 : width;
    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0) {
        if (area->y < 40 && area->x < drag_limit)
            return SDL_HITTEST_DRAGGABLE;
        return SDL_HITTEST_NORMAL;
    }
    const bool left = area->x < edge;
    const bool right = area->x >= width - edge;
    const bool top = area->y < edge;
    const bool bottom = area->y >= height - edge;
    if (left && top)
        return SDL_HITTEST_RESIZE_TOPLEFT;
    if (right && top)
        return SDL_HITTEST_RESIZE_TOPRIGHT;
    if (left && bottom)
        return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    if (right && bottom)
        return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    if (left)
        return SDL_HITTEST_RESIZE_LEFT;
    if (right)
        return SDL_HITTEST_RESIZE_RIGHT;
    if (top)
        return SDL_HITTEST_RESIZE_TOP;
    if (bottom)
        return SDL_HITTEST_RESIZE_BOTTOM;
    if (area->y < 40 && area->x < drag_limit)
        return SDL_HITTEST_DRAGGABLE;
    return SDL_HITTEST_NORMAL;
}

int32_t foundation_ui_open(const fdn_string* title, uint64_t width, uint64_t height,
                           uint64_t* handle) {
    foundation_ui* ui;
    struct nk_font_atlas* atlas;
    struct nk_font_config regular_config;
    struct nk_font_config heading_config;
    struct nk_font_config terminal_config;
    SDL_WindowFlags window_flags;
    char* window_title;

    if (handle == NULL)
        return FOUNDATION_UI_INVALID;
    *handle = 0;
    if (title == NULL || width == 0 || height == 0 || width > INT32_MAX || height > INT32_MAX) {
        return FOUNDATION_UI_INVALID;
    }
    window_title = foundation_ui_text(title);
    if (window_title == NULL)
        return FOUNDATION_UI_INVALID;
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_free(window_title);
        return FOUNDATION_UI_UNAVAILABLE;
    }
    ui = SDL_calloc(1, sizeof(*ui));
    if (ui == NULL) {
        SDL_free(window_title);
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
        return FOUNDATION_UI_FAILED;
    }
    window_flags = SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (!SDL_CreateWindowAndRenderer(window_title, (int)width, (int)height,
                                     window_flags | SDL_WINDOW_TRANSPARENT, &ui->window,
                                     &ui->renderer)) {
        if (ui->renderer != NULL)
            SDL_DestroyRenderer(ui->renderer);
        if (ui->window != NULL)
            SDL_DestroyWindow(ui->window);
        ui->window = NULL;
        ui->renderer = NULL;
        if (!SDL_CreateWindowAndRenderer(window_title, (int)width, (int)height, window_flags,
                                         &ui->window, &ui->renderer)) {
            SDL_free(window_title);
            SDL_free(ui);
            SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
            return FOUNDATION_UI_UNAVAILABLE;
        }
        ui->shape_disabled = true;
    }
    SDL_free(window_title);
    if (!SDL_SetWindowHitTest(ui->window, foundation_ui_hit_test, ui)) {
        SDL_DestroyRenderer(ui->renderer);
        SDL_DestroyWindow(ui->window);
        SDL_free(ui);
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
        return FOUNDATION_UI_FAILED;
    }
    if (!ui->shape_disabled && !foundation_ui_set_window_shape(ui)) {
        ui->shape_disabled = true;
    }
    (void)SDL_SetRenderVSync(ui->renderer, 1);
    ui->context = nk_sdl_init(ui->window, ui->renderer, nk_sdl_allocator());
    atlas = nk_sdl_font_stash_begin(ui->context);
    regular_config = nk_font_config(17.0f);
    regular_config.ttf_data_owned_by_atlas = 0;
    heading_config = nk_font_config(28.0f);
    heading_config.ttf_data_owned_by_atlas = 0;
    terminal_config = nk_font_config(17.0f);
    terminal_config.ttf_data_owned_by_atlas = 0;
    terminal_config.range = foundation_ui_terminal_glyph_ranges;
    ui->regular_font =
        nk_font_atlas_add_from_memory(atlas, (void*)foundation_inter_regular_ttf,
                                      foundation_inter_regular_ttf_len, 17.0f, &regular_config);
    ui->heading_font =
        nk_font_atlas_add_from_memory(atlas, (void*)foundation_inter_semibold_ttf,
                                      foundation_inter_semibold_ttf_len, 28.0f, &heading_config);
    ui->terminal_font =
        nk_font_atlas_add_from_memory(atlas, (void*)foundation_dejavu_mono_ttf,
                                      foundation_dejavu_mono_ttf_len, 17.0f, &terminal_config);
    if (ui->regular_font == NULL || ui->heading_font == NULL || ui->terminal_font == NULL) {
        nk_sdl_shutdown(ui->context);
        SDL_DestroyRenderer(ui->renderer);
        SDL_DestroyWindow(ui->window);
        SDL_free(ui);
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
        return FOUNDATION_UI_FAILED;
    }
    atlas->default_font = ui->regular_font;
    nk_sdl_font_stash_end(ui->context);
    ui->terminal = foundation_terminal_create(100, 32);
    if (ui->terminal == NULL) {
        nk_sdl_shutdown(ui->context);
        SDL_DestroyRenderer(ui->renderer);
        SDL_DestroyWindow(ui->window);
        SDL_free(ui);
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
        return FOUNDATION_UI_FAILED;
    }
    ui->terminal_auto_focus = true;
    ui->first_frame = true;
    foundation_ui_apply_theme(ui, false);
    if (!foundation_ui_register(ui)) {
        foundation_terminal_destroy(ui->terminal);
        nk_sdl_shutdown(ui->context);
        SDL_DestroyRenderer(ui->renderer);
        SDL_DestroyWindow(ui->window);
        SDL_free(ui);
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
        return FOUNDATION_UI_FAILED;
    }
    *handle = ui->id;
    return FOUNDATION_UI_OK;
}

void foundation_ui_close(uint64_t* handle) {
    foundation_ui* ui;
    uint64_t index;
    size_t edit_index;
    if (handle == NULL || *handle == 0)
        return;
    ui = foundation_ui_from(*handle);
    if (ui == NULL) {
        *handle = 0;
        return;
    }
    foundation_ui_unregister(ui);
    for (edit_index = 0; edit_index < ui->edit_count; edit_index++) {
        SDL_free(ui->edit_states[edit_index].name);
        SDL_free(ui->edit_states[edit_index].buffer);
    }
    SDL_free(ui->edit_states);
    for (index = 0; index < FOUNDATION_UI_SURFACE_CAPACITY; index++) {
        if (!ui->surfaces[index].used)
            continue;
        SDL_free(ui->surfaces[index].image.pixels);
        SDL_DestroyTexture(ui->surfaces[index].image.texture);
    }
    SDL_free(ui->terminal_transfer);
    foundation_terminal_destroy(ui->terminal);
    SDL_free(ui->application_image.pixels);
    SDL_DestroyTexture(ui->application_image.texture);
    nk_sdl_shutdown(ui->context);
    SDL_DestroyRenderer(ui->renderer);
    SDL_DestroyWindow(ui->window);
    SDL_free(ui);
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    *handle = 0;
}

int32_t foundation_ui_begin_frame(uint64_t handle) {
    foundation_ui* ui = foundation_ui_from(handle);
    SDL_Event event;
    bool active;
    bool resized = false;
    if (ui == NULL)
        return FOUNDATION_UI_INVALID;
    active = ui->first_frame;
    ui->first_frame = false;
    nk_input_begin(ui->context);
    while (foundation_ui_take_event(ui, &event)) {
        foundation_ui_process_event(ui, &event, &active, &resized);
    }
    while (SDL_PollEvent(&event)) {
        SDL_Window* window = SDL_GetWindowFromEvent(&event);
        if (event.type != SDL_EVENT_QUIT && window != ui->window) {
            if (window != NULL)
                foundation_ui_route_event(ui, &event);
            continue;
        }
        foundation_ui_process_event(ui, &event, &active, &resized);
    }
    nk_input_end(ui->context);
    ui->tooltip_length = 0;
    if (ui->event_overflow) {
        ui->event_overflow = false;
        return FOUNDATION_UI_FAILED;
    }
    if (resized && !ui->shape_disabled && !foundation_ui_set_window_shape(ui))
        ui->shape_disabled = true;
    if (ui->closing)
        return FOUNDATION_UI_FRAME_CLOSING;
    return active ? FOUNDATION_UI_FRAME_ACTIVE : FOUNDATION_UI_FRAME_IDLE;
}

int32_t foundation_ui_end_frame(uint64_t handle) {
    foundation_ui* ui = foundation_ui_from(handle);
    if (ui == NULL)
        return FOUNDATION_UI_INVALID;
    if (!SDL_SetRenderDrawColor(ui->renderer, ui->background.r, ui->background.g, ui->background.b,
                                ui->background.a) ||
        !SDL_RenderClear(ui->renderer)) {
        return FOUNDATION_UI_FAILED;
    }
    nk_sdl_render(ui->context, NK_ANTI_ALIASING_ON);
    if (!SDL_RenderPresent(ui->renderer))
        return FOUNDATION_UI_FAILED;
    nk_sdl_update_TextInput(ui->context);
    if (ui->terminal_focus)
        (void)SDL_StartTextInput(ui->window);
    return FOUNDATION_UI_OK;
}

int32_t foundation_ui_set_theme(uint64_t handle, uint64_t theme) {
    foundation_ui* ui = foundation_ui_from(handle);
    if (ui == NULL || theme > FOUNDATION_UI_THEME_LIGHT)
        return FOUNDATION_UI_INVALID;
    foundation_ui_apply_theme(ui, theme == FOUNDATION_UI_THEME_LIGHT);
    return FOUNDATION_UI_OK;
}

int32_t foundation_ui_set_accent(uint64_t handle, uint64_t red, uint64_t green, uint64_t blue,
                                 uint64_t alpha) {
    foundation_ui* ui = foundation_ui_from(handle);
    if (ui == NULL || red > UINT8_MAX || green > UINT8_MAX || blue > UINT8_MAX ||
        alpha > UINT8_MAX) {
        return FOUNDATION_UI_INVALID;
    }
    ui->accent = nk_rgba((nk_byte)red, (nk_byte)green, (nk_byte)blue, (nk_byte)alpha);
    ui->custom_accent = true;
    foundation_ui_apply_theme(ui, ui->light_theme);
    return FOUNDATION_UI_OK;
}

int32_t foundation_ui_size(uint64_t handle, uint64_t* width, uint64_t* height) {
    foundation_ui* ui = foundation_ui_from(handle);
    int window_width;
    int window_height;
    if (ui == NULL || width == NULL || height == NULL)
        return FOUNDATION_UI_INVALID;
    if (!SDL_GetWindowSize(ui->window, &window_width, &window_height) || window_width < 0 ||
        window_height < 0) {
        return FOUNDATION_UI_FAILED;
    }
    *width = (uint64_t)window_width;
    *height = (uint64_t)window_height;
    return FOUNDATION_UI_OK;
}

bool foundation_ui_begin_root(uint64_t handle) {
    foundation_ui* ui = foundation_ui_from(handle);
    uint64_t index;
    int width;
    int height;
    if (ui == NULL || !SDL_GetWindowSize(ui->window, &width, &height))
        return false;
    for (index = 0; index < FOUNDATION_UI_SURFACE_CAPACITY; index++) {
        ui->surfaces[index].bounds_valid = false;
    }
    ui->surface_draw_sequence = 0;
    ui->terminal_bounds_valid = false;
    return nk_begin(ui->context, "foundation-ui", nk_rect(0.0f, 0.0f, (float)width, (float)height),
                    NK_WINDOW_BACKGROUND | NK_WINDOW_NO_SCROLLBAR);
}

void foundation_ui_end_root(uint64_t handle) {
    foundation_ui* ui = foundation_ui_from(handle);
    if (ui != NULL)
        nk_end(ui->context);
}

static bool foundation_ui_window_button(foundation_ui* ui, struct nk_command_buffer* canvas,
                                        struct nk_rect bounds, uint64_t icon) {
    nk_flags state = 0;
    const bool pressed = nk_button_behavior(&state, bounds, &ui->context->input, NK_BUTTON_DEFAULT);
    const struct nk_color foreground = (state & NK_WIDGET_STATE_HOVER) != 0 ? ui->text : ui->muted;
    const float x = bounds.x + bounds.w * 0.5f;
    const float y = bounds.y + bounds.h * 0.5f;
    if (icon == 0) {
        nk_stroke_line(canvas, x - 4.0f, y + 3.0f, x + 4.0f, y + 3.0f, 1.25f, foreground);
    } else if (icon == 1) {
        nk_stroke_line(canvas, x - 5.0f, y - 1.0f, x - 5.0f, y - 5.0f, 1.25f, foreground);
        nk_stroke_line(canvas, x - 5.0f, y - 5.0f, x - 1.0f, y - 5.0f, 1.25f, foreground);
        nk_stroke_line(canvas, x + 5.0f, y + 1.0f, x + 5.0f, y + 5.0f, 1.25f, foreground);
        nk_stroke_line(canvas, x + 1.0f, y + 5.0f, x + 5.0f, y + 5.0f, 1.25f, foreground);
    } else {
        nk_stroke_line(canvas, x - 4.0f, y - 4.0f, x + 4.0f, y + 4.0f, 1.25f, foreground);
        nk_stroke_line(canvas, x + 4.0f, y - 4.0f, x - 4.0f, y + 4.0f, 1.25f, foreground);
    }
    return pressed;
}

void foundation_ui_titlebar(uint64_t handle, const fdn_string* title, const fdn_string* subtitle) {
    foundation_ui* ui = foundation_ui_from(handle);
    int width;
    int height;
    struct nk_command_buffer* canvas;
    struct nk_rect clip;
    const struct nk_user_font* font;
    float controls_left;
    float title_available;
    float title_width;
    if (ui == NULL || !foundation_ui_string_valid(title) || !foundation_ui_string_valid(subtitle) ||
        title->length > INT32_MAX || subtitle->length > INT32_MAX ||
        !SDL_GetWindowSize(ui->window, &width, &height)) {
        return;
    }
    (void)height;
    canvas = nk_window_get_canvas(ui->context);
    clip = canvas->clip;
    nk_push_scissor(canvas, nk_rect(0.0f, 0.0f, (float)width, (float)height));
    font = &ui->regular_font->handle;
    controls_left = width >= 108 ? (float)width - 108.0f : (float)width;
    title_available = controls_left - 104.0f;
    title_width = font->width(font->userdata, font->height, foundation_ui_string_data(title),
                              (int)title->length);
    if (title_available > 0.0f) {
        const float visible_title_width =
            title_width < title_available ? title_width : title_available;
        nk_draw_text(canvas, nk_rect(104.0f, 10.0f, visible_title_width, 22.0f),
                     foundation_ui_string_data(title), (int)title->length, font,
                     nk_rgba(0, 0, 0, 0), ui->text);
    }
    if (subtitle->length != 0 && title_available > title_width + 16.0f) {
        nk_draw_text(canvas,
                     nk_rect(104.0f + title_width + 16.0f, 10.0f,
                             title_available - title_width - 16.0f, 22.0f),
                     foundation_ui_string_data(subtitle), (int)subtitle->length, font,
                     nk_rgba(0, 0, 0, 0), ui->muted);
    }
    if (width >= 108) {
        if (foundation_ui_window_button(ui, canvas,
                                        nk_rect((float)width - 108.0f, 0.0f, 36.0f, 36.0f), 0)) {
            (void)SDL_MinimizeWindow(ui->window);
        }
        if (foundation_ui_window_button(ui, canvas,
                                        nk_rect((float)width - 72.0f, 0.0f, 36.0f, 36.0f), 1)) {
            if ((SDL_GetWindowFlags(ui->window) & SDL_WINDOW_MAXIMIZED) != 0) {
                (void)SDL_RestoreWindow(ui->window);
            } else {
                (void)SDL_MaximizeWindow(ui->window);
            }
        }
        if (foundation_ui_window_button(ui, canvas,
                                        nk_rect((float)width - 36.0f, 0.0f, 36.0f, 36.0f), 2)) {
            ui->closing = true;
        }
    }
    if ((SDL_GetWindowFlags(ui->window) & SDL_WINDOW_MAXIMIZED) == 0) {
        nk_stroke_rect(canvas, nk_rect(0.5f, 0.5f, (float)width - 1.0f, (float)height - 1.0f),
                       10.0f, 1.0f, ui->context->style.window.border_color);
    }
    if (ui->tooltip_length != 0) {
        const struct nk_user_font* tooltip_font = ui->context->style.font;
        const float measured = tooltip_font->width(tooltip_font->userdata, tooltip_font->height,
                                                   ui->tooltip, (int)ui->tooltip_length);
        struct nk_rect bounds = nk_rect(
            ui->tooltip_anchor.x + ui->tooltip_anchor.w + 8.0f,
            ui->tooltip_anchor.y + (ui->tooltip_anchor.h - 26.0f) * 0.5f, measured + 16.0f, 26.0f);
        if (bounds.x + bounds.w > (float)width - 8.0f) {
            bounds.x = ui->tooltip_anchor.x - bounds.w - 8.0f;
        }
        nk_fill_rect(canvas, bounds, 5.0f, ui->raised);
        nk_draw_text(canvas, nk_rect(bounds.x + 8.0f, bounds.y + 4.0f, measured, 18.0f),
                     ui->tooltip, (int)ui->tooltip_length, tooltip_font, nk_rgba(0, 0, 0, 0),
                     ui->text);
    }
    nk_push_scissor(canvas, clip);
}
