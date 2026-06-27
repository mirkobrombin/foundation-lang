#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    char command[64];
    if (argc != 2 || strcmp(argv[1], "serve") != 0) {
        return 20;
    }
    (void)fputs("{\"ready\":true,\"name\":\"greeter-process\"}\n", stdout);
    (void)fflush(stdout);
    if (fgets(command, sizeof(command), stdin) == NULL ||
        strcmp(command, "{\"cmd\":\"stop\"}\n") != 0) {
        return 21;
    }
    return 0;
}
