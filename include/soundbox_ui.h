#ifndef PRINTER_DEMO_SOUNDBOX_UI_H
#define PRINTER_DEMO_SOUNDBOX_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <lvgl/lvgl.h>

#define SOUNDBOX_UI_MAX_ITEMS 32U

typedef struct soundbox_ui soundbox_ui_t;

typedef enum {
    SOUNDBOX_UI_BACK, SOUNDBOX_UI_SETTINGS, SOUNDBOX_UI_HISTORY,
    SOUNDBOX_UI_MANUAL, SOUNDBOX_UI_VEHICLE, SOUNDBOX_UI_AMOUNT,
    SOUNDBOX_UI_PAYMENT, SOUNDBOX_UI_SCAN, SOUNDBOX_UI_DONE,
    SOUNDBOX_UI_HISTORY_ITEM, SOUNDBOX_UI_PRINT_RECEIPT,
    SOUNDBOX_UI_SIM_SUCCESS, SOUNDBOX_UI_SIM_FAILURE
} soundbox_ui_action_t;

typedef enum {
    SOUNDBOX_UI_MPM, SOUNDBOX_UI_TAP, SOUNDBOX_UI_CPM
} soundbox_ui_method_t;

typedef struct {
    soundbox_ui_action_t action;
    uint32_t value;
    uint32_t amount;
} soundbox_ui_event_t;

typedef void (*soundbox_ui_event_cb)(soundbox_ui_t *,
    const soundbox_ui_event_t *, void *);

typedef struct {
    soundbox_ui_event_cb event;
    void *user_data;
    bool simulation;
} soundbox_ui_config_t;

typedef struct {
    uint32_t id;
    const char *name;
    uint32_t price;
} soundbox_ui_vehicle_t;

typedef struct {
    const char *title;
    const char *subtitle;
    const char *amount;
} soundbox_ui_history_item_t;

typedef struct {
    const char *label;
    const char *value;
} soundbox_ui_detail_row_t;

typedef enum {
    SOUNDBOX_PRINT_READY, SOUNDBOX_PRINT_PREPARING, SOUNDBOX_PRINT_SENDING,
    SOUNDBOX_PRINT_SENT, SOUNDBOX_PRINT_FAILED, SOUNDBOX_PRINT_UNCERTAIN
} soundbox_ui_print_state_t;

/* All APIs and callbacks run in the application's LVGL context. */
soundbox_ui_t *soundbox_ui_create(lv_obj_t *parent,
    const soundbox_ui_config_t *config);
void soundbox_ui_destroy(soundbox_ui_t *ui);
bool soundbox_ui_set_status(soundbox_ui_t *ui, const char *status);
bool soundbox_ui_key(soundbox_ui_t *ui, uint32_t key);
bool soundbox_ui_show_home(soundbox_ui_t *ui, const char *merchant,
    const char *nmid, const soundbox_ui_vehicle_t *vehicles,
    size_t count, bool manual);
bool soundbox_ui_show_amount(soundbox_ui_t *ui);
bool soundbox_ui_show_vehicles(soundbox_ui_t *ui, uint32_t amount,
    const soundbox_ui_vehicle_t *vehicles, size_t count);
bool soundbox_ui_show_payment(soundbox_ui_t *ui, const char *vehicle,
    uint32_t amount);
bool soundbox_ui_show_waiting(soundbox_ui_t *ui, soundbox_ui_method_t method,
    uint32_t amount);
bool soundbox_ui_show_scanner(soundbox_ui_t *ui, uint32_t amount);
const char *soundbox_ui_scan_value(const soundbox_ui_t *ui);
bool soundbox_ui_show_processing(soundbox_ui_t *ui, uint32_t amount);
bool soundbox_ui_show_network_wait(soundbox_ui_t *ui, const char *message);
bool soundbox_ui_show_payment_qr(soundbox_ui_t *ui, uint32_t amount,
    const char *content, bool connected);
bool soundbox_ui_set_countdown(soundbox_ui_t *ui, uint32_t seconds);
bool soundbox_ui_show_notice(soundbox_ui_t *ui, const char *heading,
    const char *message);
bool soundbox_ui_show_result(soundbox_ui_t *ui, bool success,
    uint32_t amount, const char *message);
bool soundbox_ui_set_result_state(soundbox_ui_t *ui,
    soundbox_ui_print_state_t state, const char *message);
bool soundbox_ui_show_history(soundbox_ui_t *ui, uint64_t today_count,
    uint64_t today_total, const soundbox_ui_history_item_t *items,
    size_t count, const char *message);
bool soundbox_ui_show_detail(soundbox_ui_t *ui, const char *heading,
    const soundbox_ui_detail_row_t *rows, size_t count);

#endif
