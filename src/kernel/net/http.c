#include <mydos.h>
#include <net/net.h>
#include <net/tcp.h>
#include <ff.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include "github_ca.h"

#define HTTP_HOST_MAX 128
#define HTTP_PATH_MAX 192
#define HTTP_HEADER_MAX 2048
#define HTTP_BUFFER_SIZE NET_MAX_FRAME
#define HTTP_RECV_TIMEOUT_MS 8000u

static int http_append(char *out, uint32_t capacity, uint32_t *position,
                       const char *text);

typedef struct {
    char host[HTTP_HOST_MAX];
    uint16_t port;
    int secure;
    char path[HTTP_PATH_MAX];
} http_url_t;

typedef struct {
    tcp_socket_t socket;
    int secure;
    int tls_ready;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt ca;
} http_connection_t;

static int http_lower(int character) {
    if (character >= 'A' && character <= 'Z') {
        return character + ('a' - 'A');
    }
    return character;
}

static void http_copy(char *out, const char *text) {
    while (*text != '\0') {
        *out++ = *text++;
    }
    *out = '\0';
}

static int http_prefix(const char *text, const char *prefix) {
    while (*prefix != '\0') {
        if (*text == '\0' || http_lower((unsigned char)*text) !=
                             http_lower((unsigned char)*prefix)) {
            return 0;
        }
        text++;
        prefix++;
    }
    return 1;
}

static int http_parse_uint(const char *text, uint32_t *value) {
    uint32_t result = 0;
    if (*text == '\0') {
        return 0;
    }
    while (*text >= '0' && *text <= '9') {
        result = result * 10u + (uint32_t)(*text - '0');
        text++;
    }
    while (*text == ' ' || *text == '\t') {
        text++;
    }
    if (*text != '\0') {
        return 0;
    }
    *value = result;
    return 1;
}

static int http_parse_url(const char *url, http_url_t *out) {
    uint32_t scheme_length;
    if (http_prefix(url, "https://")) {
        scheme_length = 8;
        out->secure = 1;
    } else if (http_prefix(url, "http://")) {
        scheme_length = 7;
        out->secure = 0;
    } else {
        return 0;
    }
    const char *host_start = url + scheme_length;
    const char *host_end = strchr(host_start, '/');
    if (host_end == 0) {
        host_end = host_start + strlen(host_start);
    }
    uint32_t host_length = (uint32_t)(host_end - host_start);
    if (host_length == 0 || host_length >= HTTP_HOST_MAX) {
        return 0;
    }

    const char *port_separator = 0;
    for (const char *character = host_start; character < host_end; character++) {
        if (*character == ':') {
            port_separator = character;
        }
    }

    uint32_t actual_host_length = host_length;
    out->port = out->secure ? 443 : 80;
    if (port_separator != 0) {
        actual_host_length = (uint32_t)(port_separator - host_start);
        uint32_t port = 0;
        if (!http_parse_uint(port_separator + 1, &port) || port == 0 || port > 65535) {
            return 0;
        }
        out->port = (uint16_t)port;
    }
    if (actual_host_length == 0 || actual_host_length >= HTTP_HOST_MAX) {
        return 0;
    }
    memcpy(out->host, host_start, actual_host_length);
    out->host[actual_host_length] = '\0';

    if (*host_end == '/') {
        uint32_t path_length = (uint32_t)strlen(host_end);
        if (path_length == 0 || path_length >= HTTP_PATH_MAX) {
            return 0;
        }
        memcpy(out->path, host_end, path_length + 1);
    } else {
        http_copy(out->path, "/");
    }
    return 1;
}

static int http_tls_send(void *context, const unsigned char *data, size_t length) {
    http_connection_t *connection = (http_connection_t *)context;
    size_t sent = 0;
    while (sent < length) {
        uint32_t chunk = length - sent;
        if (chunk > 1200) {
            chunk = 1200;
        }
        if (!tcp_send(&connection->socket, data + sent, chunk)) {
            return sent == 0 ? -1 : (int)sent;
        }
        sent += chunk;
    }
    return (int)length;
}

static int http_tls_recv(void *context, unsigned char *buffer, size_t capacity) {
    http_connection_t *connection = (http_connection_t *)context;
    uint32_t received = 0;
    int result = tcp_recv(&connection->socket, buffer, (uint32_t)capacity,
                          HTTP_RECV_TIMEOUT_MS, &received);
    if (result < 0) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    return result == 0 ? 0 : (int)received;
}

static void http_tls_release(http_connection_t *connection) {
    if (connection->tls_ready) {
        mbedtls_ssl_close_notify(&connection->ssl);
        connection->tls_ready = 0;
    }
    mbedtls_ssl_free(&connection->ssl);
    mbedtls_ssl_config_free(&connection->config);
    mbedtls_ctr_drbg_free(&connection->ctr_drbg);
    mbedtls_entropy_free(&connection->entropy);
    mbedtls_x509_crt_free(&connection->ca);
}

static int http_tls_setup(http_connection_t *connection, const char *host) {
    int result;
    mydos_tls_platform_init();
    mbedtls_ssl_init(&connection->ssl);
    mbedtls_ssl_config_init(&connection->config);
    mbedtls_entropy_init(&connection->entropy);
    mbedtls_ctr_drbg_init(&connection->ctr_drbg);
    mbedtls_x509_crt_init(&connection->ca);

    result = mbedtls_ctr_drbg_seed(&connection->ctr_drbg, mbedtls_entropy_func,
                                   &connection->entropy,
                                   (const unsigned char *)"mydos-flint-tls", 15);
    if (result != 0) {
        http_tls_release(connection);
        return 0;
    }
    result = mbedtls_x509_crt_parse(&connection->ca,
                                    (const unsigned char *)mydos_github_ca_pem,
                                    sizeof(mydos_github_ca_pem));
    if (result != 0) {
        http_tls_release(connection);
        return 0;
    }
    result = mbedtls_ssl_config_defaults(&connection->config,
                                         MBEDTLS_SSL_IS_CLIENT,
                                         MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT);
    if (result != 0) {
        http_tls_release(connection);
        return 0;
    }
    mbedtls_ssl_conf_authmode(&connection->config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&connection->config, &connection->ca, 0);
    mbedtls_ssl_conf_rng(&connection->config, mbedtls_ctr_drbg_random,
                         &connection->ctr_drbg);
    result = mbedtls_ssl_set_hostname(&connection->ssl, host);
    if (result != 0) {
        http_tls_release(connection);
        return 0;
    }
    result = mbedtls_ssl_setup(&connection->ssl, &connection->config);
    if (result != 0) {
        http_tls_release(connection);
        return 0;
    }
    mbedtls_ssl_set_bio(&connection->ssl, connection, http_tls_send,
                        http_tls_recv, 0);
    result = mbedtls_ssl_handshake(&connection->ssl);
    if (result != 0) {
        http_tls_release(connection);
        return 0;
    }
    connection->tls_ready = 1;
    return 1;
}

static int http_connection_send(http_connection_t *connection,
                                const uint8_t *data, uint32_t length) {
    if (!connection->secure) {
        return tcp_send(&connection->socket, data, length);
    }
    uint32_t sent = 0;
    while (sent < length) {
        int result = mbedtls_ssl_write(&connection->ssl, data + sent,
                                       length - sent);
        if (result <= 0) {
            return 0;
        }
        sent += (uint32_t)result;
    }
    return 1;
}

static int http_connection_recv(http_connection_t *connection, uint8_t *buffer,
                                uint32_t capacity, uint32_t *received) {
    if (!connection->secure) {
        return tcp_recv(&connection->socket, buffer, capacity,
                        HTTP_RECV_TIMEOUT_MS, received);
    }
    *received = 0;
    int result = mbedtls_ssl_read(&connection->ssl, buffer, capacity);
    if (result < 0) {
        return -1;
    }
    *received = (uint32_t)result;
    return result == 0 ? 0 : 1;
}

static void http_connection_close(http_connection_t *connection) {
    if (connection->secure) {
        http_tls_release(connection);
    }
    tcp_close(&connection->socket);
}

static const char *http_header_end(const char *header) {
    const char *position = header;
    while (*position != '\0') {
        if (position[0] == '\r' && position[1] == '\n' &&
            position[2] == '\r' && position[3] == '\n') {
            return position + 4;
        }
        position++;
    }
    return 0;
}

static int http_header_value(const char *header, const char *name,
                             char *out, uint32_t capacity) {
    const char *line = header;
    while (*line != '\0') {
        const char *end = strchr(line, '\n');
        if (end == 0) {
            break;
        }
        if (http_prefix(line, name)) {
            const char *value = line + strlen(name);
            while (*value == ' ' || *value == '\t') {
                value++;
            }
            uint32_t length = 0;
            while (*value != '\0' && *value != '\r' && *value != '\n' &&
                   length + 1 < capacity) {
                out[length++] = *value++;
            }
            out[length] = '\0';
            return 1;
        }
        line = end + 1;
    }
    return 0;
}

static int http_status_code(const char *header) {
    const char *space = strchr(header, ' ');
    if (space == 0 || space[1] < '0' || space[1] > '9') {
        return 0;
    }
    return (space[1] - '0') * 100 + (space[2] - '0') * 10 + (space[3] - '0');
}

static int http_append(char *out, uint32_t capacity, uint32_t *position,
                       const char *text) {
    while (*text != '\0') {
        if (*position + 1 >= capacity) {
            return 0;
        }
        out[(*position)++] = *text++;
    }
    return 1;
}

static int http_redirect_url(const http_url_t *source, const char *location,
                             char *out, uint32_t capacity) {
    if (http_prefix(location, "http://") ||
        http_prefix(location, "https://")) {
        uint32_t length = (uint32_t)strlen(location);
        if (length >= capacity) {
            return 0;
        }
        memcpy(out, location, length + 1);
        return 1;
    }
    if (location[0] != '/') {
        return 0;
    }
    uint32_t position = 0;
    if (!http_append(out, capacity, &position,
                     source->secure ? "https://" : "http://") ||
        !http_append(out, capacity, &position, source->host)) {
        return 0;
    }
    if (source->port != (source->secure ? 443u : 80u)) {
        char port[16];
        port[0] = ':';
        uint32_t value = source->port;
        char digits[8];
        uint32_t count = 0;
        do {
            digits[count++] = (char)('0' + value % 10u);
            value /= 10u;
        } while (value != 0);
        uint32_t length = 1;
        for (uint32_t i = 0; i < count; i++) {
            port[length++] = digits[count - 1 - i];
        }
        port[length] = '\0';
        if (!http_append(out, capacity, &position, port)) {
            return 0;
        }
    }
    if (!http_append(out, capacity, &position, location)) {
        return 0;
    }
    out[position] = '\0';
    return 1;
}

static int http_write(FIL *file, const uint8_t *data, uint32_t length) {
    UINT written = 0;
    if (f_write(file, data, length, &written) != FR_OK || written != length) {
        return 0;
    }
    return 1;
}

static int http_get_file_internal(const char *url, const char *path, int depth) {
    http_url_t parsed;
    if (!http_parse_url(url, &parsed) || depth > 3) {
        return 0;
    }

    uint32_t server_ip;
    if (!net_parse_ipv4(parsed.host, &server_ip) &&
        !net_dns_resolve(parsed.host, &server_ip)) {
        print("flint: could not resolve host", RED, 0, 42);
        return 0;
    }

    static http_connection_t connection;
    memset(&connection, 0, sizeof(connection));
    connection.secure = parsed.secure;
    if (!tcp_connect(&connection.socket, server_ip, parsed.port)) {
        print("flint: connection failed", RED, 0, 42);
        return 0;
    }
    if (connection.secure && !http_tls_setup(&connection, parsed.host)) {
        tcp_close(&connection.socket);
        print("flint: secure handshake failed", RED, 0, 42);
        return 0;
    }

    char request[HTTP_HOST_MAX + HTTP_PATH_MAX + 128];
    uint32_t request_length = 0;
    http_append(request, sizeof(request), &request_length, "GET ");
    http_append(request, sizeof(request), &request_length, parsed.path);
    http_append(request, sizeof(request), &request_length, " HTTP/1.0\r\nHost: ");
    http_append(request, sizeof(request), &request_length, parsed.host);
    http_append(request, sizeof(request), &request_length,
                "\r\nConnection: close\r\nUser-Agent: MyDOS-flint/1.0\r\n\r\n");
    request[request_length] = '\0';
    if (!http_connection_send(&connection, (const uint8_t *)request,
                              (uint32_t)strlen(request))) {
        http_connection_close(&connection);
        return 0;
    }

    static char header[HTTP_HEADER_MAX];
    static uint8_t buffer[HTTP_BUFFER_SIZE];
    uint32_t header_length = 0;
    uint32_t body_offset = 0;
    const char *body = 0;
    int closed = 0;

    while (header_length < HTTP_HEADER_MAX) {
        uint32_t received = 0;
        int result = http_connection_recv(&connection, buffer, sizeof(buffer),
                                          &received);
        if (result < 0) {
            http_connection_close(&connection);
            print("flint: request timed out", RED, 0, 42);
            return 0;
        }
        if (result == 0) {
            closed = 1;
            break;
        }
        if (header_length + received > HTTP_HEADER_MAX) {
            received = HTTP_HEADER_MAX - header_length;
        }
        memcpy(header + header_length, buffer, received);
        header_length += received;
        header[header_length] = '\0';
        body = http_header_end(header);
        if (body != 0) {
            body_offset = (uint32_t)(body - header);
            break;
        }
    }

    if (body == 0) {
        http_connection_close(&connection);
        return 0;
    }

    int status = http_status_code(header);
    if (status != 200) {
        char location[HTTP_PATH_MAX + HTTP_HOST_MAX + 32];
        char redirect[HTTP_PATH_MAX + HTTP_HOST_MAX + 32];
        if ((status == 301 || status == 302 || status == 303 || status == 307) &&
            http_header_value(header, "Location:", location, sizeof(location)) &&
            http_redirect_url(&parsed, location, redirect, sizeof(redirect))) {
            http_connection_close(&connection);
            return http_get_file_internal(redirect, path, depth + 1);
        }
        http_connection_close(&connection);
        print("flint: download failed", RED, 0, 42);
        return 0;
    }

    char transfer[32];
    if (http_header_value(header, "Transfer-Encoding:", transfer, sizeof(transfer)) &&
        http_prefix(transfer, "chunked")) {
        http_connection_close(&connection);
        print("flint: chunked responses are not supported", RED, 0, 42);
        return 0;
    }

    uint32_t content_length = 0;
    char length_value[32];
    int has_length = http_header_value(header, "Content-Length:", length_value,
                                       sizeof(length_value)) &&
                     http_parse_uint(length_value, &content_length);

    FIL file;
    if (f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
        http_connection_close(&connection);
        print("flint: could not create file", RED, 0, 42);
        return 0;
    }

    uint32_t written_total = 0;
    uint32_t initial = header_length - body_offset;
    if (initial != 0 && !http_write(&file, (const uint8_t *)body, initial)) {
        f_close(&file);
        http_connection_close(&connection);
        return 0;
    }
    written_total += initial;

    while (!closed && (!has_length || written_total < content_length)) {
        uint32_t received = 0;
        int result = http_connection_recv(&connection, buffer, sizeof(buffer),
                                          &received);
        if (result < 0) {
            break;
        }
        if (result == 0) {
            break;
        }
        uint32_t remaining = received;
        if (has_length && written_total + remaining > content_length) {
            remaining = content_length - written_total;
        }
        if (remaining != 0 && !http_write(&file, buffer, remaining)) {
            f_close(&file);
            http_connection_close(&connection);
            return 0;
        }
        written_total += remaining;
    }

    f_close(&file);
    http_connection_close(&connection);
    if (has_length && written_total < content_length) {
        return 0;
    }
    return 1;
}

int net_http_get_file(const char *url, const char *path) {
    return http_get_file_internal(url, path, 0);
}

int net_https_get_file(const char *url, const char *path) {
    return http_get_file_internal(url, path, 0);
}
