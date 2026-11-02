#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/main.c"

int logger_level_printf(int level, int config, const char *module,
    const char *file, int line, const char *format, ...)
{
    (void)level;
    (void)config;
    (void)module;
    (void)file;
    (void)line;
    (void)format;
    return 0;
}

static unsigned int colored_pixels;
static int initcall_count;
static int fake_connected;
static int fake_watching;
static int fake_success;

int mqtt_listener_start(void) { return 0; }
void mqtt_listener_stop(void) {}
void mqtt_listener_watch(int enabled) { fake_watching = enabled; fake_success = 0; }
int mqtt_listener_connected(void) { return fake_connected; }
int mqtt_listener_take_success(void)
{
    int result = fake_success;
    fake_success = 0;
    return result;
}
int mqtt_listener_take_update(struct soundbox_event *event) { (void)event; return 0; }
int mqtt_listener_take_payment(struct soundbox_event *event) { (void)event; return 0; }
unsigned int mqtt_listener_pause(void) { return 1; }
int mqtt_listener_resume(unsigned int token) { return token == 1 ? 0 : -1; }
unsigned int mqtt_listener_dropped(void) { return 0; }

static int initcall_ok(void)
{
    ++initcall_count;
    return 0;
}

static int initcall_failed(void)
{
    ++initcall_count;
    return -1;
}

static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels)
{
    int count = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);
    int index;
    for (index = 0; index < count; ++index) {
        if (lv_color_to32(pixels[index]) != lv_color_to32(lv_color_white()))
            ++colored_pixels;
    }
    lv_disp_flush_ready(driver);
}

static void assert_inside(lv_obj_t *child, lv_obj_t *parent)
{
    lv_area_t inner;
    lv_area_t outer;
    lv_obj_get_coords(child, &inner);
    lv_obj_get_coords(parent, &outer);
    assert(inner.x1 >= outer.x1 && inner.x2 <= outer.x2);
    assert(inner.y1 >= outer.y1 && inner.y2 <= outer.y2);
}

int main(void)
{
    static const enum printer_action screen_order[] = {
        ACTION_SCAN, ACTION_QR, ACTION_RECEIPT, ACTION_GENERATE_QR,
        ACTION_HTTPS_TEST
    };
    static lv_color_t pixels[480 * 10];
    static lv_disp_draw_buf_t buffer;
    static lv_disp_drv_t driver;
    static const int sizes[][2] = {{212, 318}, {240, 320}, {320, 480}, {480, 854}};
    static const initcall_t initcalls[] = {
        {"initcall_ok", initcall_ok}, {"initcall_failed", initcall_failed}
    };
    lv_disp_t *display;
    size_t size;

    assert(do_initcall(initcalls, initcalls + 2) == 1);
    assert(initcall_count == 2);

    lv_init();
    lv_disp_draw_buf_init(&buffer, pixels, NULL, 480 * 10);
    lv_disp_drv_init(&driver);
    driver.draw_buf = &buffer;
    driver.flush_cb = flush;
    driver.hor_res = 212;
    driver.ver_res = 318;
    display = lv_disp_drv_register(&driver);
    assert(display);
    assert(pthread_mutex_init(&worker.mutex, NULL) == 0);
    for (size = 0; size < sizeof(sizes) / sizeof(sizes[0]); ++size) {
        size_t index;
        unsigned int button_count = 0;
        lv_obj_t *screen, *home = NULL;
        lv_obj_t *old = lv_scr_act();
        driver.hor_res = sizes[size][0];
        driver.ver_res = sizes[size][1];
        lv_disp_drv_update(display, &driver);
        screen = create_screen();
        main_screen = screen;
        lv_obj_del(old);
        for (index = 0; index < lv_obj_get_child_cnt(screen); ++index) {
            lv_obj_t *child = lv_obj_get_child(screen, index);
            assert_inside(child, screen);
            if (lv_obj_check_type(child, &lv_btn_class)) {
                ++button_count;
                assert(lv_obj_get_child_cnt(child) == 1);
                assert_inside(lv_obj_get_child(child, 0), child);
                if (strcmp(lv_label_get_text(lv_obj_get_child(child, 0)),
                    "Kembali ke Home") == 0) {
                    home = child;
                    assert(lv_obj_get_height(home) >= 48);
                }
            }
        }
        assert(button_count == ACTION_COUNT + 1 && home);
        for (index = 1; index < sizeof(screen_order) / sizeof(screen_order[0]); ++index) {
            lv_area_t previous;
            lv_area_t current;
            lv_obj_get_coords(buttons[screen_order[index - 1]], &previous);
            lv_obj_get_coords(buttons[screen_order[index]], &current);
            assert(previous.y2 < current.y1);
        }
        {
            lv_area_t scan;
            lv_area_t qr;
            lv_obj_get_coords(buttons[ACTION_SCAN], &scan);
            lv_obj_get_coords(buttons[ACTION_QR], &qr);
            assert(scan.y2 < qr.y1);
        }
        worker.state = WORKER_IDLE;
        lv_event_send(buttons[ACTION_QR], LV_EVENT_CLICKED, NULL);
        assert(worker.state == WORKER_QUEUED && worker.action == ACTION_QR);
        for (index = 0; index < ACTION_COUNT; ++index)
            assert(lv_obj_has_state(buttons[index], LV_STATE_DISABLED));
        lv_event_send(buttons[ACTION_GENERATE_QR], LV_EVENT_CLICKED, NULL);
        lv_event_send(buttons[ACTION_SCAN], LV_EVENT_CLICKED, NULL);
        assert(worker.state == WORKER_QUEUED && worker.action == ACTION_QR);
        snprintf(worker.message, sizeof(worker.message),
            "Possible partial print (1024 bytes accepted). No retry. Error 5/-11.");
        worker.state = WORKER_DONE;
        update_status();
        lv_obj_update_layout(screen);
        assert(worker.state == WORKER_IDLE);
        assert_inside(status_label, screen);
        for (index = 0; index < ACTION_COUNT; ++index)
            assert(!lv_obj_has_state(buttons[index], LV_STATE_DISABLED));
        assert(lv_obj_get_self_height(status_label) <= lv_obj_get_height(status_label));
        lv_event_send(buttons[ACTION_SCAN], LV_EVENT_CLICKED, NULL);
        assert(worker.state == WORKER_QUEUED && worker.action == ACTION_SCAN);
        assert(strcmp(lv_label_get_text(status_label), "Scanning for devices...") == 0);
        lv_event_send(buttons[ACTION_QR], LV_EVENT_CLICKED, NULL);
        assert(worker.action == ACTION_SCAN);
        snprintf(worker.message, sizeof(worker.message), "Printer requires PIN / passkey confirmation. Not paired.");
        worker.state = WORKER_DONE;
        update_status();
        lv_obj_update_layout(screen);
        assert(lv_obj_get_self_height(status_label) <= lv_obj_get_height(status_label));
        lv_event_send(buttons[ACTION_GENERATE_QR], LV_EVENT_CLICKED, NULL);
        assert(worker.state == WORKER_QUEUED &&
            worker.action == ACTION_GENERATE_QR);
        snprintf(worker.message, sizeof(worker.message),
            "QRIS request unavailable in simulator.");
        worker.last_action = ACTION_GENERATE_QR;
        worker.state = WORKER_DONE;
        update_status();
        assert(worker.state == WORKER_IDLE);
        lv_event_send(buttons[ACTION_HTTPS_TEST], LV_EVENT_CLICKED, NULL);
        assert(worker.state == WORKER_QUEUED && worker.action == ACTION_HTTPS_TEST);
        snprintf(worker.message, sizeof(worker.message), "HTTPS unavailable in simulator.");
        worker.last_action = ACTION_HTTPS_TEST;
        worker.state = WORKER_DONE;
        update_status();
        assert(worker.state == WORKER_IDLE);
        colored_pixels = 0;
        lv_refr_now(display);
        assert(colored_pixels > 100);
        printf("PrinterDemo UI passed at %dx%d\n", driver.hor_res, driver.ver_res);
    }
    {
        char message[128];
        execute_action(ACTION_SCAN, message, sizeof(message));
        assert(strcmp(message, "Bluetooth unavailable in simulator.") == 0);
        execute_action(ACTION_QR, message, sizeof(message));
        assert(strcmp(message, "Bluetooth unavailable in simulator.") == 0);
        execute_action(ACTION_RECEIPT, message, sizeof(message));
        assert(strcmp(message, "Bluetooth unavailable in simulator.") == 0);
        execute_action(ACTION_GENERATE_QR, message, sizeof(message));
        assert(strcmp(message, "QRIS request unavailable in simulator.") == 0);
        snprintf(worker.qr_content, sizeof(worker.qr_content), "%s",
            "000201010212");
        execute_action(ACTION_PRINT_GENERATED_QR, message, sizeof(message));
        assert(strcmp(message, "Bluetooth unavailable in simulator.") == 0);
        execute_action(ACTION_HTTPS_TEST, message, sizeof(message));
        assert(strcmp(message, "HTTPS unavailable in simulator.") == 0);
    }
    snprintf(worker.qr_content, sizeof(worker.qr_content), "%s",
        "000201010212");
    mqtt_available = 1;
    assert(qr_screen_show(worker.qr_content, "Tap the QR code to print."));
    assert(lv_scr_act() == qr_screen);
    assert(fake_watching);
    assert(strcmp(lv_label_get_text(qr_status_label),
        "MQTT disconnected; waiting for connection.") == 0);
    fake_connected = 1;
    update_status();
    assert(strcmp(lv_label_get_text(qr_status_label),
        "Waiting for payment notification.") == 0);
    fake_success = 1;
    update_status();
    assert(qr_success_message);
    assert(strcmp(lv_label_get_text(lv_msgbox_get_text(qr_success_message)),
        "Success") == 0);
    lv_msgbox_close(qr_success_message);
    qr_success_message = NULL;
    lv_event_send(qr_print_target, LV_EVENT_CLICKED, NULL);
    assert(worker.state == WORKER_QUEUED);
    assert(worker.action == ACTION_PRINT_GENERATED_QR);
    assert(lv_obj_has_state(qr_print_target, LV_STATE_DISABLED));
    assert(lv_obj_has_state(qr_back_button, LV_STATE_DISABLED));
    snprintf(worker.message, sizeof(worker.message), "Sent to printer: 42 bytes.");
    worker.last_action = ACTION_PRINT_GENERATED_QR;
    worker.state = WORKER_DONE;
    update_status();
    assert(worker.state == WORKER_IDLE);
    assert(strcmp(lv_label_get_text(qr_status_label),
        "Sent to printer: 42 bytes.") == 0);
    assert(!lv_obj_has_state(qr_print_target, LV_STATE_DISABLED));
    qr_screen_back_clicked(NULL);
    assert(lv_scr_act() == main_screen);
    assert(!fake_watching);
    mqtt_available = 0;
    fake_connected = 0;
    assert(qr_screen_show(worker.qr_content, "Tap the QR code to print."));
    assert(strcmp(lv_label_get_text(qr_status_label),
        "MQTT unavailable; check logs.") == 0);
    qr_screen_back_clicked(NULL);
    pthread_mutex_destroy(&worker.mutex);
    return 0;
}