#ifndef PRINTER_DEMO_SOUNDBOX_APP_H
#define PRINTER_DEMO_SOUNDBOX_APP_H

#include "soundbox_ui.h"
#include "soundbox_store.h"
#include "soundbox_backend.h"
#include "soundbox_event.h"
#include "qris_tap.h"

typedef struct soundbox_app soundbox_app_t;

typedef enum {
    SOUNDBOX_HOME, SOUNDBOX_AMOUNT, SOUNDBOX_VEHICLES, SOUNDBOX_PAYMENT,
    SOUNDBOX_MPM_WAIT, SOUNDBOX_TAP_CAPTURE, SOUNDBOX_CPM_START,
    SOUNDBOX_CPM_SCAN, SOUNDBOX_REQUEST, SOUNDBOX_RESULT,
    SOUNDBOX_HISTORY, SOUNDBOX_DETAIL
} soundbox_page_t;

typedef struct {
    /* Configuration strings/vehicles must outlive the controller. */
    /* Only these vehicles are shown; persisted price IDs are SB_* + 1. */
    const soundbox_ui_vehicle_t *vehicles;
    size_t vehicle_count;
    bool manual;
    bool simulation;
    void (*settings)(void *);
    void *user_data;
} soundbox_app_config_t;

soundbox_app_t *soundbox_app_create(lv_obj_t *parent,
    const soundbox_app_config_t *config);
void soundbox_app_destroy(soundbox_app_t *app);
bool soundbox_app_home(soundbox_app_t *app);
bool soundbox_app_key(soundbox_app_t *app, uint32_t key);
void soundbox_app_tick(soundbox_app_t *app, uint32_t now);
soundbox_page_t soundbox_app_page(const soundbox_app_t *app);
uint32_t soundbox_app_token(const soundbox_app_t *app);
bool soundbox_app_set_record(soundbox_app_t *app,
    const struct soundbox_record *record, int status);
void soundbox_app_updates_lost(soundbox_app_t *app);
typedef bool (*soundbox_submit_cb)(const struct soundbox_backend_job *, void *);
typedef bool (*soundbox_tap_capture_cb)(uint32_t token, void *);
typedef void (*soundbox_tap_cancel_cb)(void *);
bool soundbox_app_set_backend(soundbox_app_t *app, soundbox_submit_cb submit,
    void *user_data);
bool soundbox_app_set_tap_capture(soundbox_app_t *app,
    soundbox_tap_capture_cb capture, soundbox_tap_cancel_cb cancel,
    void *user_data);
bool soundbox_app_backend_complete(soundbox_app_t *app,
    const struct soundbox_backend_result *result, uint32_t now);
bool soundbox_app_tap_capture_complete(soundbox_app_t *app, uint32_t token,
    enum qris_tap_result result, const char *content, uint32_t now);
bool soundbox_app_scan_complete(soundbox_app_t *app, const char *content,
    uint32_t now);
bool soundbox_app_waiting_payment(const soundbox_app_t *app);
bool soundbox_app_acknowledge(soundbox_app_t *app, const struct soundbox_event *event);
void soundbox_app_mqtt_status(soundbox_app_t *app, bool connected);
/* Use only on the UI thread, with lv_tick_get() timestamps.
 * Navigation invalidates tokens; stale completions return false. */
bool soundbox_app_payment_complete(soundbox_app_t *app, uint32_t token,
    bool success, const char *message, uint32_t now);
bool soundbox_app_print_complete(soundbox_app_t *app, uint32_t token,
    soundbox_ui_print_state_t state, const char *message, uint32_t now);

#endif
