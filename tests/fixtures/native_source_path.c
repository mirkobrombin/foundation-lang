#include <stdbool.h>

bool foundation_native_source_path_is_relative(void) {
    const char *path = __FILE__;
    if (path[0] == '/' || path[0] == '\\') {
        return false;
    }
    return !(path[0] != '\0' && path[1] == ':');
}
