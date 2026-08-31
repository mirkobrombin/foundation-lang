#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "foundation/runtime.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET test_socket;
#define TEST_INVALID_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int test_socket;
#define TEST_INVALID_SOCKET (-1)
#endif

typedef struct test_server {
    test_socket listener;
    int32_t status;
#if defined(_WIN32)
    HANDLE thread;
#else
    pthread_t thread;
#endif
} test_server;

static void test_close(test_socket socket) {
#if defined(_WIN32)
    (void)closesocket(socket);
#else
    (void)close(socket);
#endif
}

static int test_send_all(test_socket socket, const char *data, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        const size_t remaining = length - offset;
        const int amount = remaining > (size_t)INT_MAX ? INT_MAX : (int)remaining;
#if defined(_WIN32)
        const int count = send(socket, data + offset, amount, 0);
#else
        const ssize_t count = send(socket, data + offset, (size_t)amount, 0);
#endif
        if (count <= 0) {
            return 0;
        }
        offset += (size_t)count;
    }
    return 1;
}

static int test_read_ping(test_socket socket) {
    char bytes[5];
    size_t offset = 0;
    while (offset < sizeof(bytes)) {
#if defined(_WIN32)
        const int count = recv(socket, bytes + offset,
                               (int)(sizeof(bytes) - offset), 0);
#else
        const ssize_t count = recv(socket, bytes + offset,
                                   sizeof(bytes) - offset, 0);
#endif
        if (count <= 0) {
            return 0;
        }
        offset += (size_t)count;
    }
    return memcmp(bytes, "ping\n", sizeof(bytes)) == 0;
}

static int test_wait_closed(test_socket socket) {
    char byte;
#if defined(_WIN32)
    DWORD timeout = 5000;
    (void)setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout,
                     (int)sizeof(timeout));
    return recv(socket, &byte, 1, 0) == 0;
#else
    struct timeval timeout = {5, 0};
    (void)setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                     (socklen_t)sizeof(timeout));
    return recv(socket, &byte, 1, 0) == 0;
#endif
}

static test_socket test_accept(test_server *server) {
    return accept(server->listener, NULL, NULL);
}

static void test_run_server(test_server *server) {
    test_socket client = test_accept(server);
    static const char response[] = "pong\nabc\r\n";
    if (client == TEST_INVALID_SOCKET || !test_read_ping(client) ||
        !test_send_all(client, response, sizeof(response) - 1)) {
        server->status = 1;
        test_close(client);
        return;
    }
    test_close(client);

    client = test_accept(server);
    if (client == TEST_INVALID_SOCKET || !test_wait_closed(client)) {
        server->status = 2;
        test_close(client);
        return;
    }
    test_close(client);

    client = test_accept(server);
    if (client == TEST_INVALID_SOCKET ||
        !test_send_all(client, "\xc3\x28\n", 3)) {
        server->status = 3;
        test_close(client);
        return;
    }
    test_close(client);

    client = test_accept(server);
    if (client == TEST_INVALID_SOCKET || !test_send_all(client, "abcd\n", 5)) {
        server->status = 4;
        test_close(client);
        return;
    }
    test_close(client);
}

#if defined(_WIN32)
static DWORD WINAPI test_server_thread(void *raw) {
    test_run_server(raw);
    return 0;
}
#else
static void *test_server_thread(void *raw) {
    test_run_server(raw);
    return NULL;
}
#endif

uint64_t foundation_test_net_server_start(uint64_t *port) {
    struct sockaddr_in address;
    test_server *server;
#if defined(_WIN32)
    int length = (int)sizeof(address);
#else
    socklen_t length = (socklen_t)sizeof(address);
#endif
    if (port == NULL) {
        return 0;
    }
    *port = 0;
#if defined(_WIN32)
    {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            return 0;
        }
    }
#endif
    server = fdn_alloc(sizeof(*server));
    server->status = 0;
    server->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server->listener == TEST_INVALID_SOCKET) {
        fdn_dealloc(server);
        return 0;
    }
    (void)memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(server->listener, (const struct sockaddr *)&address,
             (int)sizeof(address)) != 0 ||
        getsockname(server->listener, (struct sockaddr *)&address, &length) != 0 ||
        listen(server->listener, 4) != 0) {
        test_close(server->listener);
        fdn_dealloc(server);
        return 0;
    }
    *port = (uint64_t)ntohs(address.sin_port);
#if defined(_WIN32)
    server->thread = CreateThread(NULL, 0, test_server_thread, server, 0, NULL);
    if (server->thread == NULL) {
        test_close(server->listener);
        fdn_dealloc(server);
        return 0;
    }
#else
    if (pthread_create(&server->thread, NULL, test_server_thread, server) != 0) {
        test_close(server->listener);
        fdn_dealloc(server);
        return 0;
    }
#endif
    return (uint64_t)(uintptr_t)server;
}

int32_t foundation_test_net_server_join(uint64_t handle) {
    test_server *server = (test_server *)(uintptr_t)handle;
    int32_t status;
    if (server == NULL) {
        return 10;
    }
#if defined(_WIN32)
    if (WaitForSingleObject(server->thread, INFINITE) != WAIT_OBJECT_0 ||
        CloseHandle(server->thread) == 0) {
        abort();
    }
#else
    if (pthread_join(server->thread, NULL) != 0) {
        abort();
    }
#endif
    status = server->status;
    test_close(server->listener);
    fdn_dealloc(server);
    return status;
}
