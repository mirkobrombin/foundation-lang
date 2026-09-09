#ifndef FOUNDATION_UI_INTERNAL_H
#define FOUNDATION_UI_INTERNAL_H

#include <foundation/runtime.h>

#include <SDL3/SDL.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../include/foundation/ui.h"
#include "terminal_emulator.h"

#define NK_INCLUDE_COMMAND_USERDATA
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_BUTTON_TRIGGER_ON_RELEASE

#define NK_INT8 Sint8
#define NK_UINT8 Uint8
#define NK_INT16 Sint16
#define NK_UINT16 Uint16
#define NK_INT32 Sint32
#define NK_UINT32 Uint32
#define NK_SIZE_TYPE uintptr_t
#define NK_POINTER_TYPE uintptr_t
#define NK_BOOL bool
#define NK_ASSERT(condition) SDL_assert(condition)
#define NK_STATIC_ASSERT(exp) SDL_COMPILE_TIME_ASSERT(, exp)
#define NK_MEMSET(dst, c, len) SDL_memset(dst, c, len)
#define NK_MEMCPY(dst, src, len) SDL_memcpy(dst, src, len)
#define NK_VSNPRINTF(s, n, f, a) SDL_vsnprintf(s, n, f, a)
#define NK_STRTOD(str, endptr) SDL_strtod(str, endptr)
#define NK_INV_SQRT(f) (1.0f / SDL_sqrtf(f))
#define NK_SIN(f) SDL_sinf(f)
#define NK_COS(f) SDL_cosf(f)

#if defined(FOUNDATION_UI_PROVIDER_IMPLEMENTATION)
#define NK_IMPLEMENTATION
#define NK_SDL3_RENDERER_IMPLEMENTATION
#endif

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 5287)
#endif
#include "vendor/nuklear.h"
#include "vendor/nuklear_sdl3_renderer.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

typedef struct foundation_ui_input {
    uint64_t kind;
    uint64_t x;
    uint64_t y;
    uint64_t button;
    uint64_t key;
    int64_t delta;
    uint64_t text_length;
    char text[64];
    bool down;
    bool control;
    bool shift;
    bool alt;
    bool super;
} foundation_ui_input;

#define FOUNDATION_UI_INPUT_CAPACITY 512
#define FOUNDATION_UI_SURFACE_CAPACITY 16
#define FOUNDATION_UI_EVENT_CAPACITY 256
#define FOUNDATION_UI_GROUP_CAPACITY 32
#define FOUNDATION_UI_TITLEBAR_REGION_CAPACITY 64
#define FOUNDATION_UI_TERMINAL_INPUT_CAPACITY 16384
#define FOUNDATION_UI_TERMINAL_MAX_COLUMNS 240
#define FOUNDATION_UI_TERMINAL_MAX_ROWS 120
#define FOUNDATION_UI_TRAY_ACTION_CAPACITY 64
#define FOUNDATION_UI_TRAY_EVENT_CAPACITY 64

typedef struct foundation_ui_tray_action {
    struct foundation_ui* owner;
    SDL_TrayEntry* entry;
    uint64_t id;
} foundation_ui_tray_action;

typedef struct foundation_ui_edit_state {
    char* name;
    size_t name_length;
    char* buffer;
    char* mask;
    size_t capacity;
    bool secret;
} foundation_ui_edit_state;

typedef struct foundation_ui_texture {
    SDL_Texture* texture;
    uint8_t* pixels;
    uint64_t capacity;
    uint64_t width;
    uint64_t height;
} foundation_ui_texture;

typedef struct foundation_ui_group_state {
    struct nk_vec2 spacing;
    bool compact;
} foundation_ui_group_state;

typedef struct foundation_ui_surface {
    foundation_ui_texture image;
    struct nk_rect bounds;
    struct nk_rect layout_bounds;
    foundation_ui_input input_queue[FOUNDATION_UI_INPUT_CAPACITY];
    uint64_t id;
    uint64_t input_head;
    uint64_t input_count;
    uint64_t draw_order;
    uint64_t input_mode;
    bool keys[256];
    bool buttons[3];
    bool bounds_valid;
    bool input_overflow;
    bool used;
} foundation_ui_surface;

typedef struct foundation_ui {
    uint64_t id;
    SDL_Window* window;
    SDL_Renderer* renderer;
    struct nk_context* context;
    struct nk_font* regular_font;
    struct nk_font* heading_font;
    struct nk_font* terminal_font;
    foundation_ui_texture application_image;
    struct nk_rect terminal_bounds;
    foundation_ui_surface surfaces[FOUNDATION_UI_SURFACE_CAPACITY];
    foundation_ui_surface* captured_surface;
    foundation_ui_surface* focused_surface;
    foundation_ui_surface* hovered_surface;
    SDL_Event event_queue[FOUNDATION_UI_EVENT_CAPACITY];
    foundation_terminal* terminal;
    uint8_t* terminal_transfer;
    uint64_t terminal_transfer_capacity;
    char terminal_input[FOUNDATION_UI_TERMINAL_INPUT_CAPACITY];
    uint64_t terminal_input_length;
    size_t terminal_scroll;
    foundation_ui_edit_state* edit_states;
    SDL_Tray* tray;
    SDL_TrayMenu* tray_menu;
    SDL_Surface* tray_icon;
    foundation_ui_tray_action tray_actions[FOUNDATION_UI_TRAY_ACTION_CAPACITY];
    uint64_t tray_events[FOUNDATION_UI_TRAY_EVENT_CAPACITY];
    foundation_ui_group_state group_states[FOUNDATION_UI_GROUP_CAPACITY];
    struct nk_rect titlebar_regions[FOUNDATION_UI_TITLEBAR_REGION_CAPACITY];
    size_t edit_count;
    size_t edit_capacity;
    size_t titlebar_region_count;
    uint64_t group_depth;
    uint64_t surface_draw_sequence;
    uint64_t event_head;
    uint64_t event_count;
    uint64_t tray_action_count;
    uint64_t tray_event_head;
    uint64_t tray_event_count;
    uint64_t next_tray_action_id;
    int shape_width;
    int shape_height;
    bool terminal_bounds_valid;
    bool terminal_focus;
    bool terminal_auto_focus;
    bool shape_disabled;
    bool shape_maximized;
    char tooltip[128];
    uint64_t tooltip_length;
    struct nk_rect tooltip_anchor;
    struct nk_color background;
    struct nk_color panel;
    struct nk_color raised;
    struct nk_color text;
    struct nk_color muted;
    struct nk_color accent;
    bool custom_accent;
    bool light_theme;
    bool closing;
    bool first_frame;
    bool event_overflow;
    bool tray_event_overflow;
    bool titlebar_region_overflow;
} foundation_ui;

foundation_ui* foundation_ui_from(uint64_t handle);
bool foundation_ui_string_valid(const fdn_string* value);
const char* foundation_ui_string_data(const fdn_string* value);
char* foundation_ui_text(const fdn_string* value);
uint64_t foundation_ui_new_surface_id(void);
foundation_ui_surface* foundation_ui_surface_at(foundation_ui* ui, float x, float y);
void foundation_ui_leave_surface(foundation_ui* ui, foundation_ui_surface* surface);
void foundation_ui_release_surface_input(foundation_ui* ui, foundation_ui_surface* surface);
bool foundation_ui_handle_surface_event(foundation_ui* ui, const SDL_Event* event);
bool foundation_ui_handle_terminal_event(foundation_ui* ui, const SDL_Event* event);
uint8_t* foundation_ui_reserve_image(foundation_ui* ui, foundation_ui_texture* image,
                                     uint64_t width, uint64_t height, uint64_t* capacity);
int32_t foundation_ui_commit_image(foundation_ui_texture* image);
void foundation_ui_draw_icon(struct nk_command_buffer* canvas, struct nk_rect bounds, uint64_t icon,
                             struct nk_color color);
bool foundation_ui_button_input(nk_flags* state, struct nk_rect bounds,
                                const struct nk_input* input);
void foundation_ui_register_titlebar_region(foundation_ui* ui, struct nk_rect bounds);
SDL_HitTestResult foundation_ui_window_hit_test(const foundation_ui* ui, int width, int height,
                                                bool maximized, const SDL_Point* area);

#endif
