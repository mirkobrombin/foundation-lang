#include <foundation/runtime.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#pragma comment(lib, "User32.lib")
#elif defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

static int foundation_desktop_text(const fdn_string *value, char **result) {
    char *copy;
    if (value == NULL || value->length > SIZE_MAX - 1 || (value->length != 0 && value->data == NULL)) return 0;
    copy = malloc(value->length + 1);
    if (copy == NULL) return 0;
    memcpy(copy, value->data, value->length);
    copy[value->length] = '\0';
    *result = copy;
    return 1;
}

bool foundation_desktop_is_headless(void) {
    const char *value = getenv("FOUNDATION_DESKTOP_HEADLESS");
    return value != NULL && strcmp(value, "1") == 0;
}

#ifdef _WIN32
int32_t foundation_desktop_show(const fdn_string *title, const fdn_string *message) {
    char *title_text = NULL;
    char *message_text = NULL;
    int32_t status = 1;
    if (!foundation_desktop_text(title, &title_text) || !foundation_desktop_text(message, &message_text)) goto cleanup;
    status = MessageBoxA(NULL, message_text, title_text, MB_OK | MB_ICONINFORMATION) == 0 ? 1 : 0;
cleanup:
    free(title_text);
    free(message_text);
    return status;
}
#elif defined(__APPLE__)
int32_t foundation_desktop_show(const fdn_string *title, const fdn_string *message) {
    char *title_text = NULL;
    char *message_text = NULL;
    pid_t child;
    int result = 1;
    if (!foundation_desktop_text(title, &title_text) || !foundation_desktop_text(message, &message_text)) goto cleanup;
    child = fork();
    if (child == 0) {
        execl("/usr/bin/osascript", "osascript", "-e",
              "on run argv\ndisplay dialog (item 1 of argv) with title (item 2 of argv)\nend run",
              message_text, title_text, (char *)NULL);
        _exit(127);
    }
    if (child > 0 && waitpid(child, &result, 0) == child && WIFEXITED(result) && WEXITSTATUS(result) == 0) result = 0;
    else result = 1;
cleanup:
    free(title_text);
    free(message_text);
    return result;
}
#else
int32_t foundation_desktop_show(const fdn_string *title, const fdn_string *message) {
    char *title_text = NULL;
    char *message_text = NULL;
    pid_t child;
    int status = 1;
    if (!foundation_desktop_text(title, &title_text) || !foundation_desktop_text(message, &message_text)) goto cleanup;
    child = fork();
    if (child == 0) {
        execl("/usr/bin/zenity", "zenity", "--info", "--title", title_text, "--text", message_text, (char *)NULL);
        _exit(127);
    }
    if (child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0) status = 0;
    else status = 1;
cleanup:
    free(title_text);
    free(message_text);
    return status;
}
#endif
