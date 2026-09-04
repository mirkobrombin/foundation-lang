#include "foundation_openssl.h"

#include <foundation/runtime.h>

#include <openssl/pem.h>
#include <openssl/ec.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <stdio.h>
#include <string.h>

int32_t foundation_openssl_test_response_frame(const char *response, uint64_t response_length,
                                               uint64_t maximum_body, int request_head, int eof,
                                               fdn_string *response_head, uint64_t *response_body);

static uint64_t test_bytes(const char *value) {
    fdn_string text = fdn_string_static(value, strlen(value));
    return foundation_runtime_bytes_from_text(&text);
}

static int test_algorithm(int key_type, uint32_t algorithm) {
    EVP_PKEY_CTX *context = EVP_PKEY_CTX_new_id(key_type, NULL);
    EVP_PKEY *key = NULL;
    BIO *private_bio = NULL;
    BIO *public_bio = NULL;
    BUF_MEM *private_data = NULL;
    BUF_MEM *public_data = NULL;
    fdn_string private_text;
    fdn_string public_text;
    uint64_t private_handle = 0;
    uint64_t public_handle = 0;
    uint64_t input = 0;
    uint64_t tampered = 0;
    uint64_t signature = 0;
    int result = 0;

    if (context == NULL || EVP_PKEY_keygen_init(context) != 1) goto cleanup;
    if (key_type == EVP_PKEY_RSA && EVP_PKEY_CTX_set_rsa_keygen_bits(context, 2048) != 1) goto cleanup;
    if (key_type == EVP_PKEY_EC && EVP_PKEY_CTX_set_ec_paramgen_curve_nid(context, NID_X9_62_prime256v1) != 1) goto cleanup;
    if (EVP_PKEY_keygen(context, &key) != 1) goto cleanup;
    private_bio = BIO_new(BIO_s_mem());
    public_bio = BIO_new(BIO_s_mem());
    if (private_bio == NULL || public_bio == NULL ||
        PEM_write_bio_PrivateKey(private_bio, key, NULL, NULL, 0, NULL, NULL) != 1 ||
        PEM_write_bio_PUBKEY(public_bio, key) != 1) goto cleanup;
    BIO_get_mem_ptr(private_bio, &private_data);
    BIO_get_mem_ptr(public_bio, &public_data);
    private_text = fdn_string_static(private_data->data, private_data->length);
    public_text = fdn_string_static(public_data->data, public_data->length);
    private_handle = foundation_runtime_bytes_from_text(&private_text);
    public_handle = foundation_runtime_bytes_from_text(&public_text);
    input = test_bytes("foundation openssl provider");
    tampered = test_bytes("foundation openssl provider tampered");
    if (private_handle == 0 || public_handle == 0 || input == 0 || tampered == 0 ||
        foundation_openssl_sign(algorithm, private_handle, input, &signature) != FOUNDATION_OPENSSL_OK ||
        foundation_openssl_verify(algorithm, public_handle, input, signature) != FOUNDATION_OPENSSL_OK ||
        foundation_openssl_verify(algorithm, public_handle, tampered, signature) != FOUNDATION_OPENSSL_INVALID_SIGNATURE) {
        goto cleanup;
    }
    result = 1;
cleanup:
    foundation_runtime_bytes_close(&signature);
    foundation_runtime_bytes_close(&tampered);
    foundation_runtime_bytes_close(&input);
    foundation_runtime_bytes_close(&public_handle);
    foundation_runtime_bytes_close(&private_handle);
    BIO_free(public_bio);
    BIO_free(private_bio);
    EVP_PKEY_free(key);
    EVP_PKEY_CTX_free(context);
    return result;
}

static int test_ed25519_generation(void) {
    uint64_t private_key = 0;
    uint64_t public_key = 0;
    uint64_t raw_public_key = 0;
    uint64_t input = 0;
    uint64_t signature = 0;
    uint64_t raw_length = 0;
    int result = 0;
    if (foundation_openssl_ed25519_generate(&private_key, &public_key,
                                             &raw_public_key) != FOUNDATION_OPENSSL_OK ||
        foundation_runtime_bytes_length(raw_public_key, &raw_length) != 0 ||
        raw_length != 32) goto cleanup;
    input = test_bytes("generated Ed25519 key");
    if (input == 0 ||
        foundation_openssl_sign(FOUNDATION_OPENSSL_EDDSA, private_key,
                                input, &signature) != FOUNDATION_OPENSSL_OK ||
        foundation_openssl_verify(FOUNDATION_OPENSSL_EDDSA, public_key,
                                  input, signature) != FOUNDATION_OPENSSL_OK) goto cleanup;
    result = 1;
cleanup:
    foundation_runtime_bytes_close(&signature);
    foundation_runtime_bytes_close(&input);
    foundation_runtime_bytes_close(&raw_public_key);
    foundation_runtime_bytes_close(&public_key);
    foundation_runtime_bytes_close(&private_key);
    return result;
}

static int test_rejected_key(int key_type, int rsa_bits, int curve, uint32_t algorithm) {
    EVP_PKEY_CTX *context = EVP_PKEY_CTX_new_id(key_type, NULL);
    EVP_PKEY *key = NULL;
    BIO *private_bio = NULL;
    BIO *public_bio = NULL;
    BUF_MEM *private_data = NULL;
    BUF_MEM *public_data = NULL;
    fdn_string private_text;
    fdn_string public_text;
    uint64_t private_handle = 0;
    uint64_t public_handle = 0;
    uint64_t input = 0;
    uint64_t signature = 0;
    uint64_t invalid_signature = 0;
    int result = 0;
    if (context == NULL || EVP_PKEY_keygen_init(context) != 1 ||
        (key_type == EVP_PKEY_RSA && EVP_PKEY_CTX_set_rsa_keygen_bits(context, rsa_bits) != 1) ||
        (key_type == EVP_PKEY_EC && EVP_PKEY_CTX_set_ec_paramgen_curve_nid(context, curve) != 1) ||
        EVP_PKEY_keygen(context, &key) != 1) goto cleanup;
    private_bio = BIO_new(BIO_s_mem());
    public_bio = BIO_new(BIO_s_mem());
    if (private_bio == NULL || public_bio == NULL ||
        PEM_write_bio_PrivateKey(private_bio, key, NULL, NULL, 0, NULL, NULL) != 1 ||
        PEM_write_bio_PUBKEY(public_bio, key) != 1) goto cleanup;
    BIO_get_mem_ptr(private_bio, &private_data);
    BIO_get_mem_ptr(public_bio, &public_data);
    private_text = fdn_string_static(private_data->data, private_data->length);
    public_text = fdn_string_static(public_data->data, public_data->length);
    private_handle = foundation_runtime_bytes_from_text(&private_text);
    public_handle = foundation_runtime_bytes_from_text(&public_text);
    input = test_bytes("foundation openssl rejected key");
    invalid_signature = test_bytes("invalid signature");
    if (private_handle == 0 || public_handle == 0 || input == 0 || invalid_signature == 0 ||
        foundation_openssl_sign(algorithm, private_handle, input, &signature) != FOUNDATION_OPENSSL_INVALID_PRIVATE_KEY ||
        foundation_openssl_verify(algorithm, public_handle, input, invalid_signature) != FOUNDATION_OPENSSL_INVALID_PUBLIC_KEY) goto cleanup;
    result = 1;
cleanup:
    foundation_runtime_bytes_close(&signature);
    foundation_runtime_bytes_close(&invalid_signature);
    foundation_runtime_bytes_close(&input);
    foundation_runtime_bytes_close(&public_handle);
    foundation_runtime_bytes_close(&private_handle);
    BIO_free(public_bio);
    BIO_free(private_bio);
    EVP_PKEY_free(key);
    EVP_PKEY_CTX_free(context);
    return result;
}

static int test_cancelled_request(void) {
    uint64_t cancellation = foundation_runtime_cancellation_open();
    int32_t status;
    if (cancellation == 0) return 0;
    foundation_runtime_cancellation_request(cancellation);
    status = foundation_openssl_tls_http11(NULL, 0, 0, NULL, 0, 0, 0, 0,
                                           cancellation, NULL, NULL);
    foundation_runtime_cancellation_release(cancellation);
    return status == FOUNDATION_OPENSSL_CANCELLED;
}

static int test_response_frame(const char *response, uint64_t maximum, int request_head,
                               int eof, int32_t expected) {
    fdn_string head = fdn_string_static("", 0);
    uint64_t body = 0;
    int32_t status = foundation_openssl_test_response_frame(response, strlen(response), maximum,
                                                            request_head, eof, &head, &body);
    fdn_string_drop(&head);
    foundation_runtime_bytes_close(&body);
    return status == expected;
}

static int test_http_response_framing(void) {
    char oversized[65570];
    char interim[512];
    size_t interim_length = 0;
    int index;
    memset(oversized, 'a', sizeof(oversized));
    memcpy(oversized, "HTTP/1.1 200 OK\r\nX: ", 20);
    memcpy(oversized + sizeof(oversized) - 5, "\r\n\r\n", 5);
    oversized[sizeof(oversized) - 1] = '\0';
    for (index = 0; index != 9; ++index) {
        memcpy(interim + interim_length, "HTTP/1.1 100 Continue\r\n\r\n", 25);
        interim_length += 25;
    }
    memcpy(interim + interim_length, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n", 38);
    interim_length += 38;
    interim[interim_length] = '\0';
    return test_response_frame("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok", 16, 0, 0, FOUNDATION_OPENSSL_OK) &&
        test_response_frame("NOTHTTP 200 OK\r\n\r\n", 16, 0, 0, FOUNDATION_OPENSSL_PROTOCOL_FAILED) &&
        test_response_frame(interim, 16, 0, 0, FOUNDATION_OPENSSL_PROTOCOL_FAILED) &&
        test_response_frame("HTTP/1.1 101 Switching Protocols\r\n\r\n", 16, 0, 0, FOUNDATION_OPENSSL_PROTOCOL_FAILED) &&
        test_response_frame("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n", 16, 0, 0, FOUNDATION_OPENSSL_PROTOCOL_FAILED) &&
        test_response_frame("HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\na", 16, 0, 0, FOUNDATION_OPENSSL_PROTOCOL_FAILED) &&
        test_response_frame("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nZ\r\n", 16, 0, 0, FOUNDATION_OPENSSL_PROTOCOL_FAILED) &&
        test_response_frame("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nContent-Length: 1\r\n\r\n", 16, 0, 0, FOUNDATION_OPENSSL_PROTOCOL_FAILED) &&
        test_response_frame("HTTP/1.1 204 No Content\r\n\r\n", 16, 0, 0, FOUNDATION_OPENSSL_OK) &&
        test_response_frame("HTTP/1.1 200 OK\r\n\r\nclose", 16, 0, 1, FOUNDATION_OPENSSL_OK) &&
        test_response_frame(oversized, 16, 0, 0, FOUNDATION_OPENSSL_PROTOCOL_FAILED);
}

#ifndef _WIN32
typedef struct tls_fixture {
    EVP_PKEY *ca_key;
    X509 *ca;
    uint64_t ca_bundle;
    uint64_t one_certificate;
    uint64_t one_key;
    uint64_t two_certificate;
    uint64_t two_key;
} tls_fixture;

typedef struct tls_accept_loop {
    uint64_t server;
    int attempts;
    int accepted;
} tls_accept_loop;

typedef struct tls_server_race {
    pthread_mutex_t lock;
    pthread_cond_t condition;
    uint64_t server;
    uint64_t certificate;
    uint64_t private_key;
    int ready;
    int start;
    int add_certificate;
    int32_t status;
} tls_server_race;

static EVP_PKEY *test_rsa_key(void) {
    EVP_PKEY_CTX *context = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    EVP_PKEY *key = NULL;
    if (context == NULL || EVP_PKEY_keygen_init(context) != 1 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(context, 2048) != 1 ||
        EVP_PKEY_keygen(context, &key) != 1) {
        EVP_PKEY_free(key);
        key = NULL;
    }
    EVP_PKEY_CTX_free(context);
    return key;
}

static X509 *test_certificate(EVP_PKEY *key, X509 *issuer, EVP_PKEY *issuer_key,
                              const char *name, int certificate_authority) {
    X509 *certificate = X509_new();
    X509_NAME *subject;
    X509_EXTENSION *extension;
    if (certificate == NULL || X509_set_version(certificate, 2) != 1 ||
        ASN1_INTEGER_set(X509_get_serialNumber(certificate), (long)rand() + 1) != 1 ||
        X509_gmtime_adj(X509_getm_notBefore(certificate), -60) == NULL ||
        X509_gmtime_adj(X509_getm_notAfter(certificate), 86400) == NULL ||
        X509_set_pubkey(certificate, key) != 1) goto failed;
    subject = X509_get_subject_name(certificate);
    if (subject == NULL || X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
                                                       (const unsigned char *)name, -1, -1, 0) != 1 ||
        X509_set_issuer_name(certificate, issuer == NULL ? subject : X509_get_subject_name(issuer)) != 1) goto failed;
    extension = X509V3_EXT_conf_nid(NULL, NULL, NID_basic_constraints,
                                    certificate_authority ? "critical,CA:TRUE" : "critical,CA:FALSE");
    if (extension == NULL || X509_add_ext(certificate, extension, -1) != 1) {
        X509_EXTENSION_free(extension);
        goto failed;
    }
    X509_EXTENSION_free(extension);
    if (!certificate_authority) {
        char san[320];
        if (snprintf(san, sizeof(san), "DNS:%s", name) < 0 || strlen(san) >= sizeof(san)) goto failed;
        extension = X509V3_EXT_conf_nid(NULL, NULL, NID_subject_alt_name, san);
        if (extension == NULL || X509_add_ext(certificate, extension, -1) != 1) {
            X509_EXTENSION_free(extension);
            goto failed;
        }
        X509_EXTENSION_free(extension);
    }
    if (X509_sign(certificate, issuer_key == NULL ? key : issuer_key, EVP_sha256()) <= 0) goto failed;
    return certificate;
failed:
    X509_free(certificate);
    return NULL;
}

static uint64_t test_pem_bundle(X509 *certificate, X509 *chain) {
    BIO *bio = BIO_new(BIO_s_mem());
    BUF_MEM *data = NULL;
    fdn_string text;
    uint64_t result = 0;
    if (bio == NULL || PEM_write_bio_X509(bio, certificate) != 1 ||
        (chain != NULL && PEM_write_bio_X509(bio, chain) != 1)) goto cleanup;
    BIO_get_mem_ptr(bio, &data);
    if (data == NULL) goto cleanup;
    text = fdn_string_static(data->data, data->length);
    result = foundation_runtime_bytes_from_text(&text);
cleanup:
    BIO_free(bio);
    return result;
}

static uint64_t test_pem_key(EVP_PKEY *key) {
    BIO *bio = BIO_new(BIO_s_mem());
    BUF_MEM *data = NULL;
    fdn_string text;
    uint64_t result = 0;
    if (bio == NULL || PEM_write_bio_PrivateKey(bio, key, NULL, NULL, 0, NULL, NULL) != 1) goto cleanup;
    BIO_get_mem_ptr(bio, &data);
    if (data == NULL) goto cleanup;
    text = fdn_string_static(data->data, data->length);
    result = foundation_runtime_bytes_from_text(&text);
cleanup:
    BIO_free(bio);
    return result;
}

static int test_tls_fixture_open(tls_fixture *fixture) {
    EVP_PKEY *one_key = NULL;
    EVP_PKEY *two_key = NULL;
    X509 *one = NULL;
    X509 *two = NULL;
    memset(fixture, 0, sizeof(*fixture));
    fixture->ca_key = test_rsa_key();
    if (fixture->ca_key == NULL ||
        (fixture->ca = test_certificate(fixture->ca_key, NULL, NULL, "Foundation Test CA", 1)) == NULL ||
        (one_key = test_rsa_key()) == NULL || (two_key = test_rsa_key()) == NULL ||
        (one = test_certificate(one_key, fixture->ca, fixture->ca_key, "one.foundation.test", 0)) == NULL ||
        (two = test_certificate(two_key, fixture->ca, fixture->ca_key, "two.foundation.test", 0)) == NULL) goto failed;
    fixture->ca_bundle = test_pem_bundle(fixture->ca, NULL);
    fixture->one_certificate = test_pem_bundle(one, fixture->ca);
    fixture->two_certificate = test_pem_bundle(two, fixture->ca);
    fixture->one_key = test_pem_key(one_key);
    fixture->two_key = test_pem_key(two_key);
    EVP_PKEY_free(one_key);
    EVP_PKEY_free(two_key);
    X509_free(one);
    X509_free(two);
    return fixture->ca_bundle != 0 && fixture->one_certificate != 0 && fixture->two_certificate != 0 &&
        fixture->one_key != 0 && fixture->two_key != 0;
failed:
    EVP_PKEY_free(one_key);
    EVP_PKEY_free(two_key);
    X509_free(one);
    X509_free(two);
    return 0;
}

static void test_tls_fixture_close(tls_fixture *fixture) {
    foundation_runtime_bytes_close(&fixture->ca_bundle);
    foundation_runtime_bytes_close(&fixture->one_certificate);
    foundation_runtime_bytes_close(&fixture->one_key);
    foundation_runtime_bytes_close(&fixture->two_certificate);
    foundation_runtime_bytes_close(&fixture->two_key);
    X509_free(fixture->ca);
    EVP_PKEY_free(fixture->ca_key);
}

static void *test_tls_accept(void *argument) {
    tls_accept_loop *loop = argument;
    int index;
    for (index = 0; index < loop->attempts; ++index) {
        uint64_t connection = 0;
        if (foundation_openssl_server_accept(loop->server, &connection) == FOUNDATION_OPENSSL_OK) {
            uint64_t reader = 0;
            uint64_t writer = 0;
            fdn_string line = fdn_string_static("", 0);
            ++loop->accepted;
            if (foundation_openssl_server_connection_split(&connection, &reader, &writer) == FOUNDATION_OPENSSL_OK) {
                while (foundation_openssl_server_read_line(reader, 8192, &line) == FOUNDATION_OPENSSL_OK) {
                    int empty = line.length == 0;
                    fdn_string_drop(&line);
                    if (empty) break;
                }
                foundation_openssl_server_write_text(writer,
                    "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok", 59);
                foundation_openssl_server_stream_close(&reader);
                foundation_openssl_server_stream_close(&writer);
            } else {
                foundation_openssl_server_connection_close(&connection);
            }
        }
    }
    return NULL;
}

static void *test_tls_server_race(void *argument) {
    tls_server_race *race = argument;
    uint64_t connection = 0;
    pthread_mutex_lock(&race->lock);
    race->ready = 1;
    pthread_cond_signal(&race->condition);
    while (!race->start) pthread_cond_wait(&race->condition, &race->lock);
    pthread_mutex_unlock(&race->lock);
    if (race->add_certificate) {
        race->status = foundation_openssl_server_add_certificate(race->server,
            "two.foundation.test", 19, race->certificate, race->private_key);
    } else {
        race->status = foundation_openssl_server_accept(race->server, &connection);
        foundation_openssl_server_connection_close(&connection);
    }
    return NULL;
}

static int test_tls_server_close_races(void) {
    tls_fixture fixture;
    int add_certificate;
    int result = 1;
    if (!test_tls_fixture_open(&fixture)) return 0;
    for (add_certificate = 0; add_certificate != 2; ++add_certificate) {
        tls_server_race race;
        pthread_t thread;
        uint64_t server = 0;
        uint64_t port = 0;
        memset(&race, 0, sizeof(race));
        if (pthread_mutex_init(&race.lock, NULL) != 0 || pthread_cond_init(&race.condition, NULL) != 0 ||
            foundation_openssl_server_open("127.0.0.1", 9, 0, "one.foundation.test", 19,
                                           fixture.one_certificate, fixture.one_key,
                                           &server, &port) != FOUNDATION_OPENSSL_OK) {
            result = 0;
            if (server != 0) foundation_openssl_server_close(&server);
            continue;
        }
        race.server = server;
        race.certificate = fixture.two_certificate;
        race.private_key = fixture.two_key;
        race.add_certificate = add_certificate;
        if (pthread_create(&thread, NULL, test_tls_server_race, &race) != 0) {
            foundation_openssl_server_close(&server);
            result = 0;
            continue;
        }
        pthread_mutex_lock(&race.lock);
        while (!race.ready) pthread_cond_wait(&race.condition, &race.lock);
        race.start = 1;
        pthread_cond_signal(&race.condition);
        pthread_mutex_unlock(&race.lock);
        foundation_openssl_server_close(&server);
        pthread_join(thread, NULL);
        pthread_cond_destroy(&race.condition);
        pthread_mutex_destroy(&race.lock);
    }
    test_tls_fixture_close(&fixture);
    return result;
}

static int test_tcp_connect(uint16_t port) {
    struct sockaddr_in address;
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) return -1;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(socket_fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(socket_fd);
        return -1;
    }
    return socket_fd;
}

static int test_tls_client(uint16_t port, X509 *authority, const char *sni,
                           const char *hostname, int expected_success) {
    SSL_CTX *context = SSL_CTX_new(TLS_client_method());
    SSL *ssl = NULL;
    X509_STORE *store;
    int socket_fd = -1;
    int success = 0;
    if (context == NULL || (store = SSL_CTX_get_cert_store(context)) == NULL ||
        X509_STORE_add_cert(store, authority) != 1) goto cleanup;
    SSL_CTX_set_verify(context, SSL_VERIFY_PEER, NULL);
    socket_fd = test_tcp_connect(port);
    ssl = SSL_new(context);
    if (socket_fd < 0 || ssl == NULL || SSL_set_fd(ssl, socket_fd) != 1 ||
        SSL_set_tlsext_host_name(ssl, sni) != 1 || SSL_set1_host(ssl, hostname) != 1) goto cleanup;
    success = SSL_connect(ssl) == 1 && SSL_get_verify_result(ssl) == X509_V_OK;
    if (success && expected_success) {
        char response[64];
        int read;
        success = SSL_write(ssl, "GET / HTTP/1.1\r\nHost: local\r\nConnection: close\r\n\r\n", 50) == 50;
        read = success ? SSL_read(ssl, response, sizeof(response)) : 0;
        success = read >= 15 && memcmp(response, "HTTP/1.1 200 OK", 15) == 0;
    }
cleanup:
    SSL_free(ssl);
    if (socket_fd >= 0) close(socket_fd);
    SSL_CTX_free(context);
    return success == expected_success;
}

static int test_tls_server(void) {
    tls_fixture fixture;
    tls_accept_loop loop;
    pthread_t thread;
    uint64_t server = 0;
    uint64_t bound_port = 0;
    int plaintext = -1;
    int result = 0;
    if (!test_tls_fixture_open(&fixture) ||
        foundation_openssl_server_open("127.0.0.1", 9, 0, "one.foundation.test", 19,
                                       fixture.one_certificate, fixture.one_key,
                                       &server, &bound_port) != FOUNDATION_OPENSSL_OK ||
        foundation_openssl_server_add_certificate(server, "two.foundation.test", 19,
                                                  fixture.two_certificate, fixture.two_key) != FOUNDATION_OPENSSL_OK) goto cleanup;
    loop.server = server;
    loop.attempts = 5;
    loop.accepted = 0;
    if (pthread_create(&thread, NULL, test_tls_accept, &loop) != 0) goto cleanup;
    if (!test_tls_client((uint16_t)bound_port, fixture.ca, "one.foundation.test", "one.foundation.test", 1) ||
        !test_tls_client((uint16_t)bound_port, fixture.ca, "two.foundation.test", "two.foundation.test", 1) ||
        !test_tls_client((uint16_t)bound_port, fixture.ca, "one.foundation.test", "wrong.foundation.test", 0) ||
        !test_tls_client((uint16_t)bound_port, fixture.ca, "unknown.foundation.test", "unknown.foundation.test", 0)) goto joining;
    plaintext = test_tcp_connect((uint16_t)bound_port);
    if (plaintext < 0 || send(plaintext, "GET / HTTP/1.1\r\n\r\n", 18, 0) != 18) goto joining;
    close(plaintext);
    plaintext = -1;
    result = 1;
joining:
    if (plaintext >= 0) close(plaintext);
    pthread_join(thread, NULL);
    result = result && loop.accepted == 2;
cleanup:
    foundation_openssl_server_close(&server);
    test_tls_fixture_close(&fixture);
    return result;
}
#endif

int main(void) {
    if (!test_algorithm(EVP_PKEY_RSA, FOUNDATION_OPENSSL_RS256) ||
        !test_algorithm(EVP_PKEY_EC, FOUNDATION_OPENSSL_ES256) ||
        !test_algorithm(EVP_PKEY_ED25519, FOUNDATION_OPENSSL_EDDSA) ||
        !test_ed25519_generation() ||
        !test_rejected_key(EVP_PKEY_RSA, 1024, 0, FOUNDATION_OPENSSL_RS256) ||
        !test_rejected_key(EVP_PKEY_EC, 0, NID_secp384r1, FOUNDATION_OPENSSL_ES256) ||
        !test_cancelled_request() || !test_http_response_framing()) {
        return 1;
    }
#ifndef _WIN32
    if (!test_tls_server() || !test_tls_server_close_races()) return 1;
#endif
    puts("openssl asymmetric provider: ok");
    return 0;
}
