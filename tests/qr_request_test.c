#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <appsys.h>
#include <httprequest.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#ifndef HAVE_SNI
#define HAVE_SNI
#endif
#include <wolfssl/ssl.h>
#include <wolfssl/wolfcrypt/error-crypt.h>

#include "../include/qr_request.h"
#include "../include/https_client.h"

static char last_log[512];

int logger_level_printf(int level, int flags, const char *module,
    const char *file, int line, const char *format, ...)
{
    (void)level; (void)flags; (void)module; (void)file;
    va_list arguments;
    (void)line;
    va_start(arguments, format);
    vsnprintf(last_log, sizeof(last_log), format, arguments);
    va_end(arguments);
    return 0;
}

#define MOCK_SOCKET_FD 7
#define MOCK_RESPONSE_CAPACITY 4096U

static const char *mock_body;
static int mock_http_status;
static char mock_response[MOCK_RESPONSE_CAPACITY];
static size_t mock_response_length;
static size_t mock_response_offset;
static char mock_request[2048];
static size_t mock_request_length;
static uint32_t mock_tick_count;
static int mock_verify_mode = -1;
static int mock_sni_seen;
static int mock_power_save_mode;
static int mock_ca_result = WOLFSSL_SUCCESS;
static int mock_domain_result = WOLFSSL_SUCCESS;
static int mock_connect_result = WOLFSSL_SUCCESS;
static int mock_ssl_error;
static int mock_write_failed;
static int mock_ca_seen, mock_domain_seen, mock_init_count, mock_cleanup_count;

struct hostent *gethostbyname_safe(const char *name, uint8_t *host_buffer,
    size_t buffer_length)
{
    static struct hostent entry;
    static char *addresses[2];
    static uint8_t address[4] = {127, 0, 0, 1};
    (void)host_buffer;
    (void)buffer_length;
    assert(strcmp(name, "soundbox-notify-stg.netzme.com") == 0);
    entry.h_addrtype = AF_INET;
    entry.h_length = sizeof(address);
    entry.h_addr_list = addresses;
    addresses[0] = (char *)address;
    addresses[1] = NULL;
    return &entry;
}

int socket(int domain, int type, int protocol)
{
    assert(domain == AF_INET);
    assert(type == SOCK_STREAM);
    assert(protocol == 0);
    return MOCK_SOCKET_FD;
}

int connect(int socket_fd, const struct sockaddr *address, socklen_t address_length)
{
    const struct sockaddr_in *ipv4 = (const struct sockaddr_in *)address;
    assert(socket_fd == MOCK_SOCKET_FD);
    assert(address_length == sizeof(*ipv4));
    assert(ntohs(ipv4->sin_port) == 443);
    return 0;
}

int close(int socket_fd)
{
    assert(socket_fd == MOCK_SOCKET_FD);
    return 0;
}

uint32_t syscall_get_tick_count(void)
{
    return mock_tick_count;
}

int syscall_msleep(int keepms)
{
    mock_tick_count += (uint32_t)keepms;
    return 0;
}

appbool app_set_auto_powersave_mode(appbool enabled)
{
    appbool previous = (appbool)mock_power_save_mode;
    mock_power_save_mode = enabled;
    return previous;
}

int wolfSSL_Init(void)
{
    ++mock_init_count;
    return WOLFSSL_SUCCESS;
}

int wolfSSL_Cleanup(void)
{
    ++mock_cleanup_count;
    return WOLFSSL_SUCCESS;
}

WOLFSSL_METHOD *wolfTLSv1_2_client_method(void)
{
    return (WOLFSSL_METHOD *)&mock_response;
}

WOLFSSL_CTX *wolfSSL_CTX_new(WOLFSSL_METHOD *method)
{
    assert(method != NULL);
    return (WOLFSSL_CTX *)&mock_request;
}

void wolfSSL_CTX_set_verify(WOLFSSL_CTX *context, int mode,
    VerifyCallback callback)
{
    assert(context != NULL);
    assert(callback == NULL);
    mock_verify_mode = mode;
}

void wolfSSL_CTX_free(WOLFSSL_CTX *context)
{
    assert(context != NULL);
}

int wolfSSL_CTX_load_verify_locations(WOLFSSL_CTX *context, const char *path,
    const char *directory)
{
    assert(context && path && !directory);
    assert(!strcmp(path, "test-ca.pem") || !strcmp(path, SOUNDBOX_BACKEND_CA));
    mock_ca_seen = 1;
    return mock_ca_result;
}

int wolfSSL_check_domain_name(WOLFSSL *session, const char *name)
{
    assert(session && name);
    assert(!strcmp(name, "soundbox-notify-stg.netzme.com"));
    mock_domain_seen = 1;
    return mock_domain_result;
}

WOLFSSL *wolfSSL_new(WOLFSSL_CTX *context)
{
    assert(context != NULL);
    return (WOLFSSL *)&mock_response;
}

void wolfSSL_free(WOLFSSL *session)
{
    assert(session != NULL);
}

int wolfSSL_UseSNI(WOLFSSL *session, unsigned char type,
    const void *value, unsigned short length)
{
    assert(session != NULL);
    assert(type == WOLFSSL_SNI_HOST_NAME);
    assert(length == strlen("soundbox-notify-stg.netzme.com"));
    assert(memcmp(value, "soundbox-notify-stg.netzme.com", length) == 0);
    mock_sni_seen = 1;
    return WOLFSSL_SUCCESS;
}

int wolfSSL_set_fd(WOLFSSL *session, int socket_fd)
{
    assert(session != NULL);
    assert(socket_fd == MOCK_SOCKET_FD);
    return WOLFSSL_SUCCESS;
}

int wolfSSL_connect(WOLFSSL *session)
{
    assert(session != NULL);
    return mock_connect_result;
}

int wolfSSL_write(WOLFSSL *session, const void *buffer, int length)
{
    assert(session != NULL);
    assert(length > 0);
    if (mock_write_failed)
        return -1;
    assert((size_t)length <= sizeof(mock_request) - mock_request_length - 1U);
    memcpy(mock_request + mock_request_length, buffer, (size_t)length);
    mock_request_length += (size_t)length;
    mock_request[mock_request_length] = '\0';
    return length;
}

int wolfSSL_read(WOLFSSL *session, void *buffer, int length)
{
    size_t remaining;
    size_t count;
    assert(session != NULL);
    remaining = mock_response_length - mock_response_offset;
    if (!remaining)
        return 0;
    count = remaining < (size_t)length ? remaining : (size_t)length;
    memcpy(buffer, mock_response + mock_response_offset, count);
    mock_response_offset += count;
    return (int)count;
}

int wolfSSL_get_error(WOLFSSL *session, int result)
{
    (void)session;
    (void)result;
    return mock_ssl_error;
}

int wolfSSL_shutdown(WOLFSSL *session)
{
    assert(session != NULL);
    return WOLFSSL_SUCCESS;
}

static void set_response(int http_status, const char *body)
{
    int written;
    mock_http_status = http_status;
    mock_body = body;
    written = snprintf(mock_response, sizeof(mock_response),
        "HTTP/1.1 %d Test\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n\r\n"
        "%s",
        mock_http_status, (unsigned int)strlen(mock_body), mock_body);
    assert(written > 0 && (size_t)written < sizeof(mock_response));
    mock_response_length = (size_t)written;
    mock_response_offset = 0;
    mock_request[0] = '\0';
    mock_request_length = 0;
    mock_tick_count = 0;
    mock_verify_mode = -1;
    mock_sni_seen = 0;
}

static void set_chunked_response(int http_status, const char *body)
{
    int written;
    mock_http_status = http_status;
    mock_body = body;
    written = snprintf(mock_response, sizeof(mock_response),
        "HTTP/1.1 %d Test\r\n"
        "Content-Type: application/json\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n\r\n"
        "%X\r\n%s\r\n0\r\n\r\n",
        mock_http_status, (unsigned int)strlen(mock_body), mock_body);
    assert(written > 0 && (size_t)written < sizeof(mock_response));
    mock_response_length = (size_t)written;
    mock_response_offset = 0;
    mock_request[0] = '\0';
    mock_request_length = 0;
    mock_tick_count = 0;
    mock_verify_mode = -1;
    mock_sni_seen = 0;
}

int main(void)
{
    static const char success_body[] =
        "{\"status\":200,\"message\":\"Success !\",\"data\":{\"qr_content\":"
        "\"00020101021226650013ID.NETZME.WWW\"},\"request_id\":\"test\"}";
    static const char api_error_body[] =
        "{\"status\":500,\"message\":\"Failure\",\"data\":{}}";
    char qr_content[QR_REQUEST_CONTENT_MAX + 1U];
    int http_status = 0;
    int submitted;
    uint8_t response_body[2048];
    size_t response_length;

    mock_power_save_mode = 1;
    set_response(200, success_body);
    assert(qr_request_generate(qr_content, sizeof(qr_content), &http_status) ==
        QR_REQUEST_OK);
    assert(http_status == 200);
    assert(strcmp(qr_content, "00020101021226650013ID.NETZME.WWW") == 0);
    assert(mock_verify_mode == SSL_VERIFY_NONE);
    assert(mock_sni_seen);
    assert(mock_power_save_mode == 1);
    assert(strstr(mock_request,
        "POST /api/v2/device/generate-qr HTTP/1.1\r\n") != NULL);
    assert(strstr(mock_request, "Content-Type: application/json\r\n") != NULL);
    assert(strstr(mock_request,
        "\"device_id\":\"3521519013\",\"request_data\":3666,\"type\":1") != NULL);
    assert(strstr(mock_request, "\"qr_content\":\"\",\"invoice_trx_id\":\"\","
        "\"qr_content_id\":\"\"}") != NULL);

    set_chunked_response(200, success_body);
    assert(qr_request_generate(qr_content, sizeof(qr_content), &http_status) ==
        QR_REQUEST_OK);
    assert(strcmp(qr_content, "00020101021226650013ID.NETZME.WWW") == 0);

    set_response(200, api_error_body);
    assert(qr_request_generate(qr_content, sizeof(qr_content), &http_status) ==
        QR_REQUEST_INVALID_RESPONSE);
    assert(qr_content[0] == '\0');

    set_response(503, success_body);
    assert(qr_request_generate(qr_content, sizeof(qr_content), &http_status) ==
        QR_REQUEST_SERVER_FAILED);
    assert(http_status == 503);

    set_response(200, success_body);
    assert(https_client_post_json_verified("soundbox-notify-stg.netzme.com",
        "/test", SOUNDBOX_BACKEND_CA, "{}", response_body, sizeof(response_body),
        &response_length, &http_status, &submitted) == 0);
    assert(mock_verify_mode == SSL_VERIFY_PEER && mock_sni_seen);
    assert(mock_ca_seen && mock_domain_seen && submitted);
    assert(response_length == strlen(success_body));
    assert(mock_init_count == 1 && mock_cleanup_count == 0);

    set_response(200, success_body);
    mock_ca_result = -1;
    assert(https_client_post_json_verified("soundbox-notify-stg.netzme.com",
        "/test", "test-ca.pem", "{}", response_body, sizeof(response_body),
        &response_length, &http_status, &submitted) != 0);
    assert(!submitted && !mock_request_length);
    mock_ca_result = WOLFSSL_SUCCESS;

    mock_domain_result = -1;
    assert(https_client_post_json_verified("soundbox-notify-stg.netzme.com",
        "/test", "test-ca.pem", "{}", response_body, sizeof(response_body),
        &response_length, &http_status, &submitted) != 0);
    assert(!submitted && !mock_request_length);
    mock_domain_result = WOLFSSL_SUCCESS;

    mock_connect_result = -1;
    mock_ssl_error = ASN_NO_SIGNER_E;
    assert(https_client_post_json_verified("soundbox-notify-stg.netzme.com",
        "/test", SOUNDBOX_BACKEND_CA, "{}", response_body, sizeof(response_body),
        &response_length, &http_status, &submitted) != 0);
    assert(!submitted && !mock_request_length);
    assert(mock_verify_mode == SSL_VERIFY_PEER);
    assert(strstr(last_log, "HTTPS certificate signer not trusted") != NULL);
    assert(strstr(last_log, SOUNDBOX_BACKEND_CA) != NULL);
    mock_connect_result = WOLFSSL_SUCCESS;
    mock_ssl_error = 0;

    mock_write_failed = 1;
    assert(https_client_post_json_verified("soundbox-notify-stg.netzme.com",
        "/test", "test-ca.pem", "{}", response_body, sizeof(response_body),
        &response_length, &http_status, &submitted) != 0);
    assert(submitted);
    mock_write_failed = 0;
    https_client_shutdown();
    assert(mock_cleanup_count == 1);
    https_client_shutdown();
    assert(mock_cleanup_count == 1);

    puts("PrinterDemo QR request tests passed");
    return 0;
}
