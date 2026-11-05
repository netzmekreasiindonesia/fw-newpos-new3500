#include "mqtt_listener.h"

#if defined(SIM_BUILD) && !defined(SIM_MQTT_TEST)

int mqtt_listener_start(void) { return -1; }
void mqtt_listener_stop(void) {}
void mqtt_listener_watch(int enabled) { (void)enabled; }
int mqtt_listener_connected(void) { return 0; }
int mqtt_listener_take_success(void) { return 0; }
int mqtt_listener_take_payment(struct soundbox_event *event) { (void)event; return 0; }
int mqtt_listener_take_update(struct soundbox_event *event) { (void)event; return 0; }
unsigned int mqtt_listener_pause(void) { return 0; }
int mqtt_listener_resume(unsigned int token) { (void)token; return -1; }
unsigned int mqtt_listener_dropped(void) { return 0; }

#else

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef SIM_BUILD
#include <arpa/inet.h>
#include <pthread.h>
extern struct hostent *gethostbyname_safe(const char *name, uint8_t *buffer,
    size_t length);
#else
#include <network.h>
#include <os_pthread.h>
#include <syscall.h>
#endif

#include <appfile.h>
#include <cJSON.h>
#ifndef HAVE_SNI
#define HAVE_SNI
#endif
#if !defined(SIM_BUILD) && !defined(OPENSSL_EXTRA_X509_SMALL)
#define OPENSSL_EXTRA_X509_SMALL
#endif
#include <wolfssl/ssl.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#ifdef SIM_BUILD
extern WOLFSSL_X509 *wolfSSL_X509_STORE_CTX_get_current_cert(WOLFSSL_X509_STORE_CTX *);
extern int wolfSSL_X509_STORE_CTX_get_error(WOLFSSL_X509_STORE_CTX *);
extern int wolfSSL_X509_STORE_CTX_get_error_depth(WOLFSSL_X509_STORE_CTX *);
extern WOLFSSL_X509_NAME *wolfSSL_X509_get_subject_name(WOLFSSL_X509 *);
extern WOLFSSL_X509_NAME *wolfSSL_X509_get_issuer_name(WOLFSSL_X509 *);
extern char *wolfSSL_X509_NAME_oneline(WOLFSSL_X509_NAME *, char *, int);
#endif
#include "mqtt_wire.h"

#define LOGGER_MODULE_NAME "mqtt"
#include "libs/logger/logger.h"

#define MQTT_HOST "acbkcrhucmhqq-ats.iot.ap-southeast-1.amazonaws.com"
#define MQTT_TOPIC "/ota/" SOUNDBOX_BACKEND_DEVICE_ID "/update"
#define MQTT_STATE_TOPIC "/ota/device/connection/" SOUNDBOX_BACKEND_DEVICE_ID "/state"
#define MQTT_CLIENT_ID "qris_pos&" SOUNDBOX_BACKEND_DEVICE_ID
#define MQTT_CERT_DIR "resources/certs/stg/"
#define MQTT_IO_TIMEOUT_MS 10000U
#ifndef MQTT_PING_INTERVAL_MS
#define MQTT_PING_INTERVAL_MS 30000U
#endif
#define MQTT_TELEMETRY_INTERVAL_MS 60000U
#define MQTT_TELEMETRY_TOPIC "/staging/telemetry/" SOUNDBOX_BACKEND_DEVICE_ID "/status"
#define UPDATE_CAPACITY 8U

static pthread_mutex_t mutex;
static int mutex_ready;
static int running;
static int worker_done;
static int connected;
static int watching;
static int success_pending;
static struct soundbox_event payment;
static struct soundbox_event updates[UPDATE_CAPACITY];
static unsigned int update_head, update_count, dropped;
static unsigned int pause_sequence, pause_token;
static unsigned int generation;
static int ssl_initialized;
static int boot_state_published;

struct mqtt_connection {
    int fd;
    WOLFSSL_CTX *context;
    WOLFSSL *session;
};

static int should_run(void)
{
    int result;
    pthread_mutex_lock(&mutex);
    result = running;
    pthread_mutex_unlock(&mutex);
    return result;
}

static int verify_certificate(int verified, WOLFSSL_X509_STORE_CTX *store)
{
    WOLFSSL_X509 *certificate;
    WOLFSSL_X509_NAME *name;
    char subject[256];
    char issuer[256];

    if (verified || !store)
        return verified;
    logger_error("MQTT certificate verification error=%d depth=%d\n",
        wolfSSL_X509_STORE_CTX_get_error(store),
        wolfSSL_X509_STORE_CTX_get_error_depth(store));
    certificate = wolfSSL_X509_STORE_CTX_get_current_cert(store);
    if (!certificate)
        return verified;
    name = wolfSSL_X509_get_subject_name(certificate);
    if (name && wolfSSL_X509_NAME_oneline(name, subject, sizeof(subject)))
        logger_error("MQTT certificate subject: %s\n", subject);
    name = wolfSSL_X509_get_issuer_name(certificate);
    if (name && wolfSSL_X509_NAME_oneline(name, issuer, sizeof(issuer)))
        logger_error("MQTT certificate issuer: %s\n", issuer);
    return verified;
}

static void set_connected(int value)
{
    int changed;
    pthread_mutex_lock(&mutex);
    changed = connected != value;
    connected = value;
    pthread_mutex_unlock(&mutex);
    if (changed)
        logger_info("MQTT %s\n", value ? "connected and subscribed" : "disconnected");
}

static int certificate_readable(const char *path)
{
    unsigned char first_byte;
    int fd;
    int count;
    int error;

    errno = 0;
    fd = app_file_open(path, O_RDONLY);
    error = errno;
    if (fd < 0) {
        logger_error("MQTT credential open failed: %s%s (result=%d, errno=%d)\n",
            CFG_APP_PATH, path, fd, error);
        return 0;
    }
    errno = 0;
    count = app_file_read(fd, &first_byte, 1);
    error = errno;
    app_file_close(fd);
    if (count != 1) {
        logger_error("MQTT credential read failed: %s%s (result=%d, errno=%d)\n",
            CFG_APP_PATH, path, count, error);
        return 0;
    }
    return 1;
}

static void on_message(const uint8_t *message, size_t length)
{
    struct soundbox_event event;
    unsigned int current_generation;
    pthread_mutex_lock(&mutex);
    if (!running) {
        pthread_mutex_unlock(&mutex);
        return;
    }
    current_generation = generation;
    pthread_mutex_unlock(&mutex);
    logger_info("MQTT incoming PUBLISH on %s: %u bytes\n",
        MQTT_TOPIC, (unsigned)length);
    if (soundbox_event_parse(message, length, &event) != 0) {
        logger_warn("MQTT incoming business message rejected by parser\n");
        return;
    }
    pthread_mutex_lock(&mutex);
    if (!running) {
        pthread_mutex_unlock(&mutex);
        return;
    }
    event.generation = current_generation;
    if (event.type == SB_EVENT_PAYMENT && watching && generation == current_generation) {
        payment = event;
        success_pending = 1;
        watching = 0;
    } else if (event.type != SB_EVENT_PAYMENT) {
        if (update_count == UPDATE_CAPACITY) {
            ++dropped;
            pthread_mutex_unlock(&mutex);
            logger_error("MQTT business queue full; configuration update dropped\n");
            return;
        }
        updates[(update_head + update_count) % UPDATE_CAPACITY] = event;
        ++update_count;
        logger_info("MQTT configuration queued: type=%d fields=0x%lx queued=%u\n",
            event.type, (unsigned long)event.fields, update_count);
    }
    pthread_mutex_unlock(&mutex);
}

static int wait_readable(struct mqtt_connection *connection, unsigned int milliseconds)
{
#ifdef SIM_BUILD
    fd_set readers;
#else
    sci_fd_set readers;
#endif
    struct timeval timeout;
    int result;

    if (wolfSSL_pending(connection->session) > 0)
        return 1;
#ifdef SIM_BUILD
    if (connection->fd < 0 || connection->fd >= FD_SETSIZE) {
        logger_error("MQTT socket exceeds select capacity\n");
        return -1;
    }
    FD_ZERO(&readers);
    FD_SET(connection->fd, &readers);
#else
    net_FD_ZERO(&readers);
    net_FD_SET(connection->fd, &readers);
#endif
    timeout.tv_sec = milliseconds / 1000U;
    timeout.tv_usec = (milliseconds % 1000U) * 1000U;
#ifdef SIM_BUILD
    result = select(connection->fd + 1, &readers, NULL, NULL, &timeout);
#else
    result = net_select(connection->fd + 1, &readers, NULL, NULL, &timeout);
#endif
    if (result < 0 && errno == EINTR)
        return 0;
    return result;
}

static int tls_read(void *context, void *buffer, size_t length)
{
    struct mqtt_connection *connection = context;
    size_t offset = 0;
    uint32_t start = syscall_get_tick_count();

    while (offset < length && should_run() &&
        (uint32_t)(syscall_get_tick_count() - start) < MQTT_IO_TIMEOUT_MS) {
        int ready = wait_readable(connection, 500);
        int result;
        int error;
        if (ready < 0)
            return -1;
        if (!ready)
            continue;
        result = wolfSSL_read(connection->session, (uint8_t *)buffer + offset,
            (int)(length - offset));
        if (result > 0) {
            offset += (size_t)result;
            continue;
        }
        error = wolfSSL_get_error(connection->session, result);
        if (error != WOLFSSL_ERROR_WANT_READ && error != WOLFSSL_ERROR_WANT_WRITE) {
            logger_warn("MQTT TLS read failed: %d\n", error);
            return -1;
        }
    }
    return offset == length ? 0 : -1;
}

static int tls_write(void *context, const void *buffer, size_t length)
{
    struct mqtt_connection *connection = context;
    size_t offset = 0;
    uint32_t start = syscall_get_tick_count();

    while (offset < length && should_run() &&
        (uint32_t)(syscall_get_tick_count() - start) < MQTT_IO_TIMEOUT_MS) {
        int result = wolfSSL_write(connection->session,
            (const uint8_t *)buffer + offset, (int)(length - offset));
        int error;
        if (result > 0) {
            offset += (size_t)result;
            continue;
        }
        error = wolfSSL_get_error(connection->session, result);
        if (error != WOLFSSL_ERROR_WANT_READ && error != WOLFSSL_ERROR_WANT_WRITE) {
            logger_warn("MQTT TLS write failed: %d\n", error);
            return -1;
        }
        syscall_msleep(20);
    }
    return offset == length ? 0 : -1;
}

static void close_connection(struct mqtt_connection *connection)
{
    if (connection->session)
        wolfSSL_free(connection->session);
    if (connection->context)
        wolfSSL_CTX_free(connection->context);
    if (connection->fd >= 0)
        close(connection->fd);
}

static int open_connection(struct mqtt_connection *connection)
{
    char host_buffer[512];
    struct hostent *host;
    struct sockaddr_in address;
    struct timeval timeout = {2, 0};
    const unsigned char *ip;
    int result;
    uint32_t start;

    memset(connection, 0, sizeof(*connection));
    connection->fd = -1;
    host = gethostbyname_safe(MQTT_HOST, (uint8_t *)host_buffer, sizeof(host_buffer));
    if (!host || host->h_addrtype != AF_INET || host->h_length != 4 ||
        !host->h_addr_list || !host->h_addr_list[0]) {
        logger_error("MQTT DNS resolution failed for %s\n", MQTT_HOST);
        return -1;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(8883);
    memcpy(&address.sin_addr.s_addr, host->h_addr_list[0], 4);
    ip = (const unsigned char *)&address.sin_addr.s_addr;
    logger_info("MQTT resolving %s to %u.%u.%u.%u:8883\n", MQTT_HOST,
        (unsigned int)ip[0], (unsigned int)ip[1], (unsigned int)ip[2],
        (unsigned int)ip[3]);
    connection->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (connection->fd < 0) {
        logger_error("MQTT socket creation failed: errno=%d\n", errno);
        return -1;
    }
    if (setsockopt(connection->fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
        setsockopt(connection->fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0 ||
        connect(connection->fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        logger_error("MQTT TCP connection failed: errno=%d\n", errno);
        return -1;
    }
    connection->context = wolfSSL_CTX_new(wolfTLSv1_2_client_method());
    if (!connection->context) {
        logger_error("MQTT TLS context creation failed\n");
        return -1;
    }
    wolfSSL_CTX_set_verify(connection->context, WOLFSSL_VERIFY_PEER, verify_certificate);
    if (wolfSSL_CTX_load_verify_locations(connection->context,
            CFG_APP_PATH MQTT_CERT_DIR "ca.pem", NULL) != WOLFSSL_SUCCESS ||
        wolfSSL_CTX_load_verify_locations(connection->context,
            CFG_APP_PATH MQTT_CERT_DIR "starfield-g2.pem", NULL) != WOLFSSL_SUCCESS ||
        wolfSSL_CTX_load_verify_locations(connection->context,
            CFG_APP_PATH MQTT_CERT_DIR "starfield-class2.pem", NULL) != WOLFSSL_SUCCESS ||
        wolfSSL_CTX_load_verify_locations(connection->context,
            CFG_APP_PATH MQTT_CERT_DIR "amazon-cross.pem", NULL) != WOLFSSL_SUCCESS ||
        wolfSSL_CTX_use_certificate_file(connection->context,
            CFG_APP_PATH MQTT_CERT_DIR "cil.crt", WOLFSSL_FILETYPE_PEM) != WOLFSSL_SUCCESS ||
        wolfSSL_CTX_use_PrivateKey_file(connection->context,
            CFG_APP_PATH MQTT_CERT_DIR "pri.key", WOLFSSL_FILETYPE_PEM) != WOLFSSL_SUCCESS ||
        wolfSSL_CTX_check_private_key(connection->context) != WOLFSSL_SUCCESS) {
        logger_error("MQTT TLS credential setup failed\n");
        return -1;
    }
    connection->session = wolfSSL_new(connection->context);
    if (!connection->session ||
        wolfSSL_UseSNI(connection->session, WOLFSSL_SNI_HOST_NAME,
            MQTT_HOST, (unsigned short)strlen(MQTT_HOST)) != WOLFSSL_SUCCESS ||
        wolfSSL_check_domain_name(connection->session, MQTT_HOST) != WOLFSSL_SUCCESS ||
        wolfSSL_set_fd(connection->session, connection->fd) != WOLFSSL_SUCCESS) {
        logger_error("MQTT TLS hostname verification setup failed\n");
        return -1;
    }
    start = syscall_get_tick_count();
    while (should_run()) {
        int error;
        result = wolfSSL_connect(connection->session);
        if (result == WOLFSSL_SUCCESS)
            return 0;
        error = wolfSSL_get_error(connection->session, result);
        if ((error != WOLFSSL_ERROR_WANT_READ && error != WOLFSSL_ERROR_WANT_WRITE) ||
            (uint32_t)(syscall_get_tick_count() - start) >= MQTT_IO_TIMEOUT_MS) {
            logger_error("MQTT TLS handshake failed: %d\n", error);
            if (error == ASN_NO_SIGNER_E)
                logger_error("MQTT TLS certificate signer not found; check resolved IP and server certificate chain against the installed CA\n");
            return -1;
        }
        syscall_msleep(20);
    }
    return -1;
}

static void run_connection(struct mqtt_connection *connection)
{
    struct mqtt_wire_io io = {connection, tls_read, tls_write};
    uint8_t *body;
    uint32_t last_ping;
    uint32_t last_telemetry;
    int awaiting_ping = 0;

    body = malloc(MQTT_WIRE_BODY_MAX);
    if (!body) {
        logger_error("MQTT receive buffer allocation failed\n");
        return;
    }
    logger_info("MQTT TLS ready; sending CONNECT with client ID %s\n", MQTT_CLIENT_ID);
    if (mqtt_wire_connect(&io, MQTT_CLIENT_ID) != 0) {
        logger_error("MQTT CONNECT failed or CONNACK rejected for %s; check broker client-ID policy and certificate authorization\n",
            MQTT_CLIENT_ID);
        free(body);
        return;
    }
    logger_info("MQTT CONNACK accepted; subscribing to %s\n", MQTT_TOPIC);
    if (mqtt_wire_subscribe(&io, MQTT_TOPIC) != 0) {
        logger_error("MQTT SUBSCRIBE write failed for %s\n", MQTT_TOPIC);
        free(body);
        return;
    }
    last_ping = syscall_get_tick_count();
    while (should_run()) {
        uint8_t header;
        size_t length;
        const uint8_t *payload;
        size_t payload_length;
        if ((uint32_t)(syscall_get_tick_count() - last_ping) >= MQTT_IO_TIMEOUT_MS ||
            mqtt_wire_read(&io, &header, body, MQTT_WIRE_BODY_MAX, &length) != 0) {
            logger_error("MQTT SUBACK timed out or failed\n");
            free(body);
            return;
        }
        if (header == 0x90) {
            if (mqtt_wire_suback(header, body, length) != 0) {
                logger_error("MQTT subscription rejected for %s; check broker topic authorization\n", MQTT_TOPIC);
                free(body);
                return;
            }
            break;
        }
        if (mqtt_wire_publish(header, body, length, MQTT_TOPIC,
                &payload, &payload_length) != 0) {
            logger_error("MQTT unexpected packet before SUBACK\n");
            free(body);
            return;
        }
        on_message(payload, payload_length);
    }
    if (!should_run()) {
        free(body);
        return;
    }
    logger_info("MQTT SUBACK accepted for %s\n", MQTT_TOPIC);
    if (!boot_state_published) {
        static const char state[] = "{\"state\":1}";
        if (mqtt_wire_send_publish(&io, MQTT_STATE_TOPIC, state, sizeof(state) - 1U) != 0) {
            logger_error("MQTT boot-state write failed; reconnecting before retry (QoS0 delivery uncertain)\n");
            free(body);
            return;
        }
        boot_state_published = 1;
        logger_info("MQTT boot state sent to %s\n", MQTT_STATE_TOPIC);
    }
    set_connected(1);
    last_ping = syscall_get_tick_count();
    last_telemetry = last_ping - MQTT_TELEMETRY_INTERVAL_MS;
    while (should_run()) {
        uint32_t telemetry_now = syscall_get_tick_count();
        int ready;
        uint32_t now;
        if (SOUNDBOX_TELEMETRY_VERIFIED &&
            (uint32_t)(telemetry_now - last_telemetry) >= MQTT_TELEMETRY_INTERVAL_MS) {
            char telemetry[160];
            int length = snprintf(telemetry, sizeof(telemetry),
                "{\"v\":1,\"sn\":\"" SOUNDBOX_BACKEND_DEVICE_ID "\",\"up\":%lu,"
                "\"model\":\"NEW3500\",\"fw\":\"parking-preview\"}",
                (unsigned long)telemetry_now);
            if (length < 0 || (size_t)length >= sizeof(telemetry) ||
                mqtt_wire_send_publish(&io, MQTT_TELEMETRY_TOPIC, telemetry, (size_t)length) != 0) {
                logger_error("MQTT telemetry write failed; not replaying uncertain QoS0 write\n");
                break;
            }
            last_telemetry = telemetry_now;
        }
        ready = wait_readable(connection, 500);
        now = syscall_get_tick_count();
        if (ready < 0) {
            logger_warn("MQTT socket wait failed: errno=%d\n", errno);
            break;
        }
        if (ready > 0) {
            uint8_t header;
            size_t length;
            if (mqtt_wire_read(&io, &header, body, MQTT_WIRE_BODY_MAX, &length) != 0) {
                logger_warn("MQTT packet read failed\n");
                break;
            }
            if ((header & 0xf0U) == 0x30U) {
                const uint8_t *payload;
                size_t payload_length;
                if (mqtt_wire_publish(header, body, length, MQTT_TOPIC,
                        &payload, &payload_length) != 0) {
                    logger_warn("MQTT invalid or unexpected PUBLISH\n");
                    break;
                }
                on_message(payload, payload_length);
            } else if (header == 0xd0 && length == 0 && awaiting_ping) {
                awaiting_ping = 0;
            } else {
                logger_warn("MQTT unexpected packet: 0x%02x\n", header);
                break;
            }
        }
        if ((uint32_t)(now - last_ping) >= MQTT_PING_INTERVAL_MS) {
            if (awaiting_ping || mqtt_wire_ping(&io) != 0) {
                logger_warn("MQTT keepalive failed\n");
                break;
            }
            awaiting_ping = 1;
            last_ping = now;
        }
    }
    set_connected(0);
    free(body);
}

static void *mqtt_worker(void *argument)
{
    (void)argument;
    logger_info("MQTT worker started: broker=%s client=%s update=%s state=%s\n",
        MQTT_HOST, MQTT_CLIENT_ID, MQTT_TOPIC, MQTT_STATE_TOPIC);
    while (should_run()) {
        struct mqtt_connection connection;
        unsigned int delay;
        if (open_connection(&connection) == 0)
            run_connection(&connection);
        close_connection(&connection);
        for (delay = 0; delay < 50U && should_run(); ++delay)
            syscall_msleep(100);
    }
    pthread_mutex_lock(&mutex);
    worker_done = 1;
    pthread_mutex_unlock(&mutex);
    return NULL;
}

int mqtt_listener_start(void)
{
    pthread_attr_t attributes;
    pthread_t thread;
    int result;
    int ca_readable;
    int starfield_readable;
    int class2_readable;
    int cross_readable;
    int cert_readable;
    int key_readable;

    if (!mutex_ready) {
        result = pthread_mutex_init(&mutex, NULL);
        if (result != 0) {
            logger_error("MQTT mutex initialization failed: %d\n", result);
            return -1;
        }
        mutex_ready = 1;
    }
    logger_info("Starting MQTT listener for configured device %s\n", SOUNDBOX_BACKEND_DEVICE_ID);
    pthread_mutex_lock(&mutex);
    if (running) {
        pthread_mutex_unlock(&mutex);
        logger_error("MQQT listener is already running\n");
        return -1;
    }
    pthread_mutex_unlock(&mutex);
    if (app_file_set_workdir(CFG_APP_PATH) != 0) {
        logger_error("MQTT could not set SDK file working directory: %s\n",
            CFG_APP_PATH);
        return -1;
    }
    ca_readable = certificate_readable(MQTT_CERT_DIR "ca.pem");
    starfield_readable = certificate_readable(MQTT_CERT_DIR "starfield-g2.pem");
    class2_readable = certificate_readable(MQTT_CERT_DIR "starfield-class2.pem");
    cross_readable = certificate_readable(MQTT_CERT_DIR "amazon-cross.pem");
    cert_readable = certificate_readable(MQTT_CERT_DIR "cil.crt");
    key_readable = certificate_readable(MQTT_CERT_DIR "pri.key");
    if (!ca_readable || !starfield_readable || !class2_readable ||
        !cross_readable || !cert_readable || !key_readable) {
        logger_error("MQTT credentials unavailable; provision staging certificates\n");
        return -1;
    }
    if (wolfSSL_Init() != WOLFSSL_SUCCESS) {
        logger_error("MQTT wolfSSL initialization failed\n");
        return -1;
    }
    ssl_initialized = 1;
    pthread_mutex_lock(&mutex);
    running = 1;
    worker_done = 0;
    watching = success_pending = 0;
    update_head = update_count = dropped = 0;
    pause_token = 0;
    ++generation;
    pthread_mutex_unlock(&mutex);
    result = pthread_attr_init(&attributes);
    if (result == 0) {
#ifndef SIM_BUILD
        attributes.stacksize = 48 * 1024;
#endif
        result = pthread_create(&thread, &attributes, mqtt_worker, NULL);
        pthread_attr_destroy(&attributes);
#ifdef SIM_BUILD
        if (result == 0)
            pthread_detach(thread);
#endif
    }
    if (result != 0) {
        logger_error("MQTT worker creation failed: %d\n", result);
        pthread_mutex_lock(&mutex);
        running = 0;
        worker_done = 1;
        pthread_mutex_unlock(&mutex);
        wolfSSL_Cleanup();
        ssl_initialized = 0;
        return -1;
    }
    return 0;
}

void mqtt_listener_stop(void)
{
    int done;
    unsigned int discarded;
    if (!mutex_ready)
        return;
    pthread_mutex_lock(&mutex);
    running = 0;
    watching = 0;
    success_pending = 0;
    discarded = update_count;
    update_head = update_count = 0;
    pause_token = 0;
    ++generation;
    done = worker_done;
    pthread_mutex_unlock(&mutex);
    if (discarded)
        logger_warn("Stopping MQTT with %u unapplied updates; request a fresh broadcast after restart\n",
            discarded);
    while (ssl_initialized && !done) {
        syscall_msleep(20);
        pthread_mutex_lock(&mutex);
        done = worker_done;
        pthread_mutex_unlock(&mutex);
    }
    if (ssl_initialized) {
        wolfSSL_Cleanup();
        ssl_initialized = 0;
    }
}

void mqtt_listener_watch(int enabled)
{
    if (!mutex_ready)
        return;
    pthread_mutex_lock(&mutex);
    ++generation;
    watching = enabled && running;
    success_pending = 0;
    pthread_mutex_unlock(&mutex);
}

int mqtt_listener_connected(void)
{
    int result;
    if (!mutex_ready)
        return 0;
    pthread_mutex_lock(&mutex);
    result = running && connected;
    pthread_mutex_unlock(&mutex);
    return result;
}

int mqtt_listener_take_success(void)
{
    struct soundbox_event event;
    return mqtt_listener_take_payment(&event);
}

int mqtt_listener_take_payment(struct soundbox_event *event)
{
    int result;
    if (!event) {
        logger_error("Missing MQTT payment output\n");
        return 0;
    }
    if (!mutex_ready)
        return 0;
    pthread_mutex_lock(&mutex);
    result = running && !pause_token && success_pending;
    if (result) {
        *event = payment;
        success_pending = 0;
    }
    pthread_mutex_unlock(&mutex);
    return result;
}

int mqtt_listener_take_update(struct soundbox_event *event)
{
    int result;
    if (!event) {
        logger_error("Missing MQTT update output\n");
        return 0;
    }
    if (!mutex_ready)
        return 0;
    pthread_mutex_lock(&mutex);
    result = running && !pause_token && update_count;
    if (result) {
        *event = updates[update_head];
        update_head = (update_head + 1U) % UPDATE_CAPACITY;
        --update_count;
    }
    pthread_mutex_unlock(&mutex);
    return result;
}

unsigned int mqtt_listener_pause(void)
{
    unsigned int result = 0;
    if (!mutex_ready)
        return 0;
    pthread_mutex_lock(&mutex);
    if (running && !pause_token) {
        if (!++pause_sequence)
            ++pause_sequence;
        result = pause_token = pause_sequence;
    }
    pthread_mutex_unlock(&mutex);
    if (!result)
        logger_error("Cannot acquire MQTT consumption pause\n");
    return result;
}

int mqtt_listener_resume(unsigned int token)
{
    int result = -1;
    if (!mutex_ready)
        return -1;
    pthread_mutex_lock(&mutex);
    if (token && token == pause_token) {
        pause_token = 0;
        result = 0;
    }
    pthread_mutex_unlock(&mutex);
    if (result)
        logger_error("Stale or invalid MQTT pause token\n");
    return result;
}

unsigned int mqtt_listener_dropped(void)
{
    unsigned int result;
    if (!mutex_ready)
        return 0;
    pthread_mutex_lock(&mutex);
    result = dropped;
    pthread_mutex_unlock(&mutex);
    return result;
}

#endif
