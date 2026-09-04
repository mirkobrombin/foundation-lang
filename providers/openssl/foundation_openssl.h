#ifndef FOUNDATION_OPENSSL_H
#define FOUNDATION_OPENSSL_H

#include <stdint.h>

#include <foundation/runtime.h>

// OpenSSL adapter status values are stable across Foundation provider bindings.
enum foundation_openssl_status {
    FOUNDATION_OPENSSL_OK = 0,
    FOUNDATION_OPENSSL_UNAVAILABLE = 1,
    FOUNDATION_OPENSSL_INVALID_PRIVATE_KEY = 2,
    FOUNDATION_OPENSSL_INVALID_PUBLIC_KEY = 3,
    FOUNDATION_OPENSSL_INVALID_SIGNATURE = 4,
    FOUNDATION_OPENSSL_FAILED = 5,
    FOUNDATION_OPENSSL_INVALID_SERVER_NAME = 6,
    FOUNDATION_OPENSSL_PEER_REJECTED = 7,
    FOUNDATION_OPENSSL_HANDSHAKE_FAILED = 8,
    FOUNDATION_OPENSSL_PROTOCOL_FAILED = 9,
    FOUNDATION_OPENSSL_LIMIT_EXCEEDED = 10,
    FOUNDATION_OPENSSL_TIMEOUT = 11,
    FOUNDATION_OPENSSL_CANCELLED = 12,
};

enum foundation_openssl_algorithm {
    FOUNDATION_OPENSSL_RS256 = 0,
    FOUNDATION_OPENSSL_ES256 = 1,
    FOUNDATION_OPENSSL_EDDSA = 2,
};

int32_t foundation_openssl_sign(uint32_t algorithm, uint64_t private_key,
                                uint64_t signing_input, uint64_t *signature);
int32_t foundation_openssl_verify(uint32_t algorithm, uint64_t public_key,
                                  uint64_t signing_input, uint64_t signature);
int32_t foundation_openssl_ed25519_generate(uint64_t *private_key,
                                            uint64_t *public_key,
                                            uint64_t *raw_public_key);
int32_t foundation_openssl_ed25519_public(uint64_t private_key,
                                          uint64_t *public_key,
                                          uint64_t *raw_public_key);
int32_t foundation_openssl_ed25519_public_pem(uint64_t raw_public_key,
                                              uint64_t *public_key);
int32_t foundation_openssl_ed25519_self_signed(uint64_t private_key,
                                               uint64_t *certificate);

// Opens a TLS client connection after SNI and hostname verification. The handle
// is intentionally opaque: callers cannot downgrade it into a plain socket.
int32_t foundation_openssl_tls_connect(const char *server_name, uint64_t server_name_length,
                                      uint16_t port, uint64_t *connection);
void foundation_openssl_tls_close(uint64_t *connection);
int32_t foundation_openssl_tls_http11(const char *server_name, uint64_t server_name_length,
                                      uint16_t port, const char *request_head,
                                      uint64_t request_head_length, uint64_t body,
                                      uint64_t maximum_body, int64_t timeout_nanoseconds,
                                      uint64_t cancellation,
                                      fdn_string *response_head,
                                      uint64_t *response_body);
int32_t foundation_openssl_tls_http11_ca(const char *server_name, uint64_t server_name_length,
                                         uint16_t port, uint64_t authority_bundle,
                                         const char *request_head, uint64_t request_head_length,
                                         uint64_t body, uint64_t maximum_body,
                                         int64_t timeout_nanoseconds,
                                         uint64_t cancellation,
                                         fdn_string *response_head, uint64_t *response_body);

int32_t foundation_openssl_server_open(const char *address, uint64_t address_length,
                                       uint16_t port, const char *server_name,
                                       uint64_t server_name_length, uint64_t certificate,
                                       uint64_t private_key, uint64_t *server,
                                       uint64_t *bound_port);
int32_t foundation_openssl_server_open_protocol(const char *address,
                                                uint64_t address_length,
                                                uint16_t port,
                                                const char *server_name,
                                                uint64_t server_name_length,
                                                uint64_t certificate,
                                                uint64_t private_key,
                                                const char *protocol,
                                                uint64_t protocol_length,
                                                uint64_t *server,
                                                uint64_t *bound_port);
int32_t foundation_openssl_tls_connect_pinned(const char *server_name,
                                              uint64_t server_name_length,
                                              uint16_t port,
                                              uint64_t raw_public_key,
                                              const char *protocol,
                                              uint64_t protocol_length,
                                              int64_t timeout_nanoseconds,
                                              uint64_t cancellation,
                                              uint64_t *connection);
int32_t foundation_openssl_secure_server_open(const fdn_string *address,
                                              uint64_t port,
                                              const fdn_string *server_name,
                                              uint64_t certificate,
                                              uint64_t private_key,
                                              const fdn_string *protocol,
                                              uint64_t *server,
                                              uint64_t *bound_port);
int32_t foundation_openssl_secure_connect_pinned(const fdn_string *server_name,
                                                 uint64_t port,
                                                 uint64_t raw_public_key,
                                                 const fdn_string *protocol,
                                                 int64_t timeout_nanoseconds,
                                                 uint64_t cancellation,
                                                 uint64_t *connection);
int32_t foundation_openssl_secure_connect_over_pinned(
    uint64_t transport, uint64_t raw_public_key, const fdn_string *protocol,
    int64_t timeout_nanoseconds, uint64_t cancellation,
    uint64_t *connection);
int32_t foundation_openssl_secure_accept_over(
    uint64_t transport, uint64_t certificate, uint64_t private_key,
    const fdn_string *protocol, int64_t timeout_nanoseconds,
    uint64_t cancellation, uint64_t *connection);
int32_t foundation_openssl_server_add_certificate(uint64_t server, const char *server_name,
                                                  uint64_t server_name_length,
                                                  uint64_t certificate,
                                                  uint64_t private_key);
void foundation_openssl_server_close(uint64_t *server);
int32_t foundation_openssl_server_accept(uint64_t server, uint64_t *connection);
int32_t foundation_openssl_server_connection_peer(uint64_t connection, fdn_string *address);
void foundation_openssl_server_connection_close(uint64_t *connection);
int32_t foundation_openssl_server_connection_split(uint64_t *connection,
                                                   uint64_t *reader, uint64_t *writer);
void foundation_openssl_server_stream_close(uint64_t *stream);
int32_t foundation_openssl_server_read_line(uint64_t stream, uint64_t limit,
                                            fdn_string *line);
int32_t foundation_openssl_server_read_exact(uint64_t stream, uint64_t length,
                                             fdn_string *value);
int32_t foundation_openssl_server_read_exact_bytes(uint64_t stream,
                                                   uint64_t length,
                                                   uint64_t *value);
int32_t foundation_openssl_server_write_text(uint64_t stream, const char *value,
                                             uint64_t value_length);
int32_t foundation_openssl_server_write_bytes(uint64_t stream, uint64_t value);
void foundation_openssl_secure_server_close(uint64_t *server);
int32_t foundation_openssl_secure_server_accept(uint64_t server,
                                                uint64_t *connection);
int32_t foundation_openssl_secure_connection_peer(uint64_t connection,
                                                  fdn_string *address);
void foundation_openssl_secure_connection_close(uint64_t *connection);
int32_t foundation_openssl_secure_connection_split(uint64_t *connection,
                                                   uint64_t *reader,
                                                   uint64_t *writer);
int32_t foundation_openssl_secure_connection_split_controlled(
    uint64_t *connection, uint64_t *reader, uint64_t *writer,
    uint64_t *controller);
void foundation_openssl_secure_stream_close(uint64_t *stream);
void foundation_openssl_secure_stream_abort(uint64_t *stream);
int32_t foundation_openssl_secure_read_exact_bytes(uint64_t stream,
                                                   uint64_t length,
                                                   uint64_t *value);
int32_t foundation_openssl_secure_write_bytes(uint64_t stream,
                                              uint64_t value);

#endif
