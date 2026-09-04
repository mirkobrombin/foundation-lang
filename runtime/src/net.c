#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "foundation/runtime.h"
#include "bytes_internal.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wchar.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

_Static_assert(sizeof(uintptr_t) <= sizeof(uint64_t),
               "network handles require a 64-bit carrier");

enum {
    FDN_NET_OK = 0,
    FDN_NET_INVALID_ADDRESS = 1,
    FDN_NET_RESOLVE_FAILED = 2,
    FDN_NET_REFUSED = 3,
    FDN_NET_CLOSED = 4,
    FDN_NET_CANCELLED = 5,
    FDN_NET_INVALID_UTF8 = 6,
    FDN_NET_LINE_TOO_LONG = 7,
    FDN_NET_IO = 8,
    FDN_NET_EOF = 9,
    FDN_NET_ADDRESS_IN_USE = 10,
    FDN_NET_TIMEOUT = 11,
    FDN_NET_INVALID_LIMIT = 12,
    FDN_NET_PENDING = -1,
};

typedef struct fdn_net_address {
    struct sockaddr_storage value;
    int length;
} fdn_net_address;

typedef struct fdn_net_addresses {
    fdn_net_address *items;
    size_t count;
} fdn_net_addresses;

#if defined(_WIN32)
typedef SOCKET fdn_net_socket;
typedef WSAPOLLFD fdn_net_pollfd;
#define FDN_NET_INVALID_SOCKET INVALID_SOCKET
#define FDN_NET_POLL_READ POLLRDNORM
#define FDN_NET_POLL_WRITE POLLWRNORM
#else
typedef int fdn_net_socket;
typedef struct pollfd fdn_net_pollfd;
#define FDN_NET_INVALID_SOCKET (-1)
#define FDN_NET_POLL_READ POLLIN
#define FDN_NET_POLL_WRITE POLLOUT
#endif

typedef struct fdn_net_connection {
    fdn_net_socket socket;
    char *read_data;
    size_t read_length;
    size_t read_capacity;
    size_t references;
    bool full_open;
    bool read_open;
    bool write_open;
    bool read_eof;
#if defined(_WIN32)
    CRITICAL_SECTION lock;
#else
    pthread_mutex_t lock;
#endif
} fdn_net_connection;

typedef struct fdn_net_listener {
    fdn_net_socket socket;
    size_t references;
#if defined(_WIN32)
    CRITICAL_SECTION lock;
#else
    pthread_mutex_t lock;
#endif
} fdn_net_listener;

typedef enum fdn_net_request_kind {
    FDN_NET_REQUEST_ACCEPT,
    FDN_NET_REQUEST_CONNECT,
    FDN_NET_REQUEST_READ_LINE,
    FDN_NET_REQUEST_WRITE_ALL,
} fdn_net_request_kind;

typedef enum fdn_net_read_mode {
    FDN_NET_READ_LINE,
    FDN_NET_READ_EXACT_TEXT,
    FDN_NET_READ_EXACT_BYTES,
    FDN_NET_READ_SOME_BYTES,
} fdn_net_read_mode;

typedef struct fdn_net_request {
    struct fdn_net_request *next;
    fdn_reactor_operation *operation;
    fdn_net_request_kind kind;
    fdn_net_socket socket;
    uint64_t deadline;
    bool cancelled;
    bool closed;
    union {
        struct {
            fdn_net_listener *listener;
            uint64_t *result;
        } accept;
        struct {
            fdn_net_addresses *addresses;
            size_t index;
            uint64_t *result;
            bool refused;
        } connect;
        struct {
            fdn_net_connection *connection;
            size_t limit;
            fdn_string *text_result;
            uint64_t *bytes_result;
            fdn_net_read_mode mode;
        } read;
        struct {
            fdn_net_connection *connection;
            const char *data;
            size_t length;
            size_t offset;
        } write;
    } value;
} fdn_net_request;

typedef struct fdn_net_service {
    fdn_net_request *requests;
#if defined(_WIN32)
    CRITICAL_SECTION lock;
    SOCKET wake_read;
    SOCKET wake_write;
#else
    pthread_mutex_t lock;
    int wake_read;
    int wake_write;
#endif
} fdn_net_service;

#if defined(_WIN32)
static SRWLOCK fdn_net_global_lock = SRWLOCK_INIT;
static volatile LONG64 fdn_net_addresses_count;
static volatile LONG64 fdn_net_listeners_count;
static volatile LONG64 fdn_net_connections_count;
static volatile LONG64 fdn_net_requests_count;
static volatile LONG64 fdn_net_services_count;
static bool fdn_net_winsock_started;
#else
static pthread_mutex_t fdn_net_global_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_uint_fast64_t fdn_net_addresses_count;
static atomic_uint_fast64_t fdn_net_listeners_count;
static atomic_uint_fast64_t fdn_net_connections_count;
static atomic_uint_fast64_t fdn_net_requests_count;
static atomic_uint_fast64_t fdn_net_services_count;
#endif

static fdn_net_service *fdn_net_current_service;

static void fdn_net_service_signal(fdn_net_service* service);

static void fdn_net_global_enter(void) {
#if defined(_WIN32)
    AcquireSRWLockExclusive(&fdn_net_global_lock);
#else
    if (pthread_mutex_lock(&fdn_net_global_lock) != 0) {
        fdn_panic_cstr("network global lock failed");
    }
#endif
}

static void fdn_net_global_leave(void) {
#if defined(_WIN32)
    ReleaseSRWLockExclusive(&fdn_net_global_lock);
#else
    if (pthread_mutex_unlock(&fdn_net_global_lock) != 0) {
        fdn_panic_cstr("network global unlock failed");
    }
#endif
}

static void fdn_net_service_enter(fdn_net_service *service) {
#if defined(_WIN32)
    EnterCriticalSection(&service->lock);
#else
    if (pthread_mutex_lock(&service->lock) != 0) {
        fdn_panic_cstr("network service lock failed");
    }
#endif
}

static void fdn_net_service_leave(fdn_net_service *service) {
#if defined(_WIN32)
    LeaveCriticalSection(&service->lock);
#else
    if (pthread_mutex_unlock(&service->lock) != 0) {
        fdn_panic_cstr("network service unlock failed");
    }
#endif
}

static void fdn_net_connection_enter(fdn_net_connection *connection) {
#if defined(_WIN32)
    EnterCriticalSection(&connection->lock);
#else
    if (pthread_mutex_lock(&connection->lock) != 0) {
        fdn_panic_cstr("network connection lock failed");
    }
#endif
}

static void fdn_net_connection_leave(fdn_net_connection *connection) {
#if defined(_WIN32)
    LeaveCriticalSection(&connection->lock);
#else
    if (pthread_mutex_unlock(&connection->lock) != 0) {
        fdn_panic_cstr("network connection unlock failed");
    }
#endif
}

static void fdn_net_listener_enter(fdn_net_listener* listener) {
#if defined(_WIN32)
    EnterCriticalSection(&listener->lock);
#else
    if (pthread_mutex_lock(&listener->lock) != 0) {
        fdn_panic_cstr("network listener lock failed");
    }
#endif
}

static void fdn_net_listener_leave(fdn_net_listener* listener) {
#if defined(_WIN32)
    LeaveCriticalSection(&listener->lock);
#else
    if (pthread_mutex_unlock(&listener->lock) != 0) {
        fdn_panic_cstr("network listener unlock failed");
    }
#endif
}

static void fdn_net_count_add(
#if defined(_WIN32)
    volatile LONG64 *counter
#else
    atomic_uint_fast64_t *counter
#endif
) {
#if defined(_WIN32)
    if (InterlockedIncrement64(counter) <= 0) {
        fdn_panic_cstr("network live counter overflow");
    }
#else
    if (atomic_fetch_add_explicit(counter, UINT64_C(1), memory_order_relaxed) ==
        UINT64_MAX) {
        fdn_panic_cstr("network live counter overflow");
    }
#endif
}

static void fdn_net_count_remove(
#if defined(_WIN32)
    volatile LONG64 *counter
#else
    atomic_uint_fast64_t *counter
#endif
) {
#if defined(_WIN32)
    if (InterlockedDecrement64(counter) < 0) {
        fdn_panic_cstr("network live counter underflow");
    }
#else
    if (atomic_fetch_sub_explicit(counter, UINT64_C(1), memory_order_relaxed) == 0) {
        fdn_panic_cstr("network live counter underflow");
    }
#endif
}

static uint64_t fdn_net_count_read(
#if defined(_WIN32)
    volatile LONG64 *counter
#else
    atomic_uint_fast64_t *counter
#endif
) {
#if defined(_WIN32)
    return (uint64_t)InterlockedCompareExchange64(counter, 0, 0);
#else
    return atomic_load_explicit(counter, memory_order_relaxed);
#endif
}

static bool fdn_net_platform_start(void) {
#if defined(_WIN32)
    WSADATA data;
    if (!fdn_net_winsock_started) {
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            return false;
        }
        fdn_net_winsock_started = true;
    }
#endif
    return true;
}

static void fdn_net_close_socket(fdn_net_socket socket) {
    if (socket == FDN_NET_INVALID_SOCKET) {
        return;
    }
#if defined(_WIN32)
    (void)closesocket(socket);
#else
    (void)close(socket);
#endif
}

static int fdn_net_last_error(void) {
#if defined(_WIN32)
    return WSAGetLastError();
#else
    return errno;
#endif
}

static bool fdn_net_would_block(int error) {
#if defined(_WIN32)
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS ||
           error == WSAEALREADY;
#else
    return error == EAGAIN || error == EWOULDBLOCK || error == EINPROGRESS ||
           error == EALREADY;
#endif
}

static bool fdn_net_interrupted(int error) {
#if defined(_WIN32)
    return error == WSAEINTR;
#else
    return error == EINTR;
#endif
}

static bool fdn_net_refused(int error) {
#if defined(_WIN32)
    return error == WSAECONNREFUSED;
#else
    return error == ECONNREFUSED;
#endif
}

static bool fdn_net_address_in_use(int error) {
#if defined(_WIN32)
    return error == WSAEADDRINUSE;
#else
    return error == EADDRINUSE;
#endif
}

static bool fdn_net_closed_error(int error) {
#if defined(_WIN32)
    return error == WSAECONNRESET || error == WSAECONNABORTED ||
           error == WSAENOTCONN || error == WSAESHUTDOWN;
#else
    return error == ECONNRESET || error == ECONNABORTED || error == ENOTCONN ||
           error == EPIPE;
#endif
}

static bool fdn_net_nonblocking(fdn_net_socket socket) {
#if defined(_WIN32)
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) != 0) {
        return false;
    }
#if defined(SO_NOSIGPIPE)
    {
        const int enabled = 1;
        if (setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled,
                       (socklen_t)sizeof(enabled)) != 0) {
            return false;
        }
    }
#endif
    return true;
#endif
}

#if !defined(_WIN32)
static int fdn_net_send_flags(void) {
#if defined(MSG_NOSIGNAL)
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
}
#endif

static bool fdn_net_host_valid(const fdn_string *host) {
    size_t offset;
    if (host == NULL || host->data == NULL || host->length == 0 ||
        !fdn_utf8_valid(host->data, host->length)) {
        return false;
    }
    for (offset = 0; offset < host->length; ++offset) {
        if (host->data[offset] == '\0') {
            return false;
        }
    }
    return true;
}

static void fdn_net_addresses_destroy(fdn_net_addresses *addresses) {
    if (addresses == NULL) {
        return;
    }
    fdn_dealloc(addresses->items);
    fdn_dealloc(addresses);
    fdn_net_count_remove(&fdn_net_addresses_count);
}

int32_t foundation_runtime_net_resolve(const fdn_string *host, uint64_t port,
                                       uint64_t *handle) {
    fdn_net_addresses *addresses;
    size_t count = 0;
    size_t index = 0;
    if (handle == NULL) {
        fdn_panic_cstr("network address output is null");
    }
    *handle = 0;
    if (!fdn_net_host_valid(host) || port == 0 || port > UINT64_C(65535)) {
        return FDN_NET_INVALID_ADDRESS;
    }
    fdn_net_global_enter();
    const bool platform_ready = fdn_net_platform_start();
    fdn_net_global_leave();
    if (!platform_ready) {
        return FDN_NET_IO;
    }
#if defined(_WIN32)
    {
        ADDRINFOW hints;
        ADDRINFOW *result = NULL;
        ADDRINFOW *item;
        wchar_t *wide_host;
        wchar_t service[6];
        int wide_length;
        if (host->length > (size_t)INT_MAX) {
            return FDN_NET_INVALID_ADDRESS;
        }
        wide_length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, host->data,
                                          (int)host->length, NULL, 0);
        if (wide_length <= 0) {
            return FDN_NET_INVALID_ADDRESS;
        }
        wide_host = fdn_alloc(((size_t)wide_length + 1) * sizeof(*wide_host));
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, host->data,
                                (int)host->length, wide_host, wide_length) != wide_length) {
            fdn_dealloc(wide_host);
            return FDN_NET_INVALID_ADDRESS;
        }
        wide_host[wide_length] = L'\0';
        {
            unsigned int value = (unsigned int)port;
            size_t digits = 0;
            size_t left;
            do {
                service[digits++] = (wchar_t)(L'0' + value % 10U);
                value /= 10U;
            } while (value != 0U);
            service[digits] = L'\0';
            for (left = 0; left < digits / 2; ++left) {
                const wchar_t digit = service[left];
                service[left] = service[digits - left - 1];
                service[digits - left - 1] = digit;
            }
        }
        (void)memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        if (GetAddrInfoW(wide_host, service, &hints, &result) != 0) {
            fdn_dealloc(wide_host);
            return FDN_NET_RESOLVE_FAILED;
        }
        fdn_dealloc(wide_host);
        for (item = result; item != NULL; item = item->ai_next) {
            if (item->ai_addr != NULL && item->ai_addrlen <= sizeof(struct sockaddr_storage) &&
                item->ai_addrlen <= INT_MAX) {
                ++count;
            }
        }
        if (count == 0) {
            FreeAddrInfoW(result);
            return FDN_NET_RESOLVE_FAILED;
        }
        if (count > SIZE_MAX / sizeof(*addresses->items)) {
            FreeAddrInfoW(result);
            return FDN_NET_IO;
        }
        addresses = fdn_alloc(sizeof(*addresses));
        addresses->items = fdn_alloc(count * sizeof(*addresses->items));
        addresses->count = count;
        for (item = result; item != NULL; item = item->ai_next) {
            if (item->ai_addr == NULL || item->ai_addrlen > sizeof(struct sockaddr_storage) ||
                item->ai_addrlen > INT_MAX) {
                continue;
            }
            (void)memset(&addresses->items[index].value, 0,
                         sizeof(addresses->items[index].value));
            (void)memcpy(&addresses->items[index].value, item->ai_addr,
                         item->ai_addrlen);
            addresses->items[index].length = (int)item->ai_addrlen;
            ++index;
        }
        FreeAddrInfoW(result);
    }
#else
    {
        struct addrinfo hints;
        struct addrinfo *result = NULL;
        struct addrinfo *item;
        char *native_host;
        char service[6];
        if (host->length == SIZE_MAX) {
            return FDN_NET_INVALID_ADDRESS;
        }
        native_host = fdn_alloc(host->length + 1);
        (void)memcpy(native_host, host->data, host->length);
        native_host[host->length] = '\0';
        if (snprintf(service, sizeof(service), "%u", (unsigned int)port) <= 0) {
            fdn_dealloc(native_host);
            return FDN_NET_INVALID_ADDRESS;
        }
        (void)memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        if (getaddrinfo(native_host, service, &hints, &result) != 0) {
            fdn_dealloc(native_host);
            return FDN_NET_RESOLVE_FAILED;
        }
        fdn_dealloc(native_host);
        for (item = result; item != NULL; item = item->ai_next) {
            if (item->ai_addr != NULL && item->ai_addrlen <= sizeof(struct sockaddr_storage) &&
                item->ai_addrlen <= (socklen_t)INT_MAX) {
                ++count;
            }
        }
        if (count == 0) {
            freeaddrinfo(result);
            return FDN_NET_RESOLVE_FAILED;
        }
        if (count > SIZE_MAX / sizeof(*addresses->items)) {
            freeaddrinfo(result);
            return FDN_NET_IO;
        }
        addresses = fdn_alloc(sizeof(*addresses));
        addresses->items = fdn_alloc(count * sizeof(*addresses->items));
        addresses->count = count;
        for (item = result; item != NULL; item = item->ai_next) {
            if (item->ai_addr == NULL ||
                item->ai_addrlen > sizeof(struct sockaddr_storage) ||
                item->ai_addrlen > (socklen_t)INT_MAX) {
                continue;
            }
            (void)memset(&addresses->items[index].value, 0,
                         sizeof(addresses->items[index].value));
            (void)memcpy(&addresses->items[index].value, item->ai_addr,
                         item->ai_addrlen);
            addresses->items[index].length = (int)item->ai_addrlen;
            ++index;
        }
        freeaddrinfo(result);
    }
#endif
    fdn_net_count_add(&fdn_net_addresses_count);
    *handle = (uint64_t)(uintptr_t)addresses;
    return FDN_NET_OK;
}

void foundation_runtime_net_addresses_close(uint64_t handle) {
    fdn_net_addresses_destroy((fdn_net_addresses *)(uintptr_t)handle);
}

static bool fdn_net_bind_address_valid(const fdn_string *address) {
    size_t offset;
    if (address == NULL ||
        (address->length != 0 && address->data == NULL) ||
        !fdn_utf8_valid(address->data, address->length)) {
        return false;
    }
    for (offset = 0; offset < address->length; ++offset) {
        if (address->data[offset] == '\0') {
            return false;
        }
    }
    return true;
}

int32_t foundation_runtime_net_listen(const fdn_string *address, uint64_t port,
                                      uint64_t backlog, uint64_t *handle,
                                      uint64_t *bound_port) {
    fdn_net_socket native_socket = FDN_NET_INVALID_SOCKET;
    int family = AF_INET;
    struct sockaddr_storage storage;
    int length;
    int error;
    fdn_net_listener *listener;
    if (handle == NULL || bound_port == NULL) {
        fdn_panic_cstr("network listener output is null");
    }
    *handle = 0;
    *bound_port = 0;
    if (!fdn_net_bind_address_valid(address) || port > UINT64_C(65535) ||
        backlog == 0 || backlog > (uint64_t)INT_MAX) {
        return FDN_NET_INVALID_ADDRESS;
    }
    fdn_net_global_enter();
    const bool platform_ready = fdn_net_platform_start();
    fdn_net_global_leave();
    if (!platform_ready) {
        return FDN_NET_IO;
    }
    (void)memset(&storage, 0, sizeof(storage));
    if (address->length == 0) {
        struct sockaddr_in *value = (struct sockaddr_in *)&storage;
        value->sin_family = AF_INET;
        value->sin_addr.s_addr = htonl(INADDR_ANY);
        value->sin_port = htons((uint16_t)port);
        length = (int)sizeof(*value);
    } else {
#if defined(_WIN32)
        wchar_t *wide_address;
        int wide_length;
        if (address->length > (size_t)INT_MAX) {
            return FDN_NET_INVALID_ADDRESS;
        }
        wide_length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                          address->data, (int)address->length,
                                          NULL, 0);
        if (wide_length <= 0) {
            return FDN_NET_INVALID_ADDRESS;
        }
        wide_address = fdn_alloc(((size_t)wide_length + 1) * sizeof(*wide_address));
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, address->data,
                                (int)address->length, wide_address,
                                wide_length) != wide_length) {
            fdn_dealloc(wide_address);
            return FDN_NET_INVALID_ADDRESS;
        }
        wide_address[wide_length] = L'\0';
        {
            struct sockaddr_in *value = (struct sockaddr_in *)&storage;
            value->sin_family = AF_INET;
            value->sin_port = htons((uint16_t)port);
            if (InetPtonW(AF_INET, wide_address, &value->sin_addr) == 1) {
                length = (int)sizeof(*value);
            } else {
                struct sockaddr_in6 *value6 = (struct sockaddr_in6 *)&storage;
                (void)memset(&storage, 0, sizeof(storage));
                value6->sin6_family = AF_INET6;
                value6->sin6_port = htons((uint16_t)port);
                if (InetPtonW(AF_INET6, wide_address, &value6->sin6_addr) != 1) {
                    fdn_dealloc(wide_address);
                    return FDN_NET_INVALID_ADDRESS;
                }
                family = AF_INET6;
                length = (int)sizeof(*value6);
            }
        }
        fdn_dealloc(wide_address);
#else
        char *native_address;
        if (address->length == SIZE_MAX) {
            return FDN_NET_INVALID_ADDRESS;
        }
        native_address = fdn_alloc(address->length + 1);
        (void)memcpy(native_address, address->data, address->length);
        native_address[address->length] = '\0';
        {
            struct sockaddr_in *value = (struct sockaddr_in *)&storage;
            value->sin_family = AF_INET;
            value->sin_port = htons((uint16_t)port);
            if (inet_pton(AF_INET, native_address, &value->sin_addr) == 1) {
                length = (int)sizeof(*value);
            } else {
                struct sockaddr_in6 *value6 = (struct sockaddr_in6 *)&storage;
                (void)memset(&storage, 0, sizeof(storage));
                value6->sin6_family = AF_INET6;
                value6->sin6_port = htons((uint16_t)port);
                if (inet_pton(AF_INET6, native_address, &value6->sin6_addr) != 1) {
                    fdn_dealloc(native_address);
                    return FDN_NET_INVALID_ADDRESS;
                }
                family = AF_INET6;
                length = (int)sizeof(*value6);
            }
        }
        fdn_dealloc(native_address);
#endif
    }
    native_socket = (fdn_net_socket)socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (native_socket == FDN_NET_INVALID_SOCKET) {
        return FDN_NET_IO;
    }
#if !defined(_WIN32)
    {
        const int enabled = 1;
        if (setsockopt(native_socket, SOL_SOCKET, SO_REUSEADDR, &enabled,
                       (socklen_t)sizeof(enabled)) != 0) {
            fdn_net_close_socket(native_socket);
            return FDN_NET_IO;
        }
    }
#endif
#if defined(_WIN32)
    if (bind(native_socket, (const struct sockaddr *)&storage, length) != 0) {
#else
    if (bind(native_socket, (const struct sockaddr *)&storage,
             (socklen_t)length) != 0) {
#endif
        error = fdn_net_last_error();
        fdn_net_close_socket(native_socket);
        return fdn_net_address_in_use(error) ? FDN_NET_ADDRESS_IN_USE
                                             : FDN_NET_IO;
    }
    if (listen(native_socket, (int)backlog) != 0 ||
        !fdn_net_nonblocking(native_socket)) {
        fdn_net_close_socket(native_socket);
        return FDN_NET_IO;
    }
    (void)memset(&storage, 0, sizeof(storage));
#if defined(_WIN32)
    length = (int)sizeof(storage);
    if (getsockname(native_socket, (struct sockaddr *)&storage, &length) != 0) {
#else
    {
        socklen_t native_length = (socklen_t)sizeof(storage);
        if (getsockname(native_socket, (struct sockaddr *)&storage,
                        &native_length) != 0) {
#endif
            fdn_net_close_socket(native_socket);
            return FDN_NET_IO;
        }
#if !defined(_WIN32)
    }
#endif
    if (storage.ss_family == AF_INET) {
        *bound_port = (uint64_t)ntohs(((struct sockaddr_in *)&storage)->sin_port);
    } else if (storage.ss_family == AF_INET6) {
        *bound_port = (uint64_t)ntohs(((struct sockaddr_in6 *)&storage)->sin6_port);
    } else {
        fdn_net_close_socket(native_socket);
        return FDN_NET_IO;
    }
    listener = fdn_alloc(sizeof(*listener));
    listener->socket = native_socket;
    listener->references = 1;
#if defined(_WIN32)
    InitializeCriticalSection(&listener->lock);
#else
    if (pthread_mutex_init(&listener->lock, NULL) != 0) {
        fdn_dealloc(listener);
        fdn_net_close_socket(native_socket);
        fdn_panic_cstr("network listener initialization failed");
    }
#endif
    fdn_net_count_add(&fdn_net_listeners_count);
    *handle = (uint64_t)(uintptr_t)listener;
    return FDN_NET_OK;
}

static void fdn_net_listener_destroy(fdn_net_listener *listener) {
    if (listener->socket != FDN_NET_INVALID_SOCKET) {
        fdn_net_close_socket(listener->socket);
    }
#if defined(_WIN32)
    DeleteCriticalSection(&listener->lock);
#else
    if (pthread_mutex_destroy(&listener->lock) != 0) {
        fdn_panic_cstr("network listener destroy failed");
    }
#endif
    fdn_dealloc(listener);
    fdn_net_count_remove(&fdn_net_listeners_count);
}

static void fdn_net_listener_release(fdn_net_listener *listener) {
    bool destroy;
    fdn_net_listener_enter(listener);
    if (listener->references == 0) {
        fdn_net_listener_leave(listener);
        fdn_panic_cstr("network listener reference underflow");
    }
    --listener->references;
    destroy = listener->references == 0;
    fdn_net_listener_leave(listener);
    if (destroy) {
        fdn_net_listener_destroy(listener);
    }
}

static void fdn_net_listener_stop_accepts(fdn_net_listener *listener) {
    fdn_net_service *service;
    fdn_net_request *request;
    fdn_net_global_enter();
    service = fdn_net_current_service;
    if (service != NULL) {
        fdn_net_service_enter(service);
        for (request = service->requests; request != NULL; request = request->next) {
            if (request->kind == FDN_NET_REQUEST_ACCEPT &&
                request->value.accept.listener == listener) {
                request->closed = true;
            }
        }
        fdn_net_service_signal(service);
        fdn_net_service_leave(service);
    }
    fdn_net_global_leave();
}

static void fdn_net_listener_close(fdn_net_listener *listener) {
    fdn_net_socket socket = FDN_NET_INVALID_SOCKET;
    fdn_net_listener_enter(listener);
    if (listener->socket != FDN_NET_INVALID_SOCKET) {
        socket = listener->socket;
        listener->socket = FDN_NET_INVALID_SOCKET;
    }
    fdn_net_listener_leave(listener);
    if (socket != FDN_NET_INVALID_SOCKET) {
        fdn_net_close_socket(socket);
        fdn_net_listener_stop_accepts(listener);
    }
}

int32_t foundation_runtime_net_listener_control(uint64_t handle,
                                                uint64_t *controller) {
    fdn_net_listener *listener = (fdn_net_listener *)(uintptr_t)handle;
    if (controller == NULL) {
        fdn_panic_cstr("network listener controller output is null");
    }
    *controller = 0;
    if (listener == NULL) {
        return FDN_NET_CLOSED;
    }
    fdn_net_listener_enter(listener);
    if (listener->socket == FDN_NET_INVALID_SOCKET) {
        fdn_net_listener_leave(listener);
        return FDN_NET_CLOSED;
    }
    if (listener->references == SIZE_MAX) {
        fdn_net_listener_leave(listener);
        fdn_panic_cstr("network listener reference overflow");
    }
    ++listener->references;
    fdn_net_listener_leave(listener);
    *controller = handle;
    return FDN_NET_OK;
}

void foundation_runtime_net_listener_close(uint64_t handle) {
    fdn_net_listener* listener = (fdn_net_listener*)(uintptr_t)handle;
    if (listener != NULL) {
        fdn_net_listener_close(listener);
        fdn_net_listener_release(listener);
    }
}

void foundation_runtime_net_listener_controller_close(uint64_t handle) {
    fdn_net_listener* listener = (fdn_net_listener*)(uintptr_t)handle;
    if (listener != NULL) {
        fdn_net_listener_close(listener);
        fdn_net_listener_release(listener);
    }
}

void foundation_runtime_net_listener_controller_release(uint64_t handle) {
    fdn_net_listener* listener = (fdn_net_listener*)(uintptr_t)handle;
    if (listener != NULL) {
        fdn_net_listener_release(listener);
    }
}

static fdn_net_connection *fdn_net_connection_open(fdn_net_socket socket) {
    fdn_net_connection *connection = fdn_alloc(sizeof(*connection));
    connection->socket = socket;
    connection->read_data = NULL;
    connection->read_length = 0;
    connection->read_capacity = 0;
    connection->references = 1;
    connection->full_open = true;
    connection->read_open = true;
    connection->write_open = true;
    connection->read_eof = false;
#if defined(_WIN32)
    InitializeCriticalSection(&connection->lock);
#else
    if (pthread_mutex_init(&connection->lock, NULL) != 0) {
        fdn_dealloc(connection);
        fdn_net_close_socket(socket);
        fdn_panic_cstr("network connection initialization failed");
    }
#endif
    fdn_net_count_add(&fdn_net_connections_count);
    return connection;
}

static void fdn_net_connection_destroy(fdn_net_connection *connection) {
    if (connection->socket != FDN_NET_INVALID_SOCKET) {
        fdn_net_close_socket(connection->socket);
    }
    fdn_dealloc(connection->read_data);
#if defined(_WIN32)
    DeleteCriticalSection(&connection->lock);
#else
    if (pthread_mutex_destroy(&connection->lock) != 0) {
        fdn_panic_cstr("network connection destroy failed");
    }
#endif
    fdn_dealloc(connection);
    fdn_net_count_remove(&fdn_net_connections_count);
}

static void fdn_net_connection_release(fdn_net_connection *connection) {
    bool destroy;
    fdn_net_connection_enter(connection);
    if (connection->references == 0) {
        fdn_net_connection_leave(connection);
        fdn_panic_cstr("network connection reference underflow");
    }
    --connection->references;
    destroy = connection->references == 0;
    fdn_net_connection_leave(connection);
    if (destroy) {
        fdn_net_connection_destroy(connection);
    }
}

static void fdn_net_shutdown(fdn_net_socket socket, int direction) {
    if (socket == FDN_NET_INVALID_SOCKET) {
        return;
    }
#if defined(_WIN32)
    (void)shutdown(socket, direction == 0 ? SD_RECEIVE : SD_SEND);
#else
    (void)shutdown(socket, direction == 0 ? SHUT_RD : SHUT_WR);
#endif
}

static void fdn_net_close_half(fdn_net_connection *connection, bool reader) {
    bool shutdown_half = false;
    bool close_socket = false;
    fdn_net_socket socket;
    fdn_net_connection_enter(connection);
    socket = connection->socket;
    if (reader) {
        shutdown_half = connection->read_open;
        connection->read_open = false;
    } else {
        shutdown_half = connection->write_open;
        connection->write_open = false;
    }
    if (!connection->read_open && !connection->write_open &&
        connection->socket != FDN_NET_INVALID_SOCKET) {
        connection->socket = FDN_NET_INVALID_SOCKET;
        close_socket = true;
    }
    fdn_net_connection_leave(connection);
    if (close_socket) {
        fdn_net_close_socket(socket);
    } else if (shutdown_half) {
        fdn_net_shutdown(socket, reader ? 0 : 1);
    }
}

int32_t foundation_runtime_net_split(uint64_t handle, uint64_t *reader,
                                     uint64_t *writer) {
    fdn_net_connection *connection = (fdn_net_connection *)(uintptr_t)handle;
    if (reader == NULL || writer == NULL) {
        fdn_panic_cstr("network split output is null");
    }
    *reader = 0;
    *writer = 0;
    if (connection == NULL) {
        return FDN_NET_CLOSED;
    }
    fdn_net_connection_enter(connection);
    if (!connection->full_open || connection->socket == FDN_NET_INVALID_SOCKET) {
        fdn_net_connection_leave(connection);
        return FDN_NET_CLOSED;
    }
    connection->full_open = false;
    connection->references = 3;
    fdn_net_connection_leave(connection);
    *reader = handle;
    *writer = handle;
    return FDN_NET_OK;
}

int32_t foundation_runtime_net_split_controlled(uint64_t handle,
                                                uint64_t *reader,
                                                uint64_t *writer,
                                                uint64_t *controller) {
    fdn_net_connection *connection = (fdn_net_connection *)(uintptr_t)handle;
    if (reader == NULL || writer == NULL || controller == NULL) {
        fdn_panic_cstr("network controlled split output is null");
    }
    *reader = 0;
    *writer = 0;
    *controller = 0;
    if (connection == NULL) {
        return FDN_NET_CLOSED;
    }
    fdn_net_connection_enter(connection);
    if (!connection->full_open || connection->socket == FDN_NET_INVALID_SOCKET) {
        fdn_net_connection_leave(connection);
        return FDN_NET_CLOSED;
    }
    connection->full_open = false;
    connection->references = 4;
    fdn_net_connection_leave(connection);
    *reader = handle;
    *writer = handle;
    *controller = handle;
    return FDN_NET_OK;
}

int32_t foundation_runtime_net_peer_address(uint64_t handle,
                                            fdn_string *address) {
    fdn_net_connection *connection = (fdn_net_connection *)(uintptr_t)handle;
    struct sockaddr_storage peer;
#if defined(_WIN32)
    int peer_length = (int)sizeof(peer);
#else
    socklen_t peer_length = (socklen_t)sizeof(peer);
#endif
    fdn_net_socket socket;
    const void *native_address;
    char text[INET6_ADDRSTRLEN];
    size_t length;
    char *copy;

    if (address == NULL) {
        fdn_panic_cstr("network peer address output is null");
    }
    fdn_string_drop(address);
    *address = fdn_string_static("", 0);
    if (connection == NULL) {
        return FDN_NET_CLOSED;
    }

    fdn_net_connection_enter(connection);
    socket = connection->socket;
    fdn_net_connection_leave(connection);
    if (socket == FDN_NET_INVALID_SOCKET) {
        return FDN_NET_CLOSED;
    }

    (void)memset(&peer, 0, sizeof(peer));
    if (getpeername(socket, (struct sockaddr *)&peer, &peer_length) != 0) {
        return FDN_NET_IO;
    }
    if (peer.ss_family == AF_INET) {
        const struct sockaddr_in *ipv4 = (const struct sockaddr_in *)&peer;
        native_address = &ipv4->sin_addr;
    } else if (peer.ss_family == AF_INET6) {
        const struct sockaddr_in6 *ipv6 = (const struct sockaddr_in6 *)&peer;
        native_address = &ipv6->sin6_addr;
    } else {
        return FDN_NET_IO;
    }
    if (inet_ntop(peer.ss_family, native_address, text, sizeof(text)) == NULL) {
        return FDN_NET_IO;
    }

    length = strlen(text);
    if (length == 0) {
        return FDN_NET_IO;
    }
    copy = fdn_alloc(length);
    (void)memcpy(copy, text, length);
    address->data = copy;
    address->length = length;
    address->owned = 1;
    return FDN_NET_OK;
}

int32_t foundation_runtime_net_connection_take_socket(uint64_t handle,
                                                      uint64_t *result) {
    fdn_net_connection *connection = (fdn_net_connection *)(uintptr_t)handle;
    fdn_net_socket socket;
    if (connection == NULL || result == NULL) {
        return FDN_NET_CLOSED;
    }
    *result = 0;
    fdn_net_connection_enter(connection);
    if (!connection->full_open || connection->socket == FDN_NET_INVALID_SOCKET ||
        connection->read_length != 0) {
        fdn_net_connection_leave(connection);
        return FDN_NET_CLOSED;
    }
    socket = connection->socket;
    connection->socket = FDN_NET_INVALID_SOCKET;
    connection->full_open = false;
    connection->read_open = false;
    connection->write_open = false;
    fdn_net_connection_leave(connection);
#if defined(_WIN32)
    *result = (uint64_t)socket;
#else
    *result = (uint64_t)(unsigned int)socket;
#endif
    return FDN_NET_OK;
}

void foundation_runtime_net_connection_close(uint64_t handle) {
    fdn_net_connection *connection = (fdn_net_connection *)(uintptr_t)handle;
    fdn_net_socket socket = FDN_NET_INVALID_SOCKET;
    if (connection == NULL) {
        return;
    }
    fdn_net_connection_enter(connection);
    if (connection->full_open) {
        connection->full_open = false;
        connection->read_open = false;
        connection->write_open = false;
        socket = connection->socket;
        connection->socket = FDN_NET_INVALID_SOCKET;
    }
    fdn_net_connection_leave(connection);
    fdn_net_close_socket(socket);
    fdn_net_connection_release(connection);
}

void foundation_runtime_net_reader_close(uint64_t handle) {
    fdn_net_connection *connection = (fdn_net_connection *)(uintptr_t)handle;
    if (connection != NULL) {
        fdn_net_close_half(connection, true);
        fdn_net_connection_release(connection);
    }
}

void foundation_runtime_net_writer_close(uint64_t handle) {
    fdn_net_connection *connection = (fdn_net_connection *)(uintptr_t)handle;
    if (connection != NULL) {
        fdn_net_close_half(connection, false);
        fdn_net_connection_release(connection);
    }
}

void foundation_runtime_net_controller_close(uint64_t handle) {
    fdn_net_connection *connection = (fdn_net_connection *)(uintptr_t)handle;
    if (connection != NULL) {
        fdn_net_connection_release(connection);
    }
}

void foundation_runtime_net_controller_abort(uint64_t handle) {
    fdn_net_connection *connection = (fdn_net_connection *)(uintptr_t)handle;
    if (connection == NULL) {
        return;
    }
    fdn_net_connection_enter(connection);
    if (connection->socket != FDN_NET_INVALID_SOCKET) {
        connection->read_open = false;
        connection->write_open = false;
        fdn_net_shutdown(connection->socket, 0);
        fdn_net_shutdown(connection->socket, 1);
    }
    fdn_net_connection_leave(connection);
    fdn_net_connection_release(connection);
}

static bool fdn_net_service_wake_open(fdn_net_service *service) {
#if defined(_WIN32)
    struct sockaddr_in address;
    int length = (int)sizeof(address);
    service->wake_read = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    service->wake_write = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (service->wake_read == INVALID_SOCKET || service->wake_write == INVALID_SOCKET) {
        fdn_net_close_socket(service->wake_read);
        fdn_net_close_socket(service->wake_write);
        return false;
    }
    (void)memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(UINT32_C(0x7f000001));
    address.sin_port = 0;
    if (bind(service->wake_read, (const struct sockaddr *)&address,
             (int)sizeof(address)) != 0 ||
        getsockname(service->wake_read, (struct sockaddr *)&address, &length) != 0 ||
        connect(service->wake_write, (const struct sockaddr *)&address,
                (int)sizeof(address)) != 0 ||
        !fdn_net_nonblocking(service->wake_read) ||
        !fdn_net_nonblocking(service->wake_write)) {
        fdn_net_close_socket(service->wake_read);
        fdn_net_close_socket(service->wake_write);
        return false;
    }
    return true;
#else
    int descriptors[2];
    if (pipe(descriptors) != 0) {
        return false;
    }
    service->wake_read = descriptors[0];
    service->wake_write = descriptors[1];
    if (!fdn_net_nonblocking(service->wake_read) ||
        !fdn_net_nonblocking(service->wake_write)) {
        fdn_net_close_socket(service->wake_read);
        fdn_net_close_socket(service->wake_write);
        return false;
    }
    return true;
#endif
}

static void fdn_net_service_signal(fdn_net_service *service) {
    const char byte = 1;
#if defined(_WIN32)
    const int result = send(service->wake_write, &byte, 1, 0);
    if (result < 0 && !fdn_net_would_block(WSAGetLastError())) {
        fdn_panic_cstr("network service wake failed");
    }
#else
    const ssize_t result = write(service->wake_write, &byte, 1);
    if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        fdn_panic_cstr("network service wake failed");
    }
#endif
}

static void fdn_net_service_drain(fdn_net_service *service) {
    char bytes[64];
#if defined(_WIN32)
    while (recv(service->wake_read, bytes, (int)sizeof(bytes), 0) > 0) {
    }
    if (!fdn_net_would_block(WSAGetLastError())) {
        fdn_panic_cstr("network service wake drain failed");
    }
#else
    while (read(service->wake_read, bytes, sizeof(bytes)) > 0) {
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        fdn_panic_cstr("network service wake drain failed");
    }
#endif
}

static void fdn_net_service_destroy(fdn_net_service *service) {
    fdn_net_close_socket(service->wake_read);
    fdn_net_close_socket(service->wake_write);
#if defined(_WIN32)
    DeleteCriticalSection(&service->lock);
#else
    if (pthread_mutex_destroy(&service->lock) != 0) {
        fdn_panic_cstr("network service destroy failed");
    }
#endif
    fdn_dealloc(service);
    fdn_net_count_remove(&fdn_net_services_count);
}

static fdn_net_service *fdn_net_service_open(void) {
    fdn_net_service *service = fdn_alloc(sizeof(*service));
    service->requests = NULL;
#if defined(_WIN32)
    InitializeCriticalSection(&service->lock);
    service->wake_read = INVALID_SOCKET;
    service->wake_write = INVALID_SOCKET;
#else
    if (pthread_mutex_init(&service->lock, NULL) != 0) {
        fdn_dealloc(service);
        fdn_panic_cstr("network service initialization failed");
    }
    service->wake_read = -1;
    service->wake_write = -1;
#endif
    if (!fdn_net_service_wake_open(service)) {
#if defined(_WIN32)
        DeleteCriticalSection(&service->lock);
#else
        if (pthread_mutex_destroy(&service->lock) != 0) {
            fdn_panic_cstr("network service destroy failed");
        }
#endif
        fdn_dealloc(service);
        return NULL;
    }
    fdn_net_count_add(&fdn_net_services_count);
    return service;
}

static size_t fdn_net_service_count(fdn_net_service *service) {
    size_t count = 0;
    fdn_net_request *request;
    for (request = service->requests; request != NULL; request = request->next) {
        if (count == SIZE_MAX) {
            fdn_panic_cstr("network request count overflow");
        }
        ++count;
    }
    return count;
}

static bool fdn_net_service_stop_if_empty(fdn_net_service *service) {
    bool stop = false;
    fdn_net_global_enter();
    fdn_net_service_enter(service);
    if (service->requests == NULL && fdn_net_current_service == service) {
        fdn_net_current_service = NULL;
        stop = true;
    }
    fdn_net_service_leave(service);
    fdn_net_global_leave();
    if (stop) {
        fdn_net_service_destroy(service);
    }
    return stop;
}

static bool fdn_net_service_remove(fdn_net_service *service,
                                   fdn_net_request *request) {
    fdn_net_request **slot;
    fdn_net_service_enter(service);
    for (slot = &service->requests; *slot != NULL; slot = &(*slot)->next) {
        if (*slot == request) {
            *slot = request->next;
            request->next = NULL;
            fdn_net_service_leave(service);
            return true;
        }
    }
    fdn_net_service_leave(service);
    return false;
}

static bool fdn_net_service_finish(fdn_net_service *service,
                                   fdn_net_request *request, int32_t status) {
    fdn_reactor_operation *operation = request->operation;
    if (!fdn_net_service_remove(service, request)) {
        fdn_panic_cstr("network service lost request");
    }
    if (request->kind == FDN_NET_REQUEST_CONNECT &&
        request->socket != FDN_NET_INVALID_SOCKET) {
        fdn_net_close_socket(request->socket);
    } else if (request->kind == FDN_NET_REQUEST_WRITE_ALL &&
               status != FDN_NET_OK) {
        fdn_net_close_half(request->value.write.connection, false);
    }
    fdn_dealloc(request);
    fdn_net_count_remove(&fdn_net_requests_count);
    const bool stopped = fdn_net_service_stop_if_empty(service);
    fdn_reactor_complete(operation, status);
    return stopped;
}

static int32_t fdn_net_accept_ready(fdn_net_request *request) {
    fdn_net_listener *listener = request->value.accept.listener;
    fdn_net_listener_enter(listener);
    if (listener->socket == FDN_NET_INVALID_SOCKET ||
        listener->socket != request->socket) {
        fdn_net_listener_leave(listener);
        return FDN_NET_CLOSED;
    }
    for (;;) {
        fdn_net_socket native_socket =
            (fdn_net_socket)accept(request->socket, NULL, NULL);
        if (native_socket != FDN_NET_INVALID_SOCKET) {
            if (!fdn_net_nonblocking(native_socket)) {
                fdn_net_close_socket(native_socket);
                fdn_net_listener_leave(listener);
                return FDN_NET_IO;
            }
            fdn_net_connection *connection = fdn_net_connection_open(native_socket);
            *request->value.accept.result = (uint64_t)(uintptr_t)connection;
            fdn_net_listener_leave(listener);
            return FDN_NET_OK;
        }
        const int error = fdn_net_last_error();
        if (fdn_net_interrupted(error)) {
            continue;
        }
        if (fdn_net_would_block(error)) {
            fdn_net_listener_leave(listener);
            return FDN_NET_PENDING;
        }
        fdn_net_listener_leave(listener);
        return fdn_net_closed_error(error) ? FDN_NET_CLOSED : FDN_NET_IO;
    }
}

static int32_t fdn_net_connect_next(fdn_net_request *request) {
    fdn_net_addresses *addresses = request->value.connect.addresses;
    while (request->value.connect.index < addresses->count) {
        const fdn_net_address *address =
            &addresses->items[request->value.connect.index++];
        const int family = address->value.ss_family;
        fdn_net_socket native_socket =
            (fdn_net_socket)socket(family, SOCK_STREAM, IPPROTO_TCP);
        if (native_socket == FDN_NET_INVALID_SOCKET) {
            continue;
        }
        if (!fdn_net_nonblocking(native_socket)) {
            fdn_net_close_socket(native_socket);
            continue;
        }
#if defined(_WIN32)
        const int result = connect(native_socket, (const struct sockaddr *)&address->value,
                                   address->length);
#else
        const int result = connect(native_socket, (const struct sockaddr *)&address->value,
                                   (socklen_t)address->length);
#endif
        if (result == 0) {
            fdn_net_connection *connection = fdn_net_connection_open(native_socket);
            *request->value.connect.result = (uint64_t)(uintptr_t)connection;
            request->socket = FDN_NET_INVALID_SOCKET;
            return FDN_NET_OK;
        }
        const int error = fdn_net_last_error();
        if (fdn_net_would_block(error)) {
            request->socket = native_socket;
            return FDN_NET_PENDING;
        }
        if (fdn_net_refused(error)) {
            request->value.connect.refused = true;
        }
        fdn_net_close_socket(native_socket);
    }
    return request->value.connect.refused ? FDN_NET_REFUSED : FDN_NET_IO;
}

static int32_t fdn_net_connect_ready(fdn_net_request *request) {
    int error = 0;
#if defined(_WIN32)
    int length = (int)sizeof(error);
    if (getsockopt(request->socket, SOL_SOCKET, SO_ERROR, (char *)&error, &length) != 0) {
        error = fdn_net_last_error();
    }
#else
    socklen_t length = (socklen_t)sizeof(error);
    if (getsockopt(request->socket, SOL_SOCKET, SO_ERROR, &error, &length) != 0) {
        error = fdn_net_last_error();
    }
#endif
    if (error == 0) {
        fdn_net_connection *connection = fdn_net_connection_open(request->socket);
        *request->value.connect.result = (uint64_t)(uintptr_t)connection;
        request->socket = FDN_NET_INVALID_SOCKET;
        return FDN_NET_OK;
    }
    if (fdn_net_refused(error)) {
        request->value.connect.refused = true;
    }
    fdn_net_close_socket(request->socket);
    request->socket = FDN_NET_INVALID_SOCKET;
    return fdn_net_connect_next(request);
}

static void fdn_net_read_close(fdn_net_connection *connection) {
    fdn_net_connection_enter(connection);
    const bool open = connection->read_open;
    const fdn_net_socket socket = connection->socket;
    connection->read_open = false;
    fdn_net_connection_leave(connection);
    if (open) {
        fdn_net_shutdown(socket, 0);
    }
}

static int32_t fdn_net_read_publish(fdn_net_request *request, size_t length,
                                    size_t consumed) {
    fdn_net_connection *connection = request->value.read.connection;
    size_t text_length = length;
    char *data = NULL;
    const bool line = request->value.read.mode == FDN_NET_READ_LINE;
    const bool binary = request->value.read.mode == FDN_NET_READ_EXACT_BYTES ||
                        request->value.read.mode == FDN_NET_READ_SOME_BYTES;
    if (line && text_length != 0 &&
        connection->read_data[text_length - 1] == '\r') {
        --text_length;
    }
    if (!binary && !fdn_utf8_valid(connection->read_data, text_length)) {
        connection->read_length = 0;
        fdn_net_read_close(connection);
        return FDN_NET_INVALID_UTF8;
    }
    if (text_length != 0) {
        data = fdn_alloc(text_length);
        (void)memcpy(data, connection->read_data, text_length);
    }
    if (consumed < connection->read_length) {
        (void)memmove(connection->read_data, connection->read_data + consumed,
                     connection->read_length - consumed);
    }
    connection->read_length -= consumed;
    if (binary) {
        uint64_t handle = 0;
        if (fdn_bytes_adopt((uint8_t *)data, text_length, text_length,
                            &handle) != 0) {
            if (data != NULL) {
                (void)memset(data, 0, text_length);
                fdn_dealloc(data);
            }
            return FDN_NET_IO;
        }
        *request->value.read.bytes_result = handle;
    } else if (text_length == 0) {
        *request->value.read.text_result = fdn_string_static("", 0);
    } else {
        request->value.read.text_result->data = data;
        request->value.read.text_result->length = text_length;
        request->value.read.text_result->owned = 1;
    }
    return FDN_NET_OK;
}

static int32_t fdn_net_read_buffered(fdn_net_request *request) {
    fdn_net_connection *connection = request->value.read.connection;
    if (request->value.read.mode == FDN_NET_READ_EXACT_TEXT ||
        request->value.read.mode == FDN_NET_READ_EXACT_BYTES) {
        if (connection->read_length >= request->value.read.limit) {
            return fdn_net_read_publish(request, request->value.read.limit,
                                        request->value.read.limit);
        }
        if (connection->read_eof) {
            return FDN_NET_EOF;
        }
        return FDN_NET_PENDING;
    }
    if (request->value.read.mode == FDN_NET_READ_SOME_BYTES) {
        if (connection->read_length != 0) {
            const size_t length =
                connection->read_length < request->value.read.limit
                    ? connection->read_length
                    : request->value.read.limit;
            return fdn_net_read_publish(request, length, length);
        }
        return connection->read_eof ? FDN_NET_EOF : FDN_NET_PENDING;
    }
    size_t offset;
    for (offset = 0; offset < connection->read_length; ++offset) {
        if (connection->read_data[offset] == '\n') {
            size_t text_length = offset;
            if (text_length != 0 && connection->read_data[text_length - 1] == '\r') {
                --text_length;
            }
            if (text_length > request->value.read.limit) {
                connection->read_length = 0;
                fdn_net_read_close(connection);
                return FDN_NET_LINE_TOO_LONG;
            }
            return fdn_net_read_publish(request, offset, offset + 1);
        }
    }
    size_t text_length = connection->read_length;
    if (text_length != 0 && connection->read_data[text_length - 1] == '\r') {
        --text_length;
    }
    if (text_length > request->value.read.limit) {
        connection->read_length = 0;
        fdn_net_read_close(connection);
        return FDN_NET_LINE_TOO_LONG;
    }
    if (connection->read_eof) {
        if (connection->read_length == 0) {
            return FDN_NET_EOF;
        }
        return fdn_net_read_publish(request, connection->read_length,
                                    connection->read_length);
    }
    return FDN_NET_PENDING;
}

static void fdn_net_read_append(fdn_net_connection *connection, const char *data,
                                size_t length) {
    size_t required;
    if (SIZE_MAX - connection->read_length < length) {
        fdn_panic_cstr("network read buffer overflow");
    }
    required = connection->read_length + length;
    if (required > connection->read_capacity) {
        size_t capacity = connection->read_capacity == 0 ? 4096 : connection->read_capacity;
        char *grown;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2) {
                capacity = required;
            } else {
                capacity *= 2;
            }
        }
        grown = fdn_alloc(capacity);
        if (connection->read_length != 0) {
            (void)memcpy(grown, connection->read_data, connection->read_length);
        }
        fdn_dealloc(connection->read_data);
        connection->read_data = grown;
        connection->read_capacity = capacity;
    }
    (void)memcpy(connection->read_data + connection->read_length, data, length);
    connection->read_length = required;
}

static int32_t fdn_net_read_ready(fdn_net_request *request) {
    char bytes[4096];
    for (;;) {
#if defined(_WIN32)
        const int count = recv(request->socket, bytes, (int)sizeof(bytes), 0);
#else
        const ssize_t count = recv(request->socket, bytes, sizeof(bytes), 0);
#endif
        if (count > 0) {
            fdn_net_read_append(request->value.read.connection, bytes, (size_t)count);
            const int32_t status = fdn_net_read_buffered(request);
            if (status != FDN_NET_PENDING) {
                return status;
            }
            continue;
        }
        if (count == 0) {
            request->value.read.connection->read_eof = true;
            return fdn_net_read_buffered(request);
        }
        const int error = fdn_net_last_error();
        if (fdn_net_interrupted(error)) {
            continue;
        }
        if (fdn_net_would_block(error)) {
            return FDN_NET_PENDING;
        }
        return fdn_net_closed_error(error) ? FDN_NET_CLOSED : FDN_NET_IO;
    }
}

static int32_t fdn_net_write_ready(fdn_net_request *request) {
    while (request->value.write.offset < request->value.write.length) {
        const size_t remaining =
            request->value.write.length - request->value.write.offset;
        const size_t amount = remaining > (size_t)INT_MAX ? (size_t)INT_MAX : remaining;
#if defined(_WIN32)
        const int count = send(request->socket,
                               request->value.write.data +
                                   request->value.write.offset,
                               (int)amount, 0);
#else
        const ssize_t count = send(request->socket,
                                   request->value.write.data +
                                       request->value.write.offset,
                                   amount, fdn_net_send_flags());
#endif
        if (count > 0) {
            request->value.write.offset += (size_t)count;
            continue;
        }
        if (count == 0) {
            return FDN_NET_CLOSED;
        }
        const int error = fdn_net_last_error();
        if (fdn_net_interrupted(error)) {
            continue;
        }
        if (fdn_net_would_block(error)) {
            return FDN_NET_PENDING;
        }
        return fdn_net_closed_error(error) ? FDN_NET_CLOSED : FDN_NET_IO;
    }
    return FDN_NET_OK;
}

static bool fdn_net_deadline_expired(const fdn_net_request *request,
                                     uint64_t now) {
    return request->deadline != UINT64_MAX && now >= request->deadline;
}

static int fdn_net_poll_timeout(fdn_net_request **requests, size_t count,
                                uint64_t now) {
    uint64_t minimum = UINT64_MAX;
    size_t index;
    for (index = 0; index < count; ++index) {
        const uint64_t deadline = requests[index]->deadline;
        if (deadline < minimum) {
            minimum = deadline;
        }
    }
    if (minimum == UINT64_MAX) {
        return -1;
    }
    if (minimum <= now) {
        return 0;
    }
    const uint64_t remaining = minimum - now;
    uint64_t milliseconds = remaining / UINT64_C(1000000);
    if (remaining % UINT64_C(1000000) != 0) {
        ++milliseconds;
    }
    if (milliseconds > (uint64_t)INT_MAX) {
        return INT_MAX;
    }
    return (int)milliseconds;
}

static int fdn_net_poll(fdn_net_pollfd *descriptors, size_t count,
                        int timeout) {
#if defined(_WIN32)
    if (count > (size_t)ULONG_MAX) {
        fdn_panic_cstr("network poll descriptor count overflow");
    }
    return WSAPoll(descriptors, (ULONG)count, timeout);
#else
    return poll(descriptors, (nfds_t)count, timeout);
#endif
}

static int32_t fdn_net_process_request(fdn_net_request* request, short events, bool closed,
                                       bool cancelled, bool timed_out) {
    if (closed) {
        return FDN_NET_CLOSED;
    }
    if (cancelled) {
        return FDN_NET_CANCELLED;
    }
    if (timed_out) {
        return FDN_NET_TIMEOUT;
    }
    if ((events & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        if (request->kind == FDN_NET_REQUEST_ACCEPT) {
            return fdn_net_accept_ready(request);
        }
        if (request->kind == FDN_NET_REQUEST_CONNECT) {
            return fdn_net_connect_ready(request);
        }
        if (request->kind == FDN_NET_REQUEST_READ_LINE) {
            return fdn_net_read_ready(request);
        }
        return fdn_net_write_ready(request);
    }
    if (request->kind == FDN_NET_REQUEST_ACCEPT &&
        (events & FDN_NET_POLL_READ) != 0) {
        return fdn_net_accept_ready(request);
    }
    if (request->kind == FDN_NET_REQUEST_CONNECT &&
        (events & FDN_NET_POLL_WRITE) != 0) {
        return fdn_net_connect_ready(request);
    }
    if (request->kind == FDN_NET_REQUEST_READ_LINE &&
        (events & FDN_NET_POLL_READ) != 0) {
        return fdn_net_read_ready(request);
    }
    if (request->kind == FDN_NET_REQUEST_WRITE_ALL &&
        (events & FDN_NET_POLL_WRITE) != 0) {
        return fdn_net_write_ready(request);
    }
    return FDN_NET_PENDING;
}

#if defined(_WIN32)
static DWORD WINAPI fdn_net_service_thread(void *raw)
#else
static void *fdn_net_service_thread(void *raw)
#endif
{
    fdn_net_service *service = raw;
    for (;;) {
        size_t count;
        size_t index;
        fdn_net_pollfd *descriptors;
        fdn_net_request **requests;
        fdn_net_request *request;
        fdn_net_request *completed = NULL;
        int32_t completion_status = FDN_NET_PENDING;

        fdn_net_service_enter(service);
        count = fdn_net_service_count(service);
        descriptors = fdn_alloc((count + 1) * sizeof(*descriptors));
        requests = count == 0 ? NULL : fdn_alloc(count * sizeof(*requests));
        (void)memset(descriptors, 0, (count + 1) * sizeof(*descriptors));
        descriptors[0].fd = service->wake_read;
        descriptors[0].events = FDN_NET_POLL_READ;
        index = 0;
        for (request = service->requests; request != NULL; request = request->next) {
            if (index >= count) {
                fdn_panic_cstr("network request snapshot overflow");
            }
            requests[index] = request;
            descriptors[index + 1].fd = request->socket;
            descriptors[index + 1].events =
                request->kind == FDN_NET_REQUEST_ACCEPT ||
                        request->kind == FDN_NET_REQUEST_READ_LINE
                    ? FDN_NET_POLL_READ
                    : FDN_NET_POLL_WRITE;
            ++index;
        }
        if (index != count) {
            fdn_panic_cstr("network request snapshot mismatch");
        }
        fdn_net_service_leave(service);

        if (count == 0) {
            fdn_dealloc(descriptors);
            if (fdn_net_service_stop_if_empty(service)) {
                break;
            }
            fdn_dealloc(requests);
            continue;
        }

        const int timeout = fdn_net_poll_timeout(
            requests, count, foundation_runtime_time_monotonic_nanoseconds());
        const int poll_result = fdn_net_poll(descriptors, count + 1, timeout);
        if (poll_result < 0) {
            const int error = fdn_net_last_error();
            fdn_dealloc(requests);
            fdn_dealloc(descriptors);
            if (fdn_net_interrupted(error)) {
                continue;
            }
            fdn_panic_cstr("network service poll failed");
        }
        if (descriptors[0].revents != 0) {
            fdn_net_service_drain(service);
        }
        for (index = 0; index < count; ++index) {
            request = requests[index];
            fdn_net_service_enter(service);
            const bool closed = request->closed;
            const bool cancelled = request->cancelled;
            fdn_net_service_leave(service);
            const bool timed_out = fdn_net_deadline_expired(
                request, foundation_runtime_time_monotonic_nanoseconds());
            if (!cancelled && !timed_out &&
                descriptors[index + 1].revents == 0) {
                continue;
            }
            completion_status = fdn_net_process_request(request, descriptors[index + 1].revents,
                                                        closed, cancelled, timed_out);
            if (completion_status != FDN_NET_PENDING) {
                completed = request;
                break;
            }
        }
        fdn_dealloc(requests);
        fdn_dealloc(descriptors);
        if (completed != NULL &&
            fdn_net_service_finish(service, completed, completion_status)) {
            break;
        }
    }
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static bool fdn_net_service_start_thread(fdn_net_service *service) {
#if defined(_WIN32)
    HANDLE thread = CreateThread(NULL, 0, fdn_net_service_thread, service, 0, NULL);
    if (thread == NULL) {
        return false;
    }
    if (CloseHandle(thread) == 0) {
        fdn_panic_cstr("network service thread detach failed");
    }
    return true;
#else
    pthread_attr_t attributes;
    pthread_t thread;
    if (pthread_attr_init(&attributes) != 0) {
        return false;
    }
    if (pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED) != 0) {
        (void)pthread_attr_destroy(&attributes);
        return false;
    }
    const int result = pthread_create(&thread, &attributes,
                                      fdn_net_service_thread, service);
    if (pthread_attr_destroy(&attributes) != 0) {
        fdn_panic_cstr("network service thread attribute destroy failed");
    }
    return result == 0;
#endif
}

static bool fdn_net_submit(fdn_net_request *request) {
    fdn_net_service *service;
    bool created = false;
    fdn_net_global_enter();
    service = fdn_net_current_service;
    if (service == NULL) {
        service = fdn_net_service_open();
        if (service == NULL) {
            fdn_net_global_leave();
            return false;
        }
        fdn_net_current_service = service;
        created = true;
    }
    fdn_net_service_enter(service);
    request->next = service->requests;
    service->requests = request;
    fdn_net_service_leave(service);
    fdn_net_count_add(&fdn_net_requests_count);
    if (created && !fdn_net_service_start_thread(service)) {
        if (!fdn_net_service_remove(service, request)) {
            fdn_panic_cstr("network service lost failed submission");
        }
        fdn_net_current_service = NULL;
        fdn_net_count_remove(&fdn_net_requests_count);
        fdn_net_service_destroy(service);
        fdn_net_global_leave();
        return false;
    }
    if (!created) {
        fdn_net_service_signal(service);
    }
    fdn_net_global_leave();
    return true;
}

static void fdn_net_cancel(fdn_reactor_operation *operation) {
    fdn_net_service *service;
    fdn_net_request *request;
    fdn_net_global_enter();
    service = fdn_net_current_service;
    if (service != NULL) {
        fdn_net_service_enter(service);
        for (request = service->requests; request != NULL; request = request->next) {
            if (request->operation == operation) {
                request->cancelled = true;
                fdn_net_service_signal(service);
                break;
            }
        }
        fdn_net_service_leave(service);
    }
    fdn_net_global_leave();
}

void foundation_runtime_net_accept_start(uint64_t listener_handle,
                                         uint64_t *connection,
                                         fdn_reactor_operation *operation) {
    fdn_net_listener *listener =
        (fdn_net_listener *)(uintptr_t)listener_handle;
    fdn_net_request *request;
    if (connection == NULL || operation == NULL) {
        fdn_panic_cstr("invalid network accept operation");
    }
    *connection = 0;
    if (listener == NULL) {
        fdn_reactor_complete(operation, FDN_NET_CLOSED);
        return;
    }
    fdn_net_listener_enter(listener);
    if (listener->socket == FDN_NET_INVALID_SOCKET) {
        fdn_net_listener_leave(listener);
        fdn_reactor_complete(operation, FDN_NET_CLOSED);
        return;
    }
    request = fdn_alloc(sizeof(*request));
    (void)memset(request, 0, sizeof(*request));
    request->operation = operation;
    request->kind = FDN_NET_REQUEST_ACCEPT;
    request->socket = listener->socket;
    request->deadline = UINT64_MAX;
    request->value.accept.listener = listener;
    request->value.accept.result = connection;
    if (!fdn_net_submit(request)) {
        fdn_net_listener_leave(listener);
        fdn_dealloc(request);
        fdn_reactor_complete(operation, FDN_NET_IO);
        return;
    }
    fdn_net_listener_leave(listener);
}

void foundation_runtime_net_accept_cancel(fdn_reactor_operation *operation) {
    fdn_net_cancel(operation);
}

static void fdn_net_connect_start(uint64_t address_handle, uint64_t deadline,
                                  uint64_t *connection,
                                  fdn_reactor_operation *operation) {
    fdn_net_addresses *addresses = (fdn_net_addresses *)(uintptr_t)address_handle;
    fdn_net_request *request;
    int32_t status;
    if (connection == NULL || operation == NULL) {
        fdn_panic_cstr("invalid network connect operation");
    }
    *connection = 0;
    if (addresses == NULL || addresses->count == 0) {
        fdn_reactor_complete(operation, FDN_NET_INVALID_ADDRESS);
        return;
    }
    if (deadline != UINT64_MAX &&
        foundation_runtime_time_monotonic_nanoseconds() >= deadline) {
        fdn_reactor_complete(operation, FDN_NET_TIMEOUT);
        return;
    }
    request = fdn_alloc(sizeof(*request));
    (void)memset(request, 0, sizeof(*request));
    request->operation = operation;
    request->kind = FDN_NET_REQUEST_CONNECT;
    request->socket = FDN_NET_INVALID_SOCKET;
    request->deadline = deadline;
    request->value.connect.addresses = addresses;
    request->value.connect.result = connection;
    status = fdn_net_connect_next(request);
    if (status != FDN_NET_PENDING) {
        fdn_dealloc(request);
        fdn_reactor_complete(operation, status);
        return;
    }
    if (!fdn_net_submit(request)) {
        fdn_net_close_socket(request->socket);
        fdn_dealloc(request);
        fdn_reactor_complete(operation, FDN_NET_IO);
    }
}

void foundation_runtime_net_connect_start(uint64_t address_handle,
                                          uint64_t *connection,
                                          fdn_reactor_operation *operation) {
    fdn_net_connect_start(address_handle, UINT64_MAX, connection, operation);
}

void foundation_runtime_net_connect_until_start(
    uint64_t address_handle, uint64_t deadline, uint64_t *connection,
    fdn_reactor_operation *operation) {
    fdn_net_connect_start(address_handle, deadline, connection, operation);
}

void foundation_runtime_net_connect_cancel(fdn_reactor_operation *operation) {
    fdn_net_cancel(operation);
}

static void fdn_net_read_start(uint64_t reader, uint64_t limit,
                               fdn_string *text, uint64_t *bytes,
                               uint64_t deadline,
                               fdn_reactor_operation *operation,
                               fdn_net_read_mode mode) {
    fdn_net_connection *connection = (fdn_net_connection *)(uintptr_t)reader;
    fdn_net_request *request;
    fdn_net_socket socket;
    int32_t status;
    const bool binary = mode == FDN_NET_READ_EXACT_BYTES ||
                        mode == FDN_NET_READ_SOME_BYTES;
    if (operation == NULL || (binary ? bytes == NULL : text == NULL)) {
        fdn_panic_cstr("invalid network read operation");
    }
    if (binary) {
        *bytes = 0;
    } else {
        fdn_string_drop(text);
        *text = fdn_string_static("", 0);
    }
    if ((mode == FDN_NET_READ_EXACT_TEXT ||
         mode == FDN_NET_READ_EXACT_BYTES) &&
        limit > (uint64_t)SIZE_MAX) {
        fdn_reactor_complete(operation, FDN_NET_LINE_TOO_LONG);
        return;
    }
    if (mode == FDN_NET_READ_SOME_BYTES &&
        (limit == 0 || limit > (uint64_t)SIZE_MAX)) {
        fdn_reactor_complete(operation, FDN_NET_INVALID_LIMIT);
        return;
    }
    if (deadline != UINT64_MAX &&
        foundation_runtime_time_monotonic_nanoseconds() >= deadline) {
        fdn_reactor_complete(operation, FDN_NET_TIMEOUT);
        return;
    }
    if (connection == NULL) {
        fdn_reactor_complete(operation, FDN_NET_CLOSED);
        return;
    }
    fdn_net_connection_enter(connection);
    const bool open = connection->read_open;
    socket = connection->socket;
    fdn_net_connection_leave(connection);
    if (!open || socket == FDN_NET_INVALID_SOCKET) {
        fdn_reactor_complete(operation, FDN_NET_CLOSED);
        return;
    }
    request = fdn_alloc(sizeof(*request));
    (void)memset(request, 0, sizeof(*request));
    request->operation = operation;
    request->kind = FDN_NET_REQUEST_READ_LINE;
    request->socket = socket;
    request->deadline = deadline;
    request->value.read.connection = connection;
    request->value.read.limit = limit > (uint64_t)SIZE_MAX ? SIZE_MAX : (size_t)limit;
    request->value.read.text_result = text;
    request->value.read.bytes_result = bytes;
    request->value.read.mode = mode;
    status = fdn_net_read_buffered(request);
    if (status != FDN_NET_PENDING) {
        fdn_dealloc(request);
        fdn_reactor_complete(operation, status);
        return;
    }
    if (!fdn_net_submit(request)) {
        fdn_dealloc(request);
        fdn_reactor_complete(operation, FDN_NET_IO);
    }
}

void foundation_runtime_net_read_line_start(uint64_t reader, uint64_t limit,
                                            fdn_string *line,
                                            fdn_reactor_operation *operation) {
    fdn_net_read_start(reader, limit, line, NULL, UINT64_MAX, operation,
                       FDN_NET_READ_LINE);
}

void foundation_runtime_net_read_line_until_start(
    uint64_t reader, uint64_t limit, uint64_t deadline, fdn_string *line,
    fdn_reactor_operation *operation) {
    fdn_net_read_start(reader, limit, line, NULL, deadline, operation,
                       FDN_NET_READ_LINE);
}

void foundation_runtime_net_read_line_cancel(fdn_reactor_operation *operation) {
    fdn_net_cancel(operation);
}

void foundation_runtime_net_read_exact_start(uint64_t reader, uint64_t length,
                                             fdn_string *text,
                                             fdn_reactor_operation *operation) {
    fdn_net_read_start(reader, length, text, NULL, UINT64_MAX, operation,
                       FDN_NET_READ_EXACT_TEXT);
}

void foundation_runtime_net_read_exact_until_start(
    uint64_t reader, uint64_t length, uint64_t deadline, fdn_string *text,
    fdn_reactor_operation *operation) {
    fdn_net_read_start(reader, length, text, NULL, deadline, operation,
                       FDN_NET_READ_EXACT_TEXT);
}

void foundation_runtime_net_read_exact_cancel(fdn_reactor_operation *operation) {
    fdn_net_cancel(operation);
}

void foundation_runtime_net_read_exact_bytes_start(
    uint64_t reader, uint64_t length, uint64_t *bytes,
    fdn_reactor_operation *operation) {
    fdn_net_read_start(reader, length, NULL, bytes, UINT64_MAX, operation,
                       FDN_NET_READ_EXACT_BYTES);
}

void foundation_runtime_net_read_exact_bytes_until_start(
    uint64_t reader, uint64_t length, uint64_t deadline, uint64_t *bytes,
    fdn_reactor_operation *operation) {
    fdn_net_read_start(reader, length, NULL, bytes, deadline, operation,
                       FDN_NET_READ_EXACT_BYTES);
}

void foundation_runtime_net_read_some_bytes_start(
    uint64_t reader, uint64_t limit, uint64_t *bytes,
    fdn_reactor_operation *operation) {
    fdn_net_read_start(reader, limit, NULL, bytes, UINT64_MAX, operation,
                       FDN_NET_READ_SOME_BYTES);
}

void foundation_runtime_net_read_some_bytes_until_start(
    uint64_t reader, uint64_t limit, uint64_t deadline, uint64_t *bytes,
    fdn_reactor_operation *operation) {
    fdn_net_read_start(reader, limit, NULL, bytes, deadline, operation,
                       FDN_NET_READ_SOME_BYTES);
}

void foundation_runtime_net_read_some_bytes_cancel(
    fdn_reactor_operation *operation) {
    fdn_net_cancel(operation);
}

static void fdn_net_write_start(uint64_t writer, const char *data,
                                size_t length, uint64_t deadline,
                                fdn_reactor_operation *operation) {
    fdn_net_connection *connection = (fdn_net_connection *)(uintptr_t)writer;
    fdn_net_request *request;
    fdn_net_socket socket;
    int32_t status;
    if (operation == NULL) {
        fdn_panic_cstr("invalid network write operation");
    }
    if (connection == NULL || (length != 0 && data == NULL)) {
        fdn_reactor_complete(operation, FDN_NET_CLOSED);
        return;
    }
    if (deadline != UINT64_MAX &&
        foundation_runtime_time_monotonic_nanoseconds() >= deadline) {
        fdn_reactor_complete(operation, FDN_NET_TIMEOUT);
        return;
    }
    fdn_net_connection_enter(connection);
    const bool open = connection->write_open;
    socket = connection->socket;
    fdn_net_connection_leave(connection);
    if (!open || socket == FDN_NET_INVALID_SOCKET) {
        fdn_reactor_complete(operation, FDN_NET_CLOSED);
        return;
    }
    if (length == 0) {
        fdn_reactor_complete(operation, FDN_NET_OK);
        return;
    }
    request = fdn_alloc(sizeof(*request));
    (void)memset(request, 0, sizeof(*request));
    request->operation = operation;
    request->kind = FDN_NET_REQUEST_WRITE_ALL;
    request->socket = socket;
    request->deadline = deadline;
    request->value.write.connection = connection;
    request->value.write.data = data;
    request->value.write.length = length;
    status = fdn_net_write_ready(request);
    if (status != FDN_NET_PENDING) {
        if (status != FDN_NET_OK) {
            fdn_net_close_half(connection, false);
        }
        fdn_dealloc(request);
        fdn_reactor_complete(operation, status);
        return;
    }
    if (!fdn_net_submit(request)) {
        fdn_net_close_half(connection, false);
        fdn_dealloc(request);
        fdn_reactor_complete(operation, FDN_NET_IO);
    }
}

void foundation_runtime_net_write_all_start(uint64_t writer,
                                            const fdn_string *text,
                                            fdn_reactor_operation *operation) {
    if (text == NULL) {
        fdn_panic_cstr("invalid network text write operation");
    }
    fdn_net_write_start(writer, text->data, text->length, UINT64_MAX, operation);
}

void foundation_runtime_net_write_all_until_start(
    uint64_t writer, const fdn_string *text, uint64_t deadline,
    fdn_reactor_operation *operation) {
    if (text == NULL) {
        fdn_panic_cstr("invalid network text write operation");
    }
    fdn_net_write_start(writer, text->data, text->length, deadline, operation);
}

void foundation_runtime_net_write_all_cancel(fdn_reactor_operation *operation) {
    fdn_net_cancel(operation);
}

static void fdn_net_write_bytes_start(uint64_t writer, uint64_t bytes,
                                      uint64_t deadline,
                                      fdn_reactor_operation *operation) {
    const uint8_t *data = NULL;
    size_t length = 0;
    if (operation == NULL) {
        fdn_panic_cstr("invalid network byte write operation");
    }
    if (fdn_bytes_view(bytes, &data, &length) != 0) {
        fdn_reactor_complete(operation, FDN_NET_CLOSED);
        return;
    }
    fdn_net_write_start(writer, (const char *)data, length, deadline, operation);
}

void foundation_runtime_net_write_all_bytes_start(
    uint64_t writer, uint64_t bytes, fdn_reactor_operation *operation) {
    fdn_net_write_bytes_start(writer, bytes, UINT64_MAX, operation);
}

void foundation_runtime_net_write_all_bytes_until_start(
    uint64_t writer, uint64_t bytes, uint64_t deadline,
    fdn_reactor_operation *operation) {
    fdn_net_write_bytes_start(writer, bytes, deadline, operation);
}

uint64_t foundation_runtime_net_live_addresses(void) {
    return fdn_net_count_read(&fdn_net_addresses_count);
}

uint64_t foundation_runtime_net_live_listeners(void) {
    return fdn_net_count_read(&fdn_net_listeners_count);
}

uint64_t foundation_runtime_net_live_connections(void) {
    return fdn_net_count_read(&fdn_net_connections_count);
}

uint64_t foundation_runtime_net_live_requests(void) {
    return fdn_net_count_read(&fdn_net_requests_count);
}

uint64_t foundation_runtime_net_live_services(void) {
    return fdn_net_count_read(&fdn_net_services_count);
}
