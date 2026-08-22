#define _POSIX_C_SOURCE 200112L

#include "foundation_openssl.h"

#include <foundation/runtime.h>

#include <openssl/err.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
typedef SOCKET foundation_openssl_socket;
#define FOUNDATION_OPENSSL_INVALID_SOCKET INVALID_SOCKET
#define foundation_openssl_socket_close closesocket
#define FOUNDATION_OPENSSL_SOCKET_SHUTDOWN SD_BOTH
#else
#include <netdb.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
typedef int foundation_openssl_socket;
#define FOUNDATION_OPENSSL_INVALID_SOCKET (-1)
#define foundation_openssl_socket_close close
#define FOUNDATION_OPENSSL_SOCKET_SHUTDOWN SHUT_RDWR
#endif

#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif

typedef struct foundation_openssl_bytes {
    unsigned char *data;
    size_t length;
} foundation_openssl_bytes;

typedef struct foundation_openssl_connection {
    SSL_CTX *context;
    SSL *ssl;
    foundation_openssl_socket socket;
} foundation_openssl_connection;

static int foundation_openssl_cancelled(uint64_t cancellation);

typedef struct foundation_openssl_resolver {
    char *name;
    char service[6];
    struct addrinfo hints;
    struct addrinfo *addresses;
    _Atomic int complete;
    _Atomic int references;
} foundation_openssl_resolver;

static uint64_t foundation_openssl_monotonic_nanoseconds(void) {
#ifdef _WIN32
    LARGE_INTEGER frequency;
    LARGE_INTEGER value;
    if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&value) || frequency.QuadPart <= 0) return 0;
    return (uint64_t)(value.QuadPart / frequency.QuadPart) * 1000000000ULL +
        (uint64_t)((value.QuadPart % frequency.QuadPart) * 1000000000ULL / frequency.QuadPart);
#else
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0;
    return (uint64_t)value.tv_sec * 1000000000ULL + (uint64_t)value.tv_nsec;
#endif
}

static uint64_t foundation_openssl_deadline(int64_t timeout_nanoseconds) {
    uint64_t now = foundation_openssl_monotonic_nanoseconds();
    if (timeout_nanoseconds <= 0 || now > UINT64_MAX - (uint64_t)timeout_nanoseconds) return 0;
    return now + (uint64_t)timeout_nanoseconds;
}

static int foundation_openssl_deadline_remaining(uint64_t deadline, int64_t *result) {
    uint64_t now = foundation_openssl_monotonic_nanoseconds();
    if (deadline == 0 || now >= deadline) return 0;
    *result = (int64_t)(deadline - now);
    return 1;
}

static void foundation_openssl_resolver_release(foundation_openssl_resolver *resolver) {
    if (resolver == NULL || atomic_fetch_sub_explicit(&resolver->references, 1, memory_order_acq_rel) != 1) return;
    freeaddrinfo(resolver->addresses);
    OPENSSL_free(resolver->name);
    OPENSSL_free(resolver);
}

#ifdef _WIN32
static unsigned __stdcall foundation_openssl_resolver_run(void *argument) {
#else
static void *foundation_openssl_resolver_run(void *argument) {
#endif
    foundation_openssl_resolver *resolver = argument;
    resolver->addresses = NULL;
    getaddrinfo(resolver->name, resolver->service, &resolver->hints, &resolver->addresses);
    atomic_store_explicit(&resolver->complete, 1, memory_order_release);
    foundation_openssl_resolver_release(resolver);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static int foundation_openssl_resolve_until(const char *name, const char *service,
                                            const struct addrinfo *hints, uint64_t deadline,
                                            uint64_t cancellation, struct addrinfo **result) {
    foundation_openssl_resolver *resolver;
    size_t name_length;
    if (result == NULL || name == NULL || service == NULL || hints == NULL) return FOUNDATION_OPENSSL_FAILED;
    *result = NULL;
    name_length = strlen(name);
    resolver = OPENSSL_zalloc(sizeof(*resolver));
    if (resolver == NULL) return FOUNDATION_OPENSSL_FAILED;
    atomic_init(&resolver->complete, 0);
    atomic_init(&resolver->references, 1);
    resolver->name = OPENSSL_malloc(name_length + 1);
    if (resolver->name == NULL) { foundation_openssl_resolver_release(resolver); return FOUNDATION_OPENSSL_FAILED; }
    memcpy(resolver->name, name, name_length + 1);
    memcpy(resolver->service, service, sizeof(resolver->service));
    resolver->hints = *hints;
    atomic_store_explicit(&resolver->references, 2, memory_order_relaxed);
#ifdef _WIN32
    {
        uintptr_t thread = _beginthreadex(NULL, 0, foundation_openssl_resolver_run, resolver, 0, NULL);
        if (thread == 0) { foundation_openssl_resolver_release(resolver); foundation_openssl_resolver_release(resolver); return FOUNDATION_OPENSSL_FAILED; }
        CloseHandle((HANDLE)thread);
    }
#else
    {
        pthread_t thread;
        if (pthread_create(&thread, NULL, foundation_openssl_resolver_run, resolver) != 0) { foundation_openssl_resolver_release(resolver); foundation_openssl_resolver_release(resolver); return FOUNDATION_OPENSSL_FAILED; }
        pthread_detach(thread);
    }
#endif
    while (!atomic_load_explicit(&resolver->complete, memory_order_acquire)) {
        int64_t remaining;
        if (foundation_openssl_cancelled(cancellation)) { foundation_openssl_resolver_release(resolver); return FOUNDATION_OPENSSL_CANCELLED; }
        if (!foundation_openssl_deadline_remaining(deadline, &remaining)) { foundation_openssl_resolver_release(resolver); return FOUNDATION_OPENSSL_TIMEOUT; }
#ifdef _WIN32
        Sleep((DWORD)((remaining / 1000000) > 25 ? 25 : ((remaining / 1000000) == 0 ? 1 : (remaining / 1000000))));
#else
        {
            struct timespec delay = {
                .tv_sec = 0,
                .tv_nsec = remaining < 25000000 ? (long)remaining : 25000000
            };
            if (delay.tv_nsec == 0) delay.tv_nsec = 1;
            nanosleep(&delay, NULL);
        }
#endif
    }
    *result = resolver->addresses;
    resolver->addresses = NULL;
    foundation_openssl_resolver_release(resolver);
    return FOUNDATION_OPENSSL_OK;
}

static int foundation_openssl_network_open(void) {
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    return 1;
#endif
}

static void foundation_openssl_network_close(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

static int foundation_openssl_cancelled(uint64_t cancellation) {
    return cancellation != 0 && foundation_runtime_cancellation_requested(cancellation);
}

static int foundation_openssl_socket_blocking(foundation_openssl_socket socket_fd, int enabled) {
#ifdef _WIN32
    u_long mode = enabled ? 0 : 1;
    return ioctlsocket(socket_fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags < 0) return 0;
    if (enabled) flags &= ~O_NONBLOCK;
    else flags |= O_NONBLOCK;
    return fcntl(socket_fd, F_SETFL, flags) == 0;
#endif
}

static int foundation_openssl_socket_wait(foundation_openssl_socket socket_fd, int readable,
                                          int writable, uint64_t deadline,
                                          uint64_t cancellation) {
    fd_set read_set;
    fd_set write_set;
    struct timeval timeout;
    int64_t remaining;
    int selected;
    if (foundation_openssl_cancelled(cancellation)) return -2;
    if (!foundation_openssl_deadline_remaining(deadline, &remaining)) return 0;
    if (remaining > 25000000) remaining = 25000000;
    timeout.tv_sec = 0;
    timeout.tv_usec = (long)((remaining + 999) / 1000);
    if (timeout.tv_usec == 0) timeout.tv_usec = 1;
    FD_ZERO(&read_set);
    FD_ZERO(&write_set);
    if (readable) FD_SET(socket_fd, &read_set);
    if (writable) FD_SET(socket_fd, &write_set);
    selected = select((int)socket_fd + 1, readable ? &read_set : NULL,
                      writable ? &write_set : NULL, NULL, &timeout);
    if (selected > 0) return 1;
    if (selected == 0) return foundation_openssl_deadline_remaining(deadline, &remaining) ? 2 : 0;
    return -1;
}

static int foundation_openssl_tls_handshake_until(SSL *ssl, foundation_openssl_socket socket_fd,
                                                   int server, uint64_t deadline,
                                                   uint64_t cancellation) {
    int result;
    if (ssl == NULL || !foundation_openssl_socket_blocking(socket_fd, 0)) return FOUNDATION_OPENSSL_FAILED;
    for (;;) {
        int operation;
        int waited;
        if (foundation_openssl_cancelled(cancellation)) { result = FOUNDATION_OPENSSL_CANCELLED; break; }
        if (!foundation_openssl_deadline_remaining(deadline, &(int64_t){0})) { result = FOUNDATION_OPENSSL_TIMEOUT; break; }
        operation = server ? SSL_accept(ssl) : SSL_connect(ssl);
        if (operation == 1) { result = FOUNDATION_OPENSSL_OK; break; }
        switch (SSL_get_error(ssl, operation)) {
            case SSL_ERROR_WANT_READ: waited = foundation_openssl_socket_wait(socket_fd, 1, 0, deadline, cancellation); break;
            case SSL_ERROR_WANT_WRITE: waited = foundation_openssl_socket_wait(socket_fd, 0, 1, deadline, cancellation); break;
            default: result = FOUNDATION_OPENSSL_HANDSHAKE_FAILED; goto done;
        }
        if (waited == -2) { result = FOUNDATION_OPENSSL_CANCELLED; break; }
        if (waited == 0) { result = FOUNDATION_OPENSSL_TIMEOUT; break; }
        if (waited < 0) { result = FOUNDATION_OPENSSL_HANDSHAKE_FAILED; break; }
    }
done:
    foundation_openssl_socket_blocking(socket_fd, 1);
    return result;
}

static void foundation_openssl_tls_shutdown(SSL *ssl, foundation_openssl_socket socket_fd) {
    if (ssl == NULL) return;
    if (foundation_openssl_socket_blocking(socket_fd, 0)) {
        SSL_shutdown(ssl);
        foundation_openssl_socket_blocking(socket_fd, 1);
    }
}

static int foundation_openssl_connect_until(foundation_openssl_socket socket_fd,
                                            const struct sockaddr *address, size_t length,
                                            uint64_t deadline, uint64_t cancellation) {
    int error = 0;
#ifdef _WIN32
    int error_length = sizeof(error);
#else
    socklen_t error_length = sizeof(error);
#endif
    if (!foundation_openssl_socket_blocking(socket_fd, 0)) return 0;
#ifdef _WIN32
    if (connect(socket_fd, address, (int)length) != 0) {
#else
    if (connect(socket_fd, address, (socklen_t)length) != 0) {
#endif
#ifdef _WIN32
        if (WSAGetLastError() != WSAEWOULDBLOCK && WSAGetLastError() != WSAEINPROGRESS) return 0;
#else
        if (errno != EINPROGRESS) return 0;
#endif
        for (;;) {
            int waited = foundation_openssl_socket_wait(socket_fd, 0, 1, deadline, cancellation);
            if (waited == 2) continue;
            if (waited != 1 || getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, (void *)&error, &error_length) != 0 || error != 0) return 0;
            break;
        }
    }
    return foundation_openssl_socket_blocking(socket_fd, 1);
}

static int foundation_openssl_copy_bytes(uint64_t handle, foundation_openssl_bytes *result) {
    uint64_t length = 0;
    uint64_t value = 0;
    size_t index;

    result->data = NULL;
    result->length = 0;
    if (foundation_runtime_bytes_length(handle, &length) != 0 || length > SIZE_MAX) return 0;
    if (length == 0) return 1;
    result->data = OPENSSL_malloc((size_t)length);
    if (result->data == NULL) return 0;
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

static int foundation_openssl_valid_server_name(const char *value, uint64_t length) {
    uint64_t index;
    uint64_t label = 0;
    if (value == NULL || length == 0 || length > 253) return 0;
    for (index = 0; index < length; ++index) {
        unsigned char current = (unsigned char)value[index];
        if (current == 0) return 0;
        if (current == '.') {
            if (label == 0 || label > 63 || value[index - 1] == '-') return 0;
            label = 0;
            continue;
        }
        if (!((current >= 'a' && current <= 'z') || (current >= 'A' && current <= 'Z') ||
              (current >= '0' && current <= '9') || current == '-')) return 0;
        if (label == 0 && current == '-') return 0;
        ++label;
    }
    return label != 0 && label <= 63 && value[length - 1] != '-';
}

static void foundation_openssl_free_bytes(foundation_openssl_bytes *value) {
    if (value->data != NULL) OPENSSL_clear_free(value->data, value->length);
    value->data = NULL;
    value->length = 0;
}

static const EVP_MD *foundation_openssl_digest(uint32_t algorithm) {
    switch (algorithm) {
        case FOUNDATION_OPENSSL_RS256:
        case FOUNDATION_OPENSSL_ES256: return EVP_sha256();
        case FOUNDATION_OPENSSL_EDDSA: return NULL;
        default: return NULL;
    }
}

static EVP_PKEY *foundation_openssl_private_key(const foundation_openssl_bytes *value) {
    if (value->length > INT_MAX) return NULL;
    BIO *bio = BIO_new_mem_buf(value->data, (int)value->length);
    EVP_PKEY *key;
    if (bio == NULL) return NULL;
    key = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    BIO_free(bio);
    return key;
}

static EVP_PKEY *foundation_openssl_public_key(const foundation_openssl_bytes *value) {
    if (value->length > INT_MAX) return NULL;
    BIO *bio = BIO_new_mem_buf(value->data, (int)value->length);
    EVP_PKEY *key;
    if (bio == NULL) return NULL;
    key = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    BIO_free(bio);
    return key;
}

static int foundation_openssl_key_matches(uint32_t algorithm, EVP_PKEY *key) {
    const int type = EVP_PKEY_base_id(key);
    if (algorithm == FOUNDATION_OPENSSL_RS256) return type == EVP_PKEY_RSA && EVP_PKEY_bits(key) >= 2048;
    if (algorithm == FOUNDATION_OPENSSL_ES256) {
        char group[32];
        size_t length = 0;
        return EVP_PKEY_get_utf8_string_param(key, OSSL_PKEY_PARAM_GROUP_NAME,
                                               group, sizeof(group), &length) == 1 &&
            strcmp(group, "prime256v1") == 0;
    }
    if (algorithm == FOUNDATION_OPENSSL_EDDSA) return type == EVP_PKEY_ED25519;
    return 0;
}

static int32_t foundation_openssl_output_bytes(const unsigned char *data, size_t length,
                                                uint64_t *result) {
    uint64_t builder = 0;
    size_t index;
    if (result == NULL ||
        foundation_runtime_bytes_builder_open((uint64_t)length, &builder) != 0) return FOUNDATION_OPENSSL_FAILED;
    for (index = 0; index < length; ++index) {
        if (foundation_runtime_bytes_builder_append_byte(builder, data[index]) != 0) {
            foundation_runtime_bytes_builder_close(&builder);
            return FOUNDATION_OPENSSL_FAILED;
        }
    }
    if (foundation_runtime_bytes_builder_finish(&builder, result) != 0) {
        foundation_runtime_bytes_builder_close(&builder);
        return FOUNDATION_OPENSSL_FAILED;
    }
    return FOUNDATION_OPENSSL_OK;
}

static int foundation_openssl_es256_raw_signature(const unsigned char *der, size_t der_length,
                                                   unsigned char output[64]) {
    const unsigned char *cursor = der;
    ECDSA_SIG *signature = d2i_ECDSA_SIG(NULL, &cursor, (long)der_length);
    const BIGNUM *r = NULL;
    const BIGNUM *s = NULL;
    int valid = 0;
    if (signature == NULL || cursor != der + der_length) goto cleanup;
    ECDSA_SIG_get0(signature, &r, &s);
    if (BN_num_bytes(r) > 32 || BN_num_bytes(s) > 32) goto cleanup;
    if (BN_bn2binpad(r, output, 32) != 32 || BN_bn2binpad(s, output + 32, 32) != 32) goto cleanup;
    valid = 1;
cleanup:
    ECDSA_SIG_free(signature);
    return valid;
}

static int foundation_openssl_es256_der_signature(const unsigned char *raw, size_t raw_length,
                                                   unsigned char **output, size_t *output_length) {
    BIGNUM *r = NULL;
    BIGNUM *s = NULL;
    ECDSA_SIG *signature = NULL;
    unsigned char *encoded = NULL;
    unsigned char *cursor;
    int length;
    int valid = 0;
    if (raw_length != 64) return 0;
    r = BN_bin2bn(raw, 32, NULL);
    s = BN_bin2bn(raw + 32, 32, NULL);
    signature = ECDSA_SIG_new();
    if (r == NULL || s == NULL || signature == NULL || ECDSA_SIG_set0(signature, r, s) != 1) goto cleanup;
    r = NULL;
    s = NULL;
    length = i2d_ECDSA_SIG(signature, NULL);
    if (length <= 0) goto cleanup;
    encoded = OPENSSL_malloc((size_t)length);
    if (encoded == NULL) goto cleanup;
    cursor = encoded;
    if (i2d_ECDSA_SIG(signature, &cursor) != length) goto cleanup;
    *output = encoded;
    *output_length = (size_t)length;
    encoded = NULL;
    valid = 1;
cleanup:
    OPENSSL_free(encoded);
    ECDSA_SIG_free(signature);
    BN_free(r);
    BN_free(s);
    return valid;
}

int32_t foundation_openssl_sign(uint32_t algorithm, uint64_t private_key_handle,
                                uint64_t input_handle, uint64_t *signature) {
    foundation_openssl_bytes key_bytes = {0};
    foundation_openssl_bytes input = {0};
    EVP_PKEY *key = NULL;
    EVP_MD_CTX *context = NULL;
    unsigned char *output = NULL;
    unsigned char raw_signature[64];
    size_t output_length = 0;
    int32_t status = FOUNDATION_OPENSSL_FAILED;

    if (signature == NULL) return FOUNDATION_OPENSSL_FAILED;
    *signature = 0;
    if (!foundation_openssl_copy_bytes(private_key_handle, &key_bytes) ||
        !foundation_openssl_copy_bytes(input_handle, &input)) goto cleanup;
    key = foundation_openssl_private_key(&key_bytes);
    if (key == NULL || !foundation_openssl_key_matches(algorithm, key)) {
        status = FOUNDATION_OPENSSL_INVALID_PRIVATE_KEY;
        goto cleanup;
    }
    context = EVP_MD_CTX_new();
    if (context == NULL || EVP_DigestSignInit(context, NULL, foundation_openssl_digest(algorithm), NULL, key) != 1 ||
        EVP_DigestSign(context, NULL, &output_length, input.data, input.length) != 1) goto cleanup;
    output = OPENSSL_malloc(output_length);
    if (output == NULL || EVP_DigestSign(context, output, &output_length, input.data, input.length) != 1) goto cleanup;
    if (algorithm == FOUNDATION_OPENSSL_ES256) {
        if (!foundation_openssl_es256_raw_signature(output, output_length, raw_signature)) goto cleanup;
        status = foundation_openssl_output_bytes(raw_signature, sizeof(raw_signature), signature);
    } else {
        status = foundation_openssl_output_bytes(output, output_length, signature);
    }
cleanup:
    OPENSSL_clear_free(output, output_length);
    EVP_MD_CTX_free(context);
    EVP_PKEY_free(key);
    foundation_openssl_free_bytes(&key_bytes);
    foundation_openssl_free_bytes(&input);
    return status;
}

int32_t foundation_openssl_verify(uint32_t algorithm, uint64_t public_key_handle,
                                  uint64_t input_handle, uint64_t signature_handle) {
    foundation_openssl_bytes key_bytes = {0};
    foundation_openssl_bytes input = {0};
    foundation_openssl_bytes signature = {0};
    EVP_PKEY *key = NULL;
    EVP_MD_CTX *context = NULL;
    const unsigned char *signature_data;
    size_t signature_length;
    unsigned char *der_signature = NULL;
    size_t der_signature_length = 0;
    int valid;
    int32_t status = FOUNDATION_OPENSSL_FAILED;

    if (!foundation_openssl_copy_bytes(public_key_handle, &key_bytes) ||
        !foundation_openssl_copy_bytes(input_handle, &input) ||
        !foundation_openssl_copy_bytes(signature_handle, &signature)) goto cleanup;
    key = foundation_openssl_public_key(&key_bytes);
    if (key == NULL || !foundation_openssl_key_matches(algorithm, key)) {
        status = FOUNDATION_OPENSSL_INVALID_PUBLIC_KEY;
        goto cleanup;
    }
    context = EVP_MD_CTX_new();
    if (context == NULL || EVP_DigestVerifyInit(context, NULL, foundation_openssl_digest(algorithm), NULL, key) != 1) goto cleanup;
    signature_data = signature.data;
    signature_length = signature.length;
    if (algorithm == FOUNDATION_OPENSSL_ES256) {
        if (!foundation_openssl_es256_der_signature(signature.data, signature.length,
                                                    &der_signature, &der_signature_length)) {
            status = FOUNDATION_OPENSSL_INVALID_SIGNATURE;
            goto cleanup;
        }
        signature_data = der_signature;
        signature_length = der_signature_length;
    }
    valid = EVP_DigestVerify(context, signature_data, signature_length, input.data, input.length);
    status = valid == 1 ? FOUNDATION_OPENSSL_OK :
        (valid == 0 ? FOUNDATION_OPENSSL_INVALID_SIGNATURE : FOUNDATION_OPENSSL_FAILED);
cleanup:
    OPENSSL_clear_free(der_signature, der_signature_length);
    EVP_MD_CTX_free(context);
    EVP_PKEY_free(key);
    foundation_openssl_free_bytes(&key_bytes);
    foundation_openssl_free_bytes(&input);
    foundation_openssl_free_bytes(&signature);
    return status;
}

static int foundation_openssl_tls_install_ca(SSL_CTX *context, uint64_t ca_handle) {
    foundation_openssl_bytes bundle = {0};
    BIO *bio = NULL;
    X509 *certificate = NULL;
    X509_STORE *store;
    int valid = 0;
    if (ca_handle == 0) return SSL_CTX_set_default_verify_paths(context) == 1;
    if (!foundation_openssl_copy_bytes(ca_handle, &bundle) || bundle.length > INT_MAX) goto cleanup;
    bio = BIO_new_mem_buf(bundle.data, (int)bundle.length);
    store = SSL_CTX_get_cert_store(context);
    if (bio == NULL || store == NULL) goto cleanup;
    while ((certificate = PEM_read_bio_X509(bio, NULL, NULL, NULL)) != NULL) {
        if (X509_STORE_add_cert(store, certificate) != 1) {
            X509_free(certificate);
            certificate = NULL;
            goto cleanup;
        }
        X509_free(certificate);
        certificate = NULL;
        valid = 1;
    }
    ERR_clear_error();
cleanup:
    X509_free(certificate);
    BIO_free(bio);
    foundation_openssl_free_bytes(&bundle);
    return valid;
}

static int32_t foundation_openssl_tls_connect_with_ca(const char *server_name,
                                                      uint64_t server_name_length,
                                                      uint16_t port, uint64_t ca_handle,
                                                      uint64_t deadline, uint64_t cancellation,
                                                      uint64_t *result) {
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *current;
    foundation_openssl_connection *connection = NULL;
    char *name = NULL;
    char service[6];
    foundation_openssl_socket socket_fd = FOUNDATION_OPENSSL_INVALID_SOCKET;
    int32_t status = FOUNDATION_OPENSSL_HANDSHAKE_FAILED;

    int64_t remaining;
    if (result == NULL || !foundation_openssl_valid_server_name(server_name, server_name_length)) return FOUNDATION_OPENSSL_INVALID_SERVER_NAME;
    if (foundation_openssl_cancelled(cancellation)) return FOUNDATION_OPENSSL_CANCELLED;
    if (!foundation_openssl_deadline_remaining(deadline, &remaining)) return FOUNDATION_OPENSSL_TIMEOUT;
    *result = 0;
    if (!foundation_openssl_network_open()) return FOUNDATION_OPENSSL_FAILED;
    name = OPENSSL_malloc((size_t)server_name_length + 1);
    if (name == NULL) {
        status = FOUNDATION_OPENSSL_FAILED;
        goto cleanup;
    }
    memcpy(name, server_name, (size_t)server_name_length);
    name[server_name_length] = '\0';
    snprintf(service, sizeof(service), "%u", (unsigned int)port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    status = foundation_openssl_resolve_until(name, service, &hints, deadline, cancellation, &addresses);
    if (status != FOUNDATION_OPENSSL_OK) goto cleanup;
    for (current = addresses; current != NULL; current = current->ai_next) {
        if (foundation_openssl_cancelled(cancellation)) { status = FOUNDATION_OPENSSL_CANCELLED; goto cleanup; }
        if (!foundation_openssl_deadline_remaining(deadline, &remaining)) { status = FOUNDATION_OPENSSL_TIMEOUT; goto cleanup; }
        socket_fd = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (socket_fd != FOUNDATION_OPENSSL_INVALID_SOCKET &&
            foundation_openssl_connect_until(socket_fd, current->ai_addr, current->ai_addrlen,
                                             deadline, cancellation)) break;
        if (socket_fd != FOUNDATION_OPENSSL_INVALID_SOCKET) foundation_openssl_socket_close(socket_fd);
        socket_fd = FOUNDATION_OPENSSL_INVALID_SOCKET;
    }
    if (socket_fd == FOUNDATION_OPENSSL_INVALID_SOCKET) {
        if (foundation_openssl_cancelled(cancellation)) status = FOUNDATION_OPENSSL_CANCELLED;
        else if (!foundation_openssl_deadline_remaining(deadline, &remaining)) status = FOUNDATION_OPENSSL_TIMEOUT;
        goto cleanup;
    }
    connection = OPENSSL_zalloc(sizeof(*connection));
    if (connection == NULL) { status = FOUNDATION_OPENSSL_FAILED; goto cleanup; }
    connection->socket = socket_fd;
    connection->context = SSL_CTX_new(TLS_client_method());
    if (connection->context == NULL || SSL_CTX_set_min_proto_version(connection->context, TLS1_2_VERSION) != 1 ||
        !foundation_openssl_tls_install_ca(connection->context, ca_handle)) goto cleanup;
    SSL_CTX_set_verify(connection->context, SSL_VERIFY_PEER, NULL);
    connection->ssl = SSL_new(connection->context);
    if (connection->ssl == NULL || SSL_set_tlsext_host_name(connection->ssl, name) != 1 ||
        SSL_set1_host(connection->ssl, name) != 1 || SSL_set_fd(connection->ssl, socket_fd) != 1) goto cleanup;
    status = foundation_openssl_tls_handshake_until(connection->ssl, socket_fd, 0, deadline, cancellation);
    if (status != FOUNDATION_OPENSSL_OK) {
        if (status == FOUNDATION_OPENSSL_HANDSHAKE_FAILED && SSL_get_verify_result(connection->ssl) != X509_V_OK) {
            status = FOUNDATION_OPENSSL_PEER_REJECTED;
        }
        goto cleanup;
    }
    if (SSL_get_verify_result(connection->ssl) != X509_V_OK) { status = FOUNDATION_OPENSSL_PEER_REJECTED; goto cleanup; }
    *result = (uint64_t)(uintptr_t)connection;
    connection = NULL;
    socket_fd = FOUNDATION_OPENSSL_INVALID_SOCKET;
    status = FOUNDATION_OPENSSL_OK;
cleanup:
    if (addresses != NULL) freeaddrinfo(addresses);
    OPENSSL_free(name);
    if (connection != NULL) {
        SSL_free(connection->ssl);
        SSL_CTX_free(connection->context);
        if (connection->socket != FOUNDATION_OPENSSL_INVALID_SOCKET) foundation_openssl_socket_close(connection->socket);
        OPENSSL_free(connection);
    } else if (socket_fd != FOUNDATION_OPENSSL_INVALID_SOCKET) foundation_openssl_socket_close(socket_fd);
    if (status != FOUNDATION_OPENSSL_OK) foundation_openssl_network_close();
    return status;
}

int32_t foundation_openssl_tls_connect(const char *server_name, uint64_t server_name_length,
                                      uint16_t port, uint64_t *result) {
    return foundation_openssl_tls_connect_with_ca(server_name, server_name_length, port, 0,
                                                  foundation_openssl_deadline(15000000000LL), 0, result);
}

void foundation_openssl_tls_close(uint64_t *handle) {
    foundation_openssl_connection *connection;
    if (handle == NULL || *handle == 0) return;
    connection = (foundation_openssl_connection *)(uintptr_t)*handle;
    *handle = 0;
    foundation_openssl_tls_shutdown(connection->ssl, connection->socket);
    SSL_free(connection->ssl);
    SSL_CTX_free(connection->context);
    foundation_openssl_socket_close(connection->socket);
    OPENSSL_free(connection);
    foundation_openssl_network_close();
}

static int foundation_openssl_tls_write_all(SSL *ssl, foundation_openssl_socket socket_fd,
                                            const unsigned char *value,
                                            size_t length, uint64_t deadline,
                                            uint64_t cancellation) {
    size_t written = 0;
    if (ssl == NULL || (value == NULL && length != 0) || !foundation_openssl_socket_blocking(socket_fd, 0)) return 0;
    while (written < length) {
        size_t chunk = length - written;
        int count;
        int64_t remaining;
        if (foundation_openssl_cancelled(cancellation)) return -2;
        if (!foundation_openssl_deadline_remaining(deadline, &remaining)) return -1;
        if (chunk > INT_MAX) chunk = INT_MAX;
        count = SSL_write(ssl, value + written, (int)chunk);
        if (count <= 0) {
            int error = SSL_get_error(ssl, count);
            int waited;
            if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
                foundation_openssl_socket_blocking(socket_fd, 1);
                return 0;
            }
            waited = foundation_openssl_socket_wait(socket_fd, error == SSL_ERROR_WANT_READ,
                                                    error == SSL_ERROR_WANT_WRITE,
                                                    deadline, cancellation);
            if (waited == 2) continue;
            if (waited != 1) {
                foundation_openssl_socket_blocking(socket_fd, 1);
                return waited == -2 ? -2 : -1;
            }
            continue;
        }
        written += (size_t)count;
    }
    foundation_openssl_socket_blocking(socket_fd, 1);
    return 1;
}

static int32_t foundation_openssl_string(const unsigned char *data, size_t length,
                                         fdn_string *result) {
    char *copy;
    if (result == NULL || (length != 0 && !fdn_utf8_valid((const char *)data, length))) {
        return FOUNDATION_OPENSSL_PROTOCOL_FAILED;
    }
    fdn_string_drop(result);
    *result = fdn_string_static("", 0);
    if (length == 0) return FOUNDATION_OPENSSL_OK;
    copy = fdn_alloc(length);
    if (copy == NULL) return FOUNDATION_OPENSSL_FAILED;
    memcpy(copy, data, length);
    result->data = copy;
    result->length = length;
    result->owned = 1;
    return FOUNDATION_OPENSSL_OK;
}

static int foundation_openssl_ascii_equal(const unsigned char *value, size_t value_length,
                                          const char *expected) {
    return value_length == strlen(expected) &&
        strncasecmp((const char *)value, expected, value_length) == 0;
}

static const unsigned char *foundation_openssl_crlf(const unsigned char *value,
                                                    size_t length) {
    size_t index;
    for (index = 0; index + 1 < length; ++index) {
        if (value[index] == '\r' && value[index + 1] == '\n') return value + index;
    }
    return NULL;
}

static const unsigned char *foundation_openssl_head_end(const unsigned char *value,
                                                        size_t length) {
    size_t index;
    for (index = 0; index + 3 < length; ++index) {
        if (value[index] == '\r' && value[index + 1] == '\n' &&
            value[index + 2] == '\r' && value[index + 3] == '\n') return value + index + 4;
    }
    return NULL;
}

static int foundation_openssl_decimal(const unsigned char *value, size_t length,
                                      uint64_t *result) {
    size_t index;
    uint64_t parsed = 0;
    if (length == 0 || result == NULL) return 0;
    for (index = 0; index < length; ++index) {
        if (value[index] < '0' || value[index] > '9' ||
            parsed > (UINT64_MAX - (uint64_t)(value[index] - '0')) / 10) return 0;
        parsed = parsed * 10 + (uint64_t)(value[index] - '0');
    }
    *result = parsed;
    return 1;
}

static int foundation_openssl_hexadecimal(const unsigned char *value, size_t length,
                                          uint64_t *result) {
    size_t index;
    uint64_t parsed = 0;
    if (length == 0 || result == NULL) return 0;
    for (index = 0; index < length; ++index) {
        unsigned char current = value[index];
        uint64_t digit;
        if (current >= '0' && current <= '9') digit = (uint64_t)(current - '0');
        else if (current >= 'a' && current <= 'f') digit = (uint64_t)(current - 'a' + 10);
        else if (current >= 'A' && current <= 'F') digit = (uint64_t)(current - 'A' + 10);
        else return 0;
        if (parsed > (UINT64_MAX - digit) / 16) return 0;
        parsed = parsed * 16 + digit;
    }
    *result = parsed;
    return 1;
}

static int foundation_openssl_status_has_no_body(const unsigned char *head, size_t length) {
    const unsigned char *line_end = foundation_openssl_crlf(head, length);
    size_t index;
    int status;
    if (line_end == NULL || (size_t)(line_end - head) < 12 || memcmp(head, "HTTP/", 5) != 0) return -1;
    for (index = 5; index < (size_t)(line_end - head) && head[index] != ' '; ++index) {}
    if (index + 4 > (size_t)(line_end - head) || head[index] != ' ' ||
        head[index + 1] < '0' || head[index + 1] > '9' ||
        head[index + 2] < '0' || head[index + 2] > '9' ||
        head[index + 3] < '0' || head[index + 3] > '9') return -1;
    status = (head[index + 1] - '0') * 100 + (head[index + 2] - '0') * 10 + head[index + 3] - '0';
    return (status >= 100 && status < 200) || status == 204 || status == 304;
}

static int32_t foundation_openssl_chunked_body(const unsigned char *input, size_t input_length,
                                               uint64_t maximum_body, uint64_t *result) {
    size_t offset = 0;
    uint64_t builder = 0;
    uint64_t written = 0;
    if (foundation_runtime_bytes_builder_open(maximum_body, &builder) != 0) return FOUNDATION_OPENSSL_FAILED;
    for (;;) {
        const unsigned char *line_end;
        const unsigned char *extension;
        uint64_t chunk;
        size_t line_length;
        size_t index;
        if (offset >= input_length) goto failed;
        line_end = foundation_openssl_crlf(input + offset, input_length - offset);
        if (line_end == NULL) goto failed;
        line_length = (size_t)(line_end - (input + offset));
        extension = memchr(input + offset, ';', line_length);
        if (extension != NULL) line_length = (size_t)(extension - (input + offset));
        if (!foundation_openssl_hexadecimal(input + offset, line_length, &chunk)) goto failed;
        offset = (size_t)(line_end + 2 - input);
        if (chunk == 0) {
            for (;;) {
                line_end = foundation_openssl_crlf(input + offset, input_length - offset);
                if (line_end == NULL) goto failed;
                if (line_end == input + offset) {
                    offset += 2;
                    if (offset != input_length) goto failed;
                    if (foundation_runtime_bytes_builder_finish(&builder, result) != 0) goto failed;
                    return FOUNDATION_OPENSSL_OK;
                }
                offset = (size_t)(line_end + 2 - input);
            }
        }
        if (chunk > maximum_body - written || chunk > input_length - offset || chunk > SIZE_MAX) goto failed;
        for (index = 0; index < (size_t)chunk; ++index) {
            if (foundation_runtime_bytes_builder_append_byte(builder, input[offset + index]) != 0) goto failed;
        }
        written += chunk;
        offset += (size_t)chunk;
        if (offset + 2 > input_length || input[offset] != '\r' || input[offset + 1] != '\n') goto failed;
        offset += 2;
    }
failed:
    foundation_runtime_bytes_builder_close(&builder);
    return FOUNDATION_OPENSSL_PROTOCOL_FAILED;
}

static int32_t foundation_openssl_response_body(const unsigned char *response,
                                                size_t response_length, size_t head_length,
                                                uint64_t maximum_body, int request_head,
                                                uint64_t *result) {
    const unsigned char *cursor = foundation_openssl_crlf(response, head_length);
    const unsigned char *head_end = response + head_length;
    int no_body = foundation_openssl_status_has_no_body(response, head_length);
    int chunked = 0;
    int have_length = 0;
    uint64_t content_length = 0;
    if (no_body < 0 || cursor == NULL) return FOUNDATION_OPENSSL_PROTOCOL_FAILED;
    cursor += 2;
    while (cursor < head_end) {
        const unsigned char *line_end = foundation_openssl_crlf(cursor, (size_t)(head_end - cursor));
        const unsigned char *colon;
        const unsigned char *value;
        size_t name_length;
        size_t value_length;
        if (line_end == NULL) return FOUNDATION_OPENSSL_PROTOCOL_FAILED;
        if (line_end == cursor) break;
        colon = memchr(cursor, ':', (size_t)(line_end - cursor));
        if (colon == NULL) return FOUNDATION_OPENSSL_PROTOCOL_FAILED;
        name_length = (size_t)(colon - cursor);
        value = colon + 1;
        while (value < line_end && (*value == ' ' || *value == '\t')) ++value;
        value_length = (size_t)(line_end - value);
        while (value_length != 0 && (value[value_length - 1] == ' ' || value[value_length - 1] == '\t')) --value_length;
        if (foundation_openssl_ascii_equal(cursor, name_length, "Content-Length")) {
            uint64_t parsed;
            if (!foundation_openssl_decimal(value, value_length, &parsed) ||
                (have_length && parsed != content_length)) return FOUNDATION_OPENSSL_PROTOCOL_FAILED;
            have_length = 1;
            content_length = parsed;
        } else if (foundation_openssl_ascii_equal(cursor, name_length, "Transfer-Encoding")) {
            if (!foundation_openssl_ascii_equal(value, value_length, "chunked")) return FOUNDATION_OPENSSL_PROTOCOL_FAILED;
            chunked = 1;
        }
        cursor = line_end + 2;
    }
    if (chunked && have_length) return FOUNDATION_OPENSSL_PROTOCOL_FAILED;
    if (request_head || no_body) return response_length == head_length
        ? foundation_openssl_output_bytes(NULL, 0, result)
        : FOUNDATION_OPENSSL_PROTOCOL_FAILED;
    if (chunked) return foundation_openssl_chunked_body(response + head_length,
                                                        response_length - head_length,
                                                        maximum_body, result);
    if (have_length) {
        if (content_length > maximum_body || content_length > SIZE_MAX ||
            response_length - head_length != (size_t)content_length) return FOUNDATION_OPENSSL_PROTOCOL_FAILED;
        return foundation_openssl_output_bytes(response + head_length, (size_t)content_length, result);
    }
    if (response_length - head_length > maximum_body) return FOUNDATION_OPENSSL_LIMIT_EXCEEDED;
    return foundation_openssl_output_bytes(response + head_length, response_length - head_length, result);
}

typedef struct foundation_openssl_response_frame {
    size_t head_offset;
    size_t head_length;
    size_t complete_length;
    int chunked;
    int close_delimited;
} foundation_openssl_response_frame;

static int foundation_openssl_chunked_complete(const unsigned char *input, size_t length,
                                               uint64_t maximum_body, size_t *consumed) {
    size_t offset = 0;
    uint64_t body = 0;
    for (;;) {
        const unsigned char *line_end;
        const unsigned char *extension;
        size_t line_length;
        uint64_t chunk;
        if (offset >= length) return 0;
        line_end = foundation_openssl_crlf(input + offset, length - offset);
        if (line_end == NULL) return length - offset > 65536 ? -1 : 0;
        line_length = (size_t)(line_end - (input + offset));
        extension = memchr(input + offset, ';', line_length);
        if (extension != NULL) line_length = (size_t)(extension - (input + offset));
        if (!foundation_openssl_hexadecimal(input + offset, line_length, &chunk) || chunk > maximum_body - body) return -1;
        offset = (size_t)(line_end + 2 - input);
        if (chunk == 0) {
            size_t trailer_start = offset;
            for (;;) {
                line_end = foundation_openssl_crlf(input + offset, length - offset);
                if (line_end == NULL) return length - trailer_start > 65536 ? -1 : 0;
                if ((size_t)(line_end + 2 - (input + trailer_start)) > 65536) return -1;
                if (line_end == input + offset) {
                    *consumed = (size_t)(line_end + 2 - input);
                    return 1;
                }
                {
                    const unsigned char *colon = memchr(input + offset, ':', (size_t)(line_end - (input + offset)));
                    size_t index;
                    if (colon == NULL || colon == input + offset ||
                        foundation_openssl_ascii_equal(input + offset, (size_t)(colon - (input + offset)), "Content-Length") ||
                        foundation_openssl_ascii_equal(input + offset, (size_t)(colon - (input + offset)), "Transfer-Encoding")) return -1;
                    for (index = 0; index < (size_t)(colon - (input + offset)); ++index) {
                        unsigned char current = input[offset + index];
                        if (current <= 32 || current >= 127 || current == '(' || current == ')' || current == '<' || current == '>' || current == '@' || current == ',' || current == ';' || current == ':' || current == '\\' || current == '"' || current == '/' || current == '[' || current == ']' || current == '?' || current == '=' || current == '{' || current == '}') return -1;
                    }
                }
                offset = (size_t)(line_end + 2 - input);
            }
        }
        if (chunk > SIZE_MAX || chunk > length - offset) return 0;
        body += chunk;
        offset += (size_t)chunk;
        if (offset + 2 > length) return 0;
        if (input[offset] != '\r' || input[offset + 1] != '\n') return -1;
        offset += 2;
    }
}

static int foundation_openssl_response_frame_read(const unsigned char *response,
                                                  size_t response_length, uint64_t maximum_body,
                                                  int request_head, int eof,
                                                  foundation_openssl_response_frame *frame) {
    size_t offset = 0;
    size_t interim = 0;
    memset(frame, 0, sizeof(*frame));
    for (;;) {
        const unsigned char *head_end = foundation_openssl_head_end(response + offset, response_length - offset);
        const unsigned char *status_line;
        const unsigned char *cursor;
        int status;
        int no_body;
        int have_length = 0;
        int have_transfer = 0;
        uint64_t content_length = 0;
        if (head_end == NULL) return response_length > 65536 ? -1 : 0;
        if ((size_t)(head_end - response) > 65536 ||
            (size_t)(head_end - (response + offset)) > 65536) return -1;
        status_line = foundation_openssl_crlf(response + offset, (size_t)(head_end - (response + offset)));
        if (status_line == NULL || (size_t)(status_line - (response + offset)) < 12) return -1;
        cursor = response + offset;
        while (cursor < status_line && *cursor != ' ') ++cursor;
        if (cursor + 4 > status_line || cursor[1] < '0' || cursor[1] > '9' || cursor[2] < '0' || cursor[2] > '9' || cursor[3] < '0' || cursor[3] > '9') return -1;
        status = (cursor[1] - '0') * 100 + (cursor[2] - '0') * 10 + cursor[3] - '0';
        if (status >= 100 && status < 200) {
            if (status == 101 || ++interim > 8) return -1;
            offset = (size_t)(head_end - response);
            continue;
        }
        no_body = foundation_openssl_status_has_no_body(response + offset, (size_t)(head_end - (response + offset)));
        if (no_body < 0) return -1;
        cursor = status_line + 2;
        while (cursor < head_end - 2) {
            const unsigned char *line_end = foundation_openssl_crlf(cursor, (size_t)(head_end - cursor));
            const unsigned char *colon;
            const unsigned char *value;
            size_t name_length;
            size_t value_length;
            uint64_t parsed;
            if (line_end == NULL || line_end == cursor) return -1;
            colon = memchr(cursor, ':', (size_t)(line_end - cursor));
            if (colon == NULL) return -1;
            name_length = (size_t)(colon - cursor);
            value = colon + 1;
            while (value < line_end && (*value == ' ' || *value == '\t')) ++value;
            value_length = (size_t)(line_end - value);
            while (value_length != 0 && (value[value_length - 1] == ' ' || value[value_length - 1] == '\t')) --value_length;
            if (foundation_openssl_ascii_equal(cursor, name_length, "Content-Length")) {
                if (have_length || !foundation_openssl_decimal(value, value_length, &parsed)) return -1;
                have_length = 1;
                content_length = parsed;
            } else if (foundation_openssl_ascii_equal(cursor, name_length, "Transfer-Encoding")) {
                if (have_transfer || !foundation_openssl_ascii_equal(value, value_length, "chunked")) return -1;
                have_transfer = 1;
            }
            cursor = line_end + 2;
        }
        if (have_transfer && have_length) return -1;
        frame->head_offset = offset;
        frame->head_length = (size_t)(head_end - (response + offset));
        if (request_head || no_body) {
            frame->complete_length = (size_t)(head_end - response);
            return response_length == frame->complete_length ? 1 : -1;
        }
        if (have_transfer) {
            size_t consumed;
            int complete = foundation_openssl_chunked_complete(head_end, response_length - (size_t)(head_end - response), maximum_body, &consumed);
            if (complete <= 0) return complete;
            frame->chunked = 1;
            frame->complete_length = (size_t)(head_end - response) + consumed;
            return response_length == frame->complete_length ? 1 : -1;
        }
        if (have_length) {
            if (content_length > maximum_body || content_length > SIZE_MAX || content_length > SIZE_MAX - (size_t)(head_end - response)) return -1;
            frame->complete_length = (size_t)(head_end - response) + (size_t)content_length;
            if (response_length < frame->complete_length) return 0;
            return response_length == frame->complete_length ? 1 : -1;
        }
        if (response_length - (size_t)(head_end - response) > maximum_body) return -1;
        frame->close_delimited = 1;
        if (!eof) return 0;
        frame->complete_length = response_length;
        return 1;
    }
}

#ifdef FOUNDATION_OPENSSL_TESTING
int32_t foundation_openssl_test_response_frame(const char *response, uint64_t response_length,
                                               uint64_t maximum_body, int request_head, int eof,
                                               fdn_string *response_head, uint64_t *response_body) {
    foundation_openssl_response_frame frame;
    int complete;
    if (response == NULL || response_length > SIZE_MAX || response_head == NULL || response_body == NULL) {
        return FOUNDATION_OPENSSL_FAILED;
    }
    *response_body = 0;
    complete = foundation_openssl_response_frame_read((const unsigned char *)response,
                                                      (size_t)response_length, maximum_body,
                                                      request_head, eof, &frame);
    if (complete < 0) return FOUNDATION_OPENSSL_PROTOCOL_FAILED;
    if (complete == 0) return FOUNDATION_OPENSSL_FAILED;
    if (foundation_openssl_string((const unsigned char *)response + frame.head_offset,
                                  frame.head_length, response_head) != FOUNDATION_OPENSSL_OK) {
        return FOUNDATION_OPENSSL_FAILED;
    }
    return foundation_openssl_response_body((const unsigned char *)response + frame.head_offset,
                                            frame.complete_length - frame.head_offset,
                                            frame.head_length, maximum_body,
                                            request_head, response_body);
}
#endif

static int32_t foundation_openssl_tls_http11_with_ca(const char *server_name,
                                                     uint64_t server_name_length,
                                      uint16_t port, uint64_t authority_bundle,
                                      const char *request_head,
                                      uint64_t request_head_length, uint64_t body_handle,
                                      uint64_t maximum_body, int64_t timeout_nanoseconds,
                                      uint64_t cancellation,
                                      fdn_string *response_head,
                                      uint64_t *response_body) {
    foundation_openssl_connection *connection = NULL;
    foundation_openssl_bytes body = {0};
    uint64_t connection_handle = 0;
    unsigned char *response = NULL;
    size_t response_length = 0;
    size_t response_capacity = 4096;
    foundation_openssl_response_frame frame;
    uint64_t deadline;
    int64_t remaining;
    int request_head_method;
    int32_t status = FOUNDATION_OPENSSL_FAILED;

    if (foundation_openssl_cancelled(cancellation)) return FOUNDATION_OPENSSL_CANCELLED;
    if (response_body == NULL || response_head == NULL || server_name == NULL || request_head == NULL ||
        server_name_length == 0 || request_head_length < 4 || request_head_length > SIZE_MAX ||
        maximum_body == 0 || maximum_body > SIZE_MAX || timeout_nanoseconds <= 0 ||
        memcmp(request_head + request_head_length - 4, "\r\n\r\n", 4) != 0) return FOUNDATION_OPENSSL_FAILED;
    *response_body = 0;
    deadline = foundation_openssl_deadline(timeout_nanoseconds);
    if (deadline == 0) return FOUNDATION_OPENSSL_TIMEOUT;
    if (!foundation_openssl_copy_bytes(body_handle, &body)) goto cleanup;
    request_head_method = request_head_length >= 5 && memcmp(request_head, "HEAD ", 5) == 0;
    status = foundation_openssl_tls_connect_with_ca(server_name, server_name_length, port,
                                                    authority_bundle, deadline,
                                                    cancellation, &connection_handle);
    if (status != FOUNDATION_OPENSSL_OK) goto cleanup;
    connection = (foundation_openssl_connection *)(uintptr_t)connection_handle;
    if (foundation_openssl_cancelled(cancellation)) {
        status = FOUNDATION_OPENSSL_CANCELLED;
        goto cleanup;
    }
    {
        if (foundation_openssl_cancelled(cancellation)) { status = FOUNDATION_OPENSSL_CANCELLED; goto cleanup; }
        if (!foundation_openssl_deadline_remaining(deadline, &remaining)) { status = FOUNDATION_OPENSSL_TIMEOUT; goto cleanup; }
        int head_status = foundation_openssl_tls_write_all(connection->ssl, connection->socket,
                                                           (const unsigned char *)request_head,
                                                           (size_t)request_head_length, deadline, cancellation);
        int body_status = head_status == 1
            ? foundation_openssl_tls_write_all(connection->ssl, connection->socket, body.data, body.length, deadline, cancellation)
            : head_status;
        if (head_status != 1 || body_status != 1) {
            status = (head_status == -2 || body_status == -2) ? FOUNDATION_OPENSSL_CANCELLED :
                ((head_status == -1 || body_status == -1) ? FOUNDATION_OPENSSL_TIMEOUT : FOUNDATION_OPENSSL_FAILED);
            goto cleanup;
        }
    }
    response = OPENSSL_malloc(response_capacity);
    if (response == NULL) goto cleanup;
    if (!foundation_openssl_socket_blocking(connection->socket, 0)) goto cleanup;
    for (;;) {
        int read;
        if (foundation_openssl_cancelled(cancellation)) { status = FOUNDATION_OPENSSL_CANCELLED; goto cleanup; }
        if (!foundation_openssl_deadline_remaining(deadline, &remaining)) { status = FOUNDATION_OPENSSL_TIMEOUT; goto cleanup; }
        if (response_length + 1 >= response_capacity) {
            size_t next;
            unsigned char *grown;
            if (response_capacity > SIZE_MAX / 2 || maximum_body > SIZE_MAX - 65536 ||
                response_capacity > (size_t)maximum_body + 65536) {
                status = FOUNDATION_OPENSSL_LIMIT_EXCEEDED;
                goto cleanup;
            }
            next = response_capacity * 2;
            if (next > (size_t)maximum_body + 65536) next = (size_t)maximum_body + 65536;
            grown = OPENSSL_realloc(response, next);
            if (grown == NULL) goto cleanup;
            response = grown;
            response_capacity = next;
        }
        read = SSL_read(connection->ssl, response + response_length, (int)(response_capacity - response_length - 1));
        if (read > 0) {
            int complete;
            if (foundation_openssl_cancelled(cancellation)) {
                status = FOUNDATION_OPENSSL_CANCELLED;
                goto cleanup;
            }
            response_length += (size_t)read;
            response[response_length] = '\0';
            complete = foundation_openssl_response_frame_read(response, response_length, maximum_body,
                                                               request_head_method, 0, &frame);
            if (complete < 0) { status = FOUNDATION_OPENSSL_PROTOCOL_FAILED; goto cleanup; }
            if (complete > 0) {
                break;
            }
            continue;
        }
        switch (SSL_get_error(connection->ssl, read)) {
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE: {
                int waited = foundation_openssl_socket_wait(connection->socket,
                    SSL_get_error(connection->ssl, read) == SSL_ERROR_WANT_READ,
                    SSL_get_error(connection->ssl, read) == SSL_ERROR_WANT_WRITE,
                    deadline, cancellation);
                if (waited == -2) status = FOUNDATION_OPENSSL_CANCELLED;
                else if (waited == 0) status = FOUNDATION_OPENSSL_TIMEOUT;
                else if (waited < 0) status = FOUNDATION_OPENSSL_FAILED;
                if (waited == 2) continue;
                if (waited != 1) goto cleanup;
                continue;
            }
            case SSL_ERROR_ZERO_RETURN:
                if (foundation_openssl_response_frame_read(response, response_length, maximum_body,
                                                           request_head_method, 1, &frame) != 1) {
                    status = FOUNDATION_OPENSSL_PROTOCOL_FAILED;
                    goto cleanup;
                }
                break;
            default:
                status = FOUNDATION_OPENSSL_FAILED;
                goto cleanup;
        }
        break;
    }
    foundation_openssl_socket_blocking(connection->socket, 1);
    status = foundation_openssl_string(response + frame.head_offset, frame.head_length, response_head);
    if (status != FOUNDATION_OPENSSL_OK) goto cleanup;
    status = foundation_openssl_response_body(response + frame.head_offset,
                                              frame.complete_length - frame.head_offset,
                                              frame.head_length, maximum_body,
                                              request_head_method, response_body);
    if (status != FOUNDATION_OPENSSL_OK) goto cleanup;
    status = FOUNDATION_OPENSSL_OK;
cleanup:
    if (connection != NULL) foundation_openssl_socket_blocking(connection->socket, 1);
    if (status != FOUNDATION_OPENSSL_OK) foundation_runtime_bytes_close(response_body);
    if (connection_handle != 0) foundation_openssl_tls_close(&connection_handle);
    foundation_openssl_free_bytes(&body);
    OPENSSL_free(response);
    return status;
}

int32_t foundation_openssl_tls_http11(const char *server_name, uint64_t server_name_length,
                                      uint16_t port, const char *request_head,
                                      uint64_t request_head_length, uint64_t body_handle,
                                      uint64_t maximum_body, int64_t timeout_nanoseconds,
                                      uint64_t cancellation,
                                      fdn_string *response_head,
                                      uint64_t *response_body) {
    return foundation_openssl_tls_http11_with_ca(server_name, server_name_length, port, 0,
                                                 request_head, request_head_length, body_handle,
                                                 maximum_body, timeout_nanoseconds, cancellation, response_head,
                                                 response_body);
}

int32_t foundation_openssl_tls_http11_ca(const char *server_name, uint64_t server_name_length,
                                         uint16_t port, uint64_t authority_bundle,
                                         const char *request_head, uint64_t request_head_length,
                                         uint64_t body_handle, uint64_t maximum_body,
                                         int64_t timeout_nanoseconds,
                                         uint64_t cancellation,
                                         fdn_string *response_head, uint64_t *response_body) {
    return foundation_openssl_tls_http11_with_ca(server_name, server_name_length, port,
                                                 authority_bundle, request_head,
                                                 request_head_length, body_handle, maximum_body,
                                                 timeout_nanoseconds, cancellation,
                                                 response_head, response_body);
}

typedef struct foundation_openssl_server_certificate {
    char *name;
    SSL_CTX *context;
    struct foundation_openssl_server_certificate *next;
} foundation_openssl_server_certificate;

typedef struct foundation_openssl_server {
    foundation_openssl_socket socket;
    foundation_openssl_server_certificate *certificates;
    SSL_CTX *default_context;
} foundation_openssl_server;

typedef struct foundation_openssl_server_connection {
    SSL *ssl;
    foundation_openssl_socket socket;
    _Atomic size_t streams;
    CRYPTO_RWLOCK *lock;
} foundation_openssl_server_connection;

typedef struct foundation_openssl_server_stream {
    foundation_openssl_server_connection *connection;
} foundation_openssl_server_stream;

typedef struct foundation_openssl_server_record {
    uint64_t identifier;
    foundation_openssl_server *server;
    size_t users;
    int closing;
    int started;
    CRYPTO_RWLOCK *configuration;
    struct foundation_openssl_server_record *next;
} foundation_openssl_server_record;

static atomic_flag foundation_openssl_servers_lock = ATOMIC_FLAG_INIT;
static _Atomic uint64_t foundation_openssl_next_server = 1;
static foundation_openssl_server_record *foundation_openssl_servers;

static void foundation_openssl_server_registry_lock(void) {
    while (atomic_flag_test_and_set_explicit(&foundation_openssl_servers_lock, memory_order_acquire)) {}
}

static void foundation_openssl_server_registry_unlock(void) {
    atomic_flag_clear_explicit(&foundation_openssl_servers_lock, memory_order_release);
}

static foundation_openssl_server_record *foundation_openssl_server_acquire(uint64_t identifier) {
    foundation_openssl_server_record *record;
    foundation_openssl_server_registry_lock();
    for (record = foundation_openssl_servers; record != NULL; record = record->next) {
        if (record->identifier == identifier && !record->closing) {
            ++record->users;
            foundation_openssl_server_registry_unlock();
            return record;
        }
    }
    foundation_openssl_server_registry_unlock();
    return NULL;
}

static void foundation_openssl_server_release(foundation_openssl_server_record *record) {
    foundation_openssl_server_registry_lock();
    --record->users;
    foundation_openssl_server_registry_unlock();
}

static int foundation_openssl_server_start(foundation_openssl_server_record *record) {
    int started = 0;
    foundation_openssl_server_registry_lock();
    if (!record->closing) {
        record->started = 1;
        started = 1;
    }
    foundation_openssl_server_registry_unlock();
    return started;
}

static void foundation_openssl_server_certificate_free(
    foundation_openssl_server_certificate *certificate) {
    if (certificate == NULL) return;
    OPENSSL_free(certificate->name);
    SSL_CTX_free(certificate->context);
    OPENSSL_free(certificate);
}

static void foundation_openssl_server_certificates_free(
    foundation_openssl_server_certificate *certificate) {
    while (certificate != NULL) {
        foundation_openssl_server_certificate *next = certificate->next;
        foundation_openssl_server_certificate_free(certificate);
        certificate = next;
    }
}

static SSL_CTX *foundation_openssl_server_context(uint64_t certificate_handle,
                                                  uint64_t private_key_handle) {
    foundation_openssl_bytes certificate = {0};
    foundation_openssl_bytes private_key = {0};
    BIO *certificate_bio = NULL;
    BIO *private_key_bio = NULL;
    X509 *x509 = NULL;
    X509 *chain = NULL;
    EVP_PKEY *key = NULL;
    SSL_CTX *context = NULL;

    if (!foundation_openssl_copy_bytes(certificate_handle, &certificate) ||
        !foundation_openssl_copy_bytes(private_key_handle, &private_key) ||
        certificate.length > INT_MAX || private_key.length > INT_MAX) goto cleanup;
    certificate_bio = BIO_new_mem_buf(certificate.data, (int)certificate.length);
    private_key_bio = BIO_new_mem_buf(private_key.data, (int)private_key.length);
    if (certificate_bio == NULL || private_key_bio == NULL) goto cleanup;
    x509 = PEM_read_bio_X509(certificate_bio, NULL, NULL, NULL);
    key = PEM_read_bio_PrivateKey(private_key_bio, NULL, NULL, NULL);
    context = SSL_CTX_new(TLS_server_method());
    if (x509 == NULL || key == NULL || context == NULL ||
        SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION) != 1 ||
        SSL_CTX_use_certificate(context, x509) != 1 ||
        SSL_CTX_use_PrivateKey(context, key) != 1 ||
        SSL_CTX_check_private_key(context) != 1) {
        SSL_CTX_free(context);
        context = NULL;
    } else {
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
    }
cleanup:
    X509_free(chain);
    X509_free(x509);
    EVP_PKEY_free(key);
    BIO_free(certificate_bio);
    BIO_free(private_key_bio);
    foundation_openssl_free_bytes(&certificate);
    foundation_openssl_free_bytes(&private_key);
    return context;
}

static int foundation_openssl_server_name_equal(const char *left, const char *right) {
#ifdef _WIN32
    return _stricmp(left, right) == 0;
#else
    return strcasecmp(left, right) == 0;
#endif
}

static int foundation_openssl_server_name_equal_length(const char *left, const char *right,
                                                        size_t length) {
#ifdef _WIN32
    return _strnicmp(left, right, length) == 0;
#else
    return strncasecmp(left, right, length) == 0;
#endif
}

static int foundation_openssl_server_sni(SSL *ssl, int *alert, void *argument) {
    foundation_openssl_server *server = argument;
    const char *name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    foundation_openssl_server_certificate *certificate;
    (void)alert;
    if (server == NULL || name == NULL) return SSL_TLSEXT_ERR_OK;
    for (certificate = server->certificates; certificate != NULL; certificate = certificate->next) {
        if (foundation_openssl_server_name_equal(certificate->name, name)) {
            if (SSL_set_SSL_CTX(ssl, certificate->context) == NULL) return SSL_TLSEXT_ERR_ALERT_FATAL;
            return SSL_TLSEXT_ERR_OK;
        }
    }
    return SSL_TLSEXT_ERR_ALERT_FATAL;
}

static int foundation_openssl_server_add(foundation_openssl_server *server,
                                         const char *server_name, uint64_t server_name_length,
                                         uint64_t certificate_handle, uint64_t private_key_handle) {
    foundation_openssl_server_certificate *certificate;
    foundation_openssl_server_certificate *existing;
    if (server == NULL || !foundation_openssl_valid_server_name(server_name, server_name_length) ||
        server_name_length > SIZE_MAX - 1) return 0;
    for (existing = server->certificates; existing != NULL; existing = existing->next) {
        if (strlen(existing->name) == server_name_length &&
            foundation_openssl_server_name_equal_length(existing->name, server_name,
                                                        (size_t)server_name_length)) return 0;
    }
    certificate = OPENSSL_zalloc(sizeof(*certificate));
    if (certificate == NULL) return 0;
    certificate->name = OPENSSL_malloc((size_t)server_name_length + 1);
    certificate->context = foundation_openssl_server_context(certificate_handle, private_key_handle);
    if (certificate->name == NULL || certificate->context == NULL) {
        foundation_openssl_server_certificate_free(certificate);
        return 0;
    }
    memcpy(certificate->name, server_name, (size_t)server_name_length);
    certificate->name[server_name_length] = '\0';
    SSL_CTX_set_tlsext_servername_callback(certificate->context, foundation_openssl_server_sni);
    SSL_CTX_set_tlsext_servername_arg(certificate->context, server);
    if (server->default_context == NULL) server->default_context = certificate->context;
    certificate->next = server->certificates;
    server->certificates = certificate;
    return 1;
}

int32_t foundation_openssl_server_open(const char *address, uint64_t address_length,
                                       uint16_t port, const char *server_name,
                                       uint64_t server_name_length, uint64_t certificate_handle,
                                       uint64_t private_key_handle, uint64_t *result,
                                       uint64_t *bound_port) {
    char *host = NULL;
    char port_text[6];
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *current;
    foundation_openssl_server *server = NULL;
    foundation_openssl_socket socket_fd = FOUNDATION_OPENSSL_INVALID_SOCKET;
    int32_t status = FOUNDATION_OPENSSL_FAILED;

    if (result == NULL || bound_port == NULL || address == NULL ||
        address_length == 0 || address_length > SIZE_MAX - 1 ||
        memchr(address, '\0', (size_t)address_length) != NULL ||
        !foundation_openssl_valid_server_name(server_name, server_name_length)) return FOUNDATION_OPENSSL_INVALID_SERVER_NAME;
    *result = 0;
    *bound_port = 0;
    if (!foundation_openssl_network_open()) return FOUNDATION_OPENSSL_UNAVAILABLE;
    host = OPENSSL_malloc((size_t)address_length + 1);
    if (host == NULL) goto cleanup;
    memcpy(host, address, (size_t)address_length);
    host[address_length] = '\0';
    if (snprintf(port_text, sizeof(port_text), "%u", (unsigned int)port) < 0) goto cleanup;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(host, port_text, &hints, &addresses) != 0) {
        status = FOUNDATION_OPENSSL_FAILED;
        goto cleanup;
    }
    for (current = addresses; current != NULL; current = current->ai_next) {
        int enabled = 1;
        socket_fd = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (socket_fd == FOUNDATION_OPENSSL_INVALID_SOCKET) continue;
        setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&enabled, sizeof(enabled));
        if (bind(socket_fd, current->ai_addr, current->ai_addrlen) == 0 && listen(socket_fd, 128) == 0) break;
        foundation_openssl_socket_close(socket_fd);
        socket_fd = FOUNDATION_OPENSSL_INVALID_SOCKET;
    }
    if (socket_fd == FOUNDATION_OPENSSL_INVALID_SOCKET) goto cleanup;
    server = OPENSSL_zalloc(sizeof(*server));
    if (server == NULL || !foundation_openssl_server_add(server, server_name, server_name_length,
                                                          certificate_handle, private_key_handle)) goto cleanup;
    server->socket = socket_fd;
    {
        struct sockaddr_storage bound;
#ifdef _WIN32
        int bound_length = sizeof(bound);
#else
        socklen_t bound_length = sizeof(bound);
#endif
        if (getsockname(socket_fd, (struct sockaddr *)&bound, &bound_length) != 0) goto cleanup;
        if (bound.ss_family == AF_INET) *bound_port = ntohs(((struct sockaddr_in *)&bound)->sin_port);
        else if (bound.ss_family == AF_INET6) *bound_port = ntohs(((struct sockaddr_in6 *)&bound)->sin6_port);
        else goto cleanup;
    }
    {
        foundation_openssl_server_record *record = OPENSSL_zalloc(sizeof(*record));
        foundation_openssl_server_record *existing;
        if (record == NULL) goto cleanup;
        record->configuration = CRYPTO_THREAD_lock_new();
        if (record->configuration == NULL) {
            OPENSSL_free(record);
            goto cleanup;
        }
        record->server = server;
        foundation_openssl_server_registry_lock();
        do {
            record->identifier = atomic_fetch_add_explicit(&foundation_openssl_next_server, 1, memory_order_relaxed);
            if (record->identifier == 0) continue;
            for (existing = foundation_openssl_servers; existing != NULL && existing->identifier != record->identifier; existing = existing->next) {}
        } while (record->identifier == 0 || existing != NULL);
        record->next = foundation_openssl_servers;
        foundation_openssl_servers = record;
        foundation_openssl_server_registry_unlock();
        *result = record->identifier;
    }
    server = NULL;
    socket_fd = FOUNDATION_OPENSSL_INVALID_SOCKET;
    status = FOUNDATION_OPENSSL_OK;
cleanup:
    freeaddrinfo(addresses);
    OPENSSL_free(host);
    if (server != NULL) {
        foundation_openssl_server_certificates_free(server->certificates);
        OPENSSL_free(server);
    }
    if (socket_fd != FOUNDATION_OPENSSL_INVALID_SOCKET) foundation_openssl_socket_close(socket_fd);
    if (status != FOUNDATION_OPENSSL_OK) foundation_openssl_network_close();
    return status;
}

int32_t foundation_openssl_server_add_certificate(uint64_t server_handle,
                                                  const char *server_name,
                                                  uint64_t server_name_length,
                                                  uint64_t certificate_handle,
                                                  uint64_t private_key_handle) {
    foundation_openssl_server_record *record = foundation_openssl_server_acquire(server_handle);
    int32_t status;
    if (record == NULL) return FOUNDATION_OPENSSL_FAILED;
    if (!CRYPTO_THREAD_write_lock(record->configuration)) {
        foundation_openssl_server_release(record);
        return FOUNDATION_OPENSSL_FAILED;
    }
    foundation_openssl_server_registry_lock();
    status = !record->closing && !record->started ? FOUNDATION_OPENSSL_OK : FOUNDATION_OPENSSL_FAILED;
    foundation_openssl_server_registry_unlock();
    if (status == FOUNDATION_OPENSSL_OK &&
        !foundation_openssl_server_add(record->server, server_name, server_name_length,
                                       certificate_handle, private_key_handle)) status = FOUNDATION_OPENSSL_FAILED;
    CRYPTO_THREAD_unlock(record->configuration);
    foundation_openssl_server_release(record);
    return status;
}

void foundation_openssl_server_close(uint64_t *handle) {
    foundation_openssl_server_record *record;
    foundation_openssl_server_record **cursor;
    foundation_openssl_server *server;
    uint64_t identifier;
    if (handle == NULL || *handle == 0) return;
    identifier = *handle;
    foundation_openssl_server_registry_lock();
    for (cursor = &foundation_openssl_servers; *cursor != NULL && (*cursor)->identifier != identifier; cursor = &(*cursor)->next) {}
    if (*cursor == NULL || (*cursor)->closing) { foundation_openssl_server_registry_unlock(); *handle = 0; return; }
    record = *cursor;
    record->closing = 1;
    server = record->server;
    *handle = 0;
    shutdown(server->socket, FOUNDATION_OPENSSL_SOCKET_SHUTDOWN);
    foundation_openssl_socket_close(server->socket);
    foundation_openssl_server_registry_unlock();
    for (;;) {
        size_t users;
        foundation_openssl_server_registry_lock();
        users = record->users;
        foundation_openssl_server_registry_unlock();
        if (users == 0) break;
#ifdef _WIN32
        Sleep(1);
#else
        { struct timespec delay = { .tv_sec = 0, .tv_nsec = 1000000 }; nanosleep(&delay, NULL); }
#endif
    }
    foundation_openssl_server_registry_lock();
    for (cursor = &foundation_openssl_servers; *cursor != NULL && *cursor != record; cursor = &(*cursor)->next) {}
    if (*cursor != record) {
        foundation_openssl_server_registry_unlock();
        return;
    }
    *cursor = record->next;
    foundation_openssl_server_registry_unlock();
    foundation_openssl_server_certificates_free(server->certificates);
    OPENSSL_free(server);
    CRYPTO_THREAD_lock_free(record->configuration);
    OPENSSL_free(record);
    foundation_openssl_network_close();
}

int32_t foundation_openssl_server_accept(uint64_t server_handle, uint64_t *result) {
    foundation_openssl_server_record *record = foundation_openssl_server_acquire(server_handle);
    foundation_openssl_server *server;
    foundation_openssl_server_connection *connection = NULL;
    foundation_openssl_socket socket_fd = FOUNDATION_OPENSSL_INVALID_SOCKET;
    SSL *ssl = NULL;
    if (result == NULL || record == NULL) return FOUNDATION_OPENSSL_FAILED;
    server = record->server;
    *result = 0;
    if (!CRYPTO_THREAD_write_lock(record->configuration)) {
        foundation_openssl_server_release(record);
        return FOUNDATION_OPENSSL_FAILED;
    }
    if (!foundation_openssl_server_start(record)) {
        CRYPTO_THREAD_unlock(record->configuration);
        foundation_openssl_server_release(record);
        return FOUNDATION_OPENSSL_FAILED;
    }
    CRYPTO_THREAD_unlock(record->configuration);
    socket_fd = accept(server->socket, NULL, NULL);
    if (socket_fd == FOUNDATION_OPENSSL_INVALID_SOCKET) { foundation_openssl_server_release(record); return FOUNDATION_OPENSSL_FAILED; }
    ssl = SSL_new(server->default_context);
    if (ssl == NULL || SSL_set_fd(ssl, (int)socket_fd) != 1 ||
        foundation_openssl_tls_handshake_until(ssl, socket_fd, 1,
                                               foundation_openssl_deadline(15000000000LL), 0) != FOUNDATION_OPENSSL_OK) goto cleanup;
    connection = OPENSSL_zalloc(sizeof(*connection));
    if (connection == NULL) goto cleanup;
    connection->lock = CRYPTO_THREAD_lock_new();
    if (connection->lock == NULL) goto cleanup;
    connection->ssl = ssl;
    connection->socket = socket_fd;
    if (!foundation_openssl_network_open()) goto cleanup;
    *result = (uint64_t)(uintptr_t)connection;
    foundation_openssl_server_release(record);
    return FOUNDATION_OPENSSL_OK;
cleanup:
    if (connection != NULL) CRYPTO_THREAD_lock_free(connection->lock);
    OPENSSL_free(connection);
    SSL_free(ssl);
    foundation_openssl_socket_close(socket_fd);
    foundation_openssl_server_release(record);
    return FOUNDATION_OPENSSL_HANDSHAKE_FAILED;
}

int32_t foundation_openssl_server_connection_peer(uint64_t handle, fdn_string *result) {
    foundation_openssl_server_connection *connection = (foundation_openssl_server_connection *)(uintptr_t)handle;
    struct sockaddr_storage address;
#ifdef _WIN32
    int length = sizeof(address);
#else
    socklen_t length = sizeof(address);
#endif
    char host[NI_MAXHOST];
    if (connection == NULL || result == NULL || getpeername(connection->socket, (struct sockaddr *)&address, &length) != 0 ||
        getnameinfo((struct sockaddr *)&address, length, host, sizeof(host), NULL, 0, NI_NUMERICHOST) != 0) {
        return FOUNDATION_OPENSSL_FAILED;
    }
    return foundation_openssl_string((const unsigned char *)host, strlen(host), result);
}

void foundation_openssl_server_connection_close(uint64_t *handle) {
    foundation_openssl_server_connection *connection;
    if (handle == NULL || *handle == 0) return;
    connection = (foundation_openssl_server_connection *)(uintptr_t)*handle;
    *handle = 0;
    foundation_openssl_tls_shutdown(connection->ssl, connection->socket);
    SSL_free(connection->ssl);
    foundation_openssl_socket_close(connection->socket);
    CRYPTO_THREAD_lock_free(connection->lock);
    OPENSSL_free(connection);
    foundation_openssl_network_close();
}

int32_t foundation_openssl_server_connection_split(uint64_t *handle, uint64_t *reader,
                                                   uint64_t *writer) {
    foundation_openssl_server_connection *connection;
    foundation_openssl_server_stream *read_stream = NULL;
    foundation_openssl_server_stream *write_stream = NULL;
    if (handle == NULL || reader == NULL || writer == NULL || *handle == 0) return FOUNDATION_OPENSSL_FAILED;
    *reader = 0;
    *writer = 0;
    connection = (foundation_openssl_server_connection *)(uintptr_t)*handle;
    read_stream = OPENSSL_zalloc(sizeof(*read_stream));
    write_stream = OPENSSL_zalloc(sizeof(*write_stream));
    if (read_stream == NULL || write_stream == NULL) goto cleanup;
    read_stream->connection = connection;
    write_stream->connection = connection;
    atomic_init(&connection->streams, 2);
    *reader = (uint64_t)(uintptr_t)read_stream;
    *writer = (uint64_t)(uintptr_t)write_stream;
    *handle = 0;
    return FOUNDATION_OPENSSL_OK;
cleanup:
    OPENSSL_free(read_stream);
    OPENSSL_free(write_stream);
    return FOUNDATION_OPENSSL_FAILED;
}

void foundation_openssl_server_stream_close(uint64_t *handle) {
    foundation_openssl_server_stream *stream;
    foundation_openssl_server_connection *connection;
    if (handle == NULL || *handle == 0) return;
    stream = (foundation_openssl_server_stream *)(uintptr_t)*handle;
    *handle = 0;
    connection = stream->connection;
    OPENSSL_free(stream);
    if (connection != NULL && atomic_fetch_sub_explicit(&connection->streams, 1, memory_order_acq_rel) == 1) {
        foundation_openssl_tls_shutdown(connection->ssl, connection->socket);
        SSL_free(connection->ssl);
        foundation_openssl_socket_close(connection->socket);
        CRYPTO_THREAD_lock_free(connection->lock);
        OPENSSL_free(connection);
        foundation_openssl_network_close();
    }
}

static int32_t foundation_openssl_server_read(foundation_openssl_server_stream *stream,
                                              size_t limit, int line, fdn_string *result) {
    unsigned char *buffer = NULL;
    size_t length = 0;
    if (stream == NULL || result == NULL) return FOUNDATION_OPENSSL_FAILED;
    if (limit == 0) return FOUNDATION_OPENSSL_LIMIT_EXCEEDED;
    buffer = OPENSSL_malloc(limit);
    if (buffer == NULL) return FOUNDATION_OPENSSL_FAILED;
    while (length < limit) {
        int read;
        if (!CRYPTO_THREAD_write_lock(stream->connection->lock)) {
            OPENSSL_free(buffer);
            return FOUNDATION_OPENSSL_FAILED;
        }
        read = SSL_read(stream->connection->ssl, buffer + length, 1);
        CRYPTO_THREAD_unlock(stream->connection->lock);
        if (read <= 0) {
            int error = SSL_get_error(stream->connection->ssl, read);
            OPENSSL_free(buffer);
            return error == SSL_ERROR_ZERO_RETURN ? FOUNDATION_OPENSSL_PROTOCOL_FAILED : FOUNDATION_OPENSSL_FAILED;
        }
        if (line && buffer[length] == '\n') {
            int32_t status;
            if (length > 0 && buffer[length - 1] == '\r') --length;
            status = foundation_openssl_string(buffer, length, result);
            OPENSSL_free(buffer);
            return status;
        }
        ++length;
        if (!line && length == limit) {
            int32_t status = foundation_openssl_string(buffer, length, result);
            OPENSSL_free(buffer);
            return status;
        }
    }
    OPENSSL_free(buffer);
    return FOUNDATION_OPENSSL_LIMIT_EXCEEDED;
}

int32_t foundation_openssl_server_read_line(uint64_t handle, uint64_t limit, fdn_string *result) {
    if (limit > SIZE_MAX) return FOUNDATION_OPENSSL_LIMIT_EXCEEDED;
    return foundation_openssl_server_read((foundation_openssl_server_stream *)(uintptr_t)handle,
                                          (size_t)limit, 1, result);
}

int32_t foundation_openssl_server_read_exact(uint64_t handle, uint64_t length, fdn_string *result) {
    if (length > SIZE_MAX) return FOUNDATION_OPENSSL_LIMIT_EXCEEDED;
    return foundation_openssl_server_read((foundation_openssl_server_stream *)(uintptr_t)handle,
                                          (size_t)length, 0, result);
}

static int32_t foundation_openssl_server_write(foundation_openssl_server_stream *stream,
                                               const unsigned char *value, size_t length) {
    size_t written = 0;
    if (stream == NULL || (value == NULL && length != 0)) return FOUNDATION_OPENSSL_FAILED;
    while (written < length) {
        size_t chunk = length - written;
        int count;
        if (chunk > INT_MAX) chunk = INT_MAX;
        if (!CRYPTO_THREAD_write_lock(stream->connection->lock)) return FOUNDATION_OPENSSL_FAILED;
        count = SSL_write(stream->connection->ssl, value + written, (int)chunk);
        CRYPTO_THREAD_unlock(stream->connection->lock);
        if (count <= 0) return FOUNDATION_OPENSSL_FAILED;
        written += (size_t)count;
    }
    return FOUNDATION_OPENSSL_OK;
}

int32_t foundation_openssl_server_write_text(uint64_t handle, const char *value,
                                             uint64_t value_length) {
    if (value_length > SIZE_MAX) return FOUNDATION_OPENSSL_FAILED;
    return foundation_openssl_server_write((foundation_openssl_server_stream *)(uintptr_t)handle,
                                           (const unsigned char *)value, (size_t)value_length);
}

int32_t foundation_openssl_server_write_bytes(uint64_t handle, uint64_t value_handle) {
    foundation_openssl_bytes value = {0};
    int32_t status;
    if (!foundation_openssl_copy_bytes(value_handle, &value)) return FOUNDATION_OPENSSL_FAILED;
    status = foundation_openssl_server_write((foundation_openssl_server_stream *)(uintptr_t)handle,
                                             value.data, value.length);
    foundation_openssl_free_bytes(&value);
    return status;
}
