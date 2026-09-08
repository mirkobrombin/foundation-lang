#include "sdl_ui_internal.h"

int main(void) {
    static foundation_ui ui;
    SDL_Point point = {200, 20};

    if (foundation_ui_window_hit_test(&ui, 1200, 800, false, &point) !=
        SDL_HITTEST_DRAGGABLE) {
        return 1;
    }
    foundation_ui_register_titlebar_region(&ui, nk_rect(180.0f, 8.0f, 40.0f, 32.0f));
    if (foundation_ui_window_hit_test(&ui, 1200, 800, false, &point) != SDL_HITTEST_NORMAL)
        return 2;
    if (foundation_ui_window_hit_test(&ui, 1200, 800, true, &point) != SDL_HITTEST_NORMAL)
        return 3;
    point.y = 2;
    if (foundation_ui_window_hit_test(&ui, 1200, 800, false, &point) != SDL_HITTEST_RESIZE_TOP)
        return 4;
    point.x = 1120;
    point.y = 20;
    if (foundation_ui_window_hit_test(&ui, 1200, 800, false, &point) != SDL_HITTEST_NORMAL)
        return 5;
    point.x = 220;
    point.y = 50;
    if (foundation_ui_window_hit_test(&ui, 1200, 800, false, &point) != SDL_HITTEST_NORMAL)
        return 6;
    return 0;
}
