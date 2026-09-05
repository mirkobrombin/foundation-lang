#define _POSIX_C_SOURCE 200112L

#include "foundation_openssl.h"

#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/quic.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define FOUNDATION_QUIC_INVALID_SOCKET INVALID_SOCKET
#define foundation_quic_socket SOCKET
#define foundation_quic_close closesocket
#define foundation_quic_shutdown(socket) shutdown((socket), SD_BOTH)
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#define FOUNDATION_QUIC_INVALID_SOCKET (-1)
#define foundation_quic_socket int
#define foundation_quic_close close
#define foundation_quic_shutdown(socket) shutdown((socket), SHUT_RDWR)
#endif

#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif

typedef struct foundation_quic_bytes {
    unsigned char* data;
    size_t length;
} foundation_quic_bytes;

typedef struct foundation_quic_listener {
    SSL_CTX* context;
    SSL* ssl;
    foundation_quic_socket socket;
    unsigned char protocol[255];
    size_t protocol_length;
} foundation_quic_listener;

typedef struct foundation_quic_listener_record {
    uint64_t identifier;
    foundation_quic_listener* listener;
    size_t users;
    int closing;
    struct foundation_quic_listener_record* next;
} foundation_quic_listener_record;

typedef struct foundation_quic_connection {
    SSL_CTX* context;
    SSL* ssl;
    _Atomic size_t references;
    _Atomic int closing;
    char peer[NI_MAXHOST];
} foundation_quic_connection;

typedef struct foundation_quic_stream_state {
    SSL* ssl;
    foundation_quic_connection* connection;
    _Atomic size_t references;
    _Atomic int concluded;
    _Atomic int reset;
} foundation_quic_stream_state;

enum foundation_quic_stream_role {
    FOUNDATION_QUIC_STREAM_OWNER = 0,
    FOUNDATION_QUIC_STREAM_READER = 1,
    FOUNDATION_QUIC_STREAM_WRITER = 2,
    FOUNDATION_QUIC_STREAM_CONTROLLER = 3,
};

typedef struct foundation_quic_stream {
    foundation_quic_stream_state* state;
    enum foundation_quic_stream_role role;
} foundation_quic_stream;

static atomic_flag foundation_quic_listeners_lock = ATOMIC_FLAG_INIT;
static _Atomic uint64_t foundation_quic_next_listener = 1;
static foundation_quic_listener_record* foundation_quic_listeners;

static int foundation_quic_network_open(void) {
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    return 1;
#endif
}

static void foundation_quic_network_close(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

static void foundation_quic_pause(void) {
#ifdef _WIN32
    Sleep(1);
#else
    struct timespec delay = {.tv_sec = 0, .tv_nsec = 1000000};
    nanosleep(&delay, NULL);
#endif
}

static int foundation_quic_copy_bytes(uint64_t handle, foundation_quic_bytes* result) {
    uint64_t length = 0;
    uint64_t value = 0;
    size_t index;

    result->data = NULL;
    result->length = 0;
    if (foundation_runtime_bytes_length(handle, &length) != 0 || length > SIZE_MAX) {
        return 0;
    }
    if (length == 0)
        return 1;
    result->data = OPENSSL_malloc((size_t)length);
    if (result->data == NULL)
        return 0;
    for (index = 0; index < (size_t)length; ++index) {
        if (foundation_runtime_bytes_at(handle, index, &value) != 0) {
            OPENSSL_clear_free(result->data, (size_t)length);
            result->data = NULL;
            return 0;
        }
        result->data[index] = (unsigned char)value;
    }
    result->length = (size_t)length;
    return 1;
}

static void foundation_quic_free_bytes(foundation_quic_bytes* value) {
    if (value->data != NULL) {
        OPENSSL_clear_free(value->data, value->length);
    }
    value->data = NULL;
    value->length = 0;
}

static int32_t foundation_quic_output_bytes(const unsigned char* data, size_t length,
                                            uint64_t* result) {
    if (result == NULL)
        return FOUNDATION_OPENSSL_FAILED;
    return foundation_runtime_bytes_copy_from_raw(data, (uint64_t)length, result) == 0
               ? FOUNDATION_OPENSSL_OK
               : FOUNDATION_OPENSSL_FAILED;
}

static int32_t foundation_quic_output_string(const char* data, size_t length, fdn_string* result) {
    char* copy;
    if (result == NULL || (length != 0 && !fdn_utf8_valid(data, length))) {
        return FOUNDATION_OPENSSL_PROTOCOL_FAILED;
    }
    fdn_string_drop(result);
    *result = fdn_string_static("", 0);
    if (length == 0)
        return FOUNDATION_OPENSSL_OK;
    copy = fdn_alloc(length);
    if (copy == NULL)
        return FOUNDATION_OPENSSL_FAILED;
    memcpy(copy, data, length);
    result->data = copy;
    result->length = length;
    result->owned = 1;
    return FOUNDATION_OPENSSL_OK;
}

static int foundation_quic_copy_text(const fdn_string* value, char** result) {
    char* copy;
    if (value == NULL || result == NULL || value->length == 0 || value->length > SIZE_MAX - 1 ||
        memchr(value->data, '\0', value->length) != NULL) {
        return 0;
    }
    copy = OPENSSL_malloc(value->length + 1);
    if (copy == NULL)
        return 0;
    memcpy(copy, value->data, value->length);
    copy[value->length] = '\0';
    *result = copy;
    return 1;
}

static int foundation_quic_protocol(const fdn_string* value, unsigned char* wire,
                                    size_t* wire_length) {
    if (value == NULL || value->length == 0 || value->length > 255 ||
        memchr(value->data, '\0', value->length) != NULL) {
        return 0;
    }
    wire[0] = (unsigned char)value->length;
    memcpy(wire + 1, value->data, value->length);
    *wire_length = value->length + 1;
    return 1;
}

static SSL_CTX* foundation_quic_server_context(uint64_t certificate_handle,
                                               uint64_t private_key_handle) {
    foundation_quic_bytes certificate = {0};
    foundation_quic_bytes private_key = {0};
    BIO* certificate_bio = NULL;
    BIO* private_key_bio = NULL;
    X509* x509 = NULL;
    X509* chain = NULL;
    EVP_PKEY* key = NULL;
    SSL_CTX* context = NULL;

    if (!foundation_quic_copy_bytes(certificate_handle, &certificate) ||
        !foundation_quic_copy_bytes(private_key_handle, &private_key) ||
        certificate.length > INT_MAX || private_key.length > INT_MAX) {
        goto cleanup;
    }
    certificate_bio = BIO_new_mem_buf(certificate.data, (int)certificate.length);
    private_key_bio = BIO_new_mem_buf(private_key.data, (int)private_key.length);
    if (certificate_bio == NULL || private_key_bio == NULL)
        goto cleanup;
    x509 = PEM_read_bio_X509(certificate_bio, NULL, NULL, NULL);
    key = PEM_read_bio_PrivateKey(private_key_bio, NULL, NULL, NULL);
    context = SSL_CTX_new(OSSL_QUIC_server_method());
    if (x509 == NULL || key == NULL || context == NULL ||
        SSL_CTX_set_domain_flags(context,
                                 SSL_DOMAIN_FLAG_THREAD_ASSISTED | SSL_DOMAIN_FLAG_BLOCKING) != 1 ||
        SSL_CTX_use_certificate(context, x509) != 1 || SSL_CTX_use_PrivateKey(context, key) != 1 ||
        SSL_CTX_check_private_key(context) != 1) {
        SSL_CTX_free(context);
        context = NULL;
        goto cleanup;
    }
    while ((chain = PEM_read_bio_X509(certificate_bio, NULL, NULL, NULL)) != NULL) {
        if (SSL_CTX_add_extra_chain_cert(context, chain) != 1) {
            X509_free(chain);
            chain = NULL;
            SSL_CTX_free(context);
            context = NULL;
            break;
        }
        chain = NULL;
    }
    ERR_clear_error();

cleanup:
    X509_free(chain);
    X509_free(x509);
    EVP_PKEY_free(key);
    BIO_free(certificate_bio);
    BIO_free(private_key_bio);
    foundation_quic_free_bytes(&certificate);
    foundation_quic_free_bytes(&private_key);
    return context;
}

static int foundation_quic_select_alpn(SSL* ssl, const unsigned char** selected,
                                       unsigned char* selected_length, const unsigned char* offered,
                                       unsigned int offered_length, void* argument) {
    foundation_quic_listener* listener = argument;
    size_t offset = 0;
    (void)ssl;
    if (listener == NULL || selected == NULL || selected_length == NULL) {
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }
    while (offset < offered_length) {
        size_t length = offered[offset++];
        if (length == 0 || length > offered_length - offset) {
            return SSL_TLSEXT_ERR_ALERT_FATAL;
        }
        if (length == listener->protocol_length &&
            CRYPTO_memcmp(offered + offset, listener->protocol, length) == 0) {
            *selected = offered + offset;
            *selected_length = (unsigned char)length;
            return SSL_TLSEXT_ERR_OK;
        }
        offset += length;
    }
    return SSL_TLSEXT_ERR_ALERT_FATAL;
}

static void foundation_quic_listener_registry_lock(void) {
    while (
        atomic_flag_test_and_set_explicit(&foundation_quic_listeners_lock, memory_order_acquire)) {
    }
}

static void foundation_quic_listener_registry_unlock(void) {
    atomic_flag_clear_explicit(&foundation_quic_listeners_lock, memory_order_release);
}

static foundation_quic_listener_record* foundation_quic_listener_acquire(uint64_t identifier) {
    foundation_quic_listener_record* record;
    foundation_quic_listener_registry_lock();
    for (record = foundation_quic_listeners; record != NULL; record = record->next) {
        if (record->identifier == identifier && !record->closing) {
            ++record->users;
            foundation_quic_listener_registry_unlock();
            return record;
        }
    }
    foundation_quic_listener_registry_unlock();
    return NULL;
}

static void foundation_quic_listener_release(foundation_quic_listener_record* record) {
    foundation_quic_listener_registry_lock();
    --record->users;
    foundation_quic_listener_registry_unlock();
}

static int foundation_quic_listener_is_closing(foundation_quic_listener_record* record) {
    int closing;
    foundation_quic_listener_registry_lock();
    closing = record->closing;
    foundation_quic_listener_registry_unlock();
    return closing;
}

static foundation_quic_connection* foundation_quic_connection_new(SSL* ssl, SSL_CTX* context,
                                                                  const char* peer) {
    foundation_quic_connection* connection = OPENSSL_zalloc(sizeof(*connection));
    if (connection == NULL)
        return NULL;
    connection->ssl = ssl;
    connection->context = context;
    atomic_init(&connection->references, 1);
    atomic_init(&connection->closing, 0);
    if (peer != NULL) {
        strncpy(connection->peer, peer, sizeof(connection->peer) - 1);
    }
    return connection;
}

static void foundation_quic_connection_release_reference(foundation_quic_connection* connection);

static int foundation_quic_connection_add_reference(foundation_quic_connection* connection) {
    size_t references;
    if (connection == NULL || atomic_load_explicit(&connection->closing, memory_order_acquire)) {
        return 0;
    }
    references = atomic_load_explicit(&connection->references, memory_order_relaxed);
    while (references != 0) {
        if (atomic_compare_exchange_weak_explicit(&connection->references, &references,
                                                  references + 1, memory_order_acq_rel,
                                                  memory_order_relaxed)) {
            if (atomic_load_explicit(&connection->closing, memory_order_acquire)) {
                foundation_quic_connection_release_reference(connection);
                return 0;
            }
            return 1;
        }
    }
    return 0;
}

static void foundation_quic_connection_shutdown(foundation_quic_connection* connection) {
    if (connection == NULL ||
        atomic_exchange_explicit(&connection->closing, 1, memory_order_acq_rel)) {
        return;
    }
    (void)SSL_shutdown_ex(connection->ssl,
                          SSL_SHUTDOWN_FLAG_NO_STREAM_FLUSH | SSL_SHUTDOWN_FLAG_NO_BLOCK, NULL, 0);
}

static void foundation_quic_connection_release_reference(foundation_quic_connection* connection) {
    if (connection == NULL ||
        atomic_fetch_sub_explicit(&connection->references, 1, memory_order_acq_rel) != 1) {
        return;
    }
    foundation_quic_connection_shutdown(connection);
    SSL_free(connection->ssl);
    SSL_CTX_free(connection->context);
    OPENSSL_free(connection);
    foundation_quic_network_close();
}

static foundation_quic_stream* foundation_quic_stream_new(foundation_quic_connection* connection,
                                                          SSL* ssl) {
    foundation_quic_stream_state* state = NULL;
    foundation_quic_stream* stream = NULL;
    if (!foundation_quic_connection_add_reference(connection))
        return NULL;
    state = OPENSSL_zalloc(sizeof(*state));
    stream = OPENSSL_zalloc(sizeof(*stream));
    if (state == NULL || stream == NULL) {
        OPENSSL_free(state);
        OPENSSL_free(stream);
        foundation_quic_connection_release_reference(connection);
        return NULL;
    }
    state->ssl = ssl;
    state->connection = connection;
    atomic_init(&state->references, 1);
    atomic_init(&state->concluded, 0);
    atomic_init(&state->reset, 0);
    stream->state = state;
    stream->role = FOUNDATION_QUIC_STREAM_OWNER;
    return stream;
}

static int foundation_quic_stream_conclude(foundation_quic_stream_state* state) {
    return SSL_stream_conclude(state->ssl, 0);
}

static int32_t foundation_quic_stream_flush(foundation_quic_stream_state* state) {
    uint64_t pending = 0;
    for (;;) {
        if (fdn_task_cancellation_current())
            return FOUNDATION_OPENSSL_CANCELLED;
        if (atomic_load_explicit(&state->reset, memory_order_acquire))
            return FOUNDATION_OPENSSL_RESET;
        if (atomic_load_explicit(&state->connection->closing, memory_order_acquire))
            return FOUNDATION_OPENSSL_CLOSED;
        if (SSL_get_stream_write_buf_used(state->ssl, &pending) != 1)
            return FOUNDATION_OPENSSL_FAILED;
        if (pending == 0)
            return FOUNDATION_OPENSSL_OK;
        if (SSL_handle_events(state->connection->ssl) != 1)
            return FOUNDATION_OPENSSL_FAILED;
        foundation_quic_pause();
    }
}

static void foundation_quic_stream_release(foundation_quic_stream* stream) {
    foundation_quic_stream_state* state;
    if (stream == NULL)
        return;
    state = stream->state;
    if ((stream->role == FOUNDATION_QUIC_STREAM_OWNER ||
         stream->role == FOUNDATION_QUIC_STREAM_WRITER) &&
        !atomic_exchange_explicit(&state->concluded, 1, memory_order_acq_rel) &&
        !atomic_load_explicit(&state->reset, memory_order_acquire)) {
        (void)foundation_quic_stream_conclude(state);
    }
    OPENSSL_free(stream);
    if (atomic_fetch_sub_explicit(&state->references, 1, memory_order_acq_rel) != 1) {
        return;
    }
    (void)SSL_handle_events(state->connection->ssl);
    SSL_free(state->ssl);
    foundation_quic_connection_release_reference(state->connection);
    OPENSSL_free(state);
}

static int32_t foundation_quic_stream_error(SSL* ssl, int result, int writing) {
    int error = SSL_get_error(ssl, result);
    int state = writing ? SSL_get_stream_write_state(ssl) : SSL_get_stream_read_state(ssl);
    if (error == SSL_ERROR_ZERO_RETURN)
        return FOUNDATION_OPENSSL_CLOSED;
    if (state == SSL_STREAM_STATE_RESET_REMOTE) {
        return FOUNDATION_OPENSSL_RESET;
    }
    if (state == SSL_STREAM_STATE_CONN_CLOSED) {
        return FOUNDATION_OPENSSL_CLOSED;
    }
    return FOUNDATION_OPENSSL_FAILED;
}

int32_t foundation_openssl_quic_listener_open(const fdn_string* address, uint64_t port,
                                              uint64_t certificate, uint64_t private_key,
                                              const fdn_string* protocol, uint64_t* result,
                                              uint64_t* bound_port) {
    char* host = NULL;
    char service[6];
    BIO_ADDRINFO* addresses = NULL;
    const BIO_ADDRINFO* current;
    foundation_quic_listener* listener = NULL;
    foundation_quic_listener_record* record = NULL;
    foundation_quic_socket socket = FOUNDATION_QUIC_INVALID_SOCKET;
    unsigned char wire_protocol[256];
    size_t wire_length = 0;
    int32_t status = FOUNDATION_OPENSSL_FAILED;

    if (result == NULL || bound_port == NULL || port > 65535 ||
        !foundation_quic_copy_text(address, &host) ||
        !foundation_quic_protocol(protocol, wire_protocol, &wire_length)) {
        OPENSSL_free(host);
        return FOUNDATION_OPENSSL_INVALID_SERVER_NAME;
    }
    *result = 0;
    *bound_port = 0;
    if (!foundation_quic_network_open()) {
        status = FOUNDATION_OPENSSL_UNAVAILABLE;
        goto cleanup;
    }
    if (snprintf(service, sizeof(service), "%u", (unsigned int)port) < 0 ||
        !BIO_lookup_ex(host, service, BIO_LOOKUP_SERVER, AF_UNSPEC, SOCK_DGRAM, IPPROTO_UDP,
                       &addresses)) {
        goto cleanup_network;
    }
    for (current = addresses; current != NULL; current = BIO_ADDRINFO_next(current)) {
        socket = (foundation_quic_socket)BIO_socket(BIO_ADDRINFO_family(current), SOCK_DGRAM,
                                                    IPPROTO_UDP, 0);
        if (socket == FOUNDATION_QUIC_INVALID_SOCKET)
            continue;
        if (BIO_socket_nbio((int)socket, 1) &&
            BIO_bind((int)socket, BIO_ADDRINFO_address(current), BIO_SOCK_REUSEADDR)) {
            break;
        }
        foundation_quic_close(socket);
        socket = FOUNDATION_QUIC_INVALID_SOCKET;
    }
    if (socket == FOUNDATION_QUIC_INVALID_SOCKET)
        goto cleanup_network;
    listener = OPENSSL_zalloc(sizeof(*listener));
    if (listener == NULL)
        goto cleanup_network;
    listener->socket = socket;
    memcpy(listener->protocol, protocol->data, protocol->length);
    listener->protocol_length = protocol->length;
    listener->context = foundation_quic_server_context(certificate, private_key);
    if (listener->context == NULL)
        goto cleanup_network;
    SSL_CTX_set_verify(listener->context, SSL_VERIFY_NONE, NULL);
    SSL_CTX_set_alpn_select_cb(listener->context, foundation_quic_select_alpn, listener);
    listener->ssl = SSL_new_listener(listener->context, 0);
    if (listener->ssl == NULL || SSL_set_fd(listener->ssl, (int)socket) != 1 ||
        SSL_listen(listener->ssl) != 1) {
        goto cleanup_network;
    }
    {
        struct sockaddr_storage bound;
#ifdef _WIN32
        int length = sizeof(bound);
#else
        socklen_t length = sizeof(bound);
#endif
        if (getsockname(socket, (struct sockaddr*)&bound, &length) != 0) {
            goto cleanup_network;
        }
        if (bound.ss_family == AF_INET) {
            *bound_port = ntohs(((struct sockaddr_in*)&bound)->sin_port);
        } else if (bound.ss_family == AF_INET6) {
            *bound_port = ntohs(((struct sockaddr_in6*)&bound)->sin6_port);
        } else {
            goto cleanup_network;
        }
    }
    record = OPENSSL_zalloc(sizeof(*record));
    if (record == NULL)
        goto cleanup_network;
    record->listener = listener;
    foundation_quic_listener_registry_lock();
    do {
        record->identifier =
            atomic_fetch_add_explicit(&foundation_quic_next_listener, 1, memory_order_relaxed);
    } while (record->identifier == 0);
    record->next = foundation_quic_listeners;
    foundation_quic_listeners = record;
    foundation_quic_listener_registry_unlock();
    *result = record->identifier;
    socket = FOUNDATION_QUIC_INVALID_SOCKET;
    listener = NULL;
    record = NULL;
    status = FOUNDATION_OPENSSL_OK;
    goto cleanup;

cleanup_network:
    if (record != NULL)
        OPENSSL_free(record);
    if (listener != NULL) {
        SSL_free(listener->ssl);
        SSL_CTX_free(listener->context);
        OPENSSL_free(listener);
    }
    if (socket != FOUNDATION_QUIC_INVALID_SOCKET)
        foundation_quic_close(socket);
    foundation_quic_network_close();
cleanup:
    BIO_ADDRINFO_free(addresses);
    OPENSSL_free(host);
    return status;
}

void foundation_openssl_quic_listener_close(uint64_t* handle) {
    foundation_quic_listener_record* record;
    foundation_quic_listener_record** cursor;
    foundation_quic_listener* listener;
    uint64_t identifier;
    if (handle == NULL || *handle == 0)
        return;
    identifier = *handle;
    foundation_quic_listener_registry_lock();
    for (cursor = &foundation_quic_listeners;
         *cursor != NULL && (*cursor)->identifier != identifier; cursor = &(*cursor)->next) {
    }
    if (*cursor == NULL || (*cursor)->closing) {
        foundation_quic_listener_registry_unlock();
        *handle = 0;
        return;
    }
    record = *cursor;
    record->closing = 1;
    listener = record->listener;
    *handle = 0;
    foundation_quic_shutdown(listener->socket);
    foundation_quic_close(listener->socket);
    foundation_quic_listener_registry_unlock();
    for (;;) {
        size_t users;
        foundation_quic_listener_registry_lock();
        users = record->users;
        foundation_quic_listener_registry_unlock();
        if (users == 0)
            break;
        foundation_quic_pause();
    }
    foundation_quic_listener_registry_lock();
    for (cursor = &foundation_quic_listeners; *cursor != NULL && *cursor != record;
         cursor = &(*cursor)->next) {
    }
    if (*cursor == record)
        *cursor = record->next;
    foundation_quic_listener_registry_unlock();
    SSL_free(listener->ssl);
    SSL_CTX_free(listener->context);
    OPENSSL_free(listener);
    OPENSSL_free(record);
    foundation_quic_network_close();
}

int32_t foundation_openssl_quic_listener_accept(uint64_t handle, uint64_t* result) {
    foundation_quic_listener_record* record = foundation_quic_listener_acquire(handle);
    foundation_quic_connection* connection = NULL;
    SSL* ssl = NULL;
    BIO_ADDR* peer = NULL;
    char* host = NULL;
    int32_t status = FOUNDATION_OPENSSL_HANDSHAKE_FAILED;
    int error;
    if (record == NULL || result == NULL)
        return FOUNDATION_OPENSSL_FAILED;
    *result = 0;
    for (;;) {
        if (fdn_task_cancellation_current()) {
            status = FOUNDATION_OPENSSL_CANCELLED;
            goto cleanup;
        }
        ERR_clear_error();
        ssl = SSL_accept_connection(record->listener->ssl, SSL_ACCEPT_CONNECTION_NO_BLOCK);
        if (ssl != NULL)
            break;
        if (foundation_quic_listener_is_closing(record)) {
            status = FOUNDATION_OPENSSL_CLOSED;
            goto cleanup;
        }
        if (ERR_peek_error() == 0) {
            foundation_quic_pause();
            continue;
        }
        error = SSL_get_error(record->listener->ssl, 0);
        if (error != SSL_ERROR_NONE && error != SSL_ERROR_WANT_READ &&
            error != SSL_ERROR_WANT_WRITE && error != SSL_ERROR_WANT_ACCEPT) {
            goto cleanup;
        }
        foundation_quic_pause();
    }
    if (ssl == NULL || SSL_set_default_stream_mode(ssl, SSL_DEFAULT_STREAM_MODE_NONE) != 1 ||
        SSL_set_incoming_stream_policy(ssl, SSL_INCOMING_STREAM_POLICY_ACCEPT, 0) != 1) {
        goto cleanup;
    }
    while (!SSL_is_init_finished(ssl)) {
        if (fdn_task_cancellation_current()) {
            status = FOUNDATION_OPENSSL_CANCELLED;
            goto cleanup;
        }
        if (foundation_quic_listener_is_closing(record)) {
            status = FOUNDATION_OPENSSL_CLOSED;
            goto cleanup;
        }
        ERR_clear_error();
        if (SSL_handle_events(ssl) != 1)
            goto cleanup;
        foundation_quic_pause();
    }
    peer = BIO_ADDR_new();
    if (peer != NULL && BIO_dgram_get_peer(SSL_get_rbio(ssl), peer)) {
        host = BIO_ADDR_hostname_string(peer, 1);
    }
    if (!foundation_quic_network_open()) {
        status = FOUNDATION_OPENSSL_UNAVAILABLE;
        goto cleanup;
    }
    connection = foundation_quic_connection_new(ssl, NULL, host == NULL ? "" : host);
    if (connection == NULL) {
        foundation_quic_network_close();
        goto cleanup;
    }
    *result = (uint64_t)(uintptr_t)connection;
    ssl = NULL;
    status = FOUNDATION_OPENSSL_OK;

cleanup:
    OPENSSL_free(host);
    BIO_ADDR_free(peer);
    SSL_free(ssl);
    foundation_quic_listener_release(record);
    return status;
}

int32_t foundation_openssl_quic_connect_pinned(const fdn_string* server_name, uint64_t port,
                                               uint64_t raw_public_key, const fdn_string* protocol,
                                               uint64_t* result) {
    char* host = NULL;
    char service[6];
    BIO_ADDRINFO* addresses = NULL;
    const BIO_ADDRINFO* current;
    BIO_ADDR* peer = NULL;
    BIO* bio = NULL;
    SSL_CTX* context = NULL;
    SSL* ssl = NULL;
    foundation_quic_connection* connection = NULL;
    foundation_quic_bytes expected = {0};
    X509* certificate = NULL;
    EVP_PKEY* public_key = NULL;
    unsigned char actual[32];
    size_t actual_length = sizeof(actual);
    unsigned char wire_protocol[256];
    size_t wire_length = 0;
    const unsigned char* selected = NULL;
    unsigned int selected_length = 0;
    int socket = -1;
    int32_t status = FOUNDATION_OPENSSL_HANDSHAKE_FAILED;

    if (result == NULL || port == 0 || port > 65535 ||
        !foundation_quic_copy_text(server_name, &host) ||
        !foundation_quic_protocol(protocol, wire_protocol, &wire_length)) {
        OPENSSL_free(host);
        return FOUNDATION_OPENSSL_INVALID_SERVER_NAME;
    }
    *result = 0;
    if (!foundation_quic_copy_bytes(raw_public_key, &expected) ||
        expected.length != sizeof(actual)) {
        status = FOUNDATION_OPENSSL_INVALID_PUBLIC_KEY;
        goto cleanup;
    }
    if (!foundation_quic_network_open()) {
        status = FOUNDATION_OPENSSL_UNAVAILABLE;
        goto cleanup;
    }
    if (snprintf(service, sizeof(service), "%u", (unsigned int)port) < 0 ||
        !BIO_lookup_ex(host, service, BIO_LOOKUP_CLIENT, AF_UNSPEC, SOCK_DGRAM, IPPROTO_UDP,
                       &addresses)) {
        goto cleanup_network;
    }
    for (current = addresses; current != NULL; current = BIO_ADDRINFO_next(current)) {
        socket = BIO_socket(BIO_ADDRINFO_family(current), SOCK_DGRAM, IPPROTO_UDP, 0);
        if (socket == -1)
            continue;
        if (BIO_connect(socket, BIO_ADDRINFO_address(current), 0) && BIO_socket_nbio(socket, 1)) {
            peer = BIO_ADDR_dup(BIO_ADDRINFO_address(current));
            if (peer != NULL)
                break;
        }
        BIO_closesocket(socket);
        socket = -1;
    }
    if (socket == -1 || peer == NULL)
        goto cleanup_network;
    bio = BIO_new(BIO_s_datagram());
    if (bio == NULL)
        goto cleanup_network;
    BIO_set_fd(bio, socket, BIO_CLOSE);
    socket = -1;
    context = SSL_CTX_new(OSSL_QUIC_client_method());
    if (context == NULL || SSL_CTX_set_domain_flags(context, SSL_DOMAIN_FLAG_THREAD_ASSISTED |
                                                                 SSL_DOMAIN_FLAG_BLOCKING) != 1) {
        goto cleanup_network;
    }
    SSL_CTX_set_verify(context, SSL_VERIFY_NONE, NULL);
    ssl = SSL_new(context);
    if (ssl == NULL || SSL_set_default_stream_mode(ssl, SSL_DEFAULT_STREAM_MODE_NONE) != 1 ||
        SSL_set_incoming_stream_policy(ssl, SSL_INCOMING_STREAM_POLICY_ACCEPT, 0) != 1 ||
        SSL_set_tlsext_host_name(ssl, host) != 1 ||
        SSL_set_alpn_protos(ssl, wire_protocol, (unsigned int)wire_length) != 0 ||
        SSL_set1_initial_peer_addr(ssl, peer) != 1) {
        goto cleanup_network;
    }
    SSL_set_bio(ssl, bio, bio);
    bio = NULL;
    if (SSL_connect(ssl) != 1)
        goto cleanup_network;
    SSL_get0_alpn_selected(ssl, &selected, &selected_length);
    if (selected_length != protocol->length ||
        CRYPTO_memcmp(selected, protocol->data, protocol->length) != 0) {
        status = FOUNDATION_OPENSSL_PROTOCOL_FAILED;
        goto cleanup_network;
    }
    certificate = SSL_get1_peer_certificate(ssl);
    public_key = certificate == NULL ? NULL : X509_get_pubkey(certificate);
    if (public_key == NULL || EVP_PKEY_base_id(public_key) != EVP_PKEY_ED25519 ||
        EVP_PKEY_get_raw_public_key(public_key, actual, &actual_length) != 1 ||
        actual_length != expected.length ||
        CRYPTO_memcmp(actual, expected.data, expected.length) != 0) {
        status = FOUNDATION_OPENSSL_PEER_REJECTED;
        goto cleanup_network;
    }
    connection = foundation_quic_connection_new(ssl, context, host);
    if (connection == NULL)
        goto cleanup_network;
    *result = (uint64_t)(uintptr_t)connection;
    ssl = NULL;
    context = NULL;
    status = FOUNDATION_OPENSSL_OK;
    goto cleanup;

cleanup_network:
    BIO_free(bio);
    if (socket != -1)
        BIO_closesocket(socket);
    SSL_free(ssl);
    SSL_CTX_free(context);
    foundation_quic_network_close();
cleanup:
    OPENSSL_cleanse(actual, sizeof(actual));
    EVP_PKEY_free(public_key);
    X509_free(certificate);
    foundation_quic_free_bytes(&expected);
    BIO_ADDR_free(peer);
    BIO_ADDRINFO_free(addresses);
    OPENSSL_free(host);
    return status;
}

uint64_t foundation_openssl_quic_connection_retain(uint64_t handle) {
    foundation_quic_connection* connection = (foundation_quic_connection*)(uintptr_t)handle;
    return foundation_quic_connection_add_reference(connection) ? handle : 0;
}

void foundation_openssl_quic_connection_release(uint64_t* handle) {
    foundation_quic_connection* connection;
    if (handle == NULL || *handle == 0)
        return;
    connection = (foundation_quic_connection*)(uintptr_t)*handle;
    *handle = 0;
    foundation_quic_connection_release_reference(connection);
}

void foundation_openssl_quic_connection_close(uint64_t* handle) {
    foundation_quic_connection* connection;
    if (handle == NULL || *handle == 0)
        return;
    connection = (foundation_quic_connection*)(uintptr_t)*handle;
    *handle = 0;
    foundation_quic_connection_shutdown(connection);
    foundation_quic_connection_release_reference(connection);
}

int32_t foundation_openssl_quic_connection_peer(uint64_t handle, fdn_string* result) {
    foundation_quic_connection* connection = (foundation_quic_connection*)(uintptr_t)handle;
    if (connection == NULL)
        return FOUNDATION_OPENSSL_FAILED;
    return foundation_quic_output_string(connection->peer, strlen(connection->peer), result);
}

static int32_t foundation_quic_connection_stream(uint64_t handle, uint64_t* result, int accept) {
    foundation_quic_connection* connection = (foundation_quic_connection*)(uintptr_t)handle;
    foundation_quic_stream* stream = NULL;
    SSL* ssl = NULL;
    int peer_closed = 0;
    if (connection == NULL || result == NULL ||
        !foundation_quic_connection_add_reference(connection)) {
        return FOUNDATION_OPENSSL_CLOSED;
    }
    *result = 0;
    if (accept) {
        for (;;) {
            SSL_CONN_CLOSE_INFO close_info = {0};
            int error;
            if (fdn_task_cancellation_current()) {
                foundation_quic_connection_release_reference(connection);
                return FOUNDATION_OPENSSL_CANCELLED;
            }
            ERR_clear_error();
            ssl = SSL_accept_stream(connection->ssl, SSL_ACCEPT_STREAM_NO_BLOCK);
            if (ssl != NULL)
                break;
            if (atomic_load_explicit(&connection->closing, memory_order_acquire)) {
                break;
            }
            error = SSL_get_error(connection->ssl, 0);
            if (error == SSL_ERROR_ZERO_RETURN) {
                peer_closed = 1;
                break;
            }
            if (ERR_peek_error() != 0 && error != SSL_ERROR_NONE && error != SSL_ERROR_WANT_READ &&
                error != SSL_ERROR_WANT_WRITE && error != SSL_ERROR_WANT_ACCEPT) {
                break;
            }
            (void)SSL_handle_events(connection->ssl);
            if (SSL_get_conn_close_info(connection->ssl, &close_info, sizeof(close_info)) == 1) {
                peer_closed = 1;
                break;
            }
            if ((SSL_get_shutdown(connection->ssl) & SSL_RECEIVED_SHUTDOWN) != 0) {
                peer_closed = 1;
                break;
            }
            foundation_quic_pause();
        }
    } else {
        ssl = SSL_new_stream(connection->ssl, 0);
    }
    if (ssl == NULL) {
        const int closing = atomic_load_explicit(&connection->closing, memory_order_acquire);
        foundation_quic_connection_release_reference(connection);
        return closing || peer_closed ? FOUNDATION_OPENSSL_CLOSED : FOUNDATION_OPENSSL_FAILED;
    }
    if (SSL_set_blocking_mode(ssl, 0) != 1 ||
        SSL_set_event_handling_mode(ssl, SSL_VALUE_EVENT_HANDLING_MODE_IMPLICIT) != 1) {
        SSL_free(ssl);
        foundation_quic_connection_release_reference(connection);
        return FOUNDATION_OPENSSL_FAILED;
    }
    stream = foundation_quic_stream_new(connection, ssl);
    foundation_quic_connection_release_reference(connection);
    if (stream == NULL) {
        SSL_free(ssl);
        return FOUNDATION_OPENSSL_FAILED;
    }
    *result = (uint64_t)(uintptr_t)stream;
    return FOUNDATION_OPENSSL_OK;
}

int32_t foundation_openssl_quic_connection_open_stream(uint64_t connection, uint64_t* stream) {
    return foundation_quic_connection_stream(connection, stream, 0);
}

int32_t foundation_openssl_quic_connection_accept_stream(uint64_t connection, uint64_t* stream) {
    return foundation_quic_connection_stream(connection, stream, 1);
}

void foundation_openssl_quic_stream_close(uint64_t* handle) {
    foundation_quic_stream* stream;
    if (handle == NULL || *handle == 0)
        return;
    stream = (foundation_quic_stream*)(uintptr_t)*handle;
    *handle = 0;
    foundation_quic_stream_release(stream);
}

void foundation_openssl_quic_stream_abort(uint64_t* handle) {
    foundation_quic_stream* stream;
    foundation_quic_stream_state* state;
    if (handle == NULL || *handle == 0)
        return;
    stream = (foundation_quic_stream*)(uintptr_t)*handle;
    state = stream->state;
    if (!atomic_exchange_explicit(&state->reset, 1, memory_order_acq_rel)) {
        (void)SSL_stream_reset(state->ssl, NULL, 0);
    }
    foundation_openssl_quic_stream_close(handle);
}

int32_t foundation_openssl_quic_stream_finish(uint64_t* handle) {
    foundation_quic_stream* stream;
    foundation_quic_stream_state* state;
    int32_t status = FOUNDATION_OPENSSL_OK;
    if (handle == NULL || *handle == 0)
        return FOUNDATION_OPENSSL_FAILED;
    stream = (foundation_quic_stream*)(uintptr_t)*handle;
    state = stream->state;
    *handle = 0;
    if (stream->role != FOUNDATION_QUIC_STREAM_WRITER &&
        stream->role != FOUNDATION_QUIC_STREAM_OWNER) {
        status = FOUNDATION_OPENSSL_FAILED;
        goto cleanup;
    }
    if (!atomic_exchange_explicit(&state->concluded, 1, memory_order_acq_rel) &&
        foundation_quic_stream_conclude(state) != 1) {
        status = foundation_quic_stream_error(state->ssl, 0, 1);
        goto cleanup;
    }
    status = foundation_quic_stream_flush(state);

cleanup:
    foundation_quic_stream_release(stream);
    return status;
}

int32_t foundation_openssl_quic_stream_split_controlled(uint64_t* handle, uint64_t* reader,
                                                        uint64_t* writer, uint64_t* controller) {
    foundation_quic_stream* stream;
    foundation_quic_stream* read_handle = NULL;
    foundation_quic_stream* write_handle = NULL;
    foundation_quic_stream* control_handle = NULL;
    if (handle == NULL || reader == NULL || writer == NULL || controller == NULL || *handle == 0) {
        return FOUNDATION_OPENSSL_FAILED;
    }
    *reader = 0;
    *writer = 0;
    *controller = 0;
    stream = (foundation_quic_stream*)(uintptr_t)*handle;
    read_handle = OPENSSL_malloc(sizeof(*read_handle));
    write_handle = OPENSSL_malloc(sizeof(*write_handle));
    control_handle = OPENSSL_malloc(sizeof(*control_handle));
    if (read_handle == NULL || write_handle == NULL || control_handle == NULL) {
        OPENSSL_free(read_handle);
        OPENSSL_free(write_handle);
        OPENSSL_free(control_handle);
        return FOUNDATION_OPENSSL_FAILED;
    }
    read_handle->state = stream->state;
    read_handle->role = FOUNDATION_QUIC_STREAM_READER;
    write_handle->state = stream->state;
    write_handle->role = FOUNDATION_QUIC_STREAM_WRITER;
    control_handle->state = stream->state;
    control_handle->role = FOUNDATION_QUIC_STREAM_CONTROLLER;
    atomic_fetch_add_explicit(&stream->state->references, 3, memory_order_relaxed);
    *reader = (uint64_t)(uintptr_t)read_handle;
    *writer = (uint64_t)(uintptr_t)write_handle;
    *controller = (uint64_t)(uintptr_t)control_handle;
    *handle = 0;
    stream->role = FOUNDATION_QUIC_STREAM_CONTROLLER;
    foundation_quic_stream_release(stream);
    return FOUNDATION_OPENSSL_OK;
}

uint64_t foundation_openssl_quic_stream_id(uint64_t handle) {
    foundation_quic_stream* stream = (foundation_quic_stream*)(uintptr_t)handle;
    if (stream == NULL)
        return UINT64_MAX;
    return SSL_get_stream_id(stream->state->ssl);
}

uint64_t foundation_openssl_quic_stream_control(uint64_t handle) {
    foundation_quic_stream* stream = (foundation_quic_stream*)(uintptr_t)handle;
    foundation_quic_stream* control;
    if (stream == NULL)
        return 0;
    control = OPENSSL_malloc(sizeof(*control));
    if (control == NULL)
        return 0;
    control->state = stream->state;
    control->role = FOUNDATION_QUIC_STREAM_CONTROLLER;
    atomic_fetch_add_explicit(&stream->state->references, 1, memory_order_relaxed);
    return (uint64_t)(uintptr_t)control;
}

static int32_t foundation_quic_stream_read_bytes(uint64_t handle, uint64_t length, int exact,
                                                 uint64_t* result) {
    foundation_quic_stream* stream = (foundation_quic_stream*)(uintptr_t)handle;
    unsigned char* buffer = NULL;
    size_t offset = 0;
    int32_t status = FOUNDATION_OPENSSL_FAILED;
    if (stream == NULL || result == NULL || length == 0 || length > SIZE_MAX) {
        return FOUNDATION_OPENSSL_LIMIT_EXCEEDED;
    }
    *result = 0;
    buffer = OPENSSL_malloc((size_t)length);
    if (buffer == NULL)
        return FOUNDATION_OPENSSL_FAILED;
    while (offset < (size_t)length) {
        int read_result;
        int error;
        size_t read = 0;
        if (fdn_task_cancellation_current()) {
            status = FOUNDATION_OPENSSL_CANCELLED;
            goto cleanup;
        }
        if (atomic_load_explicit(&stream->state->reset, memory_order_acquire)) {
            status = FOUNDATION_OPENSSL_RESET;
            goto cleanup;
        }
        if (atomic_load_explicit(&stream->state->connection->closing, memory_order_acquire)) {
            status = FOUNDATION_OPENSSL_CLOSED;
            goto cleanup;
        }
        ERR_clear_error();
        read_result =
            SSL_read_ex(stream->state->ssl, buffer + offset, (size_t)length - offset, &read);
        if (read_result != 1) {
            error = SSL_get_error(stream->state->ssl, read_result);
            if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
                foundation_quic_pause();
                continue;
            }
            status = foundation_quic_stream_error(stream->state->ssl, read_result, 0);
            goto cleanup;
        }
        if (read == 0) {
            status = FOUNDATION_OPENSSL_CLOSED;
            goto cleanup;
        }
        offset += read;
        if (!exact)
            break;
    }
    status = foundation_quic_output_bytes(buffer, offset, result);

cleanup:
    OPENSSL_clear_free(buffer, (size_t)length);
    return status;
}

int32_t foundation_openssl_quic_stream_read(uint64_t stream, uint64_t limit, uint64_t* value) {
    return foundation_quic_stream_read_bytes(stream, limit, 0, value);
}

int32_t foundation_openssl_quic_stream_read_exact(uint64_t stream, uint64_t length,
                                                  uint64_t* value) {
    if (length == 0)
        return foundation_quic_output_bytes(NULL, 0, value);
    return foundation_quic_stream_read_bytes(stream, length, 1, value);
}

int32_t foundation_openssl_quic_stream_write(uint64_t handle, uint64_t value_handle) {
    foundation_quic_stream* stream = (foundation_quic_stream*)(uintptr_t)handle;
    foundation_quic_bytes value = {0};
    size_t offset = 0;
    int32_t status = FOUNDATION_OPENSSL_FAILED;
    if (stream == NULL || !foundation_quic_copy_bytes(value_handle, &value)) {
        return FOUNDATION_OPENSSL_FAILED;
    }
    while (offset < value.length) {
        int write_result;
        int error;
        size_t written = 0;
        if (fdn_task_cancellation_current()) {
            status = FOUNDATION_OPENSSL_CANCELLED;
            goto cleanup;
        }
        if (atomic_load_explicit(&stream->state->reset, memory_order_acquire)) {
            status = FOUNDATION_OPENSSL_RESET;
            goto cleanup;
        }
        if (atomic_load_explicit(&stream->state->connection->closing, memory_order_acquire)) {
            status = FOUNDATION_OPENSSL_CLOSED;
            goto cleanup;
        }
        ERR_clear_error();
        write_result =
            SSL_write_ex(stream->state->ssl, value.data + offset, value.length - offset, &written);
        if (write_result != 1) {
            error = SSL_get_error(stream->state->ssl, write_result);
            if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
                foundation_quic_pause();
                continue;
            }
            status = foundation_quic_stream_error(stream->state->ssl, write_result, 1);
            goto cleanup;
        }
        if (written == 0)
            goto cleanup;
        offset += written;
    }
    status = FOUNDATION_OPENSSL_OK;

cleanup:
    foundation_quic_free_bytes(&value);
    return status;
}
