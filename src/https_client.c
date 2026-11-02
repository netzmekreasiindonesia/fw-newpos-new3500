#include "https_client.h"

#if defined(SIM_BUILD) && !defined(SIM_STANDALONE_TEST)

int https_client_post_json(const char *host, const char *path,
    const char *request_body, uint8_t *response_body, size_t response_capacity,
    size_t *response_length, int *http_status)
{
    (void)host;
    (void)path;
    (void)request_body;
    (void)response_body;
    (void)response_capacity;
    (void)response_length;
    if (http_status)
        *http_status = 0;
    return -1;
}

int https_client_probe(int *http_status)
{
    if (http_status)
        *http_status = 0;
    return -1;
}

int https_client_post_json_verified(const char *host, const char *path,
    const char *ca_file, const char *request_body, uint8_t *response_body,
    size_t response_capacity, size_t *response_length, int *http_status,
    int *submission_started)
{
    (void)ca_file;
    if (submission_started)
        *submission_started = 0;
    if (response_length)
        *response_length = 0;
    return https_client_post_json(host, path, request_body, response_body,
        response_capacity, response_length, http_status);
}

void https_client_shutdown(void) {}

#else

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include <appsys.h>
#include <httprequest.h>
#ifndef SIM_BUILD
#include <network.h>
#include <syscall.h>
#endif
#ifndef HAVE_SNI
#define HAVE_SNI
#endif
#include <wolfssl/ssl.h>
#include <wolfssl/wolfcrypt/error-crypt.h>

#ifdef SIM_STANDALONE_TEST
extern struct hostent *gethostbyname_safe(const char *name, uint8_t *host_buffer,
    size_t buffer_length);
#endif

#ifdef CFG_LOGGER_ON
#define LOGGER_CFG          (LOGGER_LEVEL_DEBUG | LOGGER_TIMESTAMP | LOGGER_LEVEL | LOGGER_MODULE | LOGGER_FILE | LOGGER_LINE)
#define LOGGER_MODULE_NAME  "print-demo"
#include "libs/logger/logger.h"
#else
#define logger_debug(...) ((void)0)
#define logger_error(...) ((void)0)
#define logger_warn(...) ((void)0)
#endif

#define HTTPS_REQUEST_MAX 4096U
#define HTTPS_RESPONSE_MAX 16384U
#define HTTPS_HEADER_MAX 2048U
#define HTTPS_TIMEOUT_MS 10000U

typedef struct {
    int socket_fd;
    WOLFSSL_CTX *context;
    WOLFSSL *session;
} https_connection_t;

/* The serialized worker owns HTTPS; release globals only after both TLS owners stop. */
static int https_initialized;

void https_client_shutdown(void)
{
    if (https_initialized) {
        wolfSSL_Cleanup();
        https_initialized = 0;
    }
}

static void https_close(https_connection_t *connection)
{
    if (connection->session) {
        wolfSSL_shutdown(connection->session);
        wolfSSL_free(connection->session);
    }
    if (connection->context)
        wolfSSL_CTX_free(connection->context);
    if (connection->socket_fd >= 0)
        close(connection->socket_fd);
}

static const char *find_bytes(const char *data, size_t data_length,
    const char *needle, size_t needle_length)
{
    size_t index;

    if (needle_length > data_length)
        return NULL;
    for (index = 0; index <= data_length - needle_length; ++index) {
        if (memcmp(data + index, needle, needle_length) == 0)
            return data + index;
    }
    return NULL;
}

static int parse_headers(char *response, size_t response_length,
    size_t *header_length, int *status, size_t *content_length,
    int *has_content_length, int *chunked)
{
    char *header_end = strstr(response, "\r\n\r\n");
    char *line;
    char *status_end;

    if (!header_end)
        return response_length > HTTPS_HEADER_MAX ? -1 : 0;
    *header_length = (size_t)(header_end - response) + 4U;
    if (*header_length > HTTPS_HEADER_MAX)
        return -1;
    status_end = strstr(response, "\r\n");
    if (!status_end || sscanf(response, "HTTP/%*u.%*u %d", status) != 1 ||
        *status < 100 || *status > 599)
        return -1;

    line = status_end + 2;
    while (line < header_end) {
        char *line_end = strstr(line, "\r\n");
        char *colon;
        char *value;

        if (!line_end || line_end > header_end)
            line_end = header_end;
        *line_end = '\0';
        colon = strchr(line, ':');
        if (colon) {
            size_t name_length = (size_t)(colon - line);
            *colon = '\0';
            value = colon + 1;
            while (*value == ' ' || *value == '\t')
                ++value;

            if (name_length == 14U && strcasecmp(line, "Content-Length") == 0) {
                char *value_end;
                unsigned long parsed_length;

                errno = 0;
                parsed_length = strtoul(value, &value_end, 10);
                while (*value_end == ' ' || *value_end == '\t')
                    ++value_end;
                if (errno == ERANGE || value_end == value || *value_end ||
                    parsed_length > HTTPS_RESPONSE_MAX)
                    return -1;
                if (*has_content_length &&
                    *content_length != (size_t)parsed_length)
                    return -1;
                *content_length = (size_t)parsed_length;
                *has_content_length = 1;
            } else if (name_length == 17U &&
                strcasecmp(line, "Transfer-Encoding") == 0) {
                *chunked = strcasecmp(value, "chunked") == 0;
            }
        }
        if (line_end == header_end)
            break;
        line = line_end + 2;
    }
    return 1;
}

static int decode_chunked(const char *encoded, size_t encoded_length,
    uint8_t *body, size_t body_capacity, size_t *body_length)
{
    size_t offset = 0;
    size_t decoded_length = 0;

    *body_length = 0;
    while (offset < encoded_length) {
        char size_line[32];
        const char *line_end = find_bytes(encoded + offset,
            encoded_length - offset, "\r\n", 2);
        char *size_end;
        char *extension;
        unsigned long chunk_size;
        size_t line_length;

        if (!line_end && encoded_length - offset > sizeof(size_line) - 1U)
            return -1;
        if (!line_end)
            return 0;
        line_length = (size_t)(line_end - (encoded + offset));
        if (!line_length || line_length >= sizeof(size_line))
            return -1;
        memcpy(size_line, encoded + offset, line_length);
        size_line[line_length] = '\0';
        extension = strchr(size_line, ';');
        if (extension)
            *extension = '\0';
        errno = 0;
        chunk_size = strtoul(size_line, &size_end, 16);
        if (errno == ERANGE || size_end == size_line || *size_end)
            return -1;
        offset += line_length + 2U;

        if (!chunk_size) {
            if (encoded_length - offset >= 2U &&
                encoded[offset] == '\r' && encoded[offset + 1U] == '\n') {
                *body_length = decoded_length;
                return 1;
            }
            if (find_bytes(encoded + offset, encoded_length - offset,
                    "\r\n\r\n", 4)) {
                *body_length = decoded_length;
                return 1;
            }
            return 0;
        }
        if (chunk_size > body_capacity - decoded_length)
            return -2;
        if ((size_t)chunk_size > encoded_length - offset ||
            encoded_length - offset - (size_t)chunk_size < 2U)
            return 0;
        memcpy(body + decoded_length, encoded + offset, (size_t)chunk_size);
        decoded_length += (size_t)chunk_size;
        offset += (size_t)chunk_size;
        if (encoded[offset] != '\r' || encoded[offset + 1U] != '\n')
            return -1;
        offset += 2U;
    }
    return 0;
}

static int https_request(const char *method, const char *host, const char *path,
    const char *request_body, uint8_t *response_body, size_t response_capacity,
    size_t *response_length, int *http_status, const char *ca_file,
    int *submission_started)
{
    char host_buffer[512];
    char request[HTTPS_REQUEST_MAX];
    char *response = NULL;
    struct hostent *host_entry;
    struct sockaddr_in address;
    https_connection_t connection = {-1, NULL, NULL};
    appbool previous_power_save_mode;
    size_t request_length;
    size_t sent = 0;
    size_t received = 0;
    size_t header_length = 0;
    size_t content_length = 0;
    size_t parsed_body_length = 0;
    int has_content_length = 0;
    int chunked = 0;
    int headers_parsed = 0;
    int result = HTTP_RET_EUNKOWN;
    uint32_t start_tick;

    if (submission_started)
        *submission_started = 0;
    if (!method || !host || !path || !http_status ||
        (response_body && (!response_length || !response_capacity)) ||
        (request_body && strcmp(method, "POST") != 0))
        return HTTP_RET_EARG;
    *http_status = 0;
    if (response_length)
        *response_length = 0;
    if (request_body && strlen(request_body) > HTTPS_REQUEST_MAX / 2U)
        return HTTP_RET_EARG;

    previous_power_save_mode = app_set_auto_powersave_mode(app_off);
    host_entry = gethostbyname_safe(host, (uint8_t *)host_buffer,
        sizeof(host_buffer));
    if (!host_entry || !host_entry->h_addr_list || !host_entry->h_addr_list[0]) {
        logger_error("HTTPS DNS resolution failed for %s.\n", host);
        result = HTTP_RET_EDNS;
        goto cleanup;
    }
    response = malloc(HTTPS_RESPONSE_MAX + 1U);
    if (!response) {
        result = HTTP_RET_ENOBUFS;
        goto cleanup;
    }
    response[0] = '\0';

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(443);
    memcpy(&address.sin_addr.s_addr, host_entry->h_addr_list[0],
        sizeof(address.sin_addr.s_addr));
    connection.socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (connection.socket_fd < 0) {
        logger_error("HTTPS socket creation failed: errno=%d.\n", errno);
        result = HTTP_RET_ECONN;
        goto cleanup;
    }
    if (connect(connection.socket_fd, (struct sockaddr *)&address,
            sizeof(address)) != 0) {
        logger_error("HTTPS TCP connect failed: errno=%d.\n", errno);
        result = HTTP_RET_ECONN;
        goto cleanup;
    }
    if (!https_initialized) {
        if (wolfSSL_Init() != WOLFSSL_SUCCESS) {
            logger_error("wolfSSL_Init failed.\n");
            result = HTTP_RET_EUNKOWN;
            goto cleanup;
        }
        https_initialized = 1;
    }
    connection.context = wolfSSL_CTX_new(wolfTLSv1_2_client_method());
    if (!connection.context) {
        logger_error("wolfSSL context creation failed.\n");
        result = HTTP_RET_EUNKOWN;
        goto cleanup;
    }
    wolfSSL_CTX_set_verify(connection.context,
        ca_file ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, NULL);
    if (ca_file) {
        if (wolfSSL_CTX_load_verify_locations(connection.context, ca_file, NULL) != WOLFSSL_SUCCESS) {
            logger_error("HTTPS trust store unavailable: %s.\n", ca_file);
            result = HTTP_RET_ECONN;
            goto cleanup;
        }
        logger_debug("HTTPS peer verification enabled; loaded CA bundle: %s\n", ca_file);
    } else {
        logger_warn("HTTPS server certificate verification is disabled.\n");
    }
    connection.session = wolfSSL_new(connection.context);
    if (!connection.session) {
        logger_error("wolfSSL session creation failed.\n");
        result = HTTP_RET_EUNKOWN;
        goto cleanup;
    }
    if ((ca_file && wolfSSL_check_domain_name(connection.session, host) != WOLFSSL_SUCCESS) ||
        wolfSSL_UseSNI(connection.session, WOLFSSL_SNI_HOST_NAME,
            host, (unsigned short)strlen(host)) != WOLFSSL_SUCCESS ||
        wolfSSL_set_fd(connection.session, connection.socket_fd) != WOLFSSL_SUCCESS) {
        logger_error("wolfSSL session configuration failed for %s.\n", host);
        result = HTTP_RET_ECONN;
        goto cleanup;
    }

    {
        int request_result;
        if (request_body) {
            request_result = snprintf(request, sizeof(request),
                "%s %s HTTP/1.1\r\n"
                "Host: %s\r\n"
                "Accept: application/json\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: %u\r\n"
                "User-Agent: PrinterDemo/1.0\r\n"
                "Connection: close\r\n\r\n"
                "%s",
                method, path, host, (unsigned int)strlen(request_body),
                request_body);
        } else {
            request_result = snprintf(request, sizeof(request),
                "%s %s HTTP/1.1\r\n"
                "Host: %s\r\n"
                "Accept: */*\r\n"
                "User-Agent: PrinterDemo/1.0\r\n"
                "Connection: close\r\n\r\n",
                method, path, host);
        }
        if (request_result < 0 || (size_t)request_result >= sizeof(request)) {
            result = HTTP_RET_EARG;
            goto cleanup;
        }
        request_length = (size_t)request_result;
    }

    start_tick = syscall_get_tick_count();
    while (1) {
        int ssl_result = wolfSSL_connect(connection.session);
        int ssl_error;
        if (ssl_result == WOLFSSL_SUCCESS)
            break;
        ssl_error = wolfSSL_get_error(connection.session, ssl_result);
        if (ssl_error != WOLFSSL_ERROR_WANT_READ &&
            ssl_error != WOLFSSL_ERROR_WANT_WRITE) {
            logger_error("TLS handshake failed for %s: error=%d.\n", host,
                ssl_error);
            if (ssl_error == ASN_NO_SIGNER_E)
                logger_error("HTTPS certificate signer not trusted; CA bundle: %s\n",
                    ca_file ? ca_file : "(none)");
            result = HTTP_RET_ECONN;
            goto cleanup;
        }
        if ((uint32_t)(syscall_get_tick_count() - start_tick) >= HTTPS_TIMEOUT_MS) {
            result = HTTP_RET_ETIMEOUT;
            goto cleanup;
        }
        syscall_msleep(20);
    }

    start_tick = syscall_get_tick_count();
    while (sent < request_length) {
        int ssl_result;
        int ssl_error;
        if (submission_started)
            *submission_started = 1;
        ssl_result = wolfSSL_write(connection.session, request + sent,
            (int)(request_length - sent));
        if (ssl_result > 0) {
            sent += (size_t)ssl_result;
            continue;
        }
        ssl_error = wolfSSL_get_error(connection.session, ssl_result);
        if (ssl_error != WOLFSSL_ERROR_WANT_READ &&
            ssl_error != WOLFSSL_ERROR_WANT_WRITE) {
            logger_error("HTTPS request send failed: error=%d.\n", ssl_error);
            result = HTTP_RET_ESEND;
            goto cleanup;
        }
        if ((uint32_t)(syscall_get_tick_count() - start_tick) >= HTTPS_TIMEOUT_MS) {
            result = HTTP_RET_ETIMEOUT;
            goto cleanup;
        }
        syscall_msleep(20);
    }

    start_tick = syscall_get_tick_count();
    while (received < HTTPS_RESPONSE_MAX) {
        int ssl_result = wolfSSL_read(connection.session, response + received,
            (int)(HTTPS_RESPONSE_MAX - received));
        int ssl_error = 0;
        int connection_closed = ssl_result == 0;

        if (ssl_result > 0) {
            received += (size_t)ssl_result;
            response[received] = '\0';
        } else if (ssl_result < 0) {
            ssl_error = wolfSSL_get_error(connection.session, ssl_result);
            connection_closed = ssl_error == WOLFSSL_ERROR_ZERO_RETURN;
            if (!connection_closed &&
                ssl_error != WOLFSSL_ERROR_WANT_READ &&
                ssl_error != WOLFSSL_ERROR_WANT_WRITE) {
                logger_error("HTTPS response read failed: error=%d.\n", ssl_error);
                result = HTTP_RET_ERECV;
                goto cleanup;
            }
        }

        if (!headers_parsed) {
            int header_result = parse_headers(response, received,
                &header_length, http_status, &content_length,
                &has_content_length, &chunked);
            if (header_result < 0) {
                logger_error("Malformed or oversized HTTPS response headers "
                    "(received=%u, limit=%u).\n", (unsigned int)received,
                    HTTPS_HEADER_MAX);
                result = HTTP_RET_EPARSE;
                goto cleanup;
            }
            headers_parsed = header_result > 0;
            if (headers_parsed && !response_body) {
                result = 0;
                goto cleanup;
            }
        }

        if (headers_parsed) {
            size_t available_body = received - header_length;
            if (chunked) {
                int decode_result = decode_chunked(response + header_length,
                    available_body, response_body, response_capacity,
                    &parsed_body_length);
                if (decode_result == 1) {
                    *response_length = parsed_body_length;
                    result = 0;
                    goto cleanup;
                }
                if (decode_result == -2) {
                    result = HTTP_RET_ENOBUFS;
                    goto cleanup;
                }
                if (decode_result < 0) {
                    result = HTTP_RET_EPROTO;
                    goto cleanup;
                }
            } else if (has_content_length) {
                if (content_length > response_capacity) {
                    result = HTTP_RET_ENOBUFS;
                    goto cleanup;
                }
                if (available_body >= content_length) {
                    memcpy(response_body, response + header_length,
                        content_length);
                    *response_length = content_length;
                    result = 0;
                    goto cleanup;
                }
            } else if (connection_closed) {
                if (available_body > response_capacity) {
                    result = HTTP_RET_ENOBUFS;
                    goto cleanup;
                }
                memcpy(response_body, response + header_length, available_body);
                *response_length = available_body;
                result = 0;
                goto cleanup;
            }
        }

        if (connection_closed) {
            result = headers_parsed ? HTTP_RET_EPROTO : HTTP_RET_EPARSE;
            goto cleanup;
        }
        if ((uint32_t)(syscall_get_tick_count() - start_tick) >= HTTPS_TIMEOUT_MS) {
            result = HTTP_RET_ETIMEOUT;
            goto cleanup;
        }
        if (ssl_result < 0)
            syscall_msleep(20);
    }
    result = HTTP_RET_ENOBUFS;

cleanup:
    https_close(&connection);
    free(response);
    app_set_auto_powersave_mode(previous_power_save_mode);
    return result;
}

int https_client_post_json(const char *host, const char *path,
    const char *request_body, uint8_t *response_body, size_t response_capacity,
    size_t *response_length, int *http_status)
{
    if (!request_body || !response_body || !response_length ||
        !response_capacity)
        return HTTP_RET_EARG;
    return https_request("POST", host, path, request_body, response_body,
        response_capacity, response_length, http_status, NULL, NULL);
}

int https_client_post_json_verified(const char *host, const char *path,
    const char *ca_file, const char *request_body, uint8_t *response_body,
    size_t response_capacity, size_t *response_length, int *http_status,
    int *submission_started)
{
    if (submission_started)
        *submission_started = 0;
    if (!ca_file || !ca_file[0] || !request_body || !response_body ||
        !response_length || !response_capacity || !submission_started)
        return HTTP_RET_EARG;
    return https_request("POST", host, path, request_body, response_body,
        response_capacity, response_length, http_status, ca_file, submission_started);
}

int https_client_probe(int *http_status)
{
    if (!http_status)
        return HTTP_RET_EARG;
    logger_debug("Starting unverified HTTPS connectivity probe.\n");
    return https_request("GET", "valid-isrgrootx1.letsencrypt.org", "/",
        NULL, NULL, 0, NULL, http_status, NULL, NULL);
}

#endif
