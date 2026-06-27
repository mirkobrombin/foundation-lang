#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    char command[64];
    if (argc == 2 && strcmp(argv[1], "invalid-ready") == 0) {
        (void)fputs("{\"ready\":false}\n", stdout);
        (void)fflush(stdout);
        if (fgets(command, sizeof(command), stdin) == NULL) {
            return 0;
        }
        return 0;
    }
    if (argc != 4 || strcmp(argv[1], "serve") != 0 ||
        strcmp(argv[2], "hello world") != 0 || argv[3][0] != '\0') {
        return 20;
    }
    (void)fputs("{\"ready\":true,\"protocol\":1}\n", stdout);
    (void)fflush(stdout);
    if (fgets(command, sizeof(command), stdin) == NULL ||
        strcmp(command, "{\"cmd\":\"stop\"}\n") != 0) {
        return 21;
    }
    return 0;
}
