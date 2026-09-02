#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif

#include "foundation/runtime.h"
#include "bytes_internal.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#else
#include <dirent.h>
#include <fcntl.h>
#if defined(__linux__)
#include <linux/fs.h>
#include <sys/syscall.h>
#endif
#include <unistd.h>
#endif

enum {
    FDN_FS_NOT_FOUND = 1,
    FDN_FS_PERMISSION = 2,
    FDN_FS_INVALID_PATH = 3,
    FDN_FS_IO = 4,
    FDN_FS_TOO_LARGE = 5,
    FDN_FS_INVALID_UTF8 = 6,
    FDN_FS_ALREADY_EXISTS = 7,
    FDN_FS_NOT_DIRECTORY = 8,
    FDN_FS_IS_DIRECTORY = 9,
    FDN_FS_UNSUPPORTED = 10,
    FDN_FS_NOT_EMPTY = 11,
    FDN_FS_CROSS_DEVICE = 12,
};

static int fdn_host_path_valid(const fdn_string *path) {
    size_t offset;
    if (path == NULL || path->data == NULL || path->length == 0 ||
        !fdn_utf8_valid(path->data, path->length)) {
        return 0;
    }
    for (offset = 0; offset < path->length; ++offset) {
        if (path->data[offset] == '\0') {
            return 0;
        }
    }
    return 1;
}

static int fdn_host_prefix_valid(const fdn_string *prefix) {
    size_t offset;
    if (!fdn_host_path_valid(prefix)) {
        return 0;
    }
    for (offset = 0; offset < prefix->length; ++offset) {
        if (prefix->data[offset] == '/' || prefix->data[offset] == '\\') {
            return 0;
        }
    }
    return 1;
}

static int fdn_host_path_has_dot_component(const fdn_string *path) {
    size_t start = 0;
    size_t offset;
    if (path == NULL) {
        return 1;
    }
    for (offset = 0; offset <= path->length; ++offset) {
        const int separator = offset == path->length ||
                              path->data[offset] == '/' ||
                              path->data[offset] == '\\';
        const size_t length = offset - start;
        if (!separator) {
            continue;
        }
        if ((length == 1 && path->data[start] == '.') ||
            (length == 2 && path->data[start] == '.' &&
             path->data[start + 1] == '.')) {
            return 1;
        }
        start = offset + 1;
    }
    return 0;
}

#if defined(_WIN32)
static int32_t fdn_host_windows_status(DWORD error) {
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        return FDN_FS_NOT_FOUND;
    }
    if (error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION) {
        return FDN_FS_PERMISSION;
    }
    if (error == ERROR_INVALID_NAME || error == ERROR_BAD_PATHNAME ||
        error == ERROR_FILENAME_EXCED_RANGE) {
        return FDN_FS_INVALID_PATH;
    }
    if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) {
        return FDN_FS_ALREADY_EXISTS;
    }
    if (error == ERROR_DIRECTORY) {
        return FDN_FS_NOT_DIRECTORY;
    }
    if (error == ERROR_DIR_NOT_EMPTY) {
        return FDN_FS_NOT_EMPTY;
    }
    if (error == ERROR_NOT_SAME_DEVICE) {
        return FDN_FS_CROSS_DEVICE;
    }
    return FDN_FS_IO;
}

static wchar_t *fdn_host_windows_path(const fdn_string *path) {
    wchar_t *result;
    int length;
    if (!fdn_host_path_valid(path) || path->length > (size_t)INT_MAX) {
        return NULL;
    }
    length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path->data,
                                 (int)path->length, NULL, 0);
    if (length == 0) {
        return NULL;
    }
    result = fdn_alloc(((size_t)length + 1) * sizeof(*result));
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path->data,
                            (int)path->length, result, length) != length) {
        fdn_dealloc(result);
        return NULL;
    }
    result[length] = L'\0';
    return result;
}

static int32_t fdn_host_windows_string(const wchar_t *value, size_t length,
                                       fdn_string *result) {
    char *bytes;
    int byte_length;
    if (length > (size_t)INT_MAX) {
        return FDN_FS_TOO_LARGE;
    }
    byte_length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value,
                                      (int)length, NULL, 0, NULL, NULL);
    if (byte_length == 0 && length != 0) {
        return FDN_FS_INVALID_UTF8;
    }
    fdn_string_drop(result);
    if (byte_length == 0) {
        *result = fdn_string_static("", 0);
        return 0;
    }
    bytes = fdn_alloc((size_t)byte_length);
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, (int)length,
                            bytes, byte_length, NULL, NULL) != byte_length) {
        fdn_dealloc(bytes);
        return FDN_FS_INVALID_UTF8;
    }
    result->data = bytes;
    result->length = (size_t)byte_length;
    result->owned = 1;
    return 0;
}

static int fdn_host_windows_volume_root(const wchar_t *path) {
    size_t length = wcslen(path);
    size_t offset;
    size_t components = 0;
    while (length > 0 && (path[length - 1] == L'\\' ||
                          path[length - 1] == L'/')) {
        --length;
    }
    if (length == 0 ||
        (length == 2 && path[1] == L':')) {
        return 1;
    }
    if (length > 2 && path[1] == L':' &&
        path[2] != L'\\' && path[2] != L'/') {
        return 1;
    }
    if (length < 2 ||
        (path[0] != L'\\' && path[0] != L'/') ||
        (path[1] != L'\\' && path[1] != L'/')) {
        return 0;
    }
    offset = 2;
    if (length >= 8 && path[0] == L'\\' && path[1] == L'\\' &&
        path[2] == L'?' && path[3] == L'\\' &&
        (path[4] == L'U' || path[4] == L'u') &&
        (path[5] == L'N' || path[5] == L'n') &&
        (path[6] == L'C' || path[6] == L'c') && path[7] == L'\\') {
        offset = 8;
    }
    while (offset < length) {
        while (offset < length &&
               (path[offset] == L'\\' || path[offset] == L'/')) {
            ++offset;
        }
        if (offset == length) {
            break;
        }
        ++components;
        while (offset < length && path[offset] != L'\\' &&
               path[offset] != L'/') {
            ++offset;
        }
    }
    return components <= 2;
}
#else
static int32_t fdn_host_posix_status(int error) {
    if (error == ENOENT) {
        return FDN_FS_NOT_FOUND;
    }
    if (error == EACCES || error == EPERM) {
        return FDN_FS_PERMISSION;
    }
    if (error == EINVAL || error == ENAMETOOLONG || error == ELOOP) {
        return FDN_FS_INVALID_PATH;
    }
    if (error == EEXIST) {
        return FDN_FS_ALREADY_EXISTS;
    }
    if (error == ENOTDIR) {
        return FDN_FS_NOT_DIRECTORY;
    }
    if (error == EISDIR) {
        return FDN_FS_IS_DIRECTORY;
    }
    if (error == ENOTEMPTY) {
        return FDN_FS_NOT_EMPTY;
    }
    if (error == EXDEV) {
        return FDN_FS_CROSS_DEVICE;
    }
    return FDN_FS_IO;
}

static char *fdn_host_posix_path(const fdn_string *path) {
    char *result;
    if (!fdn_host_path_valid(path) || path->length == SIZE_MAX) {
        return NULL;
    }
    result = fdn_alloc(path->length + 1);
    (void)memcpy(result, path->data, path->length);
    result[path->length] = '\0';
    return result;
}
#endif

int32_t foundation_runtime_fs_read_text_sync_limited(const fdn_string *path,
                                                     uint64_t max_length,
                                                     fdn_string *result) {
    return foundation_runtime_fs_read_text_limited(path, max_length, result);
}

int32_t foundation_runtime_fs_read_bytes_sync_limited(const fdn_string *path,
                                                      uint64_t max_length,
                                                      uint64_t *result) {
    uint8_t *data = NULL;
    size_t length = 0;
    size_t offset = 0;
    int32_t status = 0;
    if (result == NULL) {
        fdn_panic_cstr("file bytes output is null");
    }
    *result = 0;
#if defined(_WIN32)
    {
        wchar_t *native_path = fdn_host_windows_path(path);
        HANDLE file;
        BY_HANDLE_FILE_INFORMATION info;
        uint64_t native_length;
        if (native_path == NULL) {
            return FDN_FS_INVALID_PATH;
        }
        file = CreateFileW(native_path, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
                           NULL);
        fdn_dealloc(native_path);
        if (file == INVALID_HANDLE_VALUE) {
            return fdn_host_windows_status(GetLastError());
        }
        if (GetFileInformationByHandle(file, &info) == 0) {
            status = fdn_host_windows_status(GetLastError());
            goto windows_cleanup;
        }
        if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            status = FDN_FS_INVALID_PATH;
            goto windows_cleanup;
        }
        if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            status = FDN_FS_IS_DIRECTORY;
            goto windows_cleanup;
        }
        native_length = ((uint64_t)info.nFileSizeHigh << 32U) |
                        (uint64_t)info.nFileSizeLow;
        if (native_length > max_length || native_length > SIZE_MAX) {
            status = FDN_FS_TOO_LARGE;
            goto windows_cleanup;
        }
        length = (size_t)native_length;
        if (length != 0) {
            data = fdn_alloc(length);
        }
        while (offset < length) {
            const size_t remaining = length - offset;
            const DWORD requested = remaining > UINT32_MAX
                                        ? UINT32_MAX
                                        : (DWORD)remaining;
            DWORD count = 0;
            if (ReadFile(file, data + offset, requested, &count, NULL) == 0) {
                status = fdn_host_windows_status(GetLastError());
                goto windows_cleanup;
            }
            if (count == 0) {
                break;
            }
            offset += (size_t)count;
        }
        if (offset == length) {
            uint8_t probe;
            DWORD count = 0;
            if (ReadFile(file, &probe, 1, &count, NULL) == 0) {
                status = fdn_host_windows_status(GetLastError());
            } else if (count != 0) {
                status = FDN_FS_TOO_LARGE;
            }
        }

windows_cleanup:
        if (CloseHandle(file) == 0 && status == 0) {
            status = FDN_FS_IO;
        }
    }
#else
    {
        char *native_path = fdn_host_posix_path(path);
        struct stat info;
        int file = -1;
        if (native_path == NULL) {
            return FDN_FS_INVALID_PATH;
        }
        file = open(native_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        fdn_dealloc(native_path);
        if (file < 0) {
            return fdn_host_posix_status(errno);
        }
        if (fstat(file, &info) != 0) {
            status = fdn_host_posix_status(errno);
            goto posix_cleanup;
        }
        if (S_ISDIR(info.st_mode)) {
            status = FDN_FS_IS_DIRECTORY;
            goto posix_cleanup;
        }
        if (!S_ISREG(info.st_mode)) {
            status = FDN_FS_INVALID_PATH;
            goto posix_cleanup;
        }
        if (info.st_size < 0 || (uint64_t)info.st_size > max_length ||
            (uint64_t)info.st_size > SIZE_MAX) {
            status = FDN_FS_TOO_LARGE;
            goto posix_cleanup;
        }
        length = (size_t)info.st_size;
        if (length != 0) {
            data = fdn_alloc(length);
        }
        while (offset < length) {
            const ssize_t count = read(file, data + offset, length - offset);
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                status = fdn_host_posix_status(errno);
                goto posix_cleanup;
            }
            if (count == 0) {
                break;
            }
            offset += (size_t)count;
        }
        if (offset == length) {
            uint8_t probe;
            ssize_t count;
            do {
                count = read(file, &probe, 1);
            } while (count < 0 && errno == EINTR);
            if (count < 0) {
                status = fdn_host_posix_status(errno);
            } else if (count != 0) {
                status = FDN_FS_TOO_LARGE;
            }
        }

posix_cleanup:
        if (close(file) != 0 && status == 0) {
            status = fdn_host_posix_status(errno);
        }
    }
#endif
    if (status == 0 &&
        fdn_bytes_adopt(data, offset, length, result) == 0) {
        return 0;
    }
    if (data != NULL) {
        fdn_dealloc(data);
    }
    return status == 0 ? FDN_FS_IO : status;
}

#if !defined(_WIN32)
static char *fdn_host_parent_path(const char *path) {
    const char *separator = strrchr(path, '/');
    char *result;
    size_t length;
    if (separator == NULL) {
        result = fdn_alloc(2);
        result[0] = '.';
        result[1] = '\0';
        return result;
    }
    length = separator == path ? 1 : (size_t)(separator - path);
    result = fdn_alloc(length + 1);
    (void)memcpy(result, path, length);
    result[length] = '\0';
    return result;
}

static int32_t fdn_host_posix_write_text_atomic(const char *path,
                                                const fdn_string *value) {
    static const char suffix[] = "/.fn.XXXXXX";
    struct stat existing;
    char *directory = NULL;
    char *temporary = NULL;
    size_t directory_length;
    size_t offset = 0;
    mode_t mode = 0;
    int preserve_mode = 0;
    int file = -1;
    int32_t status = FDN_FS_IO;
    unsigned int attempt;

    if (lstat(path, &existing) == 0) {
        if (S_ISLNK(existing.st_mode)) {
            return FDN_FS_INVALID_PATH;
        }
        if (S_ISDIR(existing.st_mode)) {
            return FDN_FS_IS_DIRECTORY;
        }
        if (!S_ISREG(existing.st_mode)) {
            return FDN_FS_INVALID_PATH;
        }
        mode = existing.st_mode & 07777;
        preserve_mode = 1;
    } else if (errno != ENOENT) {
        return fdn_host_posix_status(errno);
    }

    directory = fdn_host_parent_path(path);
    directory_length = strlen(directory);
    if (directory_length > SIZE_MAX - sizeof(suffix)) {
        status = FDN_FS_INVALID_PATH;
        goto cleanup;
    }
    temporary = fdn_alloc(directory_length + sizeof(suffix));
    for (attempt = 0; attempt < 16; ++attempt) {
        (void)memcpy(temporary, directory, directory_length);
        (void)memcpy(temporary + directory_length, suffix, sizeof(suffix));
        file = mkstemp(temporary);
        if (file < 0) {
            status = fdn_host_posix_status(errno);
            goto cleanup;
        }
        if (preserve_mode != 0) {
            if (fchmod(file, mode) != 0) {
                status = fdn_host_posix_status(errno);
                goto cleanup;
            }
            break;
        }
        if (close(file) != 0) {
            file = -1;
            status = fdn_host_posix_status(errno);
            goto cleanup;
        }
        file = -1;
        if (unlink(temporary) != 0) {
            status = fdn_host_posix_status(errno);
            goto cleanup;
        }
        file = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
        if (file >= 0) {
            break;
        }
        if (errno != EEXIST) {
            status = fdn_host_posix_status(errno);
            goto cleanup;
        }
    }
    if (file < 0) {
        status = FDN_FS_IO;
        goto cleanup;
    }
    if (fcntl(file, F_SETFD, FD_CLOEXEC) != 0) {
        status = fdn_host_posix_status(errno);
        goto cleanup;
    }
    while (offset < value->length) {
        const size_t remaining = value->length - offset;
        const size_t requested = remaining > (size_t)INT_MAX
                                     ? (size_t)INT_MAX
                                     : remaining;
        const ssize_t written = write(file, value->data + offset,
                                      requested);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            status = fdn_host_posix_status(errno);
            goto cleanup;
        }
        if (written == 0) {
            status = FDN_FS_IO;
            goto cleanup;
        }
        offset += (size_t)written;
    }
    if (fsync(file) != 0) {
        status = fdn_host_posix_status(errno);
        goto cleanup;
    }
    if (close(file) != 0) {
        file = -1;
        status = fdn_host_posix_status(errno);
        goto cleanup;
    }
    file = -1;
    if (rename(temporary, path) != 0) {
        status = fdn_host_posix_status(errno);
        goto cleanup;
    }
    temporary[0] = '\0';
    status = 0;

cleanup:
    if (file >= 0) {
        (void)close(file);
    }
    if (temporary != NULL && temporary[0] != '\0') {
        (void)unlink(temporary);
    }
    fdn_dealloc(temporary);
    fdn_dealloc(directory);
    return status;
}
#endif

int32_t foundation_runtime_fs_write_text_atomic(const fdn_string *path,
                                                const fdn_string *value,
                                                uint64_t max_length) {
    if (value == NULL || (value->data == NULL && value->length != 0) ||
        !fdn_utf8_valid(value->data, value->length)) {
        return FDN_FS_INVALID_UTF8;
    }
    if (value->length > max_length) {
        return FDN_FS_TOO_LARGE;
    }
#if defined(_WIN32)
    return foundation_runtime_fs_write_private_text_atomic(path, value, max_length);
#else
    {
        char *native_path = fdn_host_posix_path(path);
        int32_t status;
        if (native_path == NULL) {
            return FDN_FS_INVALID_PATH;
        }
        status = fdn_host_posix_write_text_atomic(native_path, value);
        fdn_dealloc(native_path);
        return status;
    }
#endif
}

int32_t foundation_runtime_fs_create_directory_tree(const fdn_string *path) {
#if defined(_WIN32)
    wchar_t *native_path = fdn_host_windows_path(path);
    size_t first = 0;
    size_t length;
    size_t offset;
    if (native_path == NULL) {
        return FDN_FS_INVALID_PATH;
    }
    length = wcslen(native_path);
    if (length >= 3 && native_path[1] == L':' &&
        (native_path[2] == L'\\' || native_path[2] == L'/')) {
        first = 3;
    } else if (length >= 2 &&
               (native_path[0] == L'\\' || native_path[0] == L'/') &&
               (native_path[1] == L'\\' || native_path[1] == L'/')) {
        unsigned int separators = 0;
        first = 2;
        while (first < length && separators < 2) {
            if (native_path[first] == L'\\' || native_path[first] == L'/') {
                ++separators;
            }
            ++first;
        }
    }
    while (length > first &&
           (native_path[length - 1] == L'\\' || native_path[length - 1] == L'/')) {
        native_path[--length] = L'\0';
    }
    for (offset = first; offset <= length; ++offset) {
        wchar_t saved;
        DWORD attributes;
        if (offset != length && native_path[offset] != L'\\' &&
            native_path[offset] != L'/') {
            continue;
        }
        if (offset == 0 || (offset > 0 &&
                            (native_path[offset - 1] == L'\\' ||
                             native_path[offset - 1] == L'/'))) {
            continue;
        }
        saved = native_path[offset];
        native_path[offset] = L'\0';
        if (CreateDirectoryW(native_path, NULL) == 0 &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
            const int32_t status = fdn_host_windows_status(GetLastError());
            native_path[offset] = saved;
            fdn_dealloc(native_path);
            return status;
        }
        attributes = GetFileAttributesW(native_path);
        native_path[offset] = saved;
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            fdn_dealloc(native_path);
            return FDN_FS_NOT_DIRECTORY;
        }
    }
    fdn_dealloc(native_path);
    return 0;
#else
    char *native_path = fdn_host_posix_path(path);
    size_t first;
    size_t length;
    size_t offset;
    if (native_path == NULL) {
        return FDN_FS_INVALID_PATH;
    }
    length = strlen(native_path);
    first = length != 0 && native_path[0] == '/' ? 1 : 0;
    while (length > first && native_path[length - 1] == '/') {
        native_path[--length] = '\0';
    }
    if (first == 1 && length == 1) {
        fdn_dealloc(native_path);
        return 0;
    }
    for (offset = first; offset <= length; ++offset) {
        char saved;
        struct stat info;
        if (offset != length && native_path[offset] != '/') {
            continue;
        }
        if (offset == 0 || (offset > 0 && native_path[offset - 1] == '/')) {
            continue;
        }
        saved = native_path[offset];
        native_path[offset] = '\0';
        if (mkdir(native_path, 0777) != 0 && errno != EEXIST) {
            const int32_t status = fdn_host_posix_status(errno);
            native_path[offset] = saved;
            fdn_dealloc(native_path);
            return status;
        }
        if (stat(native_path, &info) != 0) {
            const int32_t status = fdn_host_posix_status(errno);
            native_path[offset] = saved;
            fdn_dealloc(native_path);
            return status;
        }
        if (!S_ISDIR(info.st_mode)) {
            native_path[offset] = saved;
            fdn_dealloc(native_path);
            return FDN_FS_NOT_DIRECTORY;
        }
        native_path[offset] = saved;
    }
    fdn_dealloc(native_path);
    return 0;
#endif
}

int32_t foundation_runtime_fs_exists(const fdn_string *path, bool *result) {
    if (result == NULL) {
        fdn_panic_cstr("filesystem exists output is null");
    }
    *result = false;
#if defined(_WIN32)
    {
        wchar_t *native_path = fdn_host_windows_path(path);
        DWORD attributes;
        if (native_path == NULL) {
            return FDN_FS_INVALID_PATH;
        }
        attributes = GetFileAttributesW(native_path);
        fdn_dealloc(native_path);
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
                return 0;
            }
            return fdn_host_windows_status(error);
        }
        *result = true;
    }
#else
    {
        char *native_path = fdn_host_posix_path(path);
        struct stat info;
        if (native_path == NULL) {
            return FDN_FS_INVALID_PATH;
        }
        if (lstat(native_path, &info) != 0) {
            const int error = errno;
            fdn_dealloc(native_path);
            if (error == ENOENT || error == ENOTDIR) {
                return 0;
            }
            return fdn_host_posix_status(error);
        }
        fdn_dealloc(native_path);
        *result = true;
    }
#endif
    return 0;
}

int32_t foundation_runtime_fs_kind(const fdn_string *path, uint32_t *kind) {
    if (kind == NULL) {
        fdn_panic_cstr("filesystem kind output is null");
    }
    *kind = 0;
#if defined(_WIN32)
    {
        wchar_t *native_path = fdn_host_windows_path(path);
        DWORD attributes;
        if (native_path == NULL) {
            return FDN_FS_INVALID_PATH;
        }
        attributes = GetFileAttributesW(native_path);
        fdn_dealloc(native_path);
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
                return 0;
            }
            return fdn_host_windows_status(error);
        }
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            *kind = 3;
        } else if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            *kind = 2;
        } else {
            *kind = 1;
        }
    }
#else
    {
        char *native_path = fdn_host_posix_path(path);
        struct stat info;
        if (native_path == NULL) {
            return FDN_FS_INVALID_PATH;
        }
        if (lstat(native_path, &info) != 0) {
            const int error = errno;
            fdn_dealloc(native_path);
            if (error == ENOENT || error == ENOTDIR) {
                return 0;
            }
            return fdn_host_posix_status(error);
        }
        fdn_dealloc(native_path);
        if (S_ISREG(info.st_mode)) {
            *kind = 1;
        } else if (S_ISDIR(info.st_mode)) {
            *kind = 2;
        } else if (S_ISLNK(info.st_mode)) {
            *kind = 3;
        } else {
            *kind = 4;
        }
    }
#endif
    return 0;
}

int32_t foundation_runtime_fs_canonicalize(const fdn_string *path,
                                           fdn_string *result) {
    if (result == NULL) {
        fdn_panic_cstr("filesystem canonical path output is null");
    }
    fdn_string_drop(result);
    *result = fdn_string_static("", 0);
#if defined(_WIN32)
    {
        wchar_t *native_path = fdn_host_windows_path(path);
        HANDLE handle;
        DWORD attributes;
        DWORD required;
        DWORD resolved_length;
        wchar_t *resolved;
        const wchar_t *start;
        wchar_t *unc = NULL;
        size_t length;
        int32_t status;
        if (native_path == NULL) {
            return FDN_FS_INVALID_PATH;
        }
        attributes = GetFileAttributesW(native_path);
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            status = fdn_host_windows_status(GetLastError());
            fdn_dealloc(native_path);
            return status;
        }
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            fdn_dealloc(native_path);
            return FDN_FS_INVALID_PATH;
        }
        handle = CreateFileW(native_path, FILE_READ_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        fdn_dealloc(native_path);
        if (handle == INVALID_HANDLE_VALUE) {
            return fdn_host_windows_status(GetLastError());
        }
        required = GetFinalPathNameByHandleW(handle, NULL, 0, FILE_NAME_NORMALIZED);
        if (required == 0) {
            status = fdn_host_windows_status(GetLastError());
            (void)CloseHandle(handle);
            return status;
        }
        resolved = fdn_alloc(((size_t)required + 1) * sizeof(*resolved));
        resolved_length = GetFinalPathNameByHandleW(
            handle, resolved, required + 1, FILE_NAME_NORMALIZED);
        if (resolved_length == 0 || resolved_length >= required + 1) {
            status = fdn_host_windows_status(GetLastError());
            fdn_dealloc(resolved);
            (void)CloseHandle(handle);
            return status;
        }
        if (CloseHandle(handle) == 0) {
            fdn_dealloc(resolved);
            return FDN_FS_IO;
        }
        start = resolved;
        length = (size_t)resolved_length;
        if (length >= 8 && wcsncmp(start, L"\\\\?\\UNC\\", 8) == 0) {
            length -= 6;
            unc = fdn_alloc((length + 1) * sizeof(*unc));
            unc[0] = L'\\';
            unc[1] = L'\\';
            (void)memcpy(unc + 2, start + 8,
                         (length - 2) * sizeof(*unc));
            unc[length] = L'\0';
            start = unc;
        } else if (length >= 4 && wcsncmp(start, L"\\\\?\\", 4) == 0) {
            start += 4;
            length -= 4;
        }
        status = fdn_host_windows_string(start, length, result);
        fdn_dealloc(unc);
        fdn_dealloc(resolved);
        return status;
    }
#else
    {
        char *native_path = fdn_host_posix_path(path);
        struct stat info;
        char *resolved;
        size_t length;
        if (native_path == NULL) {
            return FDN_FS_INVALID_PATH;
        }
        if (lstat(native_path, &info) != 0) {
            const int32_t status = fdn_host_posix_status(errno);
            fdn_dealloc(native_path);
            return status;
        }
        if (S_ISLNK(info.st_mode)) {
            fdn_dealloc(native_path);
            return FDN_FS_INVALID_PATH;
        }
        resolved = realpath(native_path, NULL);
        fdn_dealloc(native_path);
        if (resolved == NULL) {
            return fdn_host_posix_status(errno);
        }
        length = strlen(resolved);
        result->data = fdn_alloc(length);
        if (length != 0) {
            (void)memcpy((void *)result->data, resolved, length);
        }
        result->length = length;
        result->owned = length != 0 ? 1 : 0;
        free(resolved);
        return 0;
    }
#endif
}

static int32_t fdn_host_two_paths(const fdn_string *source,
                                  const fdn_string *destination,
                                  int replace) {
#if defined(_WIN32)
    wchar_t *from = fdn_host_windows_path(source);
    wchar_t *to = fdn_host_windows_path(destination);
    BOOL moved;
    DWORD error;
    if (from == NULL || to == NULL) {
        fdn_dealloc(from);
        fdn_dealloc(to);
        return FDN_FS_INVALID_PATH;
    }
    moved = replace != 0
                ? MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
                : MoveFileW(from, to);
    error = moved != 0 ? ERROR_SUCCESS : GetLastError();
    fdn_dealloc(from);
    fdn_dealloc(to);
    return moved != 0 ? 0 : fdn_host_windows_status(error);
#else
    char *from = fdn_host_posix_path(source);
    char *to = fdn_host_posix_path(destination);
    int status;
    if (from == NULL || to == NULL) {
        fdn_dealloc(from);
        fdn_dealloc(to);
        return FDN_FS_INVALID_PATH;
    }
    if (replace != 0) {
        status = rename(from, to);
        if (status != 0) {
            status = fdn_host_posix_status(errno);
        }
    } else {
#if defined(__linux__) && defined(SYS_renameat2)
        if (syscall(SYS_renameat2, AT_FDCWD, from, AT_FDCWD, to,
                    RENAME_NOREPLACE) == 0) {
            status = 0;
        } else if (errno == ENOSYS || errno == EINVAL || errno == EOPNOTSUPP) {
            status = FDN_FS_UNSUPPORTED;
        } else {
            status = fdn_host_posix_status(errno);
        }
#elif defined(__APPLE__)
        if (renamex_np(from, to, RENAME_EXCL) == 0) {
            status = 0;
        } else if (errno == ENOTSUP || errno == EOPNOTSUPP) {
            status = FDN_FS_UNSUPPORTED;
        } else {
            status = fdn_host_posix_status(errno);
        }
#else
        status = FDN_FS_UNSUPPORTED;
#endif
    }
    fdn_dealloc(from);
    fdn_dealloc(to);
    return status;
#endif
}

int32_t foundation_runtime_fs_rename(const fdn_string *source,
                                     const fdn_string *destination) {
    return fdn_host_two_paths(source, destination, 0);
}

int32_t foundation_runtime_fs_replace(const fdn_string *source,
                                      const fdn_string *destination) {
    return fdn_host_two_paths(source, destination, 1);
}

int32_t foundation_runtime_fs_copy_file(const fdn_string *source,
                                       const fdn_string *destination) {
#if defined(_WIN32)
    wchar_t *from = fdn_host_windows_path(source);
    wchar_t *to = fdn_host_windows_path(destination);
    DWORD attributes;
    BOOL copied;
    DWORD error;
    if (from == NULL || to == NULL) {
        fdn_dealloc(from);
        fdn_dealloc(to);
        return FDN_FS_INVALID_PATH;
    }
    attributes = GetFileAttributesW(from);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const int32_t status = fdn_host_windows_status(GetLastError());
        fdn_dealloc(from);
        fdn_dealloc(to);
        return status;
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        fdn_dealloc(from);
        fdn_dealloc(to);
        return FDN_FS_INVALID_PATH;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        fdn_dealloc(from);
        fdn_dealloc(to);
        return FDN_FS_IS_DIRECTORY;
    }
    copied = CopyFileW(from, to, TRUE);
    error = copied != 0 ? ERROR_SUCCESS : GetLastError();
    fdn_dealloc(from);
    fdn_dealloc(to);
    return copied != 0 ? 0 : fdn_host_windows_status(error);
#else
    char *from = fdn_host_posix_path(source);
    char *to = fdn_host_posix_path(destination);
    int input = -1;
    int output = -1;
    int created = 0;
    struct stat info;
    int32_t status = FDN_FS_IO;
    char buffer[16384];
    if (from == NULL || to == NULL) {
        status = FDN_FS_INVALID_PATH;
        goto cleanup;
    }
    input = open(from, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (input < 0) {
        status = fdn_host_posix_status(errno);
        goto cleanup;
    }
    if (fstat(input, &info) != 0) {
        status = fdn_host_posix_status(errno);
        goto cleanup;
    }
    if (!S_ISREG(info.st_mode)) {
        status = FDN_FS_INVALID_PATH;
        goto cleanup;
    }
    output = open(to, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                  info.st_mode & 0777);
    if (output < 0) {
        status = fdn_host_posix_status(errno);
        goto cleanup;
    }
    created = 1;
    for (;;) {
        const ssize_t count = read(input, buffer, sizeof(buffer));
        size_t offset = 0;
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            status = fdn_host_posix_status(errno);
            goto cleanup;
        }
        if (count == 0) {
            break;
        }
        while (offset < (size_t)count) {
            const ssize_t written = write(output, buffer + offset,
                                          (size_t)count - offset);
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                status = fdn_host_posix_status(errno);
                goto cleanup;
            }
            offset += (size_t)written;
        }
    }
    if (close(output) != 0) {
        output = -1;
        status = fdn_host_posix_status(errno);
        goto cleanup;
    }
    output = -1;
    status = 0;

cleanup:
    if (input >= 0) {
        (void)close(input);
    }
    if (output >= 0) {
        (void)close(output);
    }
    if (status != 0 && created != 0 && to != NULL) {
        (void)unlink(to);
    }
    fdn_dealloc(from);
    fdn_dealloc(to);
    return status;
#endif
}

int32_t foundation_runtime_fs_remove_file(const fdn_string *path) {
#if defined(_WIN32)
    wchar_t *native_path = fdn_host_windows_path(path);
    DWORD attributes;
    int32_t status;
    if (native_path == NULL) {
        return FDN_FS_INVALID_PATH;
    }
    attributes = GetFileAttributesW(native_path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        status = fdn_host_windows_status(GetLastError());
        fdn_dealloc(native_path);
        return status;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        fdn_dealloc(native_path);
        return FDN_FS_IS_DIRECTORY;
    }
    if (DeleteFileW(native_path) == 0) {
        status = fdn_host_windows_status(GetLastError());
        fdn_dealloc(native_path);
        return status;
    }
    fdn_dealloc(native_path);
    return 0;
#else
    char *native_path = fdn_host_posix_path(path);
    struct stat info;
    int32_t status;
    if (native_path == NULL) {
        return FDN_FS_INVALID_PATH;
    }
    if (lstat(native_path, &info) != 0) {
        status = fdn_host_posix_status(errno);
        fdn_dealloc(native_path);
        return status;
    }
    if (S_ISDIR(info.st_mode)) {
        fdn_dealloc(native_path);
        return FDN_FS_IS_DIRECTORY;
    }
    if (unlink(native_path) != 0) {
        status = fdn_host_posix_status(errno);
        fdn_dealloc(native_path);
        return status;
    }
    fdn_dealloc(native_path);
    return 0;
#endif
}

int32_t foundation_runtime_fs_remove_empty_directory(const fdn_string *path) {
#if defined(_WIN32)
    wchar_t *native_path = fdn_host_windows_path(path);
    DWORD attributes;
    int32_t status;
    if (native_path == NULL) {
        return FDN_FS_INVALID_PATH;
    }
    attributes = GetFileAttributesW(native_path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        status = fdn_host_windows_status(GetLastError());
        fdn_dealloc(native_path);
        return status;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        fdn_dealloc(native_path);
        return FDN_FS_NOT_DIRECTORY;
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        fdn_dealloc(native_path);
        return FDN_FS_INVALID_PATH;
    }
    if (RemoveDirectoryW(native_path) == 0) {
        status = fdn_host_windows_status(GetLastError());
        fdn_dealloc(native_path);
        return status;
    }
    fdn_dealloc(native_path);
    return 0;
#else
    char *native_path = fdn_host_posix_path(path);
    struct stat info;
    int32_t status;
    if (native_path == NULL) {
        return FDN_FS_INVALID_PATH;
    }
    if (lstat(native_path, &info) != 0) {
        status = fdn_host_posix_status(errno);
        fdn_dealloc(native_path);
        return status;
    }
    if (!S_ISDIR(info.st_mode)) {
        fdn_dealloc(native_path);
        return FDN_FS_NOT_DIRECTORY;
    }
    if (rmdir(native_path) != 0) {
        status = fdn_host_posix_status(errno);
        fdn_dealloc(native_path);
        return status;
    }
    fdn_dealloc(native_path);
    return 0;
#endif
}

typedef struct fdn_host_remove_bounds {
    uint64_t remaining;
    uint64_t max_depth;
} fdn_host_remove_bounds;

static int32_t fdn_host_remove_visit(fdn_host_remove_bounds *bounds,
                                     uint64_t depth) {
    if (depth == 0 || depth > bounds->max_depth || bounds->remaining == 0) {
        return FDN_FS_TOO_LARGE;
    }
    --bounds->remaining;
    return 0;
}

#if defined(_WIN32)
static HANDLE fdn_host_windows_open_directory(const wchar_t *path) {
    BY_HANDLE_FILE_INFORMATION info;
    HANDLE directory = CreateFileW(
        path, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (directory == INVALID_HANDLE_VALUE) {
        return INVALID_HANDLE_VALUE;
    }
    if (GetFileInformationByHandle(directory, &info) == 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        (void)CloseHandle(directory);
        SetLastError(ERROR_INVALID_NAME);
        return INVALID_HANDLE_VALUE;
    }
    return directory;
}

static wchar_t *fdn_host_windows_child_path(const wchar_t *parent,
                                            const wchar_t *name) {
    const size_t parent_length = wcslen(parent);
    const size_t name_length = wcslen(name);
    const int separator = parent_length != 0 &&
                          parent[parent_length - 1] != L'\\' &&
                          parent[parent_length - 1] != L'/';
    wchar_t *result;
    if (parent_length > SIZE_MAX - name_length - (size_t)separator - 1 ||
        parent_length + name_length + (size_t)separator + 1 >
            SIZE_MAX / sizeof(*result)) {
        return NULL;
    }
    result = fdn_alloc((parent_length + name_length + (size_t)separator + 1) *
                       sizeof(*result));
    (void)memcpy(result, parent, parent_length * sizeof(*result));
    if (separator != 0) {
        result[parent_length] = L'\\';
    }
    (void)memcpy(result + parent_length + (size_t)separator, name,
                 (name_length + 1) * sizeof(*result));
    return result;
}

static int32_t fdn_host_windows_remove_tree(const wchar_t *path,
                                            uint64_t depth,
                                            fdn_host_remove_bounds *bounds) {
    DWORD attributes;
    int32_t status = fdn_host_remove_visit(bounds, depth);
    if (status != 0) {
        return status;
    }
    attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return fdn_host_windows_status(GetLastError());
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        const BOOL removed = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0
                                 ? RemoveDirectoryW(path)
                                 : DeleteFileW(path);
        return removed != 0 ? 0 : fdn_host_windows_status(GetLastError());
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return DeleteFileW(path) != 0
                   ? 0
                   : fdn_host_windows_status(GetLastError());
    }
    {
        HANDLE directory = fdn_host_windows_open_directory(path);
        wchar_t *search_path;
        WIN32_FIND_DATAW entry;
        HANDLE search;
        if (directory == INVALID_HANDLE_VALUE) {
            return fdn_host_windows_status(GetLastError());
        }
        search_path = fdn_host_windows_child_path(path, L"*");
        if (search_path == NULL) {
            (void)CloseHandle(directory);
            return FDN_FS_TOO_LARGE;
        }
        search = FindFirstFileW(search_path, &entry);
        fdn_dealloc(search_path);
        if (search == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND) {
                status = fdn_host_windows_status(error);
            }
        } else {
            for (;;) {
                if (wcscmp(entry.cFileName, L".") != 0 &&
                    wcscmp(entry.cFileName, L"..") != 0) {
                    wchar_t *child = fdn_host_windows_child_path(
                        path, entry.cFileName);
                    if (child == NULL) {
                        status = FDN_FS_TOO_LARGE;
                    } else {
                        status = fdn_host_windows_remove_tree(
                            child, depth + 1, bounds);
                        fdn_dealloc(child);
                    }
                    if (status != 0) {
                        break;
                    }
                }
                if (FindNextFileW(search, &entry) == 0) {
                    const DWORD error = GetLastError();
                    if (error != ERROR_NO_MORE_FILES) {
                        status = fdn_host_windows_status(error);
                    }
                    break;
                }
            }
            if (FindClose(search) == 0 && status == 0) {
                status = fdn_host_windows_status(GetLastError());
            }
        }
        if (CloseHandle(directory) == 0 && status == 0) {
            status = FDN_FS_IO;
        }
        if (status == 0 && RemoveDirectoryW(path) == 0) {
            status = fdn_host_windows_status(GetLastError());
        }
    }
    return status;
}
#else
static int32_t fdn_host_posix_remove_directory(
    int descriptor, uint64_t depth, fdn_host_remove_bounds *bounds) {
    DIR *directory = fdopendir(descriptor);
    struct dirent *entry;
    int32_t status = 0;
    if (directory == NULL) {
        const int error = errno;
        (void)close(descriptor);
        return fdn_host_posix_status(error);
    }
    errno = 0;
    while ((entry = readdir(directory)) != NULL) {
        struct stat info;
        const int parent = dirfd(directory);
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        status = fdn_host_remove_visit(bounds, depth + 1);
        if (status != 0) {
            break;
        }
        if (fstatat(parent, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
            status = fdn_host_posix_status(errno);
            break;
        }
        if (S_ISDIR(info.st_mode)) {
            int child = openat(parent, entry->d_name,
                               O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
            if (child < 0) {
                status = fdn_host_posix_status(errno);
                break;
            }
            status = fdn_host_posix_remove_directory(child, depth + 1, bounds);
            if (status == 0 &&
                unlinkat(parent, entry->d_name, AT_REMOVEDIR) != 0) {
                status = fdn_host_posix_status(errno);
            }
        } else if (unlinkat(parent, entry->d_name, 0) != 0) {
            status = fdn_host_posix_status(errno);
        }
        if (status != 0) {
            break;
        }
        errno = 0;
    }
    if (entry == NULL && errno != 0 && status == 0) {
        status = fdn_host_posix_status(errno);
    }
    if (closedir(directory) != 0 && status == 0) {
        status = fdn_host_posix_status(errno);
    }
    return status;
}
#endif

int32_t foundation_runtime_fs_remove_tree(const fdn_string *path,
                                          uint64_t max_entries,
                                          uint64_t max_depth) {
    fdn_host_remove_bounds bounds;
    if (!fdn_host_path_valid(path) || fdn_host_path_has_dot_component(path)) {
        return FDN_FS_INVALID_PATH;
    }
    if (max_entries == 0 || max_depth == 0 || max_depth > 128) {
        return FDN_FS_TOO_LARGE;
    }
    bounds.remaining = max_entries;
    bounds.max_depth = max_depth;
#if defined(_WIN32)
    {
        wchar_t *native_path = fdn_host_windows_path(path);
        int32_t status;
        if (native_path == NULL) {
            return FDN_FS_INVALID_PATH;
        }
        if (fdn_host_windows_volume_root(native_path)) {
            fdn_dealloc(native_path);
            return FDN_FS_INVALID_PATH;
        }
        status = fdn_host_windows_remove_tree(native_path, 1, &bounds);
        fdn_dealloc(native_path);
        return status;
    }
#else
    {
        char *native_path = fdn_host_posix_path(path);
        struct stat info;
        int32_t status = fdn_host_remove_visit(&bounds, 1);
        size_t length;
        if (native_path == NULL) {
            return FDN_FS_INVALID_PATH;
        }
        length = strlen(native_path);
        while (length > 1 && native_path[length - 1] == '/') {
            native_path[--length] = '\0';
        }
        if (length == 1 && native_path[0] == '/') {
            fdn_dealloc(native_path);
            return FDN_FS_INVALID_PATH;
        }
        if (status == 0 && lstat(native_path, &info) != 0) {
            status = fdn_host_posix_status(errno);
        }
        if (status == 0 && S_ISDIR(info.st_mode)) {
            int descriptor = open(native_path,
                                  O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
            if (descriptor < 0) {
                status = fdn_host_posix_status(errno);
            } else {
                status = fdn_host_posix_remove_directory(descriptor, 1, &bounds);
                if (status == 0 && rmdir(native_path) != 0) {
                    status = fdn_host_posix_status(errno);
                }
            }
        } else if (status == 0 && unlink(native_path) != 0) {
            status = fdn_host_posix_status(errno);
        }
        fdn_dealloc(native_path);
        return status;
    }
#endif
}

#if defined(_WIN32)
static int32_t fdn_host_windows_temp(const fdn_string *parent,
                                     const fdn_string *prefix, int directory,
                                     fdn_string *result) {
    wchar_t *native_parent = fdn_host_windows_path(parent);
    wchar_t *native_prefix = fdn_host_windows_path(prefix);
    unsigned int attempt;
    if (native_parent == NULL || native_prefix == NULL ||
        !fdn_host_prefix_valid(prefix)) {
        fdn_dealloc(native_parent);
        fdn_dealloc(native_prefix);
        return FDN_FS_INVALID_PATH;
    }
    for (attempt = 0; attempt < 128; ++attempt) {
        unsigned char random[16];
        static const wchar_t hex[] = L"0123456789abcdef";
        const size_t parent_length = wcslen(native_parent);
        const size_t prefix_length = wcslen(native_prefix);
        const int separator = parent_length != 0 &&
                              native_parent[parent_length - 1] != L'\\' &&
                              native_parent[parent_length - 1] != L'/';
        const size_t length = parent_length + (size_t)separator + prefix_length + 1 + 32;
        wchar_t *path = fdn_alloc((length + 1) * sizeof(*path));
        size_t offset = 0;
        size_t index;
        HANDLE file = INVALID_HANDLE_VALUE;
        DWORD error;
        if (BCryptGenRandom(NULL, random, sizeof(random),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
            fdn_dealloc(path);
            fdn_dealloc(native_parent);
            fdn_dealloc(native_prefix);
            return FDN_FS_IO;
        }
        (void)memcpy(path + offset, native_parent,
                     parent_length * sizeof(*path));
        offset += parent_length;
        if (separator != 0) {
            path[offset++] = L'\\';
        }
        (void)memcpy(path + offset, native_prefix,
                     prefix_length * sizeof(*path));
        offset += prefix_length;
        path[offset++] = L'-';
        for (index = 0; index < sizeof(random); ++index) {
            path[offset++] = hex[random[index] >> 4U];
            path[offset++] = hex[random[index] & 15U];
        }
        path[offset] = L'\0';
        if (directory != 0) {
            if (CreateDirectoryW(path, NULL) != 0) {
                const int32_t status = fdn_host_windows_string(path, offset, result);
                fdn_dealloc(path);
                fdn_dealloc(native_parent);
                fdn_dealloc(native_prefix);
                return status;
            }
            error = GetLastError();
        } else {
            file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                               CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, NULL);
            if (file != INVALID_HANDLE_VALUE) {
                const int32_t status = CloseHandle(file) != 0
                                           ? fdn_host_windows_string(path, offset, result)
                                           : FDN_FS_IO;
                if (status != 0) {
                    (void)DeleteFileW(path);
                }
                fdn_dealloc(path);
                fdn_dealloc(native_parent);
                fdn_dealloc(native_prefix);
                return status;
            }
            error = GetLastError();
        }
        fdn_dealloc(path);
        if (error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS) {
            fdn_dealloc(native_parent);
            fdn_dealloc(native_prefix);
            return fdn_host_windows_status(error);
        }
    }
    fdn_dealloc(native_parent);
    fdn_dealloc(native_prefix);
    return FDN_FS_IO;
}
#else
static int32_t fdn_host_posix_temp(const fdn_string *parent,
                                   const fdn_string *prefix, int directory,
                                   fdn_string *result) {
    char *native_parent = fdn_host_posix_path(parent);
    char *native_prefix = fdn_host_posix_path(prefix);
    static const char suffix[] = "-XXXXXX";
    size_t parent_length;
    size_t prefix_length;
    int separator;
    char *path;
    size_t length;
    int descriptor = -1;
    if (native_parent == NULL || native_prefix == NULL ||
        !fdn_host_prefix_valid(prefix)) {
        fdn_dealloc(native_parent);
        fdn_dealloc(native_prefix);
        return FDN_FS_INVALID_PATH;
    }
    parent_length = strlen(native_parent);
    prefix_length = strlen(native_prefix);
    separator = parent_length != 0 && native_parent[parent_length - 1] != '/';
    if (parent_length > SIZE_MAX - prefix_length - sizeof(suffix) - 1) {
        fdn_dealloc(native_parent);
        fdn_dealloc(native_prefix);
        return FDN_FS_TOO_LARGE;
    }
    length = parent_length + (size_t)separator + prefix_length + sizeof(suffix);
    path = fdn_alloc(length);
    (void)memcpy(path, native_parent, parent_length);
    if (separator != 0) {
        path[parent_length++] = '/';
    }
    (void)memcpy(path + parent_length, native_prefix, prefix_length);
    (void)memcpy(path + parent_length + prefix_length, suffix, sizeof(suffix));
    if (directory != 0) {
        if (mkdtemp(path) == NULL) {
            const int32_t status = fdn_host_posix_status(errno);
            fdn_dealloc(path);
            fdn_dealloc(native_parent);
            fdn_dealloc(native_prefix);
            return status;
        }
    } else {
        descriptor = mkstemp(path);
        if (descriptor < 0) {
            const int32_t status = fdn_host_posix_status(errno);
            fdn_dealloc(path);
            fdn_dealloc(native_parent);
            fdn_dealloc(native_prefix);
            return status;
        }
        if (close(descriptor) != 0) {
            (void)unlink(path);
            fdn_dealloc(path);
            fdn_dealloc(native_parent);
            fdn_dealloc(native_prefix);
            return FDN_FS_IO;
        }
    }
    fdn_string_drop(result);
    result->length = strlen(path);
    result->data = fdn_alloc(result->length);
    (void)memcpy((void *)result->data, path, result->length);
    result->owned = 1;
    fdn_dealloc(path);
    fdn_dealloc(native_parent);
    fdn_dealloc(native_prefix);
    return 0;
}
#endif

int32_t foundation_runtime_fs_create_temp_directory(const fdn_string *parent,
                                                    const fdn_string *prefix,
                                                    fdn_string *result) {
    if (result == NULL) {
        fdn_panic_cstr("temporary directory output is null");
    }
    fdn_string_drop(result);
    *result = fdn_string_static("", 0);
#if defined(_WIN32)
    return fdn_host_windows_temp(parent, prefix, 1, result);
#else
    return fdn_host_posix_temp(parent, prefix, 1, result);
#endif
}

int32_t foundation_runtime_fs_create_temp_file(const fdn_string *parent,
                                               const fdn_string *prefix,
                                               fdn_string *result) {
    if (result == NULL) {
        fdn_panic_cstr("temporary file output is null");
    }
    fdn_string_drop(result);
    *result = fdn_string_static("", 0);
#if defined(_WIN32)
    return fdn_host_windows_temp(parent, prefix, 0, result);
#else
    return fdn_host_posix_temp(parent, prefix, 0, result);
#endif
}

int32_t foundation_runtime_fs_is_executable(const fdn_string *path,
                                            bool *result) {
    if (result == NULL) {
        fdn_panic_cstr("filesystem executable output is null");
    }
    *result = false;
#if defined(_WIN32)
    {
        uint32_t kind = 0;
        const int32_t status = foundation_runtime_fs_kind(path, &kind);
        return status != 0 ? status : kind == 1 ? 0 : FDN_FS_INVALID_PATH;
    }
#else
    {
        char *native_path = fdn_host_posix_path(path);
        struct stat info;
        if (native_path == NULL) {
            return FDN_FS_INVALID_PATH;
        }
        if (lstat(native_path, &info) != 0) {
            const int32_t status = fdn_host_posix_status(errno);
            fdn_dealloc(native_path);
            return status;
        }
        fdn_dealloc(native_path);
        if (!S_ISREG(info.st_mode)) {
            return FDN_FS_INVALID_PATH;
        }
        *result = (info.st_mode & 0111) != 0;
        return 0;
    }
#endif
}

int32_t foundation_runtime_fs_set_executable(const fdn_string *path,
                                             bool executable) {
#if defined(_WIN32)
    bool ignored = false;
    const int32_t status = foundation_runtime_fs_is_executable(path, &ignored);
    (void)executable;
    return status != 0 ? status : FDN_FS_UNSUPPORTED;
#else
    char *native_path = fdn_host_posix_path(path);
    struct stat info;
    mode_t mode;
    int32_t status;
    if (native_path == NULL) {
        return FDN_FS_INVALID_PATH;
    }
    if (lstat(native_path, &info) != 0) {
        status = fdn_host_posix_status(errno);
        fdn_dealloc(native_path);
        return status;
    }
    if (!S_ISREG(info.st_mode)) {
        fdn_dealloc(native_path);
        return FDN_FS_INVALID_PATH;
    }
    mode = info.st_mode;
    if (executable) {
        mode |= (mode & 0444) >> 2;
    } else {
        mode &= ~((mode_t)0111);
    }
    if (chmod(native_path, mode) != 0) {
        status = fdn_host_posix_status(errno);
        fdn_dealloc(native_path);
        return status;
    }
    fdn_dealloc(native_path);
    return 0;
#endif
}
