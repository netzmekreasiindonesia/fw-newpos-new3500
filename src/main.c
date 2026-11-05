#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <appfile.h>
#include <appsys.h>
#include <key.h>
#include <lvgl/lvgl.h>
#include <lvgl/lv_surface.h>
#include <lvgl/src/extra/libs/qrcode/lv_qrcode.h>
#include <os_pthread.h>
#include <syscall.h>
#include <asyncwork.h>
#ifndef SIM_BUILD
#include <appwifi.h>
#include <httprequest.h>
#endif
#include "init.h"

#define LOGGER_CFG          (LOGGER_LEVEL_DEBUG | LOGGER_TIMESTAMP | LOGGER_LEVEL | LOGGER_MODULE | LOGGER_FILE | LOGGER_LINE)
#define LOGGER_MODULE_NAME  "print-demo"
#include "libs/logger/logger.h"

#include "print_jobs.h"
#include "printer_transport.h"
#include "https_client.h"
#include "qr_request.h"
#include "qris_tap.h"
#include "mqtt_listener.h"
#include "soundbox_app.h"

enum worker_state { WORKER_IDLE, WORKER_QUEUED, WORKER_RUNNING, WORKER_DONE };
enum printer_action { ACTION_QR, ACTION_RECEIPT, ACTION_GENERATE_QR,
                      ACTION_HTTPS_TEST, ACTION_SCAN, ACTION_COUNT,
                      ACTION_PAIR = ACTION_COUNT,
                      ACTION_PRINT_GENERATED_QR, ACTION_PARKING,
                      ACTION_TAP_CAPTURE };

static ble_device_info_t scan_devices[MAX_DEVICE_NUM];
static int               scan_device_count;

static struct printer_connection connection = {
    PRINTER_BLE_VERIFIED, PRINTER_ADDRESS_BYTES, PRINTER_ADDRESS_TYPE,
    PRINTER_SERVICE_UUID, PRINTER_WRITE_UUID, PRINTER_PACE_MS, -1
};

static struct {
    pthread_mutex_t mutex;
    enum worker_state state;
    enum printer_action action;
    enum printer_action last_action;
    int stop;
    int stopped;
    char message[128];
    char qr_content[QR_REQUEST_CONTENT_MAX + 1U];
    int storage_initialized;
    int storage_changed;
    int storage_check;
    int storage_status;
    int storage_have_record;
    int mqtt_initialized;
    struct soundbox_record storage_record;
    struct soundbox_backend_job parking_job;
    struct soundbox_backend_result parking_result;
    enum qris_tap_result tap_result;
    uint32_t tap_token;
    int tap_cancel;
    char tap_content[QR_REQUEST_CONTENT_MAX + 1U];
} worker;

static struct soundbox_store store;
static uint32_t last_storage_check;

static void publish_storage(int status)
{
    pthread_mutex_lock(&worker.mutex);
    if (status >= 0 && worker.storage_initialized && worker.storage_status < 0 &&
        worker.storage_record.sequence == store.record.sequence)
        status = worker.storage_status;
    worker.storage_record = store.record;
    worker.storage_status = status;
    worker.storage_have_record = store.record.sequence &&
        soundbox_settings_valid(&store.record.settings);
    worker.storage_changed = 1;
    worker.storage_initialized = 1;
    pthread_mutex_unlock(&worker.mutex);
    if (status < 0)
        logger_error("Parking storage unavailable (status=%d); merchant updates cannot be committed and payment requests are blocked.\n",
            status);
}

static void service_storage(void)
{
    struct soundbox_event event;
    uint32_t now = syscall_get_tick_count();
    int check, result, mqtt_ready;
    pthread_mutex_lock(&worker.mutex);
    check = worker.storage_check;
    worker.storage_check = 0;
    mqtt_ready = worker.mqtt_initialized;
    pthread_mutex_unlock(&worker.mutex);
    if (!mqtt_ready)
        return;
    if (store.ready && mqtt_ready && mqtt_listener_take_update(&event)) {
        result = soundbox_event_apply(&store, &event);
        if (result < 0)
            logger_error("MQTT configuration commit failed: type=%d fields=0x%lx result=%d\n",
                event.type, (unsigned long)event.fields, result);
        else
            logger_info("MQTT configuration committed: type=%d fields=0x%lx sequence=%llu NMID_present=%d\n",
                event.type, (unsigned long)event.fields,
                (unsigned long long)store.record.sequence,
                store.record.settings.merchant_nmid[0] != '\0');
        publish_storage(result);
    }
    if (store.ready && (check || (uint32_t)(now - last_storage_check) >= 30000U)) {
        uint64_t previous = store.record.sequence;
        result = soundbox_store_rollover(&store, (int64_t)time(NULL));
        if (result < 0 || check || store.record.sequence != previous)
            publish_storage(result);
        last_storage_check = now;
    }
}
static lv_obj_t *buttons[ACTION_COUNT];
static lv_obj_t *status_label;
static lv_obj_t *address_label;
static lv_obj_t *main_screen;
static lv_obj_t *parking_screen;
static soundbox_app_t *parking_app;
static lv_obj_t *picker_screen;
static lv_obj_t *qr_screen;
static lv_obj_t *qr_print_target;
static lv_obj_t *qr_status_label;
static lv_obj_t *qr_back_button;
static lv_obj_t *qr_success_message;
static int qr_mqtt_connected;
static int mqtt_available;
static int parking_watching;
#ifndef SIM_BUILD
static int https_async_work_ready;
#endif

#if !defined(SIM_BUILD) || defined(SOUNDBOX_BACKEND_TEST)
static bool parking_submit(const struct soundbox_backend_job *job, void *user_data)
{
    bool accepted = false;
    (void)user_data;
    pthread_mutex_lock(&worker.mutex);
    if (!worker.stop && worker.state == WORKER_IDLE) {
        worker.parking_job = *job;
        worker.action = ACTION_PARKING;
        worker.state = WORKER_QUEUED;
        accepted = true;
    }
    pthread_mutex_unlock(&worker.mutex);
    if (!accepted)
        logger_warn("Parking request rejected: worker busy or stopping.\n");
    return accepted;
}
#endif

#ifndef SIM_BUILD
static bool tap_capture_submit(uint32_t token, void *user_data)
{
    bool accepted = false;
    (void)user_data;
    pthread_mutex_lock(&worker.mutex);
    if (!worker.stop && worker.state == WORKER_IDLE &&
        SOUNDBOX_TAP_LIVE_ENABLED) {
        worker.tap_token = token;
        worker.tap_cancel = 0;
        worker.tap_content[0] = '\0';
        worker.action = ACTION_TAP_CAPTURE;
        worker.state = WORKER_QUEUED;
        accepted = true;
    }
    pthread_mutex_unlock(&worker.mutex);
    if (!accepted)
        logger_warn("NFC capture rejected: worker busy, stopping, or Tap disabled.\n");
    return accepted;
}

static void tap_capture_cancel(void *user_data)
{
    (void)user_data;
    pthread_mutex_lock(&worker.mutex);
    if (worker.action == ACTION_TAP_CAPTURE &&
        worker.state != WORKER_IDLE && worker.state != WORKER_DONE)
        worker.tap_cancel = 1;
    pthread_mutex_unlock(&worker.mutex);
}

static int tap_capture_cancelled(void *user_data)
{
    int cancelled;
    (void)user_data;
    pthread_mutex_lock(&worker.mutex);
    cancelled = worker.tap_cancel || worker.stop;
    pthread_mutex_unlock(&worker.mutex);
    return cancelled;
}
#endif

static void send_parking_receipt(struct soundbox_backend_result *result, bool test_receipt)
{
    uint8_t payload[PRINT_JOB_BUFFER_SIZE];
    size_t length;
    struct printer_report report;
    enum print_result status = test_receipt ?
        print_test_receipt_build(result->data.receipt.amount, payload, sizeof(payload), &length) :
        print_receipt_build(&result->data.receipt, payload, sizeof(payload), &length);
    if (status != PRINT_OK) {
        result->status = SB_BACKEND_INVALID;
        logger_error("Receipt format rejected before Bluetooth: %d.\n", status);
        return;
    }
    report = printer_send(&connection, payload, length);
    result->delivery = report.status == PRINTER_SENT ? SB_RECEIPT_SENT :
        report.write_attempted ? SB_RECEIPT_UNCERTAIN : SB_RECEIPT_FAILED;
    if (report.status != PRINTER_SENT)
        logger_error("Receipt delivery failed: status=%d SDK=%d accepted=%u attempted=%d; no retry.\n",
            report.status, report.sdk_error, (unsigned)report.accepted_bytes,
            report.write_attempted);
}

static void execute_parking(void)
{
    struct soundbox_backend_job *job = &worker.parking_job;
    struct soundbox_backend_result *result = &worker.parking_result;
    unsigned pause = 0;
    memset(result, 0, sizeof(*result));
    result->kind = job->kind;
    result->token = job->token;
    result->status = SB_BACKEND_UNAVAILABLE;
    if (job->kind == SB_JOB_ACCOUNT) {
        int status = soundbox_store_account(&store, &job->attempt,
            job->paid_amount, (int64_t)time(NULL));
        publish_storage(status);
        result->status = status == SB_STORE_OK || status == SB_STORE_DUPLICATE ?
            SB_BACKEND_OK : SB_BACKEND_STORAGE;
        if (result->status != SB_BACKEND_OK)
            logger_error("Acknowledged payment accounting requires reconciliation: %d.\n", status);
        return;
    }
    if (job->kind == SB_JOB_RECEIPT && job->test_receipt) {
        result->status = SB_BACKEND_OK;
        result->data.receipt.amount = job->receipt.amount;
        logger_info("Printing flow-test receipt; no transaction detail lookup.\n");
        send_parking_receipt(result, true);
        return;
    }
#ifndef SIM_BUILD
    if (!https_async_work_ready) {
        logger_error("Parking HTTPS unavailable: SDK initialization failed.\n");
        return;
    }
#endif
    if (job->kind == SB_JOB_QR) {
        int status;
        int64_t now = (int64_t)time(NULL);
        if ((job->type == 2 && !SOUNDBOX_CPM_LIVE_ENABLED) ||
            (job->type == 3 && !SOUNDBOX_TAP_LIVE_ENABLED)) {
            logger_error("QR payment blocked: backend contract is not approved.\n");
            return;
        }
        if (job->type == 1) {
            if (!mqtt_available || !mqtt_listener_connected()) {
                logger_error("MPM request not sent: MQTT disconnected.\n");
                return;
            }
            pause = mqtt_listener_pause();
            if (!pause) {
                logger_error("MPM request not sent: cannot pause MQTT business dispatch.\n");
                return;
            }
        }
        status = soundbox_store_rollover(&store, now);
        publish_storage(status);
        if (status < 0 || !soundbox_business_date(now) ||
            job->attempt.merchant_generation != store.record.merchant_generation ||
            job->attempt.date != store.record.date ||
            strcmp(job->attempt.nmid, store.record.settings.merchant_nmid) != 0) {
            logger_error("QR payment not sent: merchant/date snapshot changed or clock invalid.\n");
            result->status = SB_BACKEND_STORAGE;
            goto resume;
        }
        status = soundbox_store_begin(&store, &job->attempt);
        publish_storage(status);
        if (status != SB_STORE_OK) {
            result->status = SB_BACKEND_STORAGE;
            goto resume;
        }
        result->attempt = job->attempt;
    }
    if (connection.fd >= 0 && printer_release(&connection) != 0) {
        logger_error("Parking HTTPS not sent: BLE release failed.\n");
        goto resume;
    }
    soundbox_backend_execute(job, result);
    if (job->kind == SB_JOB_QR && (job->type == 2 || job->type == 3) &&
        result->status == SB_BACKEND_OK) {
        result->accounting_status = soundbox_store_account(&store,
            &result->attempt, job->amount, (int64_t)time(NULL));
        publish_storage(result->accounting_status);
        if (result->accounting_status != SB_STORE_OK &&
            result->accounting_status != SB_STORE_DUPLICATE)
            logger_error("QR payment confirmed; local total requires review (storage=%d).\n",
                result->accounting_status);
    }
    if (job->kind == SB_JOB_RECEIPT && result->status == SB_BACKEND_OK)
        send_parking_receipt(result, false);
    logger_info("Parking job %d completed: result %d, HTTP %d.\n",
        job->kind, result->status, result->http_status);
resume:
    if (job->kind == SB_JOB_QR && result->attempt.id &&
        (!result->submission_started ||
         (job->type != 3 && (result->status == SB_BACKEND_DECLINED ||
          result->status == SB_BACKEND_AUTH)))) {
        int status = soundbox_store_cancel(&store, result->attempt.id);
        publish_storage(status);
        if (status == SB_STORE_OK)
            memset(&result->attempt, 0, sizeof(result->attempt));
        else {
            logger_error("Unsubmitted/rejected parking attempt could not be released: %d.\n", status);
            result->status = SB_BACKEND_STORAGE;
        }
    }
    if (pause && mqtt_listener_resume(pause) != 0) {
        logger_error("MQTT resume failed; parking state requires inspection.\n");
        result->status = SB_BACKEND_UNAVAILABLE;
    }
}

static void execute_action(enum printer_action action, char *message, size_t capacity)
{
    static const struct print_format format = {
        PRINTER_NATIVE_QR_VERIFIED
    };
    uint8_t payload[PRINT_JOB_BUFFER_SIZE];
    size_t length;
    enum print_result result;
    struct printer_report report;
    logger_debug("Executing printer action: %d\n", action);
    if (action == ACTION_PARKING) {
        execute_parking();
        snprintf(message, capacity, "%s", soundbox_backend_message(worker.parking_result.status));
        return;
    }
    if (action == ACTION_TAP_CAPTURE) {
#ifdef SIM_BUILD
        worker.tap_result = QRIS_TAP_READER_ERROR;
#else
        worker.tap_result = qris_tap_native_capture(worker.tap_content,
            sizeof(worker.tap_content), tap_capture_cancelled, NULL);
#endif
        snprintf(message, capacity, "%s", worker.tap_result == QRIS_TAP_OK ?
            "NFC capture complete." :
            worker.tap_result == QRIS_TAP_TIMEOUT ? "NFC tap timed out." :
            worker.tap_result == QRIS_TAP_CANCELLED ? "NFC tap cancelled." :
            "NFC capture failed.");
        return;
    }
    if (action != ACTION_PRINT_GENERATED_QR)
        worker.qr_content[0] = '\0';

    if (action == ACTION_SCAN) {
#ifdef SIM_BUILD
        snprintf(message, capacity, "Bluetooth unavailable in simulator.");
        return;
#endif
        scan_device_count = printer_scan_list(scan_devices, MAX_DEVICE_NUM);
        if (scan_device_count < 0)
            snprintf(message, capacity, "BT scan failed.");
        else if (scan_device_count == 0)
            snprintf(message, capacity, "No BLE devices found.");
        else
            message[0] = '\0'; /* empty = picker shown by update_status */
        logger_debug("Scan complete: %d device(s) found.\n", scan_device_count);
        return;
    }

    if (action == ACTION_PAIR) {
#ifdef SIM_BUILD
        snprintf(message, capacity, "Bluetooth unavailable in simulator.");
        return;
#endif
        report = printer_connect(&connection);
        if (report.status == PRINTER_PAIRED) {
            connection.verified = 1;
            snprintf(message, capacity, "Connected and ready.");
            logger_debug("Printer successfully connected.\n");
        } else if (report.status == PRINTER_NOT_FOUND) {
            snprintf(message, capacity, "Selected printer not found.");
            logger_debug("Printer not found during connection.\n");
        } else if (report.status == PRINTER_PAIR_INTERACTION_REQUIRED) {
            snprintf(message, capacity, "Printer requires PIN / passkey confirmation.");
            logger_debug("Printer requires PIN/passkey.\n");
        } else {
            snprintf(message, capacity, "Connection failed. Error %d/%d.", report.status, report.sdk_error);
            logger_debug("Connection failed with status %d, sdk_error %d\n", report.status, report.sdk_error);
        }
        return;
    }

    if (action == ACTION_HTTPS_TEST) {
#ifdef SIM_BUILD
        snprintf(message, capacity, "HTTPS unavailable in simulator.");
#else
        char sdk_version[MIN_VERSION_LEN] = {0};
        int version_result;
        int http_status = 0;
        int http_result;

        if (!https_async_work_ready) {
            snprintf(message, capacity, "HTTPS worker unavailable.");
            logger_error("HTTPS test disabled: SDK async work is unavailable.\n");
            return;
        }

        version_result = http_get_version(sdk_version, sizeof(sdk_version));
        sdk_version[sizeof(sdk_version) - 1U] = '\0';
        if (version_result == 0)
            logger_info("HTTP SDK version: %s\n", sdk_version);
        else
            logger_warn("HTTP SDK version lookup failed: %d\n", version_result);

        http_result = https_client_probe(&http_status);
        logger_info("HTTPS probe returned %d, HTTP status %d.\n",
            http_result, http_status);
        if (http_result == 0 && http_status > 0) {
            snprintf(message, capacity, "Unverified HTTPS connected: HTTP %d.", http_status);
        } else {
            snprintf(message, capacity, "Unverified HTTPS failed: %d, HTTP %d.",
                http_result, http_status);
        }
#endif
        return;
    }

    if (action == ACTION_PRINT_GENERATED_QR) {
        if (!worker.qr_content[0]) {
            snprintf(message, capacity, "Generate QRIS first.");
            return;
        }
        result = print_qr_raster_build(worker.qr_content, payload,
            sizeof(payload), &length);
    } else if (action == ACTION_GENERATE_QR) {
#ifdef SIM_BUILD
        snprintf(message, capacity, "QRIS request unavailable in simulator.");
        return;
#else
        if (!https_async_work_ready) {
            snprintf(message, capacity, "HTTPS worker unavailable.");
            logger_error("QRIS request disabled: SDK async work is unavailable.\n");
            return;
        }
        if (connection.fd >= 0) {
            int release_result;
            logger_debug("Releasing printer BLE connection before QRIS HTTPS request.\n");
            release_result = printer_release(&connection);
            if (release_result < 0)
                logger_warn("Printer BLE release returned %d; will reconnect before printing.\n",
                    release_result);
        }
        {
            int http_status = 0;
            unsigned int pause = mqtt_available ? mqtt_listener_pause() : 0;
            enum qr_request_result fetched;
            if (mqtt_available && !pause) {
                snprintf(message, capacity, "MQTT pause unavailable; QRIS not sent.");
                return;
            }
            fetched = qr_request_generate(
                worker.qr_content, sizeof(worker.qr_content), &http_status);
            if (pause && mqtt_listener_resume(pause) != 0) {
                worker.qr_content[0] = '\0';
                snprintf(message, capacity, "MQTT resume failed; check payment state.");
                return;
            }
            logger_debug("QRIS HTTPS request completed with result %d, HTTP %d.\n",
                fetched, http_status);
            if (fetched != QR_REQUEST_OK) {
                if (fetched == QR_REQUEST_NETWORK_FAILED)
                    snprintf(message, capacity, "QRIS HTTPS request failed.");
                else if (fetched == QR_REQUEST_SERVER_FAILED)
                    snprintf(message, capacity, "QRIS service returned HTTP %d.",
                        http_status);
                else
                    snprintf(message, capacity, "Invalid QRIS response.");
                return;
            }
        }
        snprintf(message, capacity, "Tap the QR code to print.");
        return;
#endif
    } else {
        result = print_job_build((enum print_job)action, &format, payload,
            sizeof(payload), &length);
    }
    if (result != PRINT_OK) {
        const char *error = result == PRINT_QR_UNSUPPORTED ?
            "Native QR format not verified." :
            result == PRINT_NO_SPACE ? "Print payload too large." :
            "Invalid print payload.";
        snprintf(message, capacity, "%s", error);
        logger_debug("Print job build failed with result code: %d\n", result);
        return;
    }

#ifdef SIM_BUILD
    snprintf(message, capacity, "Bluetooth unavailable in simulator.");
    return;
#endif

    report = printer_send(&connection, payload, length);
    if (report.status == PRINTER_SENT) {
        snprintf(message, capacity, "Sent to printer: %u bytes.", (unsigned int)report.accepted_bytes);
        logger_debug("Successfully sent %u bytes to printer.\n", (unsigned int)report.accepted_bytes);
    } else if (report.status == PRINTER_CONFIG_REQUIRED) {
        snprintf(message, capacity, "BLE endpoint / address not verified.");
        logger_debug("BLE endpoint config required.\n");
    } else if (report.write_attempted) {
        snprintf(message, capacity, "Possible partial print (%u bytes accepted). No retry. Error %d/%d.",
            (unsigned int)report.accepted_bytes, report.status, report.sdk_error);
        logger_debug("Partial print! Accepted %u bytes. Status %d, Error %d\n",
                     (unsigned int)report.accepted_bytes, report.status, report.sdk_error);
    } else {
        snprintf(message, capacity, "BLE failed before printing. Error %d/%d.",
            report.status, report.sdk_error);
        logger_debug("BLE failed before printing. Status %d, Error %d\n", report.status, report.sdk_error);
    }
}

static void *print_worker(void *argument)
{
    (void)argument;
#ifdef SIM_BUILD
    if (app_file_set_workdir("fsroot" CFG_APP_PATH) != 0) {
#else
    if (app_file_set_workdir(CFG_APP_PATH) != 0) {
#endif
        logger_error("Storage working directory unavailable.\n");
        memset(&store, 0, sizeof(store));
        publish_storage(SB_STORE_IO);
    } else {
        publish_storage(soundbox_store_open(&store, (int64_t)time(NULL)));
    }
    last_storage_check = syscall_get_tick_count();

    for (;;) {
        enum printer_action action;
        char message[sizeof(worker.message)];

        pthread_mutex_lock(&worker.mutex);

        if (worker.stop) {
            logger_debug("Print worker stopping...\n");
            pthread_mutex_unlock(&worker.mutex);
            printer_release(&connection);

            pthread_mutex_lock(&worker.mutex);
            worker.stopped = 1;
            pthread_mutex_unlock(&worker.mutex);
            return NULL;
        }

        if (worker.state != WORKER_QUEUED) {
            int idle = worker.state == WORKER_IDLE;
            pthread_mutex_unlock(&worker.mutex);
            if (idle)
                service_storage();
            syscall_msleep(20);
            continue;
        }

        action = worker.action;
        worker.state = WORKER_RUNNING;
        pthread_mutex_unlock(&worker.mutex);

        logger_debug("Print worker picked up queued action.\n");
        execute_action(action, message, sizeof(message));

        pthread_mutex_lock(&worker.mutex);
        snprintf(worker.message, sizeof(worker.message), "%s", message);
        worker.last_action = action;
        worker.state = WORKER_DONE;
        pthread_mutex_unlock(&worker.mutex);
    }
}

static int start_worker(void)
{
    pthread_attr_t attributes;
    pthread_t thread;
    int result;

    logger_debug("Initializing print worker...\n");
    memset(&worker, 0, sizeof(worker));

    if (pthread_mutex_init(&worker.mutex, NULL) != 0) {
        logger_debug("Failed to initialize worker mutex.\n");
        return 0;
    }

    result = pthread_attr_init(&attributes);
    if (result == 0) {
#ifndef SIM_BUILD
        attributes.stacksize = 48 * 1024;
#endif
        result = pthread_create(&thread, &attributes, print_worker, NULL);
        pthread_attr_destroy(&attributes);

#ifdef SIM_BUILD
        if (result == 0)
            pthread_detach(thread);
#endif
    }

    if (result != 0) {
        logger_debug("Failed to create worker thread. Result: %d\n", result);
        pthread_mutex_destroy(&worker.mutex);
    } else {
        int initialized = 0;
        while (!initialized) {
            pthread_mutex_lock(&worker.mutex);
            initialized = worker.storage_initialized;
            pthread_mutex_unlock(&worker.mutex);
            if (!initialized)
                syscall_msleep(20);
        }
        logger_debug("Print worker thread started successfully.\n");
    }

    return result == 0;
}

static void set_busy(int busy)
{
    size_t index;

    for (index = 0; index < ACTION_COUNT; ++index) {
        if (busy)
            lv_obj_add_state(buttons[index], LV_STATE_DISABLED);
        else
            lv_obj_clear_state(buttons[index], LV_STATE_DISABLED);
    }
    if (qr_print_target) {
        if (busy)
            lv_obj_add_state(qr_print_target, LV_STATE_DISABLED);
        else
            lv_obj_clear_state(qr_print_target, LV_STATE_DISABLED);
    }
    if (qr_back_button) {
        if (busy)
            lv_obj_add_state(qr_back_button, LV_STATE_DISABLED);
        else
            lv_obj_clear_state(qr_back_button, LV_STATE_DISABLED);
    }
}

static void queue_action(enum printer_action action)
{
    int queued = 0;

    logger_debug("Print button clicked for action: %d\n", action);

    pthread_mutex_lock(&worker.mutex);
    if (worker.state == WORKER_IDLE && !worker.stop) {
        worker.action = action;
        worker.state = WORKER_QUEUED;
        queued = 1;
    }
    pthread_mutex_unlock(&worker.mutex);

    if (queued) {
        set_busy(1);
        if (action == ACTION_PRINT_GENERATED_QR && qr_status_label) {
            lv_label_set_text(qr_status_label, "Sending QR code to printer...");
        } else {
            lv_label_set_text(status_label, action == ACTION_SCAN ?
                "Scanning for devices..." : action == ACTION_HTTPS_TEST ?
            "Testing HTTPS without certificate verification..." :
            action == ACTION_GENERATE_QR ? "Generating QRIS..." :
            "Preparing / sending...");
        }
        logger_debug("Action queued successfully.\n");
    } else {
        logger_debug("Failed to queue action: Worker is not idle or is stopping.\n");
    }
}

static void print_clicked(lv_event_t *event)
{
    queue_action((enum printer_action)(intptr_t)lv_event_get_user_data(event));
}

static void qr_print_clicked(lv_event_t *event)
{
    (void)event;
    queue_action(ACTION_PRINT_GENERATED_QR);
}

static lv_obj_t *screen_label(lv_obj_t *screen, const char *text)
{
    lv_obj_t *label = lv_label_create(screen);

    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, text);

    return label;
}

static void qr_screen_back_clicked(lv_event_t *event)
{
    int busy;

    (void)event;
    if (!qr_screen)
        return;
    pthread_mutex_lock(&worker.mutex);
    busy = worker.state != WORKER_IDLE;
    pthread_mutex_unlock(&worker.mutex);
    if (busy) {
        if (qr_status_label)
            lv_label_set_text(qr_status_label, "Wait for printing to finish.");
        return;
    }
    mqtt_listener_watch(0);
    lv_scr_load(main_screen);
    lv_obj_del(qr_screen);
    qr_screen = NULL;
    qr_print_target = NULL;
    qr_status_label = NULL;
    qr_back_button = NULL;
    qr_success_message = NULL;
}

static int qr_screen_show(const char *content, const char *message)
{
    lv_coord_t width = lv_disp_get_hor_res(NULL);
    lv_coord_t height = lv_disp_get_ver_res(NULL);
    lv_coord_t qr_size = width - 24;
    lv_obj_t *qr;

    if (!content || !content[0])
        return 0;
    if (qr_size > height - 90)
        qr_size = height - 90;
    if (qr_size < 1)
        return 0;

    qr_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(qr_screen, lv_color_white(), 0);
    lv_obj_set_style_pad_all(qr_screen, 8, 0);
    lv_obj_set_style_pad_row(qr_screen, 4, 0);
    lv_obj_set_flex_flow(qr_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(qr_screen, LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(qr_screen, LV_OBJ_FLAG_SCROLLABLE);

    screen_label(qr_screen, "Tap QR code to print");
    qr = lv_qrcode_create(qr_screen, qr_size, lv_color_black(), lv_color_white());
    if (!qr || lv_qrcode_update(qr, content, (uint32_t)strlen(content)) != LV_RES_OK) {
        lv_obj_del(qr_screen);
        qr_screen = NULL;
        return 0;
    }
    qr_print_target = qr;
    lv_obj_add_flag(qr_print_target, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(qr_print_target, qr_print_clicked, LV_EVENT_CLICKED, NULL);

    qr_status_label = screen_label(qr_screen, message);
    qr_back_button = lv_btn_create(qr_screen);
    lv_obj_set_size(qr_back_button, LV_PCT(80), 32);
    lv_obj_add_event_cb(qr_back_button, qr_screen_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(qr_back_button), "Back");
    lv_obj_center(lv_obj_get_child(qr_back_button, 0));
    qr_mqtt_connected = mqtt_listener_connected();
    mqtt_listener_watch(1);
    if (!mqtt_available)
        lv_label_set_text(qr_status_label, "MQTT unavailable; check logs.");
    else if (!qr_mqtt_connected)
        lv_label_set_text(qr_status_label, "MQTT disconnected; waiting for connection.");
    lv_scr_load(qr_screen);
    lv_obj_update_layout(qr_screen);
    return 1;
}

static void picker_dismiss(void)
{
    lv_scr_load(main_screen);
    lv_obj_del(picker_screen);
    picker_screen = NULL;
    set_busy(0);
    lv_label_set_text(status_label, "Scan cancelled.");
    logger_debug("Picker dismissed by user.\n");
}

static void picker_cancel_clicked(lv_event_t *e)
{
    (void)e;
    picker_dismiss();
}

static void picker_btn_clicked(lv_event_t *e)
{
    char addr_str[24];
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    memcpy(connection.address, scan_devices[idx].mac_addr, 6);
    connection.address_type = scan_devices[idx].addr_type;
    logger_debug("User selected device %d for pairing.\n", idx);

    snprintf(addr_str, sizeof(addr_str), "%02X:%02X:%02X:%02X:%02X:%02X",
        connection.address[0], connection.address[1], connection.address[2],
        connection.address[3], connection.address[4], connection.address[5]);
    if (address_label)
        lv_label_set_text(address_label, addr_str);

    lv_scr_load(main_screen);
    lv_obj_del(picker_screen);
    picker_screen = NULL;

    pthread_mutex_lock(&worker.mutex);
    if (worker.state == WORKER_IDLE) {
        worker.action = ACTION_PAIR;
        worker.state  = WORKER_QUEUED;
    }
    pthread_mutex_unlock(&worker.mutex);

    set_busy(1);
    lv_label_set_text(status_label, "Pairing...");
}

static void picker_screen_show(void)
{
    int i;
    lv_coord_t height = lv_disp_get_ver_res(NULL);
    lv_obj_t *cancel_btn;

    picker_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(picker_screen, lv_color_hex(0xf4f6f5), 0);

    lv_obj_t *list = lv_list_create(picker_screen);
    lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(list, height >= 300 ? 8 : 4, 0);

    lv_list_add_text(list, "Select Device (or tap Cancel)");

    for (i = 0; i < scan_device_count; ++i) {
        char row[80];
        const char *name = scan_devices[i].name[0]
            ? scan_devices[i].name : "(unnamed)";
        snprintf(row, sizeof(row), "%s  %02X:%02X:%02X:%02X:%02X:%02X  %d dBm",
            name,
            scan_devices[i].mac_addr[0], scan_devices[i].mac_addr[1],
            scan_devices[i].mac_addr[2], scan_devices[i].mac_addr[3],
            scan_devices[i].mac_addr[4], scan_devices[i].mac_addr[5],
            (int)scan_devices[i].rssi);
        lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_BLUETOOTH, row);
        lv_obj_add_event_cb(btn, picker_btn_clicked, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }

    cancel_btn = lv_list_add_btn(list, LV_SYMBOL_CLOSE, "Cancel / Back");
    lv_obj_add_event_cb(cancel_btn, picker_cancel_clicked, LV_EVENT_CLICKED, NULL);

    lv_scr_load(picker_screen);
    logger_debug("Picker screen showing %d device(s).\n", scan_device_count);
}

static void diagnostics_home_clicked(lv_event_t *event)
{
    int idle;
    (void)event;
    if (!parking_app)
        return;
    pthread_mutex_lock(&worker.mutex);
    idle = worker.state == WORKER_IDLE;
    pthread_mutex_unlock(&worker.mutex);
    if (!idle) {
        lv_label_set_text(status_label, "Wait for current job.");
        return;
    }
    if (soundbox_app_home(parking_app))
        lv_scr_load(parking_screen);
}

static void diagnostics_show(void *user_data)
{
    (void)user_data;
    lv_scr_load(main_screen);
}

static void apply_storage_update(void)
{
    struct soundbox_record record;
    int status, have_record;
    pthread_mutex_lock(&worker.mutex);
    if (!worker.storage_changed || !parking_app) {
        pthread_mutex_unlock(&worker.mutex);
        return;
    }
    record = worker.storage_record;
    status = worker.storage_status;
    have_record = worker.storage_have_record;
    worker.storage_changed = 0;
    pthread_mutex_unlock(&worker.mutex);
    if (!soundbox_app_set_record(parking_app, have_record ? &record : NULL, status))
        logger_error("Could not display persisted parking state.\n");
}

static int create_parking_screen(void)
{
    static const soundbox_ui_vehicle_t vehicles[] = {
#if SOUNDBOX_MENU_MOTOR
        {SB_MOTOR + 1U, "Motor", 2000},
#endif
#if SOUNDBOX_MENU_MOBIL
        {SB_MOBIL + 1U, "Mobil", 5000},
#endif
#if SOUNDBOX_MENU_SEPEDA
        {SB_SEPEDA + 1U, "Sepeda", 1000},
#endif
#if SOUNDBOX_MENU_ELF
        {SB_ELF + 1U, "Elf", 10000},
#endif
#if SOUNDBOX_MENU_BUS
        {SB_BUS + 1U, "Bus", 15000},
#endif
        {0, NULL, 0}
    };
    soundbox_app_config_t config = {
        vehicles, sizeof(vehicles) / sizeof(vehicles[0]) - 1U, SOUNDBOX_MENU_MANUAL,
#ifdef SIM_BUILD
        true,
#else
        false,
#endif
        diagnostics_show, NULL
    };
    parking_screen = lv_obj_create(NULL);
    if (!parking_screen) {
        logger_error("Parking screen allocation failed.\n");
        return 0;
    }
    lv_obj_remove_style_all(parking_screen);
    lv_obj_set_size(parking_screen, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_obj_clear_flag(parking_screen, LV_OBJ_FLAG_SCROLLABLE);
    parking_app = soundbox_app_create(parking_screen, &config);
    if (!parking_app) {
        logger_error("Parking UI initialization failed.\n");
        lv_obj_del(parking_screen);
        parking_screen = NULL;
        return 0;
    }
#ifndef SIM_BUILD
    if (!soundbox_app_set_backend(parking_app, parking_submit, NULL))
        logger_error("Live parking backend binding failed.\n");
    if (!soundbox_app_set_tap_capture(parking_app, tap_capture_submit,
        tap_capture_cancel, NULL))
        logger_error("Live NFC capture binding failed.\n");
#endif
    logger_info("Parking UI viewport: %dx%d; LVGL %d.%d.%d (physical display unqualified).\n",
        lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL),
        LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    apply_storage_update();
    lv_scr_load(parking_screen);
    return 1;
}

static uint32_t parking_key(uint32_t key)
{
    if (key >= KEY0 && key <= KEY9)
        return '0' + key - KEY0;
    switch (key) {
    case KEY_CANCEL: return LV_KEY_ESC;
    case KEY_ENTER: return LV_KEY_ENTER;
    case KEY_BACKSPACE: return LV_KEY_BACKSPACE;
    case KEY_CLEAR: return LV_KEY_DEL;
    case KEY_UP: return LV_KEY_UP;
    case KEY_DOWN: return LV_KEY_DOWN;
    default: return 0;
    }
}

static lv_obj_t *create_screen(void)
{
    static const char *const captions[] = {
        LV_SYMBOL_IMAGE " QR code", LV_SYMBOL_LIST " Sample receipt",
        LV_SYMBOL_IMAGE " Generate QRIS", LV_SYMBOL_WIFI " HTTPS (unverified)",
        LV_SYMBOL_BLUETOOTH " Scan & Pair"
    };
    static const uint32_t colors[] = {
        0x8b3d60, 0x2e6b9e, 0x147d64, 0x455d56, 0x455d56
    };

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_coord_t height = lv_disp_get_ver_res(NULL);
    size_t index;

    logger_debug("Creating LVGL screen...\n");

    lv_obj_set_style_bg_color(screen, lv_color_hex(0xf4f6f5), 0);
    lv_obj_set_style_pad_all(screen, height >= 360 ? 12 : 6, 0);
    lv_obj_set_style_pad_row(screen, height >= 360 ? 6 : 4, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    {
        lv_obj_t *home, *label;
        screen_label(screen, "Alat & Tes");
        home = lv_btn_create(screen);
        lv_obj_set_size(home, LV_PCT(100), 48);
        lv_obj_set_style_bg_color(home, lv_color_hex(0x455d56), 0);
        lv_obj_set_style_radius(home, 6, 0);
        lv_obj_set_style_shadow_width(home, 0, 0);
        lv_obj_set_style_pad_all(home, 4, 0);
        lv_obj_add_event_cb(home, diagnostics_home_clicked, LV_EVENT_CLICKED, NULL);
        label = lv_label_create(home);
        lv_label_set_text(label, "Kembali ke Home");
        lv_obj_center(label);
    }

    {
        char address[24];
        snprintf(address, sizeof(address), "%02X:%02X:%02X:%02X:%02X:%02X",
            connection.address[0], connection.address[1], connection.address[2],
            connection.address[3], connection.address[4], connection.address[5]);
        address_label = screen_label(screen, address);
    }

    for (index = 0; index < ACTION_COUNT; ++index) {
        enum printer_action action = index == 0 ? ACTION_SCAN : (enum printer_action)(index - 1);
        lv_obj_t *label;

        buttons[action] = lv_btn_create(screen);
        lv_obj_set_size(buttons[action], LV_PCT(100), height >= 360 ? 34 : 26);
        lv_obj_set_style_radius(buttons[action], 6, 0);
        lv_obj_set_style_bg_color(buttons[action], lv_color_hex(colors[action]), 0);
        lv_obj_set_style_shadow_width(buttons[action], 0, 0);
        lv_obj_set_style_pad_all(buttons[action], 2, 0);
        lv_obj_add_event_cb(buttons[action], print_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)action);

        label = lv_label_create(buttons[action]);
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(label, captions[action]);
        lv_obj_center(label);
    }

#ifdef SIM_BUILD
    status_label = screen_label(screen, "Simulator: Bluetooth unavailable.");
#else
    status_label = screen_label(screen, "Not connected.");
#endif

    lv_obj_set_flex_grow(status_label, 1);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x39473f), 0);
    lv_scr_load(screen);
    lv_obj_update_layout(screen);

    return screen;
}

static void update_status(void)
{
    char message[sizeof(worker.message)];
    char qr_content[QR_REQUEST_CONTENT_MAX + 1U] = {0};
    enum printer_action last_action;
    enum qris_tap_result tap_result = QRIS_TAP_READER_ERROR;
    uint32_t tap_token = 0;
    char tap_content[QR_REQUEST_CONTENT_MAX + 1U] = {0};
    int done = 0;
    static unsigned int last_dropped;
    unsigned int lost = mqtt_listener_dropped();

    apply_storage_update();
    if (lost != last_dropped) {
        last_dropped = lost;
        if (lost) {
            logger_error("MQTT configuration updates lost (%u); request a fresh broadcast.\n", lost);
            if (parking_app)
                soundbox_app_updates_lost(parking_app);
        }
    }

    if (qr_screen) {
        int is_connected = mqtt_listener_connected();
        if (is_connected != qr_mqtt_connected) {
            qr_mqtt_connected = is_connected;
            lv_label_set_text(qr_status_label, is_connected ?
                "Waiting for payment notification." :
                "MQTT disconnected; waiting for connection.");
        }
        if (mqtt_listener_take_success()) {
            qr_success_message = lv_msgbox_create(qr_screen, "Success",
                "Success", NULL, true);
            if (qr_success_message) {
                lv_obj_add_flag(qr_success_message, LV_OBJ_FLAG_FLOATING);
                lv_obj_set_width(qr_success_message, lv_obj_get_width(qr_screen) - 12);
                lv_obj_center(qr_success_message);
            } else {
                logger_error("Could not display MQTT success message\n");
                lv_label_set_text(qr_status_label, "Success");
            }
        }
    }

    pthread_mutex_lock(&worker.mutex);
    if (worker.state == WORKER_DONE) {
        snprintf(message, sizeof(message), "%s", worker.message);
        last_action = worker.last_action;
        if (last_action == ACTION_GENERATE_QR)
            snprintf(qr_content, sizeof(qr_content), "%s", worker.qr_content);
        if (last_action == ACTION_TAP_CAPTURE) {
            tap_result = worker.tap_result;
            tap_token = worker.tap_token;
            snprintf(tap_content, sizeof(tap_content), "%s", worker.tap_content);
        }
        worker.state = WORKER_IDLE;
        done = 1;
    }
    pthread_mutex_unlock(&worker.mutex);

    if (done) {
        if (last_action == ACTION_PARKING) {
            if (!parking_app || !soundbox_app_backend_complete(parking_app,
                &worker.parking_result, lv_tick_get()))
                logger_warn("Parking completion is no longer relevant; persisted attempt retained.\n");
        } else if (last_action == ACTION_TAP_CAPTURE) {
            if (!parking_app || !soundbox_app_tap_capture_complete(parking_app,
                tap_token, tap_result, tap_content, lv_tick_get()))
                logger_warn("NFC capture completion is no longer relevant.\n");
        } else if (last_action == ACTION_SCAN && scan_device_count > 0) {
            set_busy(0); /* re-enable main screen buttons before hiding screen */
            picker_screen_show();
            logger_debug("Picker shown with %d device(s).\n", scan_device_count);
        } else {
            if (last_action == ACTION_GENERATE_QR && qr_content[0]) {
                set_busy(0);
                if (!qr_screen_show(qr_content, "Tap the QR code to print."))
                    lv_label_set_text(status_label,
                        "QR generated but could not be displayed.");
            } else if (last_action == ACTION_PRINT_GENERATED_QR &&
                qr_screen && qr_status_label) {
                lv_label_set_text(qr_status_label, message);
                set_busy(0);
            } else {
                logger_debug("Updating UI with worker message.\n");
                lv_label_set_text(status_label, message);
                set_busy(0);
            }
        }
    }
    if (parking_app && !qr_screen) {
        int waiting = soundbox_app_waiting_payment(parking_app);
        struct soundbox_event event;
        soundbox_app_mqtt_status(parking_app, mqtt_listener_connected() != 0);
        if (waiting != parking_watching) {
            mqtt_listener_watch(waiting);
            parking_watching = waiting;
        }
        if (waiting && mqtt_listener_take_payment(&event)) {
            if (!soundbox_app_acknowledge(parking_app, &event))
                logger_error("Payment acknowledged but accounting could not be queued.\n");
            mqtt_listener_watch(0);
            parking_watching = 0;
        }
    }
}

int app_ui_init(void)
{
    return 0;
}

static int do_initcall(const initcall_t *start, const initcall_t *end)
{
    const initcall_t *initcall;
    int errors = 0;

    for (initcall = start; initcall < end; ++initcall) {
        if (initcall->init && initcall->init() != 0)
            ++errors;
    }
    return errors;
}

#ifndef SIM_BUILD
static void log_wifi_status(void)
{
    char ip[16] = {0};
    int connected = app_wifi_is_connected();
    int result;

    if (connected != app_true) {
        logger_warn("Wi-Fi is not connected after boot (status=%d).\n", connected);
        return;
    }

    result = app_wifi_get_localip(ip);
    if (result != 0)
        logger_warn("Wi-Fi is connected, but local IP lookup failed (%d).\n", result);
    else if (!ip[0] || strcmp(ip, "0.0.0.0") == 0)
        logger_warn("Wi-Fi is connected, but no local IP is assigned.\n");
    else
        logger_info("Wi-Fi is connected after boot; local IP: %s\n", ip);
}
#endif

int lvgl_main(int argc, char **argv)
{
    lv_obj_t *screen;
    int exiting = 0;
    int early_errors;
    int late_errors;
    int system_result;
    soundbox_page_t previous_page = SOUNDBOX_HOME;

    (void)argc;
    (void)argv;

    app_ui_init();
    early_errors = do_initcall(APP_EARLY_INITCALL_START, APP_EARLY_INITCALL_END);
    late_errors = do_initcall(APP_LATE_INITCALL_START, APP_LATE_INITCALL_END);
    logger_debug("Starting lvgl_main UI loop...\n");
    if (early_errors || late_errors)
        logger_warn("Initialization errors: early=%d late=%d\n", early_errors, late_errors);

    system_result = app_sys_init();
    if (system_result != 0)
        logger_error("SDK system initialization failed (%d).\n", system_result);
    else {
        char sn[64] = {0};

        logger_debug("SDK system initialized.\n");
        if (app_read_sn(sn, sizeof(sn) - 1) == 0 && sn[0])
            logger_info("Terminal SN: %s\n", sn);
        else
            logger_warn("Terminal SN unavailable.\n");
    }

#ifndef SIM_BUILD
    if (system_result == 0) {
        log_wifi_status();
        logger_debug("Initializing SDK async work threads...\n");
        https_async_work_ready = async_work_init(3, 16 * 1024) == 0;
        if (https_async_work_ready)
            logger_debug("SDK async work threads initialized.\n");
        else
            logger_error("SDK async work initialization failed; QRIS HTTPS is disabled.\n");
    } else {
        logger_error("QRIS HTTPS disabled because SDK system initialization failed.\n");
    }
#endif

    if (!start_worker()) {
        logger_debug("Failed to start worker, aborting lvgl_main.\n");
        return -1;
    }

    mqtt_available = system_result == 0 && mqtt_listener_start() == 0;
    pthread_mutex_lock(&worker.mutex);
    worker.mqtt_initialized = 1;
    pthread_mutex_unlock(&worker.mutex);
    if (!mqtt_available)
        logger_warn("MQTT listener unavailable.\n");

    lvgl_lock();
    screen = create_screen();
    main_screen = screen;
    if (!create_parking_screen())
        lv_label_set_text(status_label, "Parking UI failed. Diagnostic tools only.");
    lvgl_unlock();

    for (;;) {
        key_info_t info = {0};
        int key = kb_get_key_info(0, &info);
        int stopped;

        lvgl_lock();
        update_status();

        if (key > 0 && info.status == KEY_STATUS_DOWN) {
            uint32_t code = info.key ? info.key : (uint32_t)key;
            uint32_t translated = parking_key(code);
            int parking_active = parking_app && lv_scr_act() == parking_screen;
            int handled = parking_active && translated &&
                soundbox_app_key(parking_app, translated);

            if (!handled && code == KEY_CANCEL) {
                logger_debug("Cancel key pressed.\n");

                if (qr_screen) {
                    qr_screen_back_clicked(NULL);
                } else if (picker_screen) {
                    picker_dismiss();
                } else if (parking_app && !parking_active) {
                    diagnostics_home_clicked(NULL);
                } else {
                    pthread_mutex_lock(&worker.mutex);
                    if (worker.state == WORKER_IDLE) {
                        worker.stop = 1;
                        exiting = 1;
                    } else {
                        lv_label_set_text(status_label, "Wait for current job.");
                        logger_debug("Cannot exit: Worker is currently busy.\n");
                    }
                    pthread_mutex_unlock(&worker.mutex);
                }
            }
        }

        if (parking_app && lv_scr_act() == parking_screen) {
            soundbox_page_t page = soundbox_app_page(parking_app);
            if (page == SOUNDBOX_HISTORY && previous_page != page) {
                pthread_mutex_lock(&worker.mutex);
                worker.storage_check = 1;
                pthread_mutex_unlock(&worker.mutex);
            }
            previous_page = page;
            soundbox_app_tick(parking_app, lv_tick_get());
        }
        if (exiting)
            set_busy(1);

        lv_timer_handler();
        lvgl_unlock();

        pthread_mutex_lock(&worker.mutex);
        stopped = worker.stopped;
        pthread_mutex_unlock(&worker.mutex);

        if (stopped) {
            logger_debug("Worker stopped, breaking main loop.\n");
            break;
        }

        syscall_msleep(5);
    }

    mqtt_listener_stop();
    https_client_shutdown();
    lvgl_lock();
    lv_obj_t *empty = lv_obj_create(NULL);
    lv_scr_load(empty);
    if (qr_screen)
        lv_obj_del(qr_screen);
    soundbox_app_destroy(parking_app);
    parking_app = NULL;
    if (parking_screen) {
        lv_obj_del(parking_screen);
        parking_screen = NULL;
    }
    lv_obj_del(screen);
    lvgl_unlock();

    pthread_mutex_destroy(&worker.mutex);
    logger_debug("lvgl_main exited cleanly.\n");

    return 0;
}