#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#endif

#include "bytes_internal.h"
#include "foundation/runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    FDN_DESKTOP_OK = 0,
    FDN_DESKTOP_UNAVAILABLE = 1,
    FDN_DESKTOP_PERMISSION = 2,
    FDN_DESKTOP_INVALID_ARGUMENT = 3,
    FDN_DESKTOP_IO = 4,
    FDN_DESKTOP_CLOSED = 5,
};

static int fdn_desktop_frame_size(uint64_t width, uint64_t height, size_t* result) {
    if (width == 0 || height == 0 || width > SIZE_MAX / 4 ||
        height > SIZE_MAX / ((size_t)width * 4)) {
        return 0;
    }
    *result = (size_t)width * (size_t)height * 4;
    return 1;
}

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct fdn_desktop_capture {
    HDC screen;
    HDC memory;
    HBITMAP bitmap;
    HGDIOBJ previous;
    uint8_t* pixels;
    int left;
    int top;
    int width;
    int height;
} fdn_desktop_capture;

typedef struct fdn_desktop_input {
    uint8_t active;
} fdn_desktop_input;

static volatile LONG64 fdn_desktop_capture_count;
static volatile LONG64 fdn_desktop_input_count;

static void fdn_desktop_capture_destroy(fdn_desktop_capture* capture) {
    if (capture == NULL) {
        return;
    }
    if (capture->memory != NULL && capture->previous != NULL) {
        (void)SelectObject(capture->memory, capture->previous);
    }
    if (capture->bitmap != NULL) {
        (void)DeleteObject(capture->bitmap);
    }
    if (capture->memory != NULL) {
        (void)DeleteDC(capture->memory);
    }
    if (capture->screen != NULL) {
        (void)ReleaseDC(NULL, capture->screen);
    }
    fdn_dealloc(capture);
    if (InterlockedDecrement64(&fdn_desktop_capture_count) < 0) {
        fdn_panic_cstr("desktop capture count underflow");
    }
}

int32_t foundation_runtime_desktop_capture_open(uint64_t* handle, uint64_t* width,
                                                uint64_t* height) {
    fdn_desktop_capture* capture;
    BITMAPINFO info;
    if (handle == NULL || width == NULL || height == NULL) {
        fdn_panic_cstr("desktop capture output is null");
    }
    *handle = 0;
    *width = 0;
    *height = 0;
    capture = fdn_alloc(sizeof(*capture));
    memset(capture, 0, sizeof(*capture));
    capture->left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    capture->top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    capture->width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    capture->height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (capture->width <= 0 || capture->height <= 0) {
        fdn_dealloc(capture);
        return FDN_DESKTOP_UNAVAILABLE;
    }
    capture->screen = GetDC(NULL);
    capture->memory = capture->screen == NULL ? NULL : CreateCompatibleDC(capture->screen);
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = capture->width;
    info.bmiHeader.biHeight = -capture->height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    if (capture->memory != NULL) {
        capture->bitmap = CreateDIBSection(capture->screen, &info, DIB_RGB_COLORS,
                                           (void**)&capture->pixels, NULL, 0);
    }
    if (capture->bitmap == NULL || capture->pixels == NULL) {
        if (capture->bitmap != NULL) {
            (void)DeleteObject(capture->bitmap);
        }
        if (capture->memory != NULL) {
            (void)DeleteDC(capture->memory);
        }
        if (capture->screen != NULL) {
            (void)ReleaseDC(NULL, capture->screen);
        }
        fdn_dealloc(capture);
        return FDN_DESKTOP_UNAVAILABLE;
    }
    (void)InterlockedIncrement64(&fdn_desktop_capture_count);
    capture->previous = SelectObject(capture->memory, capture->bitmap);
    if (capture->previous == NULL || capture->previous == HGDI_ERROR) {
        capture->previous = NULL;
        fdn_desktop_capture_destroy(capture);
        return FDN_DESKTOP_IO;
    }
    *handle = (uint64_t)(uintptr_t)capture;
    *width = (uint64_t)capture->width;
    *height = (uint64_t)capture->height;
    return FDN_DESKTOP_OK;
}

int32_t foundation_runtime_desktop_capture_frame(uint64_t handle, uint64_t* bytes_handle) {
    fdn_desktop_capture* capture = (fdn_desktop_capture*)(uintptr_t)handle;
    uint8_t* result;
    size_t length;
    size_t index;
    if (bytes_handle == NULL) {
        fdn_panic_cstr("desktop frame output is null");
    }
    *bytes_handle = 0;
    if (capture == NULL) {
        return FDN_DESKTOP_CLOSED;
    }
    if (!fdn_desktop_frame_size((uint64_t)capture->width, (uint64_t)capture->height, &length)) {
        return FDN_DESKTOP_INVALID_ARGUMENT;
    }
    if (!BitBlt(capture->memory, 0, 0, capture->width, capture->height, capture->screen,
                capture->left, capture->top, SRCCOPY | CAPTUREBLT)) {
        return FDN_DESKTOP_IO;
    }
    result = fdn_alloc(length);
    for (index = 0; index < length; index += 4) {
        result[index] = capture->pixels[index + 2];
        result[index + 1] = capture->pixels[index + 1];
        result[index + 2] = capture->pixels[index];
        result[index + 3] = 255;
    }
    if (fdn_bytes_adopt(result, length, length, bytes_handle) != 0) {
        fdn_dealloc(result);
        return FDN_DESKTOP_IO;
    }
    return FDN_DESKTOP_OK;
}

void foundation_runtime_desktop_capture_close(uint64_t* handle) {
    fdn_desktop_capture* capture;
    if (handle == NULL || *handle == 0) {
        return;
    }
    capture = (fdn_desktop_capture*)(uintptr_t)*handle;
    *handle = 0;
    fdn_desktop_capture_destroy(capture);
}

uint64_t foundation_runtime_desktop_capture_live_handles(void) {
    return (uint64_t)InterlockedCompareExchange64(&fdn_desktop_capture_count, 0, 0);
}

int32_t foundation_runtime_desktop_input_open(uint64_t* handle) {
    fdn_desktop_input* input;
    if (handle == NULL) {
        fdn_panic_cstr("desktop input output is null");
    }
    input = fdn_alloc(sizeof(*input));
    input->active = 1;
    (void)InterlockedIncrement64(&fdn_desktop_input_count);
    *handle = (uint64_t)(uintptr_t)input;
    return FDN_DESKTOP_OK;
}

static WORD fdn_desktop_windows_key(uint64_t key) {
    static const WORD digits[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};
    static const WORD top[] = {'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P'};
    static const WORD home[] = {'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L'};
    static const WORD bottom[] = {'Z', 'X', 'C', 'V', 'B', 'N', 'M'};
    if (key >= 2 && key <= 11) {
        return digits[key - 2];
    }
    if (key >= 16 && key <= 25) {
        return top[key - 16];
    }
    if (key >= 30 && key <= 38) {
        return home[key - 30];
    }
    if (key >= 44 && key <= 50) {
        return bottom[key - 44];
    }
    if (key >= 59 && key <= 68) {
        return (WORD)(VK_F1 + key - 59);
    }
    switch (key) {
    case 1:
        return VK_ESCAPE;
    case 14:
        return VK_BACK;
    case 15:
        return VK_TAB;
    case 28:
        return VK_RETURN;
    case 29:
        return VK_LCONTROL;
    case 42:
        return VK_LSHIFT;
    case 54:
        return VK_RSHIFT;
    case 56:
        return VK_LMENU;
    case 57:
        return VK_SPACE;
    case 87:
        return VK_F11;
    case 88:
        return VK_F12;
    case 97:
        return VK_RCONTROL;
    case 100:
        return VK_RMENU;
    case 102:
        return VK_HOME;
    case 103:
        return VK_UP;
    case 104:
        return VK_PRIOR;
    case 105:
        return VK_LEFT;
    case 106:
        return VK_RIGHT;
    case 107:
        return VK_END;
    case 108:
        return VK_DOWN;
    case 109:
        return VK_NEXT;
    case 110:
        return VK_INSERT;
    case 111:
        return VK_DELETE;
    case 125:
        return VK_LWIN;
    case 126:
        return VK_RWIN;
    default:
        return 0;
    }
}

static int fdn_desktop_windows_send(INPUT* input) {
    if (SendInput(1, input, sizeof(*input)) == 1) {
        return FDN_DESKTOP_OK;
    }
    return GetLastError() == ERROR_ACCESS_DENIED ? FDN_DESKTOP_PERMISSION : FDN_DESKTOP_IO;
}

int32_t foundation_runtime_desktop_input_move(uint64_t handle, uint64_t x, uint64_t y) {
    INPUT input;
    if (handle == 0) {
        return FDN_DESKTOP_CLOSED;
    }
    if (x > 32767 || y > 32767) {
        return FDN_DESKTOP_INVALID_ARGUMENT;
    }
    memset(&input, 0, sizeof(input));
    input.type = INPUT_MOUSE;
    input.mi.dx = (LONG)((x * 65535 + 16383) / 32767);
    input.mi.dy = (LONG)((y * 65535 + 16383) / 32767);
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    return fdn_desktop_windows_send(&input);
}

int32_t foundation_runtime_desktop_input_button(uint64_t handle, uint64_t button, bool down) {
    INPUT input;
    if (handle == 0) {
        return FDN_DESKTOP_CLOSED;
    }
    memset(&input, 0, sizeof(input));
    input.type = INPUT_MOUSE;
    if (button == 272) {
        input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    } else if (button == 273) {
        input.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    } else if (button == 274) {
        input.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
    } else {
        return FDN_DESKTOP_INVALID_ARGUMENT;
    }
    return fdn_desktop_windows_send(&input);
}

int32_t foundation_runtime_desktop_input_key(uint64_t handle, uint64_t key, bool down) {
    INPUT input;
    WORD virtual_key;
    if (handle == 0) {
        return FDN_DESKTOP_CLOSED;
    }
    virtual_key = fdn_desktop_windows_key(key);
    if (virtual_key == 0) {
        return FDN_DESKTOP_INVALID_ARGUMENT;
    }
    memset(&input, 0, sizeof(input));
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtual_key;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    return fdn_desktop_windows_send(&input);
}

int32_t foundation_runtime_desktop_input_scroll(uint64_t handle, int64_t delta) {
    INPUT input;
    if (handle == 0) {
        return FDN_DESKTOP_CLOSED;
    }
    if (delta < INT32_MIN / WHEEL_DELTA || delta > INT32_MAX / WHEEL_DELTA) {
        return FDN_DESKTOP_INVALID_ARGUMENT;
    }
    memset(&input, 0, sizeof(input));
    input.type = INPUT_MOUSE;
    input.mi.mouseData = (DWORD)(int32_t)(delta * WHEEL_DELTA);
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    return fdn_desktop_windows_send(&input);
}

void foundation_runtime_desktop_input_close(uint64_t* handle) {
    fdn_desktop_input* input;
    if (handle == NULL || *handle == 0) {
        return;
    }
    input = (fdn_desktop_input*)(uintptr_t)*handle;
    *handle = 0;
    fdn_dealloc(input);
    if (InterlockedDecrement64(&fdn_desktop_input_count) < 0) {
        fdn_panic_cstr("desktop input count underflow");
    }
}

uint64_t foundation_runtime_desktop_input_live_handles(void) {
    return (uint64_t)InterlockedCompareExchange64(&fdn_desktop_input_count, 0, 0);
}

#elif defined(__APPLE__)

#include <ApplicationServices/ApplicationServices.h>
#include <dlfcn.h>
#include <stdatomic.h>

typedef CGImageRef (*fdn_cg_display_create_image_fn)(CGDirectDisplayID);

typedef struct fdn_desktop_capture {
    CGDirectDisplayID display;
    size_t width;
    size_t height;
    fdn_cg_display_create_image_fn create_image;
} fdn_desktop_capture;

typedef struct fdn_desktop_input {
    uint8_t active;
} fdn_desktop_input;

static atomic_uint_fast64_t fdn_desktop_capture_count;
static atomic_uint_fast64_t fdn_desktop_input_count;

int32_t foundation_runtime_desktop_capture_open(uint64_t* handle, uint64_t* width,
                                                uint64_t* height) {
    fdn_desktop_capture* capture;
    CGDirectDisplayID display;
    void* symbol;
    if (handle == NULL || width == NULL || height == NULL) {
        fdn_panic_cstr("desktop capture output is null");
    }
    *handle = 0;
    *width = 0;
    *height = 0;
    display = CGMainDisplayID();
    capture = fdn_alloc(sizeof(*capture));
    symbol = dlsym(RTLD_DEFAULT, "CGDisplayCreateImage");
    if (symbol == NULL || sizeof(capture->create_image) != sizeof(symbol)) {
        fdn_dealloc(capture);
        return FDN_DESKTOP_UNAVAILABLE;
    }
    memcpy(&capture->create_image, &symbol, sizeof(symbol));
    capture->display = display;
    capture->width = CGDisplayPixelsWide(display);
    capture->height = CGDisplayPixelsHigh(display);
    if (capture->width == 0 || capture->height == 0) {
        fdn_dealloc(capture);
        return FDN_DESKTOP_UNAVAILABLE;
    }
    if (atomic_fetch_add_explicit(&fdn_desktop_capture_count, 1, memory_order_relaxed) ==
        UINT64_MAX) {
        fdn_panic_cstr("desktop capture count overflow");
    }
    *handle = (uint64_t)(uintptr_t)capture;
    *width = (uint64_t)capture->width;
    *height = (uint64_t)capture->height;
    return FDN_DESKTOP_OK;
}

int32_t foundation_runtime_desktop_capture_frame(uint64_t handle, uint64_t* bytes_handle) {
    fdn_desktop_capture* capture = (fdn_desktop_capture*)(uintptr_t)handle;
    CGImageRef image;
    CGColorSpaceRef color_space;
    CGContextRef context;
    uint8_t* pixels;
    size_t length;
    if (bytes_handle == NULL) {
        fdn_panic_cstr("desktop frame output is null");
    }
    *bytes_handle = 0;
    if (capture == NULL) {
        return FDN_DESKTOP_CLOSED;
    }
    if (!fdn_desktop_frame_size(capture->width, capture->height, &length)) {
        return FDN_DESKTOP_INVALID_ARGUMENT;
    }
    image = capture->create_image(capture->display);
    if (image == NULL) {
        return FDN_DESKTOP_PERMISSION;
    }
    pixels = fdn_alloc(length);
    color_space = CGColorSpaceCreateDeviceRGB();
    context = color_space == NULL
                  ? NULL
                  : CGBitmapContextCreate(pixels, capture->width, capture->height, 8,
                                          capture->width * 4, color_space,
                                          kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    if (context != NULL) {
        CGContextDrawImage(
            context, CGRectMake(0, 0, (CGFloat)capture->width, (CGFloat)capture->height), image);
    }
    CGImageRelease(image);
    if (color_space != NULL) {
        CGColorSpaceRelease(color_space);
    }
    if (context == NULL) {
        fdn_dealloc(pixels);
        return FDN_DESKTOP_IO;
    }
    CGContextRelease(context);
    if (fdn_bytes_adopt(pixels, length, length, bytes_handle) != 0) {
        fdn_dealloc(pixels);
        return FDN_DESKTOP_IO;
    }
    return FDN_DESKTOP_OK;
}

void foundation_runtime_desktop_capture_close(uint64_t* handle) {
    fdn_desktop_capture* capture;
    if (handle == NULL || *handle == 0) {
        return;
    }
    capture = (fdn_desktop_capture*)(uintptr_t)*handle;
    *handle = 0;
    fdn_dealloc(capture);
    if (atomic_fetch_sub_explicit(&fdn_desktop_capture_count, 1, memory_order_relaxed) == 0) {
        fdn_panic_cstr("desktop capture count underflow");
    }
}

uint64_t foundation_runtime_desktop_capture_live_handles(void) {
    return atomic_load_explicit(&fdn_desktop_capture_count, memory_order_relaxed);
}

int32_t foundation_runtime_desktop_input_open(uint64_t* handle) {
    fdn_desktop_input* input;
    if (handle == NULL) {
        fdn_panic_cstr("desktop input output is null");
    }
    *handle = 0;
    if (!AXIsProcessTrusted()) {
        return FDN_DESKTOP_PERMISSION;
    }
    input = fdn_alloc(sizeof(*input));
    input->active = 1;
    if (atomic_fetch_add_explicit(&fdn_desktop_input_count, 1, memory_order_relaxed) ==
        UINT64_MAX) {
        fdn_panic_cstr("desktop input count overflow");
    }
    *handle = (uint64_t)(uintptr_t)input;
    return FDN_DESKTOP_OK;
}

static CGKeyCode fdn_desktop_macos_key(uint64_t key) {
    static const CGKeyCode digits[] = {18, 19, 20, 21, 23, 22, 26, 28, 25, 29};
    static const CGKeyCode top[] = {12, 13, 14, 15, 17, 16, 32, 34, 31, 35};
    static const CGKeyCode home[] = {0, 1, 2, 3, 5, 4, 38, 40, 37};
    static const CGKeyCode bottom[] = {6, 7, 8, 9, 11, 45, 46};
    if (key >= 2 && key <= 11) {
        return digits[key - 2];
    }
    if (key >= 16 && key <= 25) {
        return top[key - 16];
    }
    if (key >= 30 && key <= 38) {
        return home[key - 30];
    }
    if (key >= 44 && key <= 50) {
        return bottom[key - 44];
    }
    switch (key) {
    case 1:
        return 53;
    case 14:
        return 51;
    case 15:
        return 48;
    case 28:
        return 36;
    case 29:
        return 59;
    case 42:
        return 56;
    case 54:
        return 60;
    case 56:
        return 58;
    case 57:
        return 49;
    case 59:
        return 122;
    case 60:
        return 120;
    case 61:
        return 99;
    case 62:
        return 118;
    case 63:
        return 96;
    case 64:
        return 97;
    case 65:
        return 98;
    case 66:
        return 100;
    case 67:
        return 101;
    case 68:
        return 109;
    case 87:
        return 103;
    case 88:
        return 111;
    case 97:
        return 62;
    case 100:
        return 61;
    case 102:
        return 115;
    case 103:
        return 126;
    case 104:
        return 116;
    case 105:
        return 123;
    case 106:
        return 124;
    case 107:
        return 119;
    case 108:
        return 125;
    case 109:
        return 121;
    case 110:
        return 114;
    case 111:
        return 117;
    case 125:
        return 55;
    case 126:
        return 54;
    default:
        return UINT16_MAX;
    }
}

static int fdn_desktop_macos_post(CGEventRef event) {
    if (event == NULL) {
        return FDN_DESKTOP_IO;
    }
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
    return FDN_DESKTOP_OK;
}

int32_t foundation_runtime_desktop_input_move(uint64_t handle, uint64_t x, uint64_t y) {
    CGRect bounds;
    CGPoint point;
    if (handle == 0) {
        return FDN_DESKTOP_CLOSED;
    }
    if (x > 32767 || y > 32767) {
        return FDN_DESKTOP_INVALID_ARGUMENT;
    }
    bounds = CGDisplayBounds(CGMainDisplayID());
    point.x = bounds.origin.x + ((double)x * bounds.size.width / 32767.0);
    point.y = bounds.origin.y + ((double)y * bounds.size.height / 32767.0);
    return fdn_desktop_macos_post(
        CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, point, kCGMouseButtonLeft));
}

int32_t foundation_runtime_desktop_input_button(uint64_t handle, uint64_t button, bool down) {
    CGEventRef current;
    CGEventType type;
    CGMouseButton mapped;
    CGPoint point;
    if (handle == 0) {
        return FDN_DESKTOP_CLOSED;
    }
    if (button == 272) {
        mapped = kCGMouseButtonLeft;
        type = down ? kCGEventLeftMouseDown : kCGEventLeftMouseUp;
    } else if (button == 273) {
        mapped = kCGMouseButtonRight;
        type = down ? kCGEventRightMouseDown : kCGEventRightMouseUp;
    } else if (button == 274) {
        mapped = kCGMouseButtonCenter;
        type = down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp;
    } else {
        return FDN_DESKTOP_INVALID_ARGUMENT;
    }
    current = CGEventCreate(NULL);
    if (current == NULL) {
        return FDN_DESKTOP_IO;
    }
    point = CGEventGetLocation(current);
    CFRelease(current);
    return fdn_desktop_macos_post(CGEventCreateMouseEvent(NULL, type, point, mapped));
}

int32_t foundation_runtime_desktop_input_key(uint64_t handle, uint64_t key, bool down) {
    CGKeyCode mapped;
    if (handle == 0) {
        return FDN_DESKTOP_CLOSED;
    }
    mapped = fdn_desktop_macos_key(key);
    if (mapped == UINT16_MAX) {
        return FDN_DESKTOP_INVALID_ARGUMENT;
    }
    return fdn_desktop_macos_post(CGEventCreateKeyboardEvent(NULL, mapped, down));
}

int32_t foundation_runtime_desktop_input_scroll(uint64_t handle, int64_t delta) {
    if (handle == 0) {
        return FDN_DESKTOP_CLOSED;
    }
    if (delta < INT32_MIN || delta > INT32_MAX) {
        return FDN_DESKTOP_INVALID_ARGUMENT;
    }
    return fdn_desktop_macos_post(
        CGEventCreateScrollWheelEvent(NULL, kCGScrollEventUnitLine, 1, (int32_t)delta));
}

void foundation_runtime_desktop_input_close(uint64_t* handle) {
    fdn_desktop_input* input;
    if (handle == NULL || *handle == 0) {
        return;
    }
    input = (fdn_desktop_input*)(uintptr_t)*handle;
    *handle = 0;
    fdn_dealloc(input);
    if (atomic_fetch_sub_explicit(&fdn_desktop_input_count, 1, memory_order_relaxed) == 0) {
        fdn_panic_cstr("desktop input count underflow");
    }
}

uint64_t foundation_runtime_desktop_input_live_handles(void) {
    return atomic_load_explicit(&fdn_desktop_input_count, memory_order_relaxed);
}

#elif defined(__linux__)

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

typedef struct _XDisplay Display;
typedef unsigned long XID;
typedef XID Window;
typedef struct _XImage XImage;

typedef struct {
    XImage* (*create_image)(Display*, void*, unsigned int, int, int, char*, unsigned int,
                            unsigned int, int, int);
    int (*destroy_image)(XImage*);
    unsigned long (*get_pixel)(XImage*, int, int);
    int (*put_pixel)(XImage*, int, int, unsigned long);
    XImage* (*sub_image)(XImage*, int, int, unsigned int, unsigned int);
    int (*add_pixel)(XImage*, long);
} fdn_ximage_functions;

struct _XImage {
    int width;
    int height;
    int xoffset;
    int format;
    char* data;
    int byte_order;
    int bitmap_unit;
    int bitmap_bit_order;
    int bitmap_pad;
    int depth;
    int bytes_per_line;
    int bits_per_pixel;
    unsigned long red_mask;
    unsigned long green_mask;
    unsigned long blue_mask;
    void* obdata;
    fdn_ximage_functions f;
};

typedef Display* (*fdn_x_open_display_fn)(const char*);
typedef int (*fdn_x_close_display_fn)(Display*);
typedef Window (*fdn_x_default_root_window_fn)(Display*);
typedef int (*fdn_x_display_size_fn)(Display*, int);
typedef XImage* (*fdn_x_get_image_fn)(Display*, Window, int, int, unsigned int, unsigned int,
                                      unsigned long, int);
typedef int (*fdn_x_error_handler_fn)(Display*, void*);
typedef fdn_x_error_handler_fn (*fdn_x_set_error_handler_fn)(fdn_x_error_handler_fn);
typedef int (*fdn_x_sync_fn)(Display*, int);

typedef struct fdn_desktop_capture {
    void* library;
    Display* display;
    Window root;
    int width;
    int height;
    fdn_x_close_display_fn close_display;
    fdn_x_get_image_fn get_image;
    fdn_x_set_error_handler_fn set_error_handler;
    fdn_x_sync_fn sync;
} fdn_desktop_capture;

typedef struct fdn_desktop_input {
    int descriptor;
} fdn_desktop_input;

static atomic_uint_fast64_t fdn_desktop_capture_count;
static atomic_uint_fast64_t fdn_desktop_input_count;
static atomic_flag fdn_desktop_x_error_lock = ATOMIC_FLAG_INIT;
static atomic_int fdn_desktop_x_error;

static int fdn_desktop_x_error_handler(Display* display, void* event) {
    (void)display;
    (void)event;
    atomic_store_explicit(&fdn_desktop_x_error, 1, memory_order_relaxed);
    return 0;
}

static XImage* fdn_desktop_x_get_image(fdn_desktop_capture* capture, unsigned int width,
                                       unsigned int height) {
    fdn_x_error_handler_fn previous_handler;
    XImage* image;
    while (atomic_flag_test_and_set_explicit(&fdn_desktop_x_error_lock, memory_order_acquire)) {
    }
    atomic_store_explicit(&fdn_desktop_x_error, 0, memory_order_relaxed);
    previous_handler = capture->set_error_handler(fdn_desktop_x_error_handler);
    image = capture->get_image(capture->display, capture->root, 0, 0, width, height, ~0UL, 2);
    (void)capture->sync(capture->display, 0);
    (void)capture->set_error_handler(previous_handler);
    atomic_flag_clear_explicit(&fdn_desktop_x_error_lock, memory_order_release);
    if (atomic_load_explicit(&fdn_desktop_x_error, memory_order_relaxed) != 0) {
        if (image != NULL && image->f.destroy_image != NULL) {
            (void)image->f.destroy_image(image);
        }
        return NULL;
    }
    return image;
}

static unsigned long fdn_desktop_component(unsigned long pixel, unsigned long mask) {
    unsigned int shift = 0;
    unsigned int bits = 0;
    unsigned long value;
    unsigned long maximum;
    if (mask == 0) {
        return 0;
    }
    while (((mask >> shift) & 1UL) == 0) {
        ++shift;
    }
    value = (pixel & mask) >> shift;
    maximum = mask >> shift;
    while ((maximum >> bits) != 0) {
        ++bits;
    }
    if (bits >= 8) {
        return value >> (bits - 8);
    }
    return (value * 255UL) / maximum;
}

static int fdn_desktop_symbol(void* library, const char* name, void* target, size_t target_size) {
    void* symbol;
    if (target == NULL || target_size != sizeof(symbol)) {
        return 0;
    }
    (void)dlerror();
    symbol = dlsym(library, name);
    if (dlerror() != NULL || symbol == NULL) {
        return 0;
    }
    memcpy(target, &symbol, sizeof(symbol));
    return 1;
}

int32_t foundation_runtime_desktop_capture_open(uint64_t* handle, uint64_t* width,
                                                uint64_t* height) {
    fdn_desktop_capture* capture;
    fdn_x_open_display_fn open_display;
    fdn_x_default_root_window_fn default_root;
    fdn_x_display_size_fn display_width;
    fdn_x_display_size_fn display_height;
    XImage* probe;
    void* library;
    if (handle == NULL || width == NULL || height == NULL) {
        fdn_panic_cstr("desktop capture output is null");
    }
    *handle = 0;
    *width = 0;
    *height = 0;
    library = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
    if (library == NULL) {
        return FDN_DESKTOP_UNAVAILABLE;
    }
    open_display = NULL;
    default_root = NULL;
    display_width = NULL;
    display_height = NULL;
    capture = fdn_alloc(sizeof(*capture));
    memset(capture, 0, sizeof(*capture));
    capture->library = library;
    if (!fdn_desktop_symbol(library, "XOpenDisplay", &open_display, sizeof(open_display)) ||
        !fdn_desktop_symbol(library, "XDefaultRootWindow", &default_root, sizeof(default_root)) ||
        !fdn_desktop_symbol(library, "XDisplayWidth", &display_width, sizeof(display_width)) ||
        !fdn_desktop_symbol(library, "XDisplayHeight", &display_height, sizeof(display_height)) ||
        !fdn_desktop_symbol(library, "XCloseDisplay", &capture->close_display,
                            sizeof(capture->close_display)) ||
        !fdn_desktop_symbol(library, "XGetImage", &capture->get_image,
                            sizeof(capture->get_image)) ||
        !fdn_desktop_symbol(library, "XSetErrorHandler", &capture->set_error_handler,
                            sizeof(capture->set_error_handler)) ||
        !fdn_desktop_symbol(library, "XSync", &capture->sync, sizeof(capture->sync))) {
        dlclose(library);
        fdn_dealloc(capture);
        return FDN_DESKTOP_UNAVAILABLE;
    }
    capture->display = open_display(NULL);
    if (capture->display == NULL) {
        dlclose(library);
        fdn_dealloc(capture);
        return FDN_DESKTOP_UNAVAILABLE;
    }
    capture->root = default_root(capture->display);
    capture->width = display_width(capture->display, 0);
    capture->height = display_height(capture->display, 0);
    if (capture->root == 0 || capture->width <= 0 || capture->height <= 0) {
        (void)capture->close_display(capture->display);
        dlclose(library);
        fdn_dealloc(capture);
        return FDN_DESKTOP_UNAVAILABLE;
    }
    probe = fdn_desktop_x_get_image(capture, 1, 1);
    if (probe == NULL || probe->f.destroy_image == NULL) {
        if (probe != NULL && probe->f.destroy_image != NULL) {
            (void)probe->f.destroy_image(probe);
        }
        (void)capture->close_display(capture->display);
        dlclose(library);
        fdn_dealloc(capture);
        return FDN_DESKTOP_UNAVAILABLE;
    }
    (void)probe->f.destroy_image(probe);
    if (atomic_fetch_add_explicit(&fdn_desktop_capture_count, 1, memory_order_relaxed) ==
        UINT64_MAX) {
        fdn_panic_cstr("desktop capture count overflow");
    }
    *handle = (uint64_t)(uintptr_t)capture;
    *width = (uint64_t)capture->width;
    *height = (uint64_t)capture->height;
    return FDN_DESKTOP_OK;
}

int32_t foundation_runtime_desktop_capture_frame(uint64_t handle, uint64_t* bytes_handle) {
    fdn_desktop_capture* capture = (fdn_desktop_capture*)(uintptr_t)handle;
    XImage* image;
    uint8_t* pixels;
    size_t length;
    int x;
    int y;
    if (bytes_handle == NULL) {
        fdn_panic_cstr("desktop frame output is null");
    }
    *bytes_handle = 0;
    if (capture == NULL) {
        return FDN_DESKTOP_CLOSED;
    }
    if (!fdn_desktop_frame_size((uint64_t)capture->width, (uint64_t)capture->height, &length)) {
        return FDN_DESKTOP_INVALID_ARGUMENT;
    }
    image = fdn_desktop_x_get_image(capture, (unsigned int)capture->width,
                                    (unsigned int)capture->height);
    if (image == NULL || image->f.get_pixel == NULL || image->f.destroy_image == NULL) {
        if (image != NULL && image->f.destroy_image != NULL) {
            (void)image->f.destroy_image(image);
        }
        return FDN_DESKTOP_IO;
    }
    pixels = fdn_alloc(length);
    for (y = 0; y < capture->height; ++y) {
        for (x = 0; x < capture->width; ++x) {
            const unsigned long pixel = image->f.get_pixel(image, x, y);
            const size_t offset = ((size_t)y * (size_t)capture->width + (size_t)x) * 4;
            pixels[offset] = (uint8_t)fdn_desktop_component(pixel, image->red_mask);
            pixels[offset + 1] = (uint8_t)fdn_desktop_component(pixel, image->green_mask);
            pixels[offset + 2] = (uint8_t)fdn_desktop_component(pixel, image->blue_mask);
            pixels[offset + 3] = 255;
        }
    }
    (void)image->f.destroy_image(image);
    if (fdn_bytes_adopt(pixels, length, length, bytes_handle) != 0) {
        fdn_dealloc(pixels);
        return FDN_DESKTOP_IO;
    }
    return FDN_DESKTOP_OK;
}

void foundation_runtime_desktop_capture_close(uint64_t* handle) {
    fdn_desktop_capture* capture;
    if (handle == NULL || *handle == 0) {
        return;
    }
    capture = (fdn_desktop_capture*)(uintptr_t)*handle;
    *handle = 0;
    (void)capture->close_display(capture->display);
    (void)dlclose(capture->library);
    fdn_dealloc(capture);
    if (atomic_fetch_sub_explicit(&fdn_desktop_capture_count, 1, memory_order_relaxed) == 0) {
        fdn_panic_cstr("desktop capture count underflow");
    }
}

uint64_t foundation_runtime_desktop_capture_live_handles(void) {
    return atomic_load_explicit(&fdn_desktop_capture_count, memory_order_relaxed);
}

static int fdn_desktop_input_ioctl(int descriptor, unsigned long request, int value) {
    return ioctl(descriptor, request, value) == 0;
}

static int fdn_desktop_input_emit(fdn_desktop_input* input, uint16_t type, uint16_t code,
                                  int32_t value) {
    struct input_event event;
    ssize_t written;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.code = code;
    event.value = value;
    written = write(input->descriptor, &event, sizeof(event));
    return written == (ssize_t)sizeof(event);
}

static int fdn_desktop_input_sync(fdn_desktop_input* input) {
    return fdn_desktop_input_emit(input, EV_SYN, SYN_REPORT, 0);
}

int32_t foundation_runtime_desktop_input_open(uint64_t* handle) {
    fdn_desktop_input* input;
    struct uinput_user_dev device;
    int descriptor;
    int code;
    if (handle == NULL) {
        fdn_panic_cstr("desktop input output is null");
    }
    *handle = 0;
    descriptor = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (descriptor < 0) {
        return errno == EACCES || errno == EPERM ? FDN_DESKTOP_PERMISSION : FDN_DESKTOP_UNAVAILABLE;
    }
    if (!fdn_desktop_input_ioctl(descriptor, UI_SET_EVBIT, EV_KEY) ||
        !fdn_desktop_input_ioctl(descriptor, UI_SET_EVBIT, EV_ABS) ||
        !fdn_desktop_input_ioctl(descriptor, UI_SET_EVBIT, EV_REL) ||
        !fdn_desktop_input_ioctl(descriptor, UI_SET_RELBIT, REL_WHEEL) ||
        !fdn_desktop_input_ioctl(descriptor, UI_SET_ABSBIT, ABS_X) ||
        !fdn_desktop_input_ioctl(descriptor, UI_SET_ABSBIT, ABS_Y)) {
        (void)close(descriptor);
        return FDN_DESKTOP_IO;
    }
    for (code = 0; code <= 255; ++code) {
        if (!fdn_desktop_input_ioctl(descriptor, UI_SET_KEYBIT, code)) {
            (void)close(descriptor);
            return FDN_DESKTOP_IO;
        }
    }
    if (!fdn_desktop_input_ioctl(descriptor, UI_SET_KEYBIT, BTN_LEFT) ||
        !fdn_desktop_input_ioctl(descriptor, UI_SET_KEYBIT, BTN_RIGHT) ||
        !fdn_desktop_input_ioctl(descriptor, UI_SET_KEYBIT, BTN_MIDDLE)) {
        (void)close(descriptor);
        return FDN_DESKTOP_IO;
    }
    memset(&device, 0, sizeof(device));
    (void)strncpy(device.name, "Foundation remote input", UINPUT_MAX_NAME_SIZE - 1);
    device.id.bustype = BUS_USB;
    device.id.vendor = 0x1d6b;
    device.id.product = 0x0104;
    device.id.version = 1;
    device.absmin[ABS_X] = 0;
    device.absmax[ABS_X] = 32767;
    device.absmin[ABS_Y] = 0;
    device.absmax[ABS_Y] = 32767;
    if (write(descriptor, &device, sizeof(device)) != (ssize_t)sizeof(device) ||
        ioctl(descriptor, UI_DEV_CREATE) != 0) {
        (void)close(descriptor);
        return FDN_DESKTOP_IO;
    }
    input = fdn_alloc(sizeof(*input));
    input->descriptor = descriptor;
    if (atomic_fetch_add_explicit(&fdn_desktop_input_count, 1, memory_order_relaxed) ==
        UINT64_MAX) {
        fdn_panic_cstr("desktop input count overflow");
    }
    *handle = (uint64_t)(uintptr_t)input;
    return FDN_DESKTOP_OK;
}

#else

int32_t foundation_runtime_desktop_capture_open(uint64_t* handle, uint64_t* width,
                                                uint64_t* height) {
    if (handle == NULL || width == NULL || height == NULL) {
        fdn_panic_cstr("desktop capture output is null");
    }
    *handle = 0;
    *width = 0;
    *height = 0;
    return FDN_DESKTOP_UNAVAILABLE;
}

int32_t foundation_runtime_desktop_capture_frame(uint64_t handle, uint64_t* bytes_handle) {
    (void)handle;
    if (bytes_handle == NULL) {
        fdn_panic_cstr("desktop frame output is null");
    }
    *bytes_handle = 0;
    return FDN_DESKTOP_UNAVAILABLE;
}

void foundation_runtime_desktop_capture_close(uint64_t* handle) {
    if (handle != NULL) {
        *handle = 0;
    }
}

uint64_t foundation_runtime_desktop_capture_live_handles(void) { return 0; }

int32_t foundation_runtime_desktop_input_open(uint64_t* handle) {
    if (handle == NULL) {
        fdn_panic_cstr("desktop input output is null");
    }
    *handle = 0;
    return FDN_DESKTOP_UNAVAILABLE;
}

#endif

#if !defined(_WIN32) && !defined(__APPLE__)
#if !defined(__linux__)

int32_t foundation_runtime_desktop_input_move(uint64_t handle, uint64_t x, uint64_t y) {
    (void)handle;
    (void)x;
    (void)y;
    return FDN_DESKTOP_UNAVAILABLE;
}

int32_t foundation_runtime_desktop_input_button(uint64_t handle, uint64_t button, bool down) {
    (void)handle;
    (void)button;
    (void)down;
    return FDN_DESKTOP_UNAVAILABLE;
}

int32_t foundation_runtime_desktop_input_key(uint64_t handle, uint64_t key, bool down) {
    (void)handle;
    (void)key;
    (void)down;
    return FDN_DESKTOP_UNAVAILABLE;
}

int32_t foundation_runtime_desktop_input_scroll(uint64_t handle, int64_t delta) {
    (void)handle;
    (void)delta;
    return FDN_DESKTOP_UNAVAILABLE;
}

void foundation_runtime_desktop_input_close(uint64_t* handle) {
    if (handle != NULL) {
        *handle = 0;
    }
}

uint64_t foundation_runtime_desktop_input_live_handles(void) { return 0; }

#else

int32_t foundation_runtime_desktop_input_move(uint64_t handle, uint64_t x, uint64_t y) {
    fdn_desktop_input* input = (fdn_desktop_input*)(uintptr_t)handle;
    if (input == NULL) {
        return FDN_DESKTOP_CLOSED;
    }
    if (x > 32767 || y > 32767) {
        return FDN_DESKTOP_INVALID_ARGUMENT;
    }
    if (!fdn_desktop_input_emit(input, EV_ABS, ABS_X, (int32_t)x) ||
        !fdn_desktop_input_emit(input, EV_ABS, ABS_Y, (int32_t)y) ||
        !fdn_desktop_input_sync(input)) {
        return FDN_DESKTOP_IO;
    }
    return FDN_DESKTOP_OK;
}

int32_t foundation_runtime_desktop_input_button(uint64_t handle, uint64_t button, bool down) {
    fdn_desktop_input* input = (fdn_desktop_input*)(uintptr_t)handle;
    if (input == NULL) {
        return FDN_DESKTOP_CLOSED;
    }
    if (button != BTN_LEFT && button != BTN_RIGHT && button != BTN_MIDDLE) {
        return FDN_DESKTOP_INVALID_ARGUMENT;
    }
    if (!fdn_desktop_input_emit(input, EV_KEY, (uint16_t)button, down ? 1 : 0) ||
        !fdn_desktop_input_sync(input)) {
        return FDN_DESKTOP_IO;
    }
    return FDN_DESKTOP_OK;
}

int32_t foundation_runtime_desktop_input_key(uint64_t handle, uint64_t key, bool down) {
    fdn_desktop_input* input = (fdn_desktop_input*)(uintptr_t)handle;
    if (input == NULL) {
        return FDN_DESKTOP_CLOSED;
    }
    if (key > 255) {
        return FDN_DESKTOP_INVALID_ARGUMENT;
    }
    if (!fdn_desktop_input_emit(input, EV_KEY, (uint16_t)key, down ? 1 : 0) ||
        !fdn_desktop_input_sync(input)) {
        return FDN_DESKTOP_IO;
    }
    return FDN_DESKTOP_OK;
}

int32_t foundation_runtime_desktop_input_scroll(uint64_t handle, int64_t delta) {
    fdn_desktop_input* input = (fdn_desktop_input*)(uintptr_t)handle;
    if (input == NULL) {
        return FDN_DESKTOP_CLOSED;
    }
    if (delta < INT32_MIN || delta > INT32_MAX) {
        return FDN_DESKTOP_INVALID_ARGUMENT;
    }
    if (!fdn_desktop_input_emit(input, EV_REL, REL_WHEEL, (int32_t)delta) ||
        !fdn_desktop_input_sync(input)) {
        return FDN_DESKTOP_IO;
    }
    return FDN_DESKTOP_OK;
}

void foundation_runtime_desktop_input_close(uint64_t* handle) {
    fdn_desktop_input* input;
    if (handle == NULL || *handle == 0) {
        return;
    }
    input = (fdn_desktop_input*)(uintptr_t)*handle;
    *handle = 0;
    (void)ioctl(input->descriptor, UI_DEV_DESTROY);
    (void)close(input->descriptor);
    fdn_dealloc(input);
    if (atomic_fetch_sub_explicit(&fdn_desktop_input_count, 1, memory_order_relaxed) == 0) {
        fdn_panic_cstr("desktop input count underflow");
    }
}

uint64_t foundation_runtime_desktop_input_live_handles(void) {
    return atomic_load_explicit(&fdn_desktop_input_count, memory_order_relaxed);
}

#endif
#endif
