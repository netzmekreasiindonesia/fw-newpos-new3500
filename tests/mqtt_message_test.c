#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MQTT_PING_INTERVAL_MS 50U
#undef SOUNDBOX_TELEMETRY_VERIFIED
#define SOUNDBOX_TELEMETRY_VERIFIED 1
#include "../src/mqtt_listener.c"

static int missing_file;
static int verify_mode;
static int verify_domain;
static int sni_set;
static int fail_handshake;
static int context_marker;
static int session_marker;
static int method_marker;
static int ssl_fd;
static int fail_dns;
static int fail_workdir;
static int ssl_references;
static char last_log[512];
static char payload_log[4096];
static int capture_payload;
static int cert_marker;
static int name_marker;
static int store_marker;
static int loaded_ca_count;
static int fail_boot_write;

struct hostent *gethostbyname_safe(const char *name, uint8_t *buffer, size_t length)
{
    static char address[] = {127, 0, 0, 1};
    static char *addresses[] = {address, NULL};
    static struct hostent host;
    (void)buffer; (void)length;
    assert(strcmp(name, MQTT_HOST) == 0);
    if (fail_dns)
        return NULL;
    host.h_addrtype = AF_INET;
    host.h_length = 4;
    host.h_addr_list = addresses;
    return &host;
}

int __wrap_connect(int fd, const struct sockaddr *address, socklen_t length)
{
    const struct sockaddr_in *ipv4 = (const struct sockaddr_in *)address;
    (void)fd; (void)length;
    assert(ipv4->sin_family == AF_INET && ntohs(ipv4->sin_port) == 8883);
    return 0;
}

uint32_t syscall_get_tick_count(void)
{
    struct timespec time;
    assert(clock_gettime(CLOCK_MONOTONIC, &time) == 0);
    return (uint32_t)(time.tv_sec * 1000U + time.tv_nsec / 1000000U);
}
int syscall_msleep(int ms) { return usleep((useconds_t)ms * 1000U); }

int app_file_open(const char *path, int flags)
{
    (void)flags;
    assert(strncmp(path, MQTT_CERT_DIR, strlen(MQTT_CERT_DIR)) == 0);
    if (missing_file) {
        errno = EACCES;
        return -1;
    }
    return 1;
}
int app_file_set_workdir(const char *path)
{
    assert(strcmp(path, CFG_APP_PATH) == 0);
    return fail_workdir ? -1 : 0;
}
int app_file_read(int fd, void *buffer, int length)
{
    (void)fd; (void)buffer; (void)length;
    return 1;
}
int app_file_close(int fd) { (void)fd; return 0; }

int wolfSSL_Init(void) { ++ssl_references; return WOLFSSL_SUCCESS; }
int wolfSSL_Cleanup(void) { --ssl_references; return WOLFSSL_SUCCESS; }
WOLFSSL_METHOD *wolfTLSv1_2_client_method(void)
{
    return (WOLFSSL_METHOD *)&method_marker;
}
WOLFSSL_CTX *wolfSSL_CTX_new(WOLFSSL_METHOD *method)
{
    assert(method == (WOLFSSL_METHOD *)&method_marker);
    return (WOLFSSL_CTX *)&context_marker;
}
void wolfSSL_CTX_free(WOLFSSL_CTX *ctx) { assert(ctx == (WOLFSSL_CTX *)&context_marker); }
void wolfSSL_free(WOLFSSL *session) { assert(session == (WOLFSSL *)&session_marker); }
void wolfSSL_CTX_set_verify(WOLFSSL_CTX *ctx, int mode, VerifyCallback callback)
{
    assert(ctx == (WOLFSSL_CTX *)&context_marker && callback == verify_certificate);
    verify_mode = mode;
}
int wolfSSL_X509_STORE_CTX_get_error(WOLFSSL_X509_STORE_CTX *store)
{
    assert(store == (WOLFSSL_X509_STORE_CTX *)&store_marker);
    return ASN_NO_SIGNER_E;
}
int wolfSSL_X509_STORE_CTX_get_error_depth(WOLFSSL_X509_STORE_CTX *store)
{
    assert(store == (WOLFSSL_X509_STORE_CTX *)&store_marker);
    return 2;
}
WOLFSSL_X509 *wolfSSL_X509_STORE_CTX_get_current_cert(WOLFSSL_X509_STORE_CTX *store)
{
    assert(store == (WOLFSSL_X509_STORE_CTX *)&store_marker);
    return (WOLFSSL_X509 *)&cert_marker;
}
WOLFSSL_X509_NAME *wolfSSL_X509_get_subject_name(WOLFSSL_X509 *certificate)
{
    assert(certificate == (WOLFSSL_X509 *)&cert_marker);
    return (WOLFSSL_X509_NAME *)&name_marker;
}
WOLFSSL_X509_NAME *wolfSSL_X509_get_issuer_name(WOLFSSL_X509 *certificate)
{
    return wolfSSL_X509_get_subject_name(certificate);
}
char *wolfSSL_X509_NAME_oneline(WOLFSSL_X509_NAME *name, char *buffer, int length)
{
    assert(name == (WOLFSSL_X509_NAME *)&name_marker);
    snprintf(buffer, (size_t)length, "test CA");
    return buffer;
}
int wolfSSL_CTX_load_verify_locations(WOLFSSL_CTX *ctx, const char *path, const char *dir)
{
    static const char *paths[] = {
        CFG_APP_PATH MQTT_CERT_DIR "ca.pem",
        CFG_APP_PATH MQTT_CERT_DIR "starfield-g2.pem",
        CFG_APP_PATH MQTT_CERT_DIR "starfield-class2.pem",
        CFG_APP_PATH MQTT_CERT_DIR "amazon-cross.pem"
    };
    assert(ctx == (WOLFSSL_CTX *)&context_marker && !dir);
    assert(strcmp(path, paths[loaded_ca_count % 4]) == 0);
    ++loaded_ca_count;
    return WOLFSSL_SUCCESS;
}
int wolfSSL_CTX_use_certificate_file(WOLFSSL_CTX *ctx, const char *path, int type)
{
    assert(ctx == (WOLFSSL_CTX *)&context_marker && type == WOLFSSL_FILETYPE_PEM &&
        strcmp(path, CFG_APP_PATH MQTT_CERT_DIR "cil.crt") == 0);
    return WOLFSSL_SUCCESS;
}
int wolfSSL_CTX_use_PrivateKey_file(WOLFSSL_CTX *ctx, const char *path, int type)
{
    assert(ctx == (WOLFSSL_CTX *)&context_marker && type == WOLFSSL_FILETYPE_PEM &&
        strcmp(path, CFG_APP_PATH MQTT_CERT_DIR "pri.key") == 0);
    return WOLFSSL_SUCCESS;
}
int wolfSSL_CTX_check_private_key(const WOLFSSL_CTX *ctx)
{
    assert(ctx == (WOLFSSL_CTX *)&context_marker);
    return WOLFSSL_SUCCESS;
}
WOLFSSL *wolfSSL_new(WOLFSSL_CTX *ctx)
{
    assert(ctx == (WOLFSSL_CTX *)&context_marker);
    return (WOLFSSL *)&session_marker;
}
int wolfSSL_UseSNI(WOLFSSL *session, unsigned char type, const void *name, unsigned short length)
{
    assert(session == (WOLFSSL *)&session_marker && type == WOLFSSL_SNI_HOST_NAME &&
        length == strlen(MQTT_HOST) && memcmp(name, MQTT_HOST, length) == 0);
    sni_set = 1;
    return WOLFSSL_SUCCESS;
}
int wolfSSL_check_domain_name(WOLFSSL *session, const char *name)
{
    assert(session == (WOLFSSL *)&session_marker && strcmp(name, MQTT_HOST) == 0);
    verify_domain = 1;
    return WOLFSSL_SUCCESS;
}
int wolfSSL_set_fd(WOLFSSL *session, int fd)
{
    assert(session == (WOLFSSL *)&session_marker && fd >= 0);
    ssl_fd = fd;
    return WOLFSSL_SUCCESS;
}
int wolfSSL_connect(WOLFSSL *session)
{
    assert(session == (WOLFSSL *)&session_marker);
    return fail_handshake ? -1 : WOLFSSL_SUCCESS;
}
int wolfSSL_get_error(WOLFSSL *session, int result)
{
    assert(session == (WOLFSSL *)&session_marker && result == -1);
    return ASN_NO_SIGNER_E;
}
int wolfSSL_pending(WOLFSSL *session)
{
    assert(session == (WOLFSSL *)&session_marker);
    return 0;
}
int wolfSSL_read(WOLFSSL *session, void *buffer, int length)
{
    int result;
    assert(session == (WOLFSSL *)&session_marker);
    result = (int)recv(ssl_fd, buffer, (size_t)length, 0);
    return result == 0 ? -1 : result;
}
int wolfSSL_write(WOLFSSL *session, const void *buffer, int length)
{
    assert(session == (WOLFSSL *)&session_marker);
    if (fail_boot_write && length > 2 && ((const uint8_t *)buffer)[0] == 0x30) {
        fail_boot_write = 0;
        return -1;
    }
    return (int)send(ssl_fd, buffer, (size_t)length, 0);
}

int logger_level_printf(int level, int flags, const char *module,
    const char *file, int line, const char *format, ...)
{
    va_list arguments;
    (void)level; (void)flags; (void)module; (void)file; (void)line;
    va_start(arguments, format);
    vsnprintf(last_log, sizeof(last_log), format, arguments);
    va_end(arguments);
    if (capture_payload && strncmp(last_log, "MQTT payload ", 13) == 0) {
        size_t used = strlen(payload_log);
        assert(used + strlen(last_log) < sizeof(payload_log));
        strcpy(payload_log + used, last_log);
    }
    return 0;
}

static void test_payload_logging(void)
{
    uint8_t message[637];
    capture_payload = 1;
    memset(message, 'A', sizeof(message));
    memcpy(message + 630, "THE_END", 7);
    payload_log[0] = '\0';
    log_payload(message, sizeof(message));
    assert(strstr(payload_log, "MQTT payload [0..96)/637: ") != NULL);
    assert(strstr(payload_log, "MQTT payload [576..637)/637: ") != NULL);
    assert(strstr(payload_log, "THE_END\n") != NULL);
    memset(message, 0, 96);
    message[1] = '\n';
    message[2] = 0xff;
    payload_log[0] = '\0';
    log_payload(message, 96);
    assert(strstr(payload_log, "\\x00\\x0a\\xff") != NULL);
    assert(strlen(last_log) < sizeof(last_log) - 1U);
    payload_log[0] = '\0';
    capture_payload = 0;
}

static void deliver(const char *message)
{
    on_message((const uint8_t *)message, strlen(message));
}

static int broker_read(void *context, void *buffer, size_t length)
{
    int fd = *(int *)context;
    size_t offset = 0;
    while (offset < length) {
        ssize_t count = recv(fd, (uint8_t *)buffer + offset, length - offset, 0);
        if (count <= 0)
            return -1;
        offset += (size_t)count;
    }
    return 0;
}

static void *run_test_connection(void *context)
{
    run_connection(context);
    return NULL;
}

static int broker_write(void *context, const void *buffer, size_t length)
{
    int fd = *(int *)context;
    size_t offset = 0;
    while (offset < length) {
        ssize_t count = send(fd, (const uint8_t *)buffer + offset, length - offset, 0);
        if (count <= 0)
            return -1;
        offset += (size_t)count;
    }
    return 0;
}

static void test_connection(int fail_state)
{
    static const uint8_t connack[] = {0x20, 2, 0, 0};
    static const uint8_t suback[] = {0x90, 3, 0, 1, 0};
    static const char message[] = "{\"broadcast_type\":1}";
    static const uint8_t pingresp[] = {0xd0, 0};
    int sockets[2];
    pthread_t thread;
    struct mqtt_connection connection;
    struct mqtt_wire_io broker;
    uint8_t body[256], header, packet[256];
    size_t length, topic_length = strlen(MQTT_TOPIC), index;
    int success = 0;
    int expect_state = !boot_state_published;
    unsigned int pause = mqtt_listener_pause();

    assert(pause);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    {
        struct timeval timeout = {2, 0};
        assert(setsockopt(sockets[1], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
    }
    memset(&connection, 0, sizeof(connection));
    connection.fd = ssl_fd = sockets[0];
    connection.session = (WOLFSSL *)&session_marker;
    broker.context = &sockets[1];
    broker.read = broker_read;
    broker.write = broker_write;
    mqtt_listener_watch(1);
    fail_boot_write = fail_state;
    assert(pthread_create(&thread, NULL, run_test_connection, &connection) == 0);
    assert(mqtt_wire_read(&broker, &header, body, sizeof(body), &length) == 0 && header == 0x10);
    assert(send(sockets[1], connack, sizeof(connack), 0) == sizeof(connack));
    assert(mqtt_wire_read(&broker, &header, body, sizeof(body), &length) == 0 && header == 0x82);
    assert(length == 5U + strlen(MQTT_TOPIC));
    assert(memcmp(body + 4, MQTT_TOPIC, strlen(MQTT_TOPIC)) == 0);
    packet[0] = 0x30;
    packet[1] = (uint8_t)(2U + topic_length + sizeof(message) - 1U);
    packet[2] = 0;
    packet[3] = (uint8_t)topic_length;
    memcpy(packet + 4, MQTT_TOPIC, topic_length);
    memcpy(packet + 4 + topic_length, message, sizeof(message) - 1U);
    length = 4U + topic_length + sizeof(message) - 1U;
    for (index = 0; index < length; ++index)
        assert(send(sockets[1], packet + index, 1, 0) == 1);
    assert(send(sockets[1], suback, sizeof(suback), 0) == sizeof(suback));
    if (fail_state) {
        assert(pthread_join(thread, NULL) == 0);
        assert(!boot_state_published && !mqtt_listener_connected());
        assert(strstr(last_log, "boot-state write failed"));
        assert(mqtt_listener_resume(pause) == 0);
        close(sockets[0]);
        close(sockets[1]);
        return;
    }
    if (expect_state) {
        const uint8_t *payload;
        size_t payload_length;
        assert(mqtt_wire_read(&broker, &header, body, sizeof(body), &length) == 0);
        assert(header == 0x30);
        assert(mqtt_wire_publish(header, body, length, MQTT_STATE_TOPIC,
            &payload, &payload_length) == 0);
        assert(payload_length == strlen("{\"state\":1}"));
        assert(memcmp(payload, "{\"state\":1}", payload_length) == 0);
    }
    assert(mqtt_wire_read(&broker, &header, body, sizeof(body), &length) == 0 && header == 0x30);
    {
        const uint8_t *payload;
        size_t payload_length;
        assert(mqtt_wire_publish(header, body, length, MQTT_TELEMETRY_TOPIC,
            &payload, &payload_length) == 0);
        assert(payload_length && memcmp(payload, "{\"v\":1,", 7) == 0);
    }
    assert(mqtt_wire_read(&broker, &header, body, sizeof(body), &length) == 0 &&
        header == 0xc0 && length == 0);
    assert(send(sockets[1], pingresp, sizeof(pingresp), 0) == sizeof(pingresp));
    assert(!mqtt_listener_take_success());
    assert(mqtt_listener_resume(pause) == 0);
    for (index = 0; index < 100 && !success; ++index) {
        success = mqtt_listener_take_success();
        if (!success)
            usleep(10000);
    }
    assert(success);
    {
        static const char merchant[] =
            "{\"broadcast_type\":4,\"merchant_name\":\"Test parking\","
            "\"merchant_name_l2\":\"Gate A\",\"merchant_nmid\":\"TEST-NMID\"}";
        struct soundbox_event event;
        assert(mqtt_wire_send_publish(&broker, MQTT_TOPIC, merchant,
            sizeof(merchant) - 1U) == 0);
        success = 0;
        for (index = 0; index < 100 && !success; ++index) {
            success = mqtt_listener_take_update(&event);
            if (!success)
                usleep(1000);
        }
        assert(success && event.type == SB_EVENT_MERCHANT);
        assert(!strcmp(event.settings.merchant_name, "Test parking"));
        assert(!strcmp(event.settings.merchant_name_l2, "Gate A"));
        assert(!strcmp(event.settings.merchant_nmid, "TEST-NMID"));
    }
    for (index = 0; index < 100 && !mqtt_listener_connected(); ++index)
        usleep(10000);
    assert(mqtt_listener_connected());
    close(sockets[1]);
    assert(pthread_join(thread, NULL) == 0);
    assert(!mqtt_listener_connected());
    close(sockets[0]);
}

int main(void)
{
    static const char *sample =
        "{\"biz_type\":1,\"broadcast_type\":1,\"ctime\":1793306924,"
        "\"datetime\":\"20261030034844\",\"lang\":0,\"money\":2000,"
        "\"request_id\":\"2567472108542026103093600814\",\"sound\":5}";
    static const uint8_t nul_message[] = {'{', 0, '}'};
    struct mqtt_connection connection;
    struct soundbox_event event;
    unsigned int pause, i;

    assert(pthread_mutex_init(&mutex, NULL) == 0);
    test_payload_logging();
    mutex_ready = 1;
    running = 1;
    assert(verify_certificate(1, NULL) == 1);
    assert(verify_certificate(0, (WOLFSSL_X509_STORE_CTX *)&store_marker) == 0);
    assert(strstr(last_log, "certificate issuer: test CA") != NULL);
    assert(certificate_readable(MQTT_CERT_DIR "ca.pem"));
    missing_file = 1;
    assert(!certificate_readable(MQTT_CERT_DIR "pri.key"));
    missing_file = 0;
    assert(open_connection(&connection) == 0);
    assert(verify_mode == WOLFSSL_VERIFY_PEER && verify_domain && sni_set &&
        loaded_ca_count == 4);
    assert(strstr(last_log, "127.0.0.1:8883") != NULL);
    close_connection(&connection);
    fail_handshake = 1;
    assert(open_connection(&connection) != 0);
    assert(strstr(last_log, "certificate signer not found") != NULL);
    close_connection(&connection);
    fail_handshake = 0;
    test_connection(1);
    test_connection(0);
    assert(boot_state_published);
    test_connection(0);
    deliver(sample);
    assert(!mqtt_listener_take_success());
    mqtt_listener_watch(1);
    deliver("{\"biz_type\":2}");
    deliver("{\"biz_type\":\"1\"}");
    deliver("{\"biz_type\":1.5}");
    deliver("{\"biz_type\":1,\"biz_type\":1}");
    deliver("{\"biz_type\":1} trailing");
    deliver("{\"biz_type\":1,\"x\":\"\\u0000\"}");
    on_message(nul_message, sizeof(nul_message));
    assert(!mqtt_listener_take_success());
    mqtt_listener_watch(1);
    deliver("{\"broadcast_type\":\"1\",\"money\":\"18446744073709551615\"}");
    assert(mqtt_listener_take_payment(&event) && event.money == UINT64_MAX);
    assert(!mqtt_listener_take_payment(&event));
    mqtt_listener_watch(1);
    deliver("{\"broadcast_type\":1}");
    assert(mqtt_listener_take_payment(&event) && event.money == 0);
    mqtt_listener_watch(1);
    deliver("{\"broadcast_type\":1,\"broadcast_type\":1}");
    deliver("{\"broadcast_type\":\"01\"}");
    deliver("{\"broadcast_type\":1,\"money\":-1}");
    deliver("{\"broadcast_type\":1,\"money\":\"20abc\"}");
    deliver("{\"broadcast_type\":1,\"money\":1.5}");
    deliver("{\"broadcast_type\":1,\"money\":9007199254740992}");
    assert(!mqtt_listener_take_success());
    pause = mqtt_listener_pause();
    assert(pause && !mqtt_listener_pause());
    for (i = 0; i < UPDATE_CAPACITY + 1U; ++i) {
        char message[96];
        snprintf(message, sizeof(message), "{\"broadcast_type\":4,\"merchant_name\":\"Parkir %u\"}", i);
        deliver(message);
    }
    deliver(sample);
    assert(!mqtt_listener_take_update(&event) && !mqtt_listener_take_success());
    assert(mqtt_listener_dropped() == 1);
    assert(mqtt_listener_resume(pause + 1U) != 0);
    assert(!mqtt_listener_take_update(&event));
    assert(mqtt_listener_resume(pause) == 0);
    assert(mqtt_listener_take_payment(&event) && event.money == 2000);
    for (i = 0; i < UPDATE_CAPACITY; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "Parkir %u", i);
        assert(mqtt_listener_take_update(&event));
        assert(strcmp(event.settings.merchant_name, name) == 0);
    }
    assert(!mqtt_listener_take_update(&event));
    mqtt_listener_watch(1);
    deliver(sample);
    assert(mqtt_listener_take_success());
    deliver(sample);
    assert(!mqtt_listener_take_success());
    mqtt_listener_watch(1);
    deliver(sample);
    mqtt_listener_watch(0);
    assert(!mqtt_listener_take_success());
    running = 0;
    mqtt_listener_watch(1);
    deliver(sample);
    assert(!mqtt_listener_take_success());
    fail_workdir = 1;
    assert(mqtt_listener_start() == -1 && ssl_references == 0);
    fail_workdir = 0;
    missing_file = 1;
    assert(mqtt_listener_start() == -1 && ssl_references == 0);
    missing_file = 0;
    fail_dns = 1;
    assert(mqtt_listener_start() == 0 && ssl_references == 1);
    assert(mqtt_listener_start() == -1 && ssl_references == 1);
    usleep(30000);
    mqtt_listener_stop();
    assert(!mqtt_listener_connected() && ssl_references == 0);
    assert(mqtt_listener_start() == 0 && ssl_references == 1);
    mqtt_listener_stop();
    assert(ssl_references == 0);
    assert(pthread_mutex_destroy(&mutex) == 0);
    puts("MQTT message tests passed");
    return 0;
}
