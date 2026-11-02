#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define SOUNDBOX_BACKEND_TEST
#include "../src/main.c"
#include "soundbox_images.h"
#include "soundbox_fonts.h"

int logger_level_printf(int level, int config, const char *module,
    const char *file, int line, const char *format, ...)
{
    (void)level; (void)config; (void)module; (void)file;
    (void)line; (void)format;
    return 0;
}

int mqtt_listener_start(void) { return 0; }
void mqtt_listener_stop(void) {}
static int mock_watching, payment_pending;
static struct soundbox_event payment_event;
void mqtt_listener_watch(int enabled) { mock_watching = enabled; payment_pending = 0; }
static int mock_mqtt_connected;
int mqtt_listener_connected(void) { return mock_mqtt_connected; }
int mqtt_listener_take_success(void) { return 0; }
int mqtt_listener_take_payment(struct soundbox_event *event)
{
    if (!mock_watching || !payment_pending)
        return 0;
    *event = payment_event;
    payment_pending = 0;
    return 1;
}
unsigned int mqtt_listener_pause(void) { return 1; }
int mqtt_listener_resume(unsigned int token) { return token == 1 ? 0 : -1; }
static int update_pending;
static struct soundbox_event update_event;
int mqtt_listener_take_update(struct soundbox_event *event)
{
    if (!update_pending)
        return 0;
    *event = update_event;
    update_pending = 0;
    return 1;
}
unsigned int mqtt_listener_dropped(void) { return 0; }

static uint32_t clock_ms;
uint32_t __wrap_lv_tick_get(void) { return clock_ms; }

static lv_color_t framebuffer[480 * 854];

static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels)
{
    lv_coord_t x, y;
    for (y = area->y1; y <= area->y2; ++y)
        for (x = area->x1; x <= area->x2; ++x)
            framebuffer[y * driver->hor_res + x] = *pixels++;
    lv_disp_flush_ready(driver);
}

static void capture(const char *name)
{
    const char *directory = getenv("SOUNDBOX_CAPTURE_DIR");
    char path[512];
    int width = lv_disp_get_hor_res(NULL), height = lv_disp_get_ver_res(NULL);
    FILE *file;
    int i, length;
    if (!directory)
        return;
    length = snprintf(path, sizeof(path), "%s/%dx%d-%s.ppm",
        directory, width, height, name);
    assert(length > 0 && (size_t)length < sizeof(path));
    lv_obj_invalidate(parking_screen);
    lv_refr_now(NULL);
    file = fopen(path, "wb");
    assert(file);
    assert(fprintf(file, "P6\n%d %d\n255\n", width, height) > 0);
    for (i = 0; i < width * height; ++i) {
        lv_color32_t color;
        unsigned char rgb[3];
        color.full = lv_color_to32(framebuffer[i]);
        rgb[0] = color.ch.red;
        rgb[1] = color.ch.green;
        rgb[2] = color.ch.blue;
        assert(fwrite(rgb, 1, sizeof(rgb), file) == sizeof(rgb));
    }
    assert(fclose(file) == 0);
}

static lv_obj_t *find_label(lv_obj_t *obj, const char *caption)
{
    uint32_t i;
    if (lv_obj_check_type(obj, &lv_label_class) &&
        strcmp(lv_label_get_text(obj), caption) == 0)
        return obj;
    for (i = 0; i < lv_obj_get_child_cnt(obj); ++i) {
        lv_obj_t *found = find_label(lv_obj_get_child(obj, i), caption);
        if (found)
            return found;
    }
    return NULL;
}

static lv_obj_t *find_button(const char *caption)
{
    lv_obj_t *label = find_label(parking_screen, caption);
    if (!label)
        return NULL;
    label = lv_obj_get_parent(label);
    assert(lv_obj_check_type(label, &lv_btn_class));
    return label;
}

static lv_obj_t *find_image(lv_obj_t *obj, const lv_img_dsc_t *source)
{
    uint32_t i;
    if (lv_obj_check_type(obj, &lv_img_class) && lv_img_get_src(obj) == source)
        return obj;
    for (i = 0; i < lv_obj_get_child_cnt(obj); ++i) {
        lv_obj_t *found = find_image(lv_obj_get_child(obj, i), source);
        if (found)
            return found;
    }
    return NULL;
}

static void inside(lv_obj_t *obj, lv_obj_t *parent)
{
    lv_area_t a, b;
    lv_obj_get_coords(obj, &a);
    lv_obj_get_coords(parent, &b);
    if (!(a.x1 >= b.x1 && a.x2 <= b.x2 && a.y1 >= b.y1 && a.y2 <= b.y2)) {
        fprintf(stderr, "Object (%d,%d)-(%d,%d) outside (%d,%d)-(%d,%d)\n",
            a.x1, a.y1, a.x2, a.y2, b.x1, b.y1, b.x2, b.y2);
        if (lv_obj_check_type(obj, &lv_label_class))
            fprintf(stderr, "Label: %s\n", lv_label_get_text(obj));
        assert(0);
    }
}

static void buttons_visible(lv_obj_t *obj)
{
    uint32_t i;
    for (i = 0; i < lv_obj_get_child_cnt(obj); ++i) {
        lv_obj_t *child = lv_obj_get_child(obj, i);
        if (lv_obj_check_type(child, &lv_btn_class)) {
            uint32_t j;
            assert(lv_obj_get_width(child) >= 22);
            assert(lv_obj_get_height(child) >= 22);
            inside(child, lv_obj_get_parent(child));
            for (j = 0; j < lv_obj_get_child_cnt(child); ++j)
                inside(lv_obj_get_child(child, j), child);
        }
        buttons_visible(child);
    }
}

static void result_text_visible(lv_obj_t *obj)
{
    uint32_t i;
    for (i = 0; i < lv_obj_get_child_cnt(obj); ++i) {
        lv_obj_t *child = lv_obj_get_child(obj, i);
        if (lv_obj_check_type(child, &lv_label_class) ||
            lv_obj_check_type(child, &lv_img_class)) {
            inside(child, obj);
            inside(child, parking_screen);
        }
        result_text_visible(child);
    }
}

static void layout(void)
{
    lv_obj_update_layout(parking_screen);
    buttons_visible(parking_screen);
}

static void click(const char *caption)
{
    lv_obj_t *btn = find_button(caption);
    assert(btn);
    lv_event_send(btn, LV_EVENT_CLICKED, NULL);
    lv_obj_update_layout(parking_screen);
}

static void advance(uint32_t ms)
{
    clock_ms += ms;
    soundbox_app_tick(parking_app, lv_tick_get());
    lv_obj_update_layout(parking_screen);
}

static void go_home(void)
{
    assert(soundbox_app_home(parking_app));
    lv_obj_update_layout(parking_screen);
}

static void start_method(const char *method)
{
    lv_area_t left, right, center;
    go_home();
    assert(find_image(parking_screen, &soundbox_img_netzme_name));
    assert(find_image(parking_screen, &soundbox_img_motor));
    assert(find_image(parking_screen, &soundbox_img_calculator));
    capture("home");
    click("Motor");
    assert(soundbox_app_page(parking_app) == SOUNDBOX_PAYMENT);
    layout();
    assert(find_image(find_button("QRIS MPM"), &soundbox_img_qris_white));
    assert(find_image(find_button("QRIS Tap"), &soundbox_img_qris_tap_white));
    assert(find_image(find_button("QRIS CPM"), &soundbox_img_qris_white));
    lv_obj_get_coords(find_button("QRIS MPM"), &left);
    lv_obj_get_coords(find_button("QRIS Tap"), &right);
    lv_obj_get_coords(find_button("QRIS CPM"), &center);
    assert(left.y1 == right.y1 && center.y1 > left.y2);
    assert(abs((center.x1 + center.x2) - lv_obj_get_width(parking_screen)) <= 2);
    capture("methods");
    click(method);
    layout();
    if (strcmp(method, "QRIS Tap") == 0)
        assert(find_image(parking_screen, &soundbox_img_wait_nfc));
    capture(method);
}

static void result(void)
{
    start_method("QRIS MPM");
    assert(soundbox_app_page(parking_app) == SOUNDBOX_MPM_WAIT);
    click("Simulasi OK");
    assert(soundbox_app_page(parking_app) == SOUNDBOX_RESULT);
    assert(find_label(parking_screen, "SIMULASI - tidak ada pembayaran nyata"));
    layout();
    result_text_visible(parking_screen);
    assert(find_image(parking_screen, &soundbox_img_success_payment));
    capture("success");
}

static void test_amounts(void)
{
    const char *digits;
    go_home();
    click("Input Nominal");
    layout();
    click("Enter");
    assert(soundbox_app_page(parking_app) == SOUNDBOX_AMOUNT);
    assert(find_label(parking_screen, "Minimal Rp 1.000"));
    click("1"); click("0"); click("0"); click("0"); click("Enter");
    assert(soundbox_app_page(parking_app) == SOUNDBOX_VEHICLES);
    assert(find_label(parking_screen, "Rp 1.000"));
    layout();
    click("Motor");
    assert(find_label(parking_screen, "Rp. 1.000"));
    assert(soundbox_app_key(parking_app, LV_KEY_ESC));
    assert(soundbox_app_page(parking_app) == SOUNDBOX_VEHICLES);
    assert(soundbox_app_key(parking_app, LV_KEY_ESC));
    assert(soundbox_app_page(parking_app) == SOUNDBOX_AMOUNT);
    assert(soundbox_app_key(parking_app, LV_KEY_DEL));
    for (digits = "10000000"; *digits; ++digits)
        assert(soundbox_app_key(parking_app, (uint32_t)*digits));
    assert(find_label(parking_screen, "Rp 10.000.000"));
    assert(soundbox_app_key(parking_app, '1'));
    assert(find_label(parking_screen, "Maksimal Rp 10.000.000"));
    assert(find_label(parking_screen, "Rp 10.000.000"));
    assert(soundbox_app_key(parking_app, LV_KEY_ENTER));
    assert(soundbox_app_page(parking_app) == SOUNDBOX_VEHICLES);
    click("Mobil");
    assert(find_label(parking_screen, "Rp. 10.000.000"));
    layout();
    go_home();
    click("Input Nominal");
    for (digits = "1000"; *digits; ++digits)
        assert(soundbox_app_key(parking_app, (uint32_t)*digits));
    assert(soundbox_app_key(parking_app, LV_KEY_BACKSPACE));
    assert(find_label(parking_screen, "Rp 100"));
    assert(soundbox_app_key(parking_app, LV_KEY_ENTER));
    assert(soundbox_app_page(parking_app) == SOUNDBOX_AMOUNT);
    assert(soundbox_app_key(parking_app, LV_KEY_DEL));
    assert(find_label(parking_screen, "Rp 0"));
}

static void test_result(void)
{
    uint32_t token;
    result();
    token = soundbox_app_token(parking_app);
    assert(!soundbox_app_payment_complete(parking_app, token, true, "Duplicate", lv_tick_get()));
    advance(2999);
    assert(soundbox_app_page(parking_app) == SOUNDBOX_RESULT);
    assert(find_label(parking_screen, "Home dalam 1 detik"));
    click("Cetak Struk");
    assert(lv_obj_has_state(find_button("Cetak Struk"), LV_STATE_DISABLED));
    assert(lv_obj_has_state(find_button("Home"), LV_STATE_DISABLED));
    click("Cetak Struk");
    click("Home");
    assert(soundbox_app_key(parking_app, LV_KEY_ESC));
    advance(1);
    assert(soundbox_app_page(parking_app) == SOUNDBOX_RESULT);
    advance(499);
    assert(!lv_obj_has_state(find_button("Home"), LV_STATE_DISABLED));
    assert(lv_obj_has_state(find_button("Cetak Struk"), LV_STATE_DISABLED));
    layout();
    advance(2999);
    assert(soundbox_app_page(parking_app) == SOUNDBOX_RESULT);
    advance(1);
    assert(soundbox_app_page(parking_app) == SOUNDBOX_HOME);
    assert(!soundbox_app_print_complete(parking_app, token, SOUNDBOX_PRINT_SENT,
        "Late", lv_tick_get()));

    result();
    token = soundbox_app_token(parking_app);
    click("Cetak Struk");
    assert(soundbox_app_print_complete(parking_app, token, SOUNDBOX_PRINT_FAILED,
        "Printer gagal. Kembali ke Home.", lv_tick_get()));
    advance(60000);
    assert(soundbox_app_page(parking_app) == SOUNDBOX_RESULT);
    assert(find_label(parking_screen, "Pembayaran berhasil!"));
    assert(!soundbox_app_print_complete(parking_app, token, SOUNDBOX_PRINT_SENT,
        "Late duplicate", lv_tick_get()));
    click("Home");
    assert(soundbox_app_page(parking_app) == SOUNDBOX_HOME);

    result();
    click("Cetak Struk");
    assert(soundbox_app_print_complete(parking_app, soundbox_app_token(parking_app),
        SOUNDBOX_PRINT_UNCERTAIN, "Mungkin tercetak. Jangan ulangi.", lv_tick_get()));
    advance(60000);
    assert(soundbox_app_page(parking_app) == SOUNDBOX_RESULT);
    assert(lv_obj_has_state(find_button("Cetak Struk"), LV_STATE_DISABLED));
    click("Home");

    start_method("QRIS MPM");
    click("Simulasi gagal");
    assert(soundbox_app_page(parking_app) == SOUNDBOX_RESULT);
    assert(find_button("Cetak Struk") == NULL);
    layout();
    assert(find_image(parking_screen, &soundbox_img_failed_payment));
    result_text_visible(parking_screen);
    capture("failure");
    advance(2999);
    assert(soundbox_app_page(parking_app) == SOUNDBOX_RESULT);
    advance(1);
    assert(soundbox_app_page(parking_app) == SOUNDBOX_HOME);

    start_method("QRIS MPM");
    assert(soundbox_app_payment_complete(parking_app, soundbox_app_token(parking_app),
        true, "Wraparound", UINT32_MAX - 1000U));
    soundbox_app_tick(parking_app, (UINT32_MAX - 1000U) + 2999U);
    assert(soundbox_app_page(parking_app) == SOUNDBOX_RESULT);
    soundbox_app_tick(parking_app, (UINT32_MAX - 1000U) + 3000U);
    assert(soundbox_app_page(parking_app) == SOUNDBOX_HOME);
}

static void test_capture(void)
{
    uint32_t token;
    start_method("QRIS Tap");
    assert(soundbox_app_page(parking_app) == SOUNDBOX_TAP_CAPTURE);
    token = soundbox_app_token(parking_app);
    assert(!soundbox_app_payment_complete(parking_app, token, true, "Not yet", lv_tick_get()));
    click("Simulasi OK");
    assert(soundbox_app_page(parking_app) == SOUNDBOX_REQUEST);
    layout();
    click("Simulasi OK");
    assert(soundbox_app_page(parking_app) == SOUNDBOX_RESULT);
    start_method("QRIS CPM");
    assert(soundbox_app_page(parking_app) == SOUNDBOX_CPM_START);
    assert(find_button("Simulasi OK") == NULL);
    click("Pindai");
    assert(soundbox_app_page(parking_app) == SOUNDBOX_CPM_SCAN);
    layout();
    click("Simulasi OK");
    assert(soundbox_app_page(parking_app) == SOUNDBOX_REQUEST);
    click("Simulasi gagal");
    assert(soundbox_app_page(parking_app) == SOUNDBOX_RESULT);
    assert(find_button("Cetak Struk") == NULL);
    start_method("QRIS CPM");
    click("Pindai");
    advance(59999);
    assert(soundbox_app_page(parking_app) == SOUNDBOX_CPM_SCAN);
    advance(1);
    assert(soundbox_app_page(parking_app) == SOUNDBOX_RESULT);
    assert(find_label(parking_screen, "Waktu pindai habis"));
    start_method("QRIS MPM");
    token = soundbox_app_token(parking_app);
    assert(soundbox_app_key(parking_app, LV_KEY_ESC));
    assert(soundbox_app_page(parking_app) == SOUNDBOX_PAYMENT);
    assert(!soundbox_app_payment_complete(parking_app, token, true, "Late", lv_tick_get()));
    click("QRIS MPM");
    advance(299999);
    assert(soundbox_app_page(parking_app) == SOUNDBOX_MPM_WAIT);
    advance(1);
    assert(soundbox_app_page(parking_app) == SOUNDBOX_RESULT);
}

static void test_navigation(void)
{
    lv_obj_t *home = lv_obj_get_parent(find_label(main_screen, "Kembali ke Home"));
    assert(lv_obj_check_type(home, &lv_btn_class));
    go_home();
    assert(!soundbox_app_key(parking_app, LV_KEY_ESC));
    assert(soundbox_app_key(parking_app, LV_KEY_UP));
    assert(soundbox_app_key(parking_app, LV_KEY_UP));
    assert(soundbox_app_key(parking_app, LV_KEY_ENTER));
    assert(lv_scr_act() == main_screen);
    lv_event_send(home, LV_EVENT_CLICKED, NULL);
    assert(lv_scr_act() == parking_screen);
    assert(soundbox_app_key(parking_app, LV_KEY_UP));
    assert(soundbox_app_key(parking_app, LV_KEY_ENTER));
    assert(soundbox_app_page(parking_app) == SOUNDBOX_HISTORY);
    capture("history");
    click("SIMULASI - Motor");
    assert(soundbox_app_page(parking_app) == SOUNDBOX_DETAIL);
    capture("detail");
    assert(soundbox_app_key(parking_app, LV_KEY_ESC));
    assert(soundbox_app_page(parking_app) == SOUNDBOX_HISTORY);
    assert(soundbox_app_key(parking_app, LV_KEY_ESC));
    assert(soundbox_app_page(parking_app) == SOUNDBOX_HOME);
    worker.state = WORKER_RUNNING;
    diagnostics_show(NULL);
    lv_event_send(home, LV_EVENT_CLICKED, NULL);
    assert(lv_scr_act() == main_screen);
    assert(strcmp(lv_label_get_text(status_label), "Wait for current job.") == 0);
    worker.state = WORKER_IDLE;
    lv_event_send(home, LV_EVENT_CLICKED, NULL);
    assert(lv_scr_act() == parking_screen);
}

static void test_manual_only_and_device(void)
{
    soundbox_app_config_t config = {NULL, 0, true, false, diagnostics_show, NULL};
    soundbox_app_destroy(parking_app);
    parking_app = soundbox_app_create(parking_screen, &config);
    assert(parking_app);
    layout();
    assert(find_button("Input Nominal"));
    click("Input Nominal");
    click("1"); click("0"); click("0"); click("0"); click("Enter");
    assert(soundbox_app_page(parking_app) == SOUNDBOX_PAYMENT);
    assert(find_label(parking_screen, "Lainnya"));
    assert(soundbox_app_key(parking_app, LV_KEY_ESC));
    assert(soundbox_app_page(parking_app) == SOUNDBOX_AMOUNT);
    click("1"); click("0"); click("0"); click("0"); click("Enter");
    click("QRIS MPM");
    assert(find_button("Simulasi OK") == NULL);
    assert(find_label(parking_screen, "Pratinjau: layanan pembayaran belum aktif."));
    assert(soundbox_app_payment_complete(parking_app, soundbox_app_token(parking_app),
        true, "Test completion only", lv_tick_get()));
    click("Cetak Struk");
    advance(60000);
    assert(soundbox_app_page(parking_app) == SOUNDBOX_RESULT);
    assert(find_label(parking_screen, "Data struk kurang:\nID tidak tersedia."));
    assert(find_label(parking_screen, "Pembayaran berhasil!"));
    click("Home");
}

static soundbox_ui_event_t last_event;
static void capture_event(soundbox_ui_t *ui, const soundbox_ui_event_t *event, void *data)
{
    (void)ui;
    (void)data;
    last_event = *event;
}

static void exact_bounds(lv_obj_t *obj, int x, int y, int width, int height)
{
    lv_area_t area;
    if (!obj)
        fprintf(stderr, "Missing mapped object at (%d,%d,%d,%d)\n", x, y, width, height);
    assert(obj);
    lv_obj_get_coords(obj, &area);
    assert(area.x1 == x && area.y1 == y);
    assert(lv_area_get_width(&area) == width && lv_area_get_height(&area) == height);
}

static lv_obj_t *find_type(lv_obj_t *obj, const lv_obj_class_t *type)
{
    uint32_t i;
    if (lv_obj_check_type(obj, type))
        return obj;
    for (i = 0; i < lv_obj_get_child_cnt(obj); ++i) {
        lv_obj_t *found = find_type(lv_obj_get_child(obj, i), type);
        if (found)
            return found;
    }
    return NULL;
}

static void test_dq72_layout(void)
{
    static const soundbox_ui_vehicle_t vehicles[] = {
        {1, "Motor", 2000}, {2, "Mobil", 3000}, {3, "Sepeda", 1000}, {5, "Bus", 10000}
    };
    soundbox_ui_config_t config = {capture_event, NULL, false};
    soundbox_ui_t *ui;
    lv_obj_t *old_screen;
    bool reference_size = lv_obj_get_width(parking_screen) == 212;
    size_t count;
    soundbox_app_destroy(parking_app);
    parking_app = NULL;
    ui = soundbox_ui_create(parking_screen, &config);
    assert(ui);
    assert(spacemonorbold20.line_height == 40 && spacemonoreg18.line_height == 36);
    assert(spacemonorbold8.line_height == 16 && spacemonoreg7.line_height == 13);
    for (count = 1; count <= 4; ++count) {
        assert(soundbox_ui_show_home(ui, "MERCHANT", "NMID uji", vehicles, count, false));
        lv_obj_update_layout(parking_screen);
        if (reference_size) {
            lv_obj_t *motor = find_button("Motor");
            exact_bounds(find_image(parking_screen, &soundbox_img_gearblack), 10, 27, 22, 22);
            exact_bounds(find_image(parking_screen, &soundbox_img_netzme_name), 47, 22, 119, 25);
            exact_bounds(find_image(parking_screen, &soundbox_img_history), 180, 27, 22, 22);
            exact_bounds(motor, 3, 110, count <= 2 ? 206 : 101, count == 1 ? 188 : 92);
            if (count >= 3) {
                exact_bounds(find_image(motor, &soundbox_img_motor), 33, 116, 40, 40);
                exact_bounds(find_label(motor, "Rp. 2.000"), 8, 164, 91, 16);
                exact_bounds(find_label(motor, "Motor"), 8, 182, 91, 13);
                exact_bounds(find_button("Sepeda"), count == 3 ? 55 : 3, 206, 101, 92);
            }
        }
    }
    capture("dq72-reference-home");
    assert(soundbox_ui_show_payment(ui, "Motor", 2000));
    lv_obj_update_layout(parking_screen);
    if (reference_size) {
        lv_obj_t *mpm = find_button("QRIS MPM");
        exact_bounds(find_button("QRIS MPM"), 6, 120, 100, 75);
        exact_bounds(find_button("QRIS Tap"), 106, 120, 100, 75);
        exact_bounds(find_button("QRIS CPM"), 56, 200, 100, 75);
        exact_bounds(find_image(find_button("QRIS MPM"), &soundbox_img_qris_white), 23, 135, 67, 25);
        exact_bounds(find_image(find_button("QRIS Tap"), &soundbox_img_qris_tap_white), 123, 138, 67, 18);
        exact_bounds(find_image(parking_screen, &soundbox_img_netzme_small), 153, 298, 49, 10);
        assert(lv_obj_get_y(find_label(parking_screen, "Motor")) == 50);
        assert(lv_obj_get_y(find_label(parking_screen, "Rp. 2.000")) == 75);
        exact_bounds(lv_obj_get_child(mpm, 0), 9, 120, 95, 75);
        assert(!lv_obj_has_flag(lv_obj_get_child(mpm, 0), LV_OBJ_FLAG_CLICKABLE));
        assert(lv_obj_get_style_text_font(find_label(parking_screen, "Motor"), 0) ==
            &spacemonorbold20);
        assert(lv_obj_get_style_bg_color(lv_obj_get_parent(lv_obj_get_parent(mpm)), 0).full ==
            lv_color_hex(0xffffff).full);
    }
    capture("dq72-reference-methods");
    assert(soundbox_ui_show_payment(ui, "Mobil", 10000000));
    lv_obj_update_layout(parking_screen);
    {
        lv_obj_t *amount = find_label(parking_screen, "Rp. 10.000.000");
        lv_area_t a, b;
        assert(amount);
        lv_obj_get_coords(amount, &a);
        lv_obj_get_coords(find_button("QRIS MPM"), &b);
        assert(a.y2 < b.y1);
        inside(amount, parking_screen);
    }
    assert(soundbox_ui_show_waiting(ui, SOUNDBOX_UI_TAP, 2000));
    lv_obj_update_layout(parking_screen);
    if (reference_size) {
        exact_bounds(find_image(parking_screen, &soundbox_img_wait_nfc), 36, 51, 140, 76);
        exact_bounds(find_image(parking_screen, &soundbox_img_batalkan), 117, 15, 90, 22);
        assert(lv_obj_get_y(find_label(parking_screen, "Nominal Pembayaran")) == 135);
    }
    capture("dq72-reference-tap");
    assert(soundbox_ui_show_waiting(ui, SOUNDBOX_UI_CPM, 2000));
    lv_obj_update_layout(parking_screen);
    if (reference_size)
        exact_bounds(find_button("Pindai"), 31, 109, 150, 100);
    capture("dq72-reference-cpm");
    assert(soundbox_ui_show_scanner(ui, 2000));
    lv_obj_update_layout(parking_screen);
    if (reference_size)
        exact_bounds(lv_obj_get_parent(find_label(parking_screen,
            "Pratinjau kamera\nbelum terhubung")), 8, 50, 196, 150);
    capture("dq72-reference-scanner");
    assert(soundbox_ui_show_payment_qr(ui, 2000, "test-qr", true));
    lv_obj_update_layout(parking_screen);
    if (reference_size) {
        lv_area_t qr_area, instruction_area;
        exact_bounds(find_type(parking_screen, &lv_qrcode_class), 26, 100, 160, 160);
        lv_obj_get_coords(find_type(parking_screen, &lv_qrcode_class), &qr_area);
        lv_obj_get_coords(find_label(parking_screen, "Selesaikan pembayaran dalam"), &instruction_area);
        assert(qr_area.y2 < instruction_area.y1);
    }
    assert(find_label(parking_screen, "05:00"));
    assert(soundbox_ui_set_countdown(ui, 299));
    assert(find_label(parking_screen, "04:59"));
    capture("dq72-reference-qr");
    assert(soundbox_ui_show_result(ui, true, 2000, "Test completion only"));
    lv_obj_update_layout(parking_screen);
    if (reference_size) {
        exact_bounds(find_image(parking_screen, &soundbox_img_success_payment), 34, 15, 144, 136);
        exact_bounds(lv_obj_get_parent(find_button("Home")), 0, 223, 212, 95);
        assert(lv_obj_get_y(find_label(parking_screen, "Pembayaran berhasil!")) == 151);
    }
    capture("dq72-reference-success");
    {
        static const soundbox_ui_history_item_t history[] = {
            {"Issuer", "2026-11-02", "Rp 2.000"}
        };
        static const soundbox_ui_detail_row_t rows[] = {
            {"RRN", "123456789012"}, {"Nama Merchant", "Merchant dengan nama panjang"}
        };
        assert(soundbox_ui_show_history(ui, 1, 2000, history, 1, "Riwayat dari layanan."));
        lv_obj_update_layout(parking_screen);
        if (reference_size) {
            exact_bounds(lv_obj_get_parent(find_label(parking_screen, "Riwayat Transaksi")),
                0, 16, 212, 30);
            assert(lv_obj_get_y(find_label(parking_screen, "Transaksi Hari Ini")) == 10);
            exact_bounds(lv_obj_get_parent(find_label(parking_screen, "Transaksi Hari Ini")),
                0, 46, 212, 262);
        }
        capture("dq72-reference-history");
        assert(soundbox_ui_show_detail(ui, "Issuer", rows, 2));
        lv_obj_update_layout(parking_screen);
        if (reference_size)
            exact_bounds(lv_obj_get_parent(find_label(parking_screen, "Issuer")),
                0, 51, 212, 262);
        capture("dq72-reference-detail");
    }
    soundbox_ui_destroy(ui);
    old_screen = parking_screen;
    assert(create_parking_screen());
    lv_obj_del(old_screen);
}

static void test_large_menu(void)
{
    static const char *const long_name =
        "Merchant dengan nama panjang untuk pengujian tampilan parkir";
    soundbox_ui_vehicle_t vehicles[SOUNDBOX_UI_MAX_ITEMS];
    soundbox_ui_config_t config = {capture_event, NULL, false};
    soundbox_ui_t *ui;
    lv_obj_t *button, *parent;
    size_t i;
    soundbox_app_destroy(parking_app);
    parking_app = NULL;
    ui = soundbox_ui_create(parking_screen, &config);
    assert(ui);
    {
        static const soundbox_ui_vehicle_t reference_vehicles[] = {
            {1, "Motor", 2000}, {2, "Mobil", 3000},
            {3, "Sepeda", 1000}, {5, "Bus", 10000}
        };
        lv_obj_t *card;
        assert(soundbox_ui_show_home(ui, "Merchant pratinjau", "NMID uji",
            reference_vehicles, 4, false));
        layout();
        card = find_button("Motor");
        assert(lv_obj_check_type(lv_obj_get_child(card, 0), &lv_img_class));
        assert(strcmp(lv_label_get_text(lv_obj_get_child(card, 1)), "Rp. 2.000") == 0);
        assert(strcmp(lv_label_get_text(lv_obj_get_child(card, 2)), "Motor") == 0);
        assert(lv_img_get_zoom(find_image(card, &soundbox_img_motor)) == 256);
        capture("dq72-home");
    }
    for (i = 0; i < SOUNDBOX_UI_MAX_ITEMS; ++i)
        vehicles[i] = (soundbox_ui_vehicle_t){(uint32_t)i, "Kendaraan", 10000000};
    assert(soundbox_ui_show_home(ui, long_name, "NMID-PRATINJAU",
        vehicles, SOUNDBOX_UI_MAX_ITEMS, true));
    lv_obj_update_layout(parking_screen);
    assert(find_label(parking_screen, long_name));
    button = find_button("Kendaraan");
    for (i = 0; i < lv_obj_get_child_cnt(button); ++i)
        inside(lv_obj_get_child(button, (int32_t)i), button);
    button = find_button("Input Nominal");
    assert(button);
    lv_obj_scroll_to_view_recursive(button, LV_ANIM_OFF);
    lv_obj_update_layout(parking_screen);
    inside(button, parking_screen);
    lv_event_send(button, LV_EVENT_CLICKED, NULL);
    assert(last_event.action == SOUNDBOX_UI_MANUAL);
    assert(!soundbox_ui_show_home(ui, long_name, "", vehicles,
        SOUNDBOX_UI_MAX_ITEMS + 1U, true));
    assert(soundbox_ui_show_result(ui, true, 10000000,
        "SIMULASI dengan pesan panjang, bukan pembayaran yang sebenarnya."));
    layout();
    assert(find_label(parking_screen, "Rp 10.000.000"));
    result_text_visible(parking_screen);
    soundbox_ui_destroy(ui);

    parent = lv_obj_create(parking_screen);
    ui = soundbox_ui_create(parent, &config);
    assert(ui);
    lv_obj_del(parent);
    assert(!soundbox_ui_key(ui, LV_KEY_ENTER));
    soundbox_ui_destroy(ui);
}

static void test_persisted_view(void)
{
    static const soundbox_ui_vehicle_t vehicles[] = {
        {1, "Motor", 2000}, {2, "Mobil", 5000}, {3, "Sepeda", 1000},
        {4, "Elf", 10000}, {5, "Bus", 15000}
    };
    soundbox_app_config_t config = {vehicles, 5, true, true, diagnostics_show, NULL};
    struct soundbox_record record = {0};
    unsigned i;
    parking_app = soundbox_app_create(parking_screen, &config);
    assert(parking_app);
    strcpy(record.settings.merchant_name, "Merchant tersimpan");
    strcpy(record.settings.merchant_nmid, "NMID-01");
    for (i = 0; i < SOUNDBOX_PRICE_COUNT; ++i)
        record.settings.prices[i] = 3000;
    record.count = UINT64_C(4294967296);
    record.total = UINT64_C(18446744073709551615);
    record.date = 20261102;
    assert(soundbox_app_set_record(parking_app, &record, SB_STORE_OK));
    assert(find_label(parking_screen, "Merchant tersimpan"));
    assert(find_label(parking_screen, "NMID-01"));
    assert(find_label(parking_screen, "Rp. 3.000"));
    click("Motor");
    record.settings.prices[SB_MOTOR] = 4000;
    assert(soundbox_app_set_record(parking_app, &record, SB_STORE_OK));
    assert(soundbox_app_page(parking_app) == SOUNDBOX_PAYMENT);
    assert(find_label(parking_screen, "Rp. 3.000"));
    go_home();
    assert(find_button("Motor") && find_label(parking_screen, "Rp. 4.000"));
    assert(find_image(find_button("Sepeda"), &soundbox_img_sepeda));
    assert(find_image(find_button("Elf"), &soundbox_img_elf));
    assert(find_image(find_button("Bus"), &soundbox_img_bus));
    layout();
    capture("persisted-home");
    click("Input Nominal");
    click("1"); click("0"); click("0"); click("0"); click("Enter");
    click("Sepeda");
    assert(find_label(parking_screen, "Sepeda") && find_label(parking_screen, "Rp. 1.000"));
    go_home();
    assert(soundbox_app_key(parking_app, LV_KEY_UP));
    assert(soundbox_app_key(parking_app, LV_KEY_ENTER));
    assert(soundbox_app_page(parking_app) == SOUNDBOX_HISTORY);
    assert(find_label(parking_screen, "4294967296"));
    assert(find_label(parking_screen, "Rp 18.446.744.073.709.551.615"));
    assert(soundbox_app_set_record(parking_app, NULL, SB_STORE_IO));
    assert(find_label(parking_screen, "Penyimpanan gagal. Total tidak terjamin."));
}

static void assert_menu(const int enabled[6])
{
    static const char *const names[] = {
        "Motor", "Mobil", "Sepeda", "Elf", "Bus", "Input Nominal"
    };
    size_t i;
    for (i = 0; i < 6; ++i)
        assert((find_button(names[i]) != NULL) == (enabled[i] != 0));
}

static struct soundbox_backend_result backend_result;
static int mock_backend_response;
static struct printer_report receipt_report;
static unsigned receipt_send_calls;
static unsigned receipt_backend_calls;
struct printer_report __wrap_printer_send(struct printer_connection *printer,
    const uint8_t *payload, size_t length)
{
    assert(printer == &connection && payload && length);
    if (worker.parking_job.test_receipt) {
        assert(length < PRINT_JOB_BUFFER_SIZE);
        {
            char text[PRINT_JOB_BUFFER_SIZE];
            memcpy(text, payload, length);
            text[length] = 0;
            /* ESC/POS initialization includes NUL bytes before the text. */
            assert(strstr(text + 11, "STRUK UJI COBA"));
            assert(strstr(text + 11, "BUKAN STRUK RESMI"));
            assert(strstr(text + 11, "TOTAL BAYAR: Rp 7.000\n"));
        }
    }
    ++receipt_send_calls;
    return receipt_report;
}
void __real_soundbox_backend_execute(const struct soundbox_backend_job *,
    struct soundbox_backend_result *);
void __wrap_soundbox_backend_execute(const struct soundbox_backend_job *job,
    struct soundbox_backend_result *result)
{
    if (job->kind == SB_JOB_RECEIPT)
        ++receipt_backend_calls;
    if (!mock_backend_response) {
        __real_soundbox_backend_execute(job, result);
        return;
    }
    *result = backend_result;
    result->kind = job->kind;
    result->token = job->token;
    result->attempt = job->attempt;
}

static void complete_backend(enum soundbox_backend_status status)
{
    backend_result.kind = worker.parking_job.kind;
    backend_result.token = worker.parking_job.token;
    backend_result.status = status;
    worker.state = WORKER_IDLE;
    assert(soundbox_app_backend_complete(parking_app, &backend_result, lv_tick_get()));
}

static void test_live_backend(void)
{
    static const soundbox_ui_vehicle_t vehicles[] = {{SB_MOTOR + 1U, "Motor", 5000}};
    soundbox_app_config_t config = {vehicles, 1, false, false, diagnostics_show, NULL};
    struct soundbox_record record = {0};
    struct soundbox_event acknowledgement = {0};
    uint32_t old_token;
    lv_obj_t *old;
    soundbox_app_destroy(parking_app);
    parking_app = soundbox_app_create(parking_screen, &config);
    assert(parking_app);
    worker.state = WORKER_IDLE;
    assert(soundbox_app_set_backend(parking_app, parking_submit, NULL));
    strcpy(record.settings.merchant_nmid, "TEST-NMID");
    record.date = 20261102;
    for (old_token = 0; old_token < SOUNDBOX_PRICE_COUNT; ++old_token)
        record.settings.prices[old_token] = 5000;
    assert(soundbox_app_set_record(parking_app, &record, SB_STORE_OK));
    memset(&backend_result, 0, sizeof(backend_result));
    click("Motor");
    click("QRIS MPM");
    assert(worker.state == WORKER_QUEUED);
    assert(worker.parking_job.kind == SB_JOB_QR && worker.parking_job.type == 1);
    assert(worker.parking_job.amount == 5000 && !worker.parking_job.qr_content[0]);
    assert(!parking_submit(&worker.parking_job, NULL));
    acknowledgement.type = SB_EVENT_PAYMENT;
    acknowledgement.money = 2000;
    assert(!soundbox_app_acknowledge(parking_app, &acknowledgement));
    backend_result.attempt.id = 42;
    strcpy(backend_result.data.qr_content, "00020101021226650013ID.NETZME.WWW");
    complete_backend(SB_BACKEND_OK);
    assert(soundbox_app_waiting_payment(parking_app));
    layout();
    assert(find_label(parking_screen, "MQTT terputus.\nNotifikasi mungkin terlewat."));
    assert(find_label(parking_screen, "05:00"));
    advance(1000);
    assert(find_label(parking_screen, "04:59"));
    soundbox_app_mqtt_status(parking_app, true);
    assert(find_label(parking_screen, "Scan QR untuk membayar."));
    assert(find_label(parking_screen, "04:59"));
    assert(soundbox_app_acknowledge(parking_app, &acknowledgement));
    assert(worker.parking_job.kind == SB_JOB_ACCOUNT);
    assert(worker.parking_job.attempt.id == 42 && worker.parking_job.paid_amount == 2000);
    assert(!soundbox_app_acknowledge(parking_app, &acknowledgement));
    complete_backend(SB_BACKEND_STORAGE);
    assert(find_label(parking_screen, "Pembayaran berhasil!"));
    assert(find_label(parking_screen, "Total belum terjamin. Periksa riwayat."));
    advance(30000);
    assert(soundbox_app_page(parking_app) == SOUNDBOX_RESULT);
    assert(!soundbox_app_backend_complete(parking_app, &backend_result, lv_tick_get()));
    click("Cetak Struk");
    if (SOUNDBOX_TEST_RECEIPT) {
        assert(worker.state == WORKER_QUEUED && worker.parking_job.test_receipt);
        assert(!worker.parking_job.transaction_id[0] && !worker.parking_job.event_type[0]);
        complete_backend(SB_BACKEND_INVALID);
    } else {
        assert(find_label(parking_screen, "Data struk kurang:\nID tidak tersedia."));
    }
    assert(find_label(parking_screen, "Pembayaran berhasil!"));
    click("Home");
    click("Motor"); click("QRIS MPM");
    complete_backend(SB_BACKEND_NETWORK);
    assert(find_label(parking_screen, "Status belum diketahui"));
    advance(30000);
    assert(soundbox_app_page(parking_app) == SOUNDBOX_RESULT);
    click("Home");
    click("Motor"); click("QRIS MPM");
    old_token = worker.parking_job.token;
    assert(soundbox_app_key(parking_app, LV_KEY_ESC));
    worker.state = WORKER_IDLE;
    backend_result.token = old_token;
    assert(!soundbox_app_backend_complete(parking_app, &backend_result, lv_tick_get()));
    click("Home");
    click("Motor"); click("QRIS MPM");
    backend_result.attempt.id = 43;
    strcpy(backend_result.data.qr_content, "test-qr");
    complete_backend(SB_BACKEND_OK);
    advance(300000);
    assert(find_label(parking_screen, "Status belum diketahui"));
    click("Home");
    click("Motor"); click("QRIS Tap");
    assert(find_label(parking_screen, "Capture belum tersedia"));
    assert(worker.state == WORKER_IDLE);
    click("Home");
    click("Motor"); click("QRIS CPM");
    assert(find_label(parking_screen, "Capture belum tersedia"));
    click("Home");
    assert(soundbox_app_key(parking_app, LV_KEY_UP));
    assert(soundbox_app_key(parking_app, LV_KEY_ENTER));
    assert(worker.parking_job.kind == SB_JOB_HISTORY);
    memset(&backend_result, 0, sizeof(backend_result));
    backend_result.data.history.count = 1;
    strcpy(backend_result.data.history.entries[0].transaction_id, "T1");
    strcpy(backend_result.data.history.entries[0].event_type, "qr");
    strcpy(backend_result.data.history.entries[0].title, "Test issuer");
    strcpy(backend_result.data.history.entries[0].subtitle, "Test time");
    strcpy(backend_result.data.history.entries[0].amount, "Rp 5000");
    complete_backend(SB_BACKEND_OK);
    click("Test issuer");
    assert(worker.parking_job.kind == SB_JOB_DETAIL);
    assert(!strcmp(worker.parking_job.transaction_id, "T1"));
    assert(!strcmp(worker.parking_job.event_type, "qr"));
    memset(&backend_result, 0, sizeof(backend_result));
    backend_result.data.detail[0].label = "ID transaksi";
    strcpy(backend_result.data.detail[0].value, "T1");
    complete_backend(SB_BACKEND_OK);
    assert(find_label(parking_screen, "T1"));
    assert(soundbox_app_key(parking_app, LV_KEY_ESC));
    assert(find_button("Test issuer"));
    assert(soundbox_app_key(parking_app, LV_KEY_ESC));
    assert(soundbox_app_page(parking_app) == SOUNDBOX_HOME);
    assert(soundbox_app_key(parking_app, LV_KEY_UP));
    assert(soundbox_app_key(parking_app, LV_KEY_ENTER));
    complete_backend(SB_BACKEND_SERVER);
    assert(find_label(parking_screen, "Permintaan gagal"));
    click("Home");
    worker.state = WORKER_RUNNING;
    click("Motor"); click("QRIS MPM");
    assert(find_label(parking_screen, "Permintaan tidak dikirim"));
    worker.state = WORKER_IDLE;
    soundbox_app_destroy(parking_app);
    old = parking_screen;
    assert(create_parking_screen());
    lv_obj_del(old);
}

static void test_menu_selection(void)
{
    static const soundbox_ui_vehicle_t vehicles[] = {
        {1, "Motor", 2000}, {2, "Mobil", 5000}, {3, "Sepeda", 1000},
        {4, "Elf", 10000}, {5, "Bus", 15000}
    };
    const int built_menu[6] = {
        SOUNDBOX_MENU_MOTOR, SOUNDBOX_MENU_MOBIL, SOUNDBOX_MENU_SEPEDA,
        SOUNDBOX_MENU_ELF, SOUNDBOX_MENU_BUS, SOUNDBOX_MENU_MANUAL
    };
    struct soundbox_record record = {0};
    unsigned mask;
    size_t i;
    for (i = 0; i < SOUNDBOX_PRICE_COUNT; ++i)
        record.settings.prices[i] = 3000U + (uint32_t)i * 1000U;
    assert_menu(built_menu);
    assert(soundbox_app_set_record(parking_app, &record, SB_STORE_OK));
    assert_menu(built_menu);
    soundbox_app_destroy(parking_app);
    parking_app = NULL;
    {
        soundbox_app_config_t empty = {NULL, 0, false, true, NULL, NULL};
        assert(!soundbox_app_create(parking_screen, &empty));
    }
    for (mask = 1; mask < 64; ++mask) {
        soundbox_ui_vehicle_t selected[5];
        soundbox_app_config_t config = {selected, 0, (mask & 32U) != 0, true, NULL, NULL};
        int enabled[6];
        for (i = 0; i < 6; ++i) {
            enabled[i] = (mask & (1U << i)) != 0;
            if (i < 5 && enabled[i])
                selected[config.vehicle_count++] = vehicles[i];
        }
        parking_app = soundbox_app_create(parking_screen, &config);
        assert(parking_app);
        assert_menu(enabled);
        assert(soundbox_app_set_record(parking_app, &record, SB_STORE_OK));
        assert_menu(enabled);
        record.settings.prices[SB_MOTOR] += 1000;
        assert(soundbox_app_set_record(parking_app, &record, SB_STORE_OK));
        assert_menu(enabled);
        for (i = 0; i < config.vehicle_count; ++i) {
            char price[32];
            unsigned expected = record.settings.prices[selected[i].id - 1U];
            snprintf(price, sizeof(price), "Rp. %u.%03u", expected / 1000U, expected % 1000U);
            click(selected[i].name);
            assert(soundbox_app_page(parking_app) == SOUNDBOX_PAYMENT);
            assert(find_label(parking_screen, price));
            go_home();
        }
        if (config.manual) {
            click("Input Nominal");
            click("1"); click("0"); click("0"); click("0"); click("Enter");
            if (config.vehicle_count) {
                assert(soundbox_app_page(parking_app) == SOUNDBOX_VEHICLES);
                enabled[5] = 0;
                assert_menu(enabled);
                click(selected[0].name);
            } else {
                assert(soundbox_app_page(parking_app) == SOUNDBOX_PAYMENT);
                assert(find_label(parking_screen, "Lainnya"));
            }
            assert(find_label(parking_screen, "Rp. 1.000"));
            assert(soundbox_app_key(parking_app, LV_KEY_ESC));
            assert(soundbox_app_page(parking_app) ==
                (config.vehicle_count ? SOUNDBOX_VEHICLES : SOUNDBOX_AMOUNT));
        }
        soundbox_app_destroy(parking_app);
        parking_app = NULL;
    }
    {
        lv_obj_t *old = parking_screen;
        assert(create_parking_screen());
        lv_obj_del(old);
    }
}

static void finish_parking_job(void)
{
    assert(worker.state == WORKER_QUEUED);
    execute_parking();
    worker.last_action = ACTION_PARKING;
    worker.state = WORKER_DONE;
    update_status();
}

static void test_live_receipts(void)
{
    static const soundbox_ui_vehicle_t vehicles[] = {{SB_MOTOR + 1U, "Motor", 5000}};
    soundbox_app_config_t config = {vehicles, 1, false, false, diagnostics_show, NULL};
    struct soundbox_record record = {0};
    struct soundbox_event event = {0};
    struct soundbox_backend_result old_result;
    unsigned mode;
    lv_obj_t *old;
    soundbox_app_destroy(parking_app);
    parking_app = soundbox_app_create(parking_screen, &config);
    assert(parking_app && soundbox_app_set_backend(parking_app, parking_submit, NULL));
    record.date = 20261102;
    for (mode = 0; mode < SOUNDBOX_PRICE_COUNT; ++mode)
        record.settings.prices[mode] = 5000;
    event.type = SB_EVENT_PAYMENT;
    event.money = 7000;
    if (!SOUNDBOX_TEST_RECEIPT) {
        strcpy(event.transaction_id, "T001");
        strcpy(event.event_type, "qr");
    }
    for (mode = 0; mode < 6; ++mode) {
        unsigned sent_before = receipt_send_calls;
        unsigned backend_before = receipt_backend_calls;
        uint32_t token;
        worker.state = WORKER_IDLE;
        strcpy(record.settings.merchant_name, "Original merchant");
        strcpy(record.settings.merchant_nmid, "Original NMID");
        assert(soundbox_app_set_record(parking_app, &record, SB_STORE_OK));
        memset(&backend_result, 0, sizeof(backend_result));
        click("Motor"); click("QRIS MPM");
        backend_result.attempt.id = 42;
        strcpy(backend_result.data.qr_content, "test-qr");
        complete_backend(SB_BACKEND_OK);
        strcpy(record.settings.merchant_name, "Later merchant");
        strcpy(record.settings.merchant_nmid, "Later NMID");
        assert(soundbox_app_set_record(parking_app, &record, SB_STORE_OK));
        assert(soundbox_app_acknowledge(parking_app, &event));
        complete_backend(SB_BACKEND_OK);
        token = soundbox_app_token(parking_app);
        advance(2999);
        if (mode == 5)
            worker.state = WORKER_RUNNING;
        click("Cetak Struk");
        assert(find_label(parking_screen, "Pembayaran berhasil!"));
        if (mode == 5) {
            assert(find_label(parking_screen,
                "Layanan cetak sibuk /\ntidak tersedia."));
            worker.state = WORKER_IDLE;
        } else {
            assert(worker.state == WORKER_QUEUED);
            assert(worker.parking_job.kind == SB_JOB_RECEIPT);
            assert(worker.parking_job.test_receipt == (SOUNDBOX_TEST_RECEIPT != 0));
            assert(!strcmp(worker.parking_job.transaction_id, SOUNDBOX_TEST_RECEIPT ? "" : "T001"));
            assert(!strcmp(worker.parking_job.event_type, SOUNDBOX_TEST_RECEIPT ? "" : "qr"));
            assert(!strcmp(worker.parking_job.receipt.merchant_name, "Original merchant"));
            assert(!strcmp(worker.parking_job.receipt.nmid, "Original NMID"));
            assert(worker.parking_job.receipt.amount == 7000);
            assert(lv_obj_has_state(find_button("Cetak Struk"), LV_STATE_DISABLED));
            click("Cetak Struk");
            click("Home");
            assert(soundbox_app_key(parking_app, LV_KEY_ESC));
            advance(10000);
            assert(soundbox_app_page(parking_app) == SOUNDBOX_RESULT);
            memset(&backend_result, 0, sizeof(backend_result));
            backend_result.status = mode == 4 ? SB_BACKEND_INVALID : SB_BACKEND_OK;
            backend_result.data.receipt = worker.parking_job.receipt;
            backend_result.data.receipt.paid_at = 1700000000;
            strcpy(backend_result.data.receipt.issuer, "Actual issuer");
            strcpy(backend_result.data.receipt.rrn, mode == 3 ? "bad\033" : "000012345678");
            if (SOUNDBOX_TEST_RECEIPT && mode >= 3)
                worker.parking_job.receipt.amount = mode == 3 ? 0 : UINT64_MAX;
            receipt_report = (struct printer_report){PRINTER_SENT, 400, 1, 0};
            if (mode == 1)
                receipt_report = (struct printer_report){PRINTER_CONNECT_FAILED, 0, 0, -1};
            if (mode == 2)
                receipt_report = (struct printer_report){PRINTER_WRITE_FAILED, 0, 1, -1};
            mock_backend_response = 1;
            finish_parking_job();
            mock_backend_response = 0;
            assert(receipt_backend_calls == backend_before + (SOUNDBOX_TEST_RECEIPT ? 0 : 1));
            assert(receipt_send_calls == sent_before + (mode < 3));
            old_result = worker.parking_result;
            assert(!soundbox_app_backend_complete(parking_app, &old_result, lv_tick_get()));
            assert(find_label(parking_screen, "Pembayaran berhasil!"));
            if (mode == 0) {
                assert(find_label(parking_screen, "Struk telah dikirim ke printer"));
                advance(2999);
                assert(soundbox_app_page(parking_app) == SOUNDBOX_RESULT);
                advance(1);
                assert(soundbox_app_page(parking_app) == SOUNDBOX_HOME);
            } else {
                if (mode == 1)
                    assert(find_label(parking_screen, "Cetak gagal.\nPeriksa printer."));
                if (mode == 2)
                    assert(find_label(parking_screen,
                        "Mungkin tercetak.\nJangan ulangi."));
                if (mode == 3 || mode == 4)
                    assert(find_label(parking_screen,
                        "Data / format struk\ntidak valid / lengkap."));
                advance(30000);
                assert(soundbox_app_page(parking_app) == SOUNDBOX_RESULT);
            }
        }
        if (soundbox_app_page(parking_app) == SOUNDBOX_RESULT)
            click("Home");
        assert(soundbox_app_page(parking_app) == SOUNDBOX_HOME);
        assert(!soundbox_app_print_complete(parking_app, token,
            SOUNDBOX_PRINT_SENT, "Late", lv_tick_get()));
        assert(receipt_send_calls == sent_before + (mode < 3));
    }
    soundbox_app_destroy(parking_app);
    old = parking_screen;
    assert(create_parking_screen());
    lv_obj_del(old);
}

static void test_live_worker(void)
{
    static const soundbox_ui_vehicle_t vehicles[] = {{SB_MOTOR + 1U, "Motor", 2000}};
    soundbox_app_config_t config = {vehicles, 1, false, false, diagnostics_show, NULL};
    soundbox_app_destroy(parking_app);
    parking_app = soundbox_app_create(parking_screen, &config);
    assert(parking_app && soundbox_app_set_backend(parking_app, parking_submit, NULL));
    assert(soundbox_app_set_record(parking_app, &store.record, SB_STORE_OK));
    mock_mqtt_connected = mqtt_available = mock_backend_response = 1;
    memset(&backend_result, 0, sizeof(backend_result));
    backend_result.status = SB_BACKEND_OK;
    backend_result.submission_started = 1;
    strcpy(backend_result.data.qr_content, "test-qr");
    click("Motor"); click("QRIS MPM");
    finish_parking_job();
    assert(store.record.pending && soundbox_app_waiting_payment(parking_app));
    assert(mock_watching);
    payment_event.type = SB_EVENT_PAYMENT;
    payment_event.money = 7000;
    payment_pending = 1;
    update_status();
    assert(worker.parking_job.kind == SB_JOB_ACCOUNT && !mock_watching);
    finish_parking_job();
    assert(find_label(parking_screen, "Pembayaran berhasil!"));
    assert(!store.record.pending && store.record.count == 2 && store.record.total == 9000);
    payment_pending = 1;
    update_status();
    assert(store.record.count == 2);
    payment_pending = 0;
    if (SOUNDBOX_TEST_RECEIPT) {
        unsigned sent_before = receipt_send_calls;
        unsigned backend_before = receipt_backend_calls;
        uint64_t sequence = store.record.sequence;
        click("Cetak Struk");
        assert(worker.parking_job.test_receipt && worker.parking_job.receipt.amount == 7000);
        assert(!worker.parking_job.transaction_id[0]);
        receipt_report = (struct printer_report){PRINTER_SENT, 200, 1, 0};
        finish_parking_job();
        assert(receipt_send_calls == sent_before + 1U);
        assert(receipt_backend_calls == backend_before);
        assert(find_label(parking_screen, "Struk telah dikirim ke printer"));
        assert(store.record.sequence == sequence &&
            store.record.count == 2 && store.record.total == 9000);
    }
    click("Home");
    backend_result.status = SB_BACKEND_NETWORK;
    click("Motor"); click("QRIS MPM");
    finish_parking_job();
    assert(store.record.pending && find_label(parking_screen, "Status belum diketahui"));
    click("Home");
    click("Motor"); click("QRIS MPM");
    finish_parking_job();
    assert(worker.parking_result.status == SB_BACKEND_STORAGE && store.record.pending);
    assert(soundbox_store_cancel(&store, store.record.attempt.id) == SB_STORE_OK);
    publish_storage(SB_STORE_OK);
    update_status();
    click("Home");
    backend_result.status = SB_BACKEND_DECLINED;
    click("Motor"); click("QRIS MPM");
    finish_parking_job();
    assert(!store.record.pending && !worker.parking_result.attempt.id);
    assert(find_label(parking_screen, "Permintaan gagal"));
    mock_mqtt_connected = mqtt_available = mock_backend_response = 0;
}

static void test_storage_worker(void)
{
    char temporary[] = "/tmp/printerdemo-storage-XXXXXX";
    char *directory = mkdtemp(temporary);
    uint64_t sequence;
    assert(directory && app_file_set_workdir(directory) == 0);
    assert(create_parking_screen());
    worker.state = WORKER_IDLE;
    worker.mqtt_initialized = 1;
    assert(soundbox_store_open(&store, INT64_C(1793577600)) >= 0);
    publish_storage(SB_STORE_OK);
    update_status();
    assert(soundbox_event_parse((const unsigned char *)
        "{\"broadcast_type\":4,\"merchant_name\":\"Worker merchant\",\"merchant_name_l2\":\"Utara\",\"merchant_nmid\":\"M1\"}",
        strlen("{\"broadcast_type\":4,\"merchant_name\":\"Worker merchant\",\"merchant_name_l2\":\"Utara\",\"merchant_nmid\":\"M1\"}"),
        &update_event) == 0);
    update_pending = 1;
    service_storage();
    update_status();
    assert(!update_pending && find_label(parking_screen, "Worker merchant\nUtara"));
    assert(find_label(parking_screen, "M1"));
    sequence = store.record.sequence;
    click("Motor");
    click("QRIS MPM");
    click("Simulasi OK");
    click("Cetak Struk");
    advance(500);
    advance(3000);
    assert(store.record.sequence == sequence && store.record.count == 0);
    publish_storage(SB_STORE_IO);
    update_status();
    worker.storage_check = 1;
    service_storage();
    update_status();
    assert(find_label(parking_screen, "SIMULASI | Penyimpanan gagal"));
    soundbox_app_updates_lost(parking_app);
    assert(soundbox_app_set_record(parking_app, &store.record, SB_STORE_OK));
    assert(find_label(parking_screen, "SIMULASI | Pembaruan MQTT hilang"));
    assert(soundbox_store_rollover(&store, (int64_t)time(NULL)) >= 0);
    memset(&worker.parking_job, 0, sizeof(worker.parking_job));
    worker.parking_job.kind = SB_JOB_ACCOUNT;
    assert(soundbox_store_begin(&store, &worker.parking_job.attempt) == SB_STORE_OK);
    worker.parking_job.paid_amount = 2000;
    execute_parking();
    assert(worker.parking_result.status == SB_BACKEND_OK);
    assert(store.record.count == 1 && store.record.total == 2000);
    execute_parking();
    assert(worker.parking_result.status == SB_BACKEND_OK);
    assert(store.record.count == 1 && store.record.total == 2000);
    memset(&worker.parking_job, 0, sizeof(worker.parking_job));
    worker.parking_job.kind = SB_JOB_QR;
    worker.parking_job.type = 1;
    worker.parking_job.amount = 5000;
    worker.parking_job.attempt.merchant_generation = store.record.merchant_generation;
    worker.parking_job.attempt.date = store.record.date;
    strcpy(worker.parking_job.attempt.nmid, store.record.settings.merchant_nmid);
    sequence = store.record.next_attempt;
    mqtt_available = 1;
    execute_parking();
    assert(worker.parking_result.status == SB_BACKEND_UNAVAILABLE);
    assert(store.record.next_attempt == sequence && !store.record.pending);
    mock_mqtt_connected = 1;
    execute_parking();
    assert(worker.parking_result.status == SB_BACKEND_NETWORK);
    assert(store.record.next_attempt == sequence + 1U && !store.record.pending);
    assert(!worker.parking_result.attempt.id && !worker.parking_result.submission_started);
    mock_mqtt_connected = mqtt_available = 0;
    test_live_worker();
    soundbox_app_destroy(parking_app);
    parking_app = NULL;
    lv_scr_load(main_screen);
    lv_obj_del(parking_screen);
    parking_screen = NULL;
    assert(app_file_remove("data/setting.ini") == 0);
    assert(app_file_remove("data/dtotal.ini") == 0);
    assert(app_file_rmappdir("data") == 0);
    assert(rmdir(directory) == 0);
    assert(app_file_set_workdir("") == 0);
}

int main(void)
{
    static lv_color_t pixels[480 * 10];
    static lv_disp_draw_buf_t buffer;
    static lv_disp_drv_t driver;
    static const int sizes[][2] = {{212,318}, {240,320}, {320,480}, {480,854}};
    lv_disp_t *display;
    const bool full_menu = SOUNDBOX_MENU_MOTOR && SOUNDBOX_MENU_MOBIL &&
        SOUNDBOX_MENU_SEPEDA && SOUNDBOX_MENU_ELF && SOUNDBOX_MENU_BUS &&
        SOUNDBOX_MENU_MANUAL;
    size_t i;
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
    assert(parking_key(KEY0) == '0' && parking_key(KEY9) == '9');
    assert(parking_key(KEY_ENTER) == LV_KEY_ENTER);
    assert(parking_key(KEY_BACKSPACE) == LV_KEY_BACKSPACE);
    assert(parking_key(KEY_CLEAR) == LV_KEY_DEL);
    assert(parking_key(KEY_UP) == LV_KEY_UP && parking_key(KEY_DOWN) == LV_KEY_DOWN);
    assert(parking_key(KEY_CANCEL) == LV_KEY_ESC && parking_key(KEY_INVALID) == 0);
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        lv_obj_t *old = lv_scr_act();
        driver.hor_res = sizes[i][0];
        driver.ver_res = sizes[i][1];
        lv_disp_drv_update(display, &driver);
        main_screen = create_screen();
        assert(create_parking_screen());
        lv_obj_del(old);
        layout();
        test_dq72_layout();
        test_menu_selection();
        test_live_backend();
        test_live_receipts();
        if (full_menu) {
            test_amounts();
            test_result();
            test_capture();
            test_navigation();
            test_manual_only_and_device();
            test_large_menu();
            test_persisted_view();
        }
        soundbox_app_destroy(parking_app);
        parking_app = NULL;
        lv_scr_load(main_screen);
        lv_obj_del(parking_screen);
        parking_screen = NULL;
        printf("Soundbox workflow passed at %dx%d\n", sizes[i][0], sizes[i][1]);
    }
    if (full_menu)
        test_storage_worker();
    else
        puts("Selected-menu build: ran menu coverage; full workflow requires all tiles enabled.");
    pthread_mutex_destroy(&worker.mutex);
    puts("Soundbox UI/controller tests passed.");
    return 0;
}
