#include "soundbox_ui.h"
#include "soundbox_images.h"
#include "soundbox_fonts.h"
#include "qr_request.h"

#include <stdio.h>
#include <string.h>

#ifndef SIM_BUILD
#include <lv_scanner.h>
#endif

#if LVGL_VERSION_MAJOR != 8 || LVGL_VERSION_MINOR != 3
#error "The soundbox UI requires LVGL 8.3.x"
#endif

#define BLUE 0x3597d3
#define WHITE 0xffffff
#define BLACK 0x111111
#define MIN_AMOUNT 1000U
#define MAX_AMOUNT 10000000U
#define MAX_BINDINGS (SOUNDBOX_UI_MAX_ITEMS + 16U)

typedef struct {
    soundbox_ui_t *ui;
    soundbox_ui_event_t event;
    bool digit;
} binding_t;

struct soundbox_ui {
    lv_obj_t *root;
    lv_obj_t *body;
    lv_obj_t *status_label;
    lv_obj_t *amount_label;
    lv_obj_t *error_label;
    lv_obj_t *print_button;
    lv_obj_t *home_button;
    lv_obj_t *result_status;
    lv_obj_t *payment_timer;
#ifndef SIM_BUILD
    lv_obj_t *scanner;
#endif
    lv_group_t *group;
    soundbox_ui_config_t config;
    binding_t bindings[MAX_BINDINGS];
    size_t binding_count;
    uint32_t amount;
    char status[96];
    char scan_value[QR_REQUEST_CONTENT_MAX + 1U];
    bool compact;
    bool result;
    bool ok;
};

static bool invalid(void)
{
    LV_LOG_ERROR("Invalid soundbox UI arguments or state");
    return false;
}

static const char *text(const char *s)
{
    return s ? s : "";
}

static void money(uint64_t value, char out[32])
{
    char digits[21];
    size_t i, n, used = 3;
    snprintf(digits, sizeof(digits), "%llu", (unsigned long long)value);
    n = strlen(digits);
    memcpy(out, "Rp ", 3);
    for (i = 0; i < n; ++i) {
        out[used++] = digits[i];
        if (i + 1U < n && (n - i - 1U) % 3U == 0)
            out[used++] = '.';
    }
    out[used] = '\0';
}

static void payment_money(uint32_t value, char out[32])
{
    char formatted[32];
    money(value, formatted);
    snprintf(out, 32, "Rp. %.27s", formatted + 3);
}

static void emit(soundbox_ui_t *ui, soundbox_ui_event_t event)
{
    ui->config.event(ui, &event, ui->config.user_data);
}

static bool amount_key(soundbox_ui_t *ui, uint32_t value)
{
    char formatted[32];
    soundbox_ui_event_t event = {SOUNDBOX_UI_AMOUNT, 0, ui->amount};
    if (value <= 9U) {
        if (ui->amount > (MAX_AMOUNT - value) / 10U) {
            lv_label_set_text(ui->error_label, "Maksimal Rp 10.000.000");
            return true;
        }
        ui->amount = ui->amount * 10U + value;
    } else if (value == 10U) {
        ui->amount /= 10U;
    } else if (value == 12U) {
        ui->amount = 0;
    } else {
        if (ui->amount < MIN_AMOUNT) {
            lv_label_set_text(ui->error_label, "Minimal Rp 1.000");
            return true;
        }
        emit(ui, event);
        return true;
    }
    money(ui->amount, formatted);
    lv_label_set_text(ui->amount_label, formatted);
    lv_label_set_text(ui->error_label, "");
    return true;
}

static void clicked(lv_event_t *event)
{
    binding_t *binding = lv_event_get_user_data(event);
    if (lv_obj_has_state(lv_event_get_target(event), LV_STATE_DISABLED))
        return;
    if (binding->digit)
        amount_key(binding->ui, binding->event.value);
    else
        emit(binding->ui, binding->event);
}

static lv_obj_t *object(soundbox_ui_t *ui, lv_obj_t *parent)
{
    lv_obj_t *obj;
    if (!ui->ok || !parent)
        return NULL;
    obj = lv_obj_create(parent);
    if (!obj) {
        LV_LOG_ERROR("Soundbox object allocation failed");
        ui->ok = false;
        return NULL;
    }
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *label(soundbox_ui_t *ui, lv_obj_t *parent,
    const char *caption, uint32_t color, bool heading)
{
    lv_obj_t *obj;
    if (!ui->ok || !parent)
        return NULL;
    obj = lv_label_create(parent);
    if (!obj) {
        LV_LOG_ERROR("Soundbox label allocation failed");
        ui->ok = false;
        return NULL;
    }
    lv_label_set_text(obj, text(caption));
    lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(obj, LV_PCT(100));
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(obj, heading ?
        &lv_font_montserrat_18 : &lv_font_montserrat_14, 0);
    return obj;
}

static lv_obj_t *flow(soundbox_ui_t *ui, lv_obj_t *parent,
    lv_flex_flow_t direction)
{
    lv_obj_t *obj = object(ui, parent);
    if (!obj)
        return NULL;
    lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(obj, direction);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(obj, 4, 0);
    lv_obj_set_style_pad_column(obj, 4, 0);
    return obj;
}

static lv_img_header_t image_header(const char *source)
{
    lv_img_header_t header = {0};
    if (lv_img_decoder_get_info(source, &header) != LV_RES_OK)
        LV_LOG_ERROR("Soundbox image missing: %s", source);
    return header;
}

static lv_obj_t *image(soundbox_ui_t *ui, lv_obj_t *parent,
    const char *source, lv_coord_t max_width, lv_coord_t max_height)
{
    lv_obj_t *obj;
    uint32_t zoom, height_zoom;
    lv_img_header_t header;
    if (!ui->ok || !parent)
        return NULL;
    header = image_header(source);
    obj = lv_img_create(parent);
    if (!obj) {
        LV_LOG_ERROR("Soundbox image allocation failed");
        ui->ok = false;
        return NULL;
    }
    zoom = 256U;
    if (header.w && header.h && (header.w > max_width || header.h > max_height)) {
        /* Reserve the transformed image's antialiasing border. */
        zoom = (uint32_t)(max_width - 5) * 256U / header.w;
        height_zoom = (uint32_t)(max_height - 5) * 256U / header.h;
        if (zoom > height_zoom)
            zoom = height_zoom;
    }
    lv_img_set_src(obj, source);
    lv_img_set_size_mode(obj, LV_IMG_SIZE_MODE_REAL);
    lv_img_set_zoom(obj, (uint16_t)zoom);
    lv_obj_refresh_self_size(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static lv_obj_t *button(soundbox_ui_t *ui, lv_obj_t *parent,
    const char *caption, lv_coord_t width, lv_coord_t height,
    soundbox_ui_action_t action, uint32_t value, uint32_t amount, bool digit)
{
    lv_obj_t *obj, *caption_obj;
    binding_t *binding;
    if (!ui->ok || !parent)
        return NULL;
    if (ui->binding_count >= MAX_BINDINGS) {
        LV_LOG_ERROR("Soundbox button binding capacity exceeded");
        ui->ok = false;
        return NULL;
    }
    obj = lv_btn_create(parent);
    if (!obj) {
        LV_LOG_ERROR("Soundbox button allocation failed");
        ui->ok = false;
        return NULL;
    }
    lv_obj_set_size(obj, width, height);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(obj, lv_color_hex(BLUE), 0);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 4, 0);
    caption_obj = caption && caption[0] ? label(ui, obj, caption, WHITE, false) : NULL;
    if (caption_obj)
        lv_obj_center(caption_obj);
    binding = &ui->bindings[ui->binding_count++];
    binding->ui = ui;
    binding->event = (soundbox_ui_event_t){action, value, amount};
    binding->digit = digit;
    lv_obj_add_event_cb(obj, clicked, LV_EVENT_CLICKED, binding);
    lv_group_add_obj(ui->group, obj);
    return obj;
}

static lv_obj_t *picture_button(soundbox_ui_t *ui, lv_obj_t *parent,
    const char *caption, const char *source,
    lv_coord_t width, lv_coord_t height, lv_coord_t image_height,
    soundbox_ui_action_t action, uint32_t value, uint32_t amount)
{
    lv_obj_t *obj = button(ui, parent, caption, width, height,
        action, value, amount, false);
    lv_obj_t *icon;
    if (!obj)
        return NULL;
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(obj, 2, 0);
    icon = image(ui, obj, source, image_header(source).w, image_height);
    if (icon)
        lv_obj_move_to_index(icon, 0);
    return obj;
}

static bool page(soundbox_ui_t *ui, const char *title, bool back, bool blue)
{
    lv_obj_t *header, *heading;
    if (!ui || !ui->root)
        return invalid();
#ifndef SIM_BUILD
    if (ui->scanner) {
        lv_scanner_stop(ui->scanner);
        ui->scanner = NULL;
    }
#endif
    lv_group_remove_all_objs(ui->group);
    lv_obj_clean(ui->root);
    ui->binding_count = 0;
    ui->amount_label = NULL;
    ui->error_label = NULL;
    ui->print_button = NULL;
    ui->home_button = NULL;
    ui->result_status = NULL;
    ui->payment_timer = NULL;
    ui->result = false;
    ui->ok = true;
    ui->compact = lv_obj_get_height(ui->root) < 400;
    lv_obj_set_flex_flow(ui->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(ui->root, lv_color_hex(blue ? BLUE : WHITE), 0);
    ui->status_label = label(ui, ui->root, ui->status,
        blue ? WHITE : BLACK, false);
    if (ui->status_label) {
        lv_obj_set_height(ui->status_label, 18);
        lv_label_set_long_mode(ui->status_label, LV_LABEL_LONG_DOT);
    }
    if (back || (title && title[0])) {
        header = flow(ui, ui->root, LV_FLEX_FLOW_ROW);
        if (header) {
            lv_obj_set_height(header, 48);
            lv_obj_set_style_bg_color(header, lv_color_hex(BLUE), 0);
            lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
            if (back)
                picture_button(ui, header, "", soundbox_img_btn_back,
                    48, 48, 21, SOUNDBOX_UI_BACK, 0, 0);
            heading = label(ui, header, title, blue ? WHITE : BLACK, false);
            if (heading) {
                lv_obj_set_width(heading, 0);
                lv_obj_set_flex_grow(heading, 1);
            }
        }
    }
    ui->body = flow(ui, ui->root, LV_FLEX_FLOW_COLUMN);
    if (ui->body) {
        lv_obj_set_height(ui->body, 0);
        lv_obj_set_flex_grow(ui->body, 1);
        lv_obj_set_style_pad_all(ui->body, ui->compact ? 4 : 12, 0);
        lv_obj_add_flag(ui->body, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(ui->body, LV_DIR_VER);
    }
    return ui->ok;
}

static lv_coord_t sx(soundbox_ui_t *ui, lv_coord_t value)
{
    return (int32_t)value * lv_obj_get_width(ui->root) / 212;
}

static lv_coord_t sy(soundbox_ui_t *ui, lv_coord_t value)
{
    return (int32_t)value * lv_obj_get_height(ui->root) / 318;
}

static void bounds(soundbox_ui_t *ui, lv_obj_t *obj,
    lv_coord_t x, lv_coord_t y, lv_coord_t width, lv_coord_t height)
{
    if (!obj)
        return;
    lv_obj_set_pos(obj, sx(ui, x), sy(ui, y));
    lv_obj_set_size(obj, sx(ui, width), sy(ui, height));
}

static lv_obj_t *placed_label(soundbox_ui_t *ui, lv_obj_t *parent,
    const char *caption, const lv_font_t *font, uint32_t color,
    lv_coord_t x, lv_coord_t y, lv_coord_t width)
{
    lv_obj_t *obj = label(ui, parent, caption, color, false);
    if (obj) {
        lv_obj_set_style_text_font(obj, font, 0);
        lv_obj_set_style_text_letter_space(obj, 0, 0);
        lv_obj_set_style_text_line_space(obj, 0, 0);
        lv_obj_set_width(obj, sx(ui, width));
        lv_obj_set_pos(obj, sx(ui, x), sy(ui, y));
    }
    return obj;
}

static lv_obj_t *placed_image(soundbox_ui_t *ui, lv_obj_t *parent,
    const char *source, lv_coord_t x, lv_coord_t y)
{
    lv_img_header_t header = image_header(source);
    lv_obj_t *obj = image(ui, parent, source, header.w, header.h);
    if (obj)
        lv_obj_set_pos(obj, sx(ui, x) + (sx(ui, header.w) - header.w) / 2,
            sy(ui, y) + (sy(ui, header.h) - header.h) / 2);
    return obj;
}

static lv_obj_t *flat_button(soundbox_ui_t *ui, lv_obj_t *parent,
    const char *caption, lv_coord_t x, lv_coord_t y,
    lv_coord_t width, lv_coord_t height, soundbox_ui_action_t action,
    uint32_t value, uint32_t amount, bool digit)
{
    lv_obj_t *obj = button(ui, parent, caption, sx(ui, width), sy(ui, height),
        action, value, amount, digit);
    if (obj) {
        lv_obj_set_pos(obj, sx(ui, x), sy(ui, y));
        lv_obj_set_style_pad_all(obj, 0, 0);
        lv_obj_set_style_border_width(obj, 0, 0);
        lv_obj_set_style_radius(obj, 10, 0);
        lv_obj_set_style_transform_width(obj, 0, LV_STATE_PRESSED);
        lv_obj_set_style_transform_height(obj, 0, LV_STATE_PRESSED);
        if (caption && caption[0])
            lv_obj_set_style_text_font(lv_obj_get_child(obj, 0), &spacemonorbold8, 0);
    }
    return obj;
}

static bool fixed_page(soundbox_ui_t *ui, const char *title, bool back, bool blue)
{
    lv_obj_t *header, *btn;
    if (!page(ui, "", false, blue))
        return false;
    lv_obj_set_layout(ui->root, 0);
    lv_obj_set_layout(ui->body, 0);
    lv_obj_set_flex_grow(ui->body, 0);
    lv_obj_set_style_pad_all(ui->body, 0, 0);
    bounds(ui, ui->body, 0, 0, 212, 318);
    lv_obj_clear_flag(ui->body, LV_OBJ_FLAG_SCROLLABLE);
    if (title && title[0]) {
        header = object(ui, ui->root);
        bounds(ui, header, 0, 16, 212, 30);
        if (header) {
            lv_obj_set_style_bg_color(header, lv_color_hex(BLUE), 0);
            lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
            placed_label(ui, header, title, &lv_font_montserrat_12,
                BLACK, 30, 9, 182);
            if (back) {
                btn = flat_button(ui, header, "", 0, 0, 30, 30,
                    SOUNDBOX_UI_BACK, 0, 0, false);
                if (btn) {
                    lv_obj_t *arrow = placed_label(ui, btn, LV_SYMBOL_LEFT,
                        &lv_font_montserrat_14, BLACK, 0, 0, 18);
                    if (arrow)
                        lv_obj_center(arrow);
                }
            }
        }
    }
    bounds(ui, ui->status_label, 0, 0, 212, 16);
    if (ui->status_label) {
        lv_obj_set_style_text_font(ui->status_label, &spacemonoreg6, 0);
        lv_obj_move_foreground(ui->status_label);
    }
    return ui->ok;
}

static void logo_footer(soundbox_ui_t *ui, lv_coord_t height)
{
    lv_obj_t *footer = object(ui, ui->body);
    bounds(ui, footer, 0, 318 - height, 212, height);
    if (footer) {
        lv_obj_t *logo = image(ui, footer, soundbox_img_netzme_small, 49, 10);
        lv_obj_set_style_bg_color(footer, lv_color_hex(WHITE), 0);
        lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, 0);
        if (logo)
            lv_obj_align(logo, LV_ALIGN_RIGHT_MID, -sx(ui, 10), 0);
    }
}

static void root_deleted(lv_event_t *event)
{
    soundbox_ui_t *ui = lv_event_get_user_data(event);
#ifndef SIM_BUILD
    if (ui->scanner) {
        lv_scanner_stop(ui->scanner);
        ui->scanner = NULL;
    }
#endif
    ui->root = NULL;
}

#ifndef SIM_BUILD
static void scanner_value_changed(lv_event_t *event)
{
    soundbox_ui_t *ui = lv_event_get_user_data(event);
    lv_obj_t *scanner = lv_event_get_target(event);
    const char *value = lv_scanner_get_value(scanner);
    size_t length = 0;

    lv_scanner_stop(scanner);
    ui->scanner = NULL;
    if (value) {
        while (length <= QR_REQUEST_CONTENT_MAX && value[length])
            ++length;
    }
    if (!value || !length || length > QR_REQUEST_CONTENT_MAX) {
        ui->scan_value[0] = '\0';
        LV_LOG_ERROR("Scanner returned empty or oversized QR content");
    } else {
        memcpy(ui->scan_value, value, length);
        ui->scan_value[length] = '\0';
    }
    emit(ui, (soundbox_ui_event_t){SOUNDBOX_UI_SCAN, 1, 0});
}
#endif

soundbox_ui_t *soundbox_ui_create(lv_obj_t *parent,
    const soundbox_ui_config_t *config)
{
    soundbox_ui_t *ui;
    if (!parent || !config || !config->event) {
        invalid();
        return NULL;
    }
    ui = lv_mem_alloc(sizeof(*ui));
    if (!ui) {
        LV_LOG_ERROR("Soundbox context allocation failed");
        return NULL;
    }
    memset(ui, 0, sizeof(*ui));
    ui->config = *config;
    ui->group = lv_group_create();
    ui->ok = true;
    ui->root = object(ui, parent);
    if (!ui->group || !ui->root) {
        LV_LOG_ERROR("Soundbox root or focus group allocation failed");
        soundbox_ui_destroy(ui);
        return NULL;
    }
    lv_obj_set_size(ui->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(ui->root, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(ui->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(ui->root, root_deleted, LV_EVENT_DELETE, ui);
    lv_obj_update_layout(parent);
    return ui;
}

void soundbox_ui_destroy(soundbox_ui_t *ui)
{
    if (!ui)
        return;
    if (ui->root)
        lv_obj_del(ui->root);
    if (ui->group)
        lv_group_del(ui->group);
    lv_mem_free(ui);
}

bool soundbox_ui_set_status(soundbox_ui_t *ui, const char *status)
{
    if (!ui || !ui->root || !status || strlen(status) >= sizeof(ui->status))
        return invalid();
    snprintf(ui->status, sizeof(ui->status), "%s", status);
    if (ui->status_label)
        lv_label_set_text(ui->status_label, status);
    return true;
}

bool soundbox_ui_key(soundbox_ui_t *ui, uint32_t key)
{
    lv_obj_t *focused;
    if (!ui || !ui->root)
        return invalid();
    if (key == LV_KEY_ESC) {
        soundbox_ui_event_t event = {SOUNDBOX_UI_BACK, 0, 0};
        emit(ui, event);
        return true;
    }
    if (ui->amount_label && ui->error_label) {
        if (key >= '0' && key <= '9')
            return amount_key(ui, key - '0');
        if (key == LV_KEY_BACKSPACE || key == LV_KEY_DEL || key == LV_KEY_ENTER)
            return amount_key(ui, key == LV_KEY_BACKSPACE ? 10 :
                key == LV_KEY_DEL ? 12 : 11);
    }
    if (key == LV_KEY_NEXT || key == LV_KEY_DOWN)
        lv_group_focus_next(ui->group);
    else if (key == LV_KEY_PREV || key == LV_KEY_UP)
        lv_group_focus_prev(ui->group);
    else if (key == LV_KEY_ENTER) {
        focused = lv_group_get_focused(ui->group);
        if (focused && !lv_obj_has_state(focused, LV_STATE_DISABLED))
            lv_event_send(focused, LV_EVENT_CLICKED, NULL);
    } else {
        return false;
    }
    return true;
}

static const char *vehicle_image(const char *name)
{
    static const char *const names[] = {"Motor", "Mobil", "Sepeda", "Elf", "Bus", "Truk / Bus"};
    static const char *const images[] = {
        soundbox_img_motor, soundbox_img_mobil, soundbox_img_sepeda,
        soundbox_img_elf, soundbox_img_bus, soundbox_img_bus
    };
    size_t i;
    for (i = 0; i < sizeof(images) / sizeof(images[0]); ++i)
        if (strcmp(text(name), names[i]) == 0)
            return images[i];
    return soundbox_img_calculator;
}

static lv_obj_t *vehicle_cards(soundbox_ui_t *ui,
    const soundbox_ui_vehicle_t *vehicles, size_t count, bool prices, bool manual,
    lv_coord_t top)
{
    size_t total = count + (manual ? 1U : 0U);
    size_t columns = total <= 2 ? 1 : 2;
    size_t rows = (total + columns - 1U) / columns;
    lv_coord_t width = (212 - 6 - ((int)columns - 1) * 4) / (int)columns;
    lv_coord_t height = rows ? (318 - top - (prices ? 20 : 34) -
        ((int)rows - 1) * 4) / (int)rows : 92;
    lv_obj_t *grid = object(ui, ui->body);
    size_t i;
    if (!prices && height > 75)
        height = 75;
    /* Keep dense menus readable instead of reproducing DQ72's negative icon y. */
    if (height < (prices ? 83 : 64))
        height = prices ? 83 : 64;
    bounds(ui, grid, 0, top, 212, rows ? (int)rows * (height + 4) - 4 : 0);
    for (i = 0; i < total; ++i) {
        char caption[40], formatted[32];
        bool is_manual = i == count;
        size_t in_row = total - i / columns * columns;
        lv_coord_t x, icon_y;
        lv_obj_t *obj, *icon, *price, *name;
        const char *caption_name = is_manual ? "Input Nominal" : vehicles[i].name;
        const char *source = is_manual ? soundbox_img_calculator :
            vehicle_image(vehicles[i].name);
        if (in_row > columns)
            in_row = columns;
        x = (212 - ((int)in_row * width + ((int)in_row - 1) * 4)) / 2 +
            (int)(i % columns) * (width + 4);
        money(is_manual ? 0 : vehicles[i].price, formatted);
        snprintf(caption, sizeof(caption), "Rp. %s", formatted + 3);
        obj = flat_button(ui, grid, "", x, (int)(i / columns) * (height + 4),
            width, height, is_manual ? SOUNDBOX_UI_MANUAL : SOUNDBOX_UI_VEHICLE,
            is_manual ? 0 : vehicles[i].id,
            prices && !is_manual ? vehicles[i].price : ui->amount, false);
        if (obj) {
            icon_y = (height - (prices ? 79 : 60)) / 2;
            icon = image(ui, obj, source, 40, 40);
            if (icon)
                lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, sy(ui, icon_y));
            price = placed_label(ui, obj, prices ? (is_manual ? caption_name : caption) :
                caption_name, &spacemonorbold8, WHITE, 5, icon_y + (prices ? 48 : 44),
                width - 10);
            if (price) {
                lv_label_set_long_mode(price, LV_LABEL_LONG_CLIP);
                lv_obj_set_height(price, 16);
            }
            if (prices) {
                name = placed_label(ui, obj, is_manual ? "" : caption_name,
                    &spacemonoreg7, WHITE, 5, icon_y + 66, width - 10);
                if (name) {
                    lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
                    lv_obj_set_height(name, 13);
                }
            }
        }
        if (i == 0 && obj)
            lv_group_focus_obj(obj);
    }
    return grid;
}

bool soundbox_ui_show_home(soundbox_ui_t *ui, const char *merchant,
    const char *nmid, const soundbox_ui_vehicle_t *vehicles,
    size_t count, bool manual)
{
    lv_obj_t *settings, *history, *merchant_label, *nmid_label;
    lv_coord_t top = 110;
    if (count > SOUNDBOX_UI_MAX_ITEMS || (count && !vehicles))
        return invalid();
    if (!fixed_page(ui, "", false, false))
        return false;
    lv_obj_add_flag(ui->body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ui->body, LV_DIR_VER);
    settings = flat_button(ui, ui->body, "", 10, 27, 22, 22,
        SOUNDBOX_UI_SETTINGS, 0, 0, false);
    if (settings) {
        lv_obj_set_style_bg_opa(settings, LV_OPA_TRANSP, 0);
        placed_image(ui, settings, soundbox_img_gearblack, 0, 0);
    }
    placed_image(ui, ui->body, soundbox_img_netzme_name, 47, 22);
    history = flat_button(ui, ui->body, "", 180, 27, 22, 22,
        SOUNDBOX_UI_HISTORY, 0, 0, false);
    if (history) {
        lv_obj_set_style_bg_opa(history, LV_OPA_TRANSP, 0);
        placed_image(ui, history, soundbox_img_history, 0, 0);
    }
    merchant_label = placed_label(ui, ui->body, merchant, &spacemonorbold11,
        BLACK, 3, strchr(text(merchant), '\n') ? 46 : 62, 206);
    lv_obj_update_layout(ui->body);
    if (merchant_label) {
        lv_coord_t bottom = lv_obj_get_y(merchant_label) + lv_obj_get_height(merchant_label);
        lv_coord_t nmid_y = bottom > sy(ui, 85) ? bottom + sy(ui, 2) : sy(ui, 85);
        nmid_label = placed_label(ui, ui->body, nmid, &spacemonoreg8, BLACK, 3, 85, 206);
        if (nmid_label) {
            lv_obj_set_y(nmid_label, nmid_y);
            lv_obj_update_layout(ui->body);
            bottom = nmid_y + lv_obj_get_height(nmid_label) + sy(ui, 9);
            if (bottom > sy(ui, top))
                top = (int32_t)bottom * 318 / lv_obj_get_height(ui->root);
        }
    }
    vehicle_cards(ui, vehicles, count, true, manual, top);
    return ui->ok;
}

bool soundbox_ui_show_amount(soundbox_ui_t *ui)
{
    static const char *const keys[] = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9",
        "0", LV_SYMBOL_BACKSPACE, "Enter"
    };
    static const uint32_t values[] = {1,2,3,4,5,6,7,8,9,0,10,11};
    lv_obj_t *line;
    size_t i;
    if (!fixed_page(ui, "Masukkan Nominal", true, true))
        return false;
    ui->amount = 0;
    ui->amount_label = placed_label(ui, ui->body, "Rp 0", &lv_font_montserrat_16,
        WHITE, 0, 76, 212);
    ui->error_label = placed_label(ui, ui->body, "", &spacemonoreg6, WHITE, 0, 60, 212);
    line = object(ui, ui->body);
    bounds(ui, line, 10, 100, 192, 1);
    if (line) {
        lv_obj_set_style_bg_color(line, lv_color_hex(WHITE), 0);
        lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    }
    for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        lv_obj_t *key = flat_button(ui, ui->body, keys[i], 7 + (i % 3) * 67,
            112 + (i / 3) * 44, 63, 40, SOUNDBOX_UI_AMOUNT, values[i], 0, true);
        if (key) {
            lv_obj_set_style_bg_color(key, lv_color_hex(i == 11 ? BLUE : WHITE), 0);
            lv_obj_set_style_text_font(lv_obj_get_child(key, 0), &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(lv_obj_get_child(key, 0),
                lv_color_hex(i == 11 ? WHITE : BLACK), 0);
            lv_obj_set_style_radius(key, 8, 0);
        }
    }
    logo_footer(ui, 30);
    return ui->ok;
}

bool soundbox_ui_show_vehicles(soundbox_ui_t *ui, uint32_t amount,
    const soundbox_ui_vehicle_t *vehicles, size_t count)
{
    char formatted[32];
    if (count > SOUNDBOX_UI_MAX_ITEMS || (count && !vehicles) ||
        amount < MIN_AMOUNT || amount > MAX_AMOUNT)
        return invalid();
    if (!fixed_page(ui, "Tipe Kendaraan", true, false))
        return false;
    ui->amount = amount;
    money(amount, formatted);
    placed_label(ui, ui->body, formatted, &spacemonoreg16, BLACK, 0, 60, 212);
    lv_obj_add_flag(ui->body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ui->body, LV_DIR_VER);
    vehicle_cards(ui, vehicles, count, false, false, 110);
    logo_footer(ui, 30);
    return ui->ok;
}

bool soundbox_ui_show_payment(soundbox_ui_t *ui, const char *vehicle,
    uint32_t amount)
{
    static const char *const captions[] = {"QRIS MPM", "QRIS Tap", "QRIS CPM"};
    char formatted[32];
    uint32_t method;
    if (!fixed_page(ui, "Pilih metode pembayaran", true, false))
        return false;
    placed_label(ui, ui->body, vehicle, &spacemonorbold20, BLACK, 0, 50, 212);
    payment_money(amount, formatted);
    placed_label(ui, ui->body, formatted,
        lv_txt_get_width(formatted, strlen(formatted), &spacemonoreg18, 0, LV_TEXT_FLAG_NONE) >
            sx(ui, 212) ? &spacemonoreg16 : &spacemonoreg18,
        BLACK, 0, 75, 212);
    for (method = SOUNDBOX_UI_MPM; method <= SOUNDBOX_UI_CPM; ++method) {
        lv_coord_t x = method == SOUNDBOX_UI_MPM ? 9 :
            method == SOUNDBOX_UI_TAP ? 109 : 59;
        lv_coord_t y = method == SOUNDBOX_UI_CPM ? 200 : 120;
        lv_obj_t *obj = flat_button(ui, ui->body, "", x - 3, y, 100, 75,
            SOUNDBOX_UI_PAYMENT, method, amount, false);
        if (obj) {
            lv_obj_t *box = object(ui, obj);
            if (method == SOUNDBOX_UI_CPM)
                lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, sy(ui, y));
            lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
            bounds(ui, box, 3, 0, 95, 75);
            if (box) {
                lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_set_style_bg_color(box, lv_color_hex(BLUE), 0);
                lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
                lv_obj_set_style_radius(box, 10, 0);
            }
            placed_image(ui, obj, method == SOUNDBOX_UI_TAP ?
                soundbox_img_qris_tap_white : soundbox_img_qris_white,
                17, method == SOUNDBOX_UI_TAP ? 18 : 15);
            placed_label(ui, obj, captions[method], &spacemonorbold8,
                WHITE, 3, 44, 95);
        }
        if (method == SOUNDBOX_UI_MPM && obj)
            lv_group_focus_obj(obj);
    }
    logo_footer(ui, 30);
    return ui->ok;
}

static void simulation_controls(soundbox_ui_t *ui)
{
    lv_obj_t *row;
    if (!ui->config.simulation)
        return;
    row = flow(ui, ui->body, LV_FLEX_FLOW_ROW);
    button(ui, row, "Simulasi OK", LV_PCT(48), 48,
        SOUNDBOX_UI_SIM_SUCCESS, 0, 0, false);
    button(ui, row, "Simulasi gagal", LV_PCT(48), 48,
        SOUNDBOX_UI_SIM_FAILURE, 0, 0, false);
}

static void fixed_simulation_controls(soundbox_ui_t *ui)
{
    if (!ui->config.simulation)
        return;
    flat_button(ui, ui->body, "Simulasi OK", 3, 264, 101, 24,
        SOUNDBOX_UI_SIM_SUCCESS, 0, 0, false);
    flat_button(ui, ui->body, "Simulasi gagal", 108, 264, 101, 24,
        SOUNDBOX_UI_SIM_FAILURE, 0, 0, false);
}

static void payment_brand(soundbox_ui_t *ui, bool tap)
{
    lv_obj_t *cancel;
    placed_image(ui, ui->body, tap ?
        soundbox_img_qris_tap_black : soundbox_img_qris_black, 5, 15);
    cancel = flat_button(ui, ui->body, "", 117, 15, 90, 22,
        SOUNDBOX_UI_BACK, 0, 0, false);
    if (cancel) {
        lv_obj_set_style_bg_opa(cancel, LV_OPA_TRANSP, 0);
        placed_image(ui, cancel, soundbox_img_batalkan, 0, 0);
    }
}

static void waiting_amount(soundbox_ui_t *ui, uint32_t amount)
{
    char formatted[32];
    money(amount, formatted);
    label(ui, ui->body, formatted, BLACK, true);
}

#ifdef SIM_BUILD
static void scanner_corners(soundbox_ui_t *ui, lv_obj_t *preview)
{
    static const lv_point_t horizontal[] = {{0, 0}, {16, 0}};
    static const lv_point_t vertical[] = {{0, 0}, {0, 16}};
    static const lv_align_t corners[] = {
        LV_ALIGN_TOP_LEFT, LV_ALIGN_TOP_RIGHT, LV_ALIGN_BOTTOM_LEFT, LV_ALIGN_BOTTOM_RIGHT
    };
    size_t i;
    if (!preview || !ui->ok)
        return;
    for (i = 0; i < 8; ++i) {
        lv_obj_t *line = lv_line_create(preview);
        if (!line) {
            LV_LOG_ERROR("Scanner corner allocation failed");
            ui->ok = false;
            return;
        }
        lv_line_set_points(line, i % 2 ? vertical : horizontal, 2);
        lv_obj_set_style_line_width(line, 3, 0);
        lv_obj_set_style_line_color(line, lv_color_hex(BLUE), 0);
        lv_obj_align(line, corners[i / 2], (i / 2) % 2 ? -6 : 6,
            i / 2 >= 2 ? -6 : 6);
    }
}
#endif

bool soundbox_ui_show_waiting(soundbox_ui_t *ui, soundbox_ui_method_t method,
    uint32_t amount)
{
    char formatted[32];
    lv_obj_t *scan;
    if (method < SOUNDBOX_UI_MPM || method > SOUNDBOX_UI_CPM)
        return invalid();
    if (!fixed_page(ui, method == SOUNDBOX_UI_CPM ? "QRIS CPM" : "",
        method == SOUNDBOX_UI_CPM, false))
        return false;
    payment_money(amount, formatted);
    if (method == SOUNDBOX_UI_CPM) {
        scan = flat_button(ui, ui->body, "Pindai", 31, 109, 150, 100,
            SOUNDBOX_UI_SCAN, 0, amount, false);
        if (scan) {
            lv_obj_set_style_text_font(lv_obj_get_child(scan, 0), &spacemonorbold16, 0);
            lv_group_focus_obj(scan);
        }
        placed_label(ui, ui->body, "Arahkan kamera ke kode QRIS",
            &spacemonoreg7, BLACK, 0, 259, 212);
        placed_label(ui, ui->body, formatted, &spacemonorbold14, BLACK, 0, 272, 212);
        logo_footer(ui, 20);
    } else if (method == SOUNDBOX_UI_TAP) {
        payment_brand(ui, true);
        placed_image(ui, ui->body, soundbox_img_wait_nfc, 36, 51);
        placed_label(ui, ui->body, "Nominal Pembayaran", &spacemonorbold12,
            BLACK, 0, 135, 212);
        placed_label(ui, ui->body, formatted, &spacemonorbold10, BLACK, 0, 161, 212);
        placed_label(ui, ui->body, "Tempelkan handphone anda untuk",
            &spacemonoreg7, BLACK, 0, 183, 212);
        placed_label(ui, ui->body, "membayar. Jangan lupa",
            &spacemonoreg7, BLACK, 0, 198, 212);
        placed_label(ui, ui->body, "untuk mengaktifkan NFC",
            &spacemonorbold8, BLACK, 0, 211, 212);
        placed_label(ui, ui->body, "pada handphone Anda",
            &spacemonoreg7, BLACK, 0, 227, 212);
        logo_footer(ui, 30);
        fixed_simulation_controls(ui);
    } else {
        payment_brand(ui, false);
        placed_label(ui, ui->body, "Nominal Pembayaran", &spacemonoreg8,
            BLACK, 0, 65, 212);
        placed_label(ui, ui->body, formatted, &spacemonorbold11, BLACK, 0, 80, 212);
        placed_label(ui, ui->body, "QR belum tersedia.\nScan QR terminal untuk membayar.",
            &spacemonoreg8, BLACK, 6, 140, 200);
        logo_footer(ui, 30);
        fixed_simulation_controls(ui);
    }
    if (method == SOUNDBOX_UI_CPM && !ui->config.simulation)
        placed_label(ui, ui->body, "Siapkan QR pembayaran pelanggan.",
            &spacemonoreg6, BLACK, 0, 60, 212);
    else if (method == SOUNDBOX_UI_TAP && !ui->config.simulation)
        placed_label(ui, ui->body, "Menunggu satu kartu NFC.",
            &spacemonoreg6, BLACK, 0, 290, 212);
    else
        placed_label(ui, ui->body, ui->config.simulation ?
            "SIMULASI - bukan pembayaran" :
            "Pratinjau: layanan pembayaran belum aktif.",
            &spacemonoreg6, BLACK, 0, method == SOUNDBOX_UI_CPM ? 60 : 290, 212);
    return ui->ok;
}

bool soundbox_ui_show_scanner(soundbox_ui_t *ui, uint32_t amount)
{
    lv_obj_t *preview;
    char formatted[32];
    if (!fixed_page(ui, "QRIS CPM", true, false))
        return false;
#ifdef SIM_BUILD
    preview = object(ui, ui->body);
    bounds(ui, preview, 8, 50, 196, 150);
    if (preview) {
        lv_obj_set_style_bg_opa(preview, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(preview, lv_color_hex(0x303030), 0);
        lv_obj_set_style_radius(preview, 4, 0);
        placed_label(ui, preview, "Pratinjau kamera\nbelum terhubung",
            &spacemonoreg8, WHITE, 0, 55, 196);
        scanner_corners(ui, preview);
    }
#else
    preview = lv_scanner_create(ui->body);
    if (!preview) {
        LV_LOG_ERROR("QR scanner could not be created");
        return false;
    }
    ui->scanner = preview;
    bounds(ui, preview, 8, 50, 196, 150);
    lv_scanner_set_formats(preview, S_FORMAT_QR_CODE);
    lv_scanner_set_preview_align(preview, 0, 0);
    lv_scanner_set_beep(preview, true);
    lv_obj_add_event_cb(preview, scanner_value_changed,
        LV_EVENT_VALUE_CHANGED, ui);
    ui->scan_value[0] = '\0';
    lv_scanner_start(preview);
#endif
    payment_money(amount, formatted);
    placed_label(ui, ui->body, formatted, &spacemonorbold14, BLACK, 0, 210, 212);
#ifdef SIM_BUILD
    fixed_simulation_controls(ui);
    placed_label(ui, ui->body, "Tidak ada kamera aktif pada pratinjau.",
        &spacemonoreg6, BLACK, 0, 290, 212);
#endif
    return ui->ok;
}

const char *soundbox_ui_scan_value(const soundbox_ui_t *ui)
{
    return ui ? ui->scan_value : NULL;
}

bool soundbox_ui_show_processing(soundbox_ui_t *ui, uint32_t amount)
{
    if (!page(ui, "Memproses pembayaran", true, false))
        return false;
    waiting_amount(ui, amount);
    if (ui->config.simulation) {
        label(ui, ui->body, "SIMULASI - tidak ada pembayaran nyata", BLACK, false);
        simulation_controls(ui);
    } else
        label(ui, ui->body, "Mengirim QR pelanggan ke layanan pembayaran.", BLACK, false);
    return ui->ok;
}

bool soundbox_ui_show_network_wait(soundbox_ui_t *ui, const char *message)
{
    if (!page(ui, "Menghubungi layanan", true, false))
        return false;
    label(ui, ui->body, message, BLACK, false);
    return ui->ok;
}

bool soundbox_ui_show_payment_qr(soundbox_ui_t *ui, uint32_t amount,
    const char *content, bool connected)
{
    lv_obj_t *qr;
    char formatted[32];
    lv_coord_t size;
    if (!ui || !content || !content[0])
        return invalid();
    if (!fixed_page(ui, "", false, false))
        return false;
    payment_brand(ui, false);
    placed_label(ui, ui->body, "Nominal Pembayaran", &spacemonoreg8, BLACK, 0, 65, 212);
    payment_money(amount, formatted);
    placed_label(ui, ui->body, formatted, &spacemonorbold11, BLACK, 0, 80, 212);
    size = sx(ui, 160);
    if (size > sy(ui, 160))
        size = sy(ui, 160);
    qr = lv_qrcode_create(ui->body, size, lv_color_hex(BLACK), lv_color_hex(WHITE));
    if (!qr || lv_qrcode_update(qr, content, strlen(content)) != LV_RES_OK) {
        LV_LOG_ERROR("Could not encode payment QR");
        return false;
    }
    lv_obj_align(qr, LV_ALIGN_TOP_MID, 0, sy(ui, 100));
    placed_label(ui, ui->body, connected ? "Scan QR untuk membayar." :
        "MQTT terputus.\nNotifikasi mungkin terlewat.",
        &spacemonoreg6, connected ? BLACK : 0xcc0000, 0, 40, 212);
    /* DQ72's y=252 instruction overlaps the QR's last eight rows. */
    placed_label(ui, ui->body, "Selesaikan pembayaran dalam",
        &spacemonoreg7, BLACK, 0, 262, 212);
    ui->payment_timer = placed_label(ui, ui->body, "05:00",
        &spacemonoreg7, 0xe74949, 0, 275, 212);
    logo_footer(ui, 30);
    return ui->ok;
}

bool soundbox_ui_set_countdown(soundbox_ui_t *ui, uint32_t seconds)
{
    if (!ui || !ui->root || !ui->payment_timer)
        return invalid();
    lv_label_set_text_fmt(ui->payment_timer, "%02lu:%02lu",
        (unsigned long)(seconds / 60U), (unsigned long)(seconds % 60U));
    return true;
}

bool soundbox_ui_show_notice(soundbox_ui_t *ui, const char *heading,
    const char *message)
{
    if (!page(ui, heading, false, false))
        return false;
    label(ui, ui->body, message, BLACK, false);
    button(ui, ui->body, "Home", LV_PCT(100), 48, SOUNDBOX_UI_DONE, 0, 0, false);
    return ui->ok;
}

bool soundbox_ui_show_result(soundbox_ui_t *ui, bool success,
    uint32_t amount, const char *message)
{
    lv_obj_t *footer, *description;
    char formatted[32];
    if (!fixed_page(ui, "", false, true))
        return false;
    ui->result = true;
    placed_image(ui, ui->body, success ? soundbox_img_success_payment :
        soundbox_img_failed_payment, success ? 34 : 33, 15);
    placed_label(ui, ui->body, success ? "Pembayaran berhasil!" : "Pembayaran gagal!",
        &spacemonorbold11, WHITE, 0, 151, 212);
    money(amount, formatted);
    if (success) {
        placed_label(ui, ui->body, formatted, &spacemonoreg8, WHITE, 0, 173, 212);
        placed_label(ui, ui->body, "Terima kasih atas pembayaran Anda.",
            &spacemonoreg6, WHITE, 0, 189, 212);
    }
    description = placed_label(ui, ui->body, message, &spacemonoreg6,
        WHITE, 3, success ? 201 : 175, 206);
    if (description) {
        lv_obj_set_height(description, sy(ui, success ? 22 : 40));
        lv_label_set_long_mode(description, LV_LABEL_LONG_DOT);
    }
    footer = object(ui, ui->body);
    bounds(ui, footer, 0, 223, 212, 95);
    if (footer) {
        lv_obj_set_style_bg_color(footer, lv_color_hex(WHITE), 0);
        lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, 0);
    }
    ui->result_status = placed_label(ui, footer, "", &spacemonoreg6, BLACK, 3, 1, 206);
    if (ui->result_status) {
        lv_obj_set_height(ui->result_status, sy(ui, 24));
        lv_label_set_long_mode(ui->result_status, LV_LABEL_LONG_DOT);
    }
    /* Receipt actions are a PrinterDemo extension inside the original 95px panel. */
    if (success)
        ui->print_button = flat_button(ui, footer, "Cetak Struk", 3, 27, 101, 28,
            SOUNDBOX_UI_PRINT_RECEIPT, 0, amount, false);
    ui->home_button = flat_button(ui, footer, "Home", success ? 108 : 3, 27,
        success ? 101 : 206, 28,
        SOUNDBOX_UI_DONE, 0, 0, false);
    placed_image(ui, footer, soundbox_img_tokonetzme, 3, 58);
    placed_image(ui, footer, soundbox_img_qris_tap_black, 123, 65);
    if (ui->print_button)
        lv_group_focus_obj(ui->print_button);
    return ui->ok;
}

bool soundbox_ui_set_result_state(soundbox_ui_t *ui,
    soundbox_ui_print_state_t state, const char *message)
{
    bool busy = state == SOUNDBOX_PRINT_PREPARING || state == SOUNDBOX_PRINT_SENDING;
    if (!ui || !ui->root || !ui->result || !ui->ok ||
        state < SOUNDBOX_PRINT_READY || state > SOUNDBOX_PRINT_UNCERTAIN)
        return invalid();
    lv_label_set_text(ui->result_status, text(message));
    if (ui->print_button) {
        if (state == SOUNDBOX_PRINT_READY)
            lv_obj_clear_state(ui->print_button, LV_STATE_DISABLED);
        else
            lv_obj_add_state(ui->print_button, LV_STATE_DISABLED);
    }
    if (busy)
        lv_obj_add_state(ui->home_button, LV_STATE_DISABLED);
    else
        lv_obj_clear_state(ui->home_button, LV_STATE_DISABLED);
    if (!busy && state != SOUNDBOX_PRINT_READY)
        lv_group_focus_obj(ui->home_button);
    return true;
}

static lv_coord_t history_row(soundbox_ui_t *ui, lv_coord_t y,
    const char *key_text, const char *value_text, const lv_font_t *value_font)
{
    lv_obj_t *row = object(ui, ui->body);
    lv_obj_t *key, *value;
    if (!row)
        return y;
    lv_obj_set_pos(row, sx(ui, 3), y);
    lv_obj_set_size(row, sx(ui, 207), LV_SIZE_CONTENT);
    key = placed_label(ui, row, key_text, &spacemonoreg7, BLACK, 15, 12, 88);
    value = placed_label(ui, row, value_text, value_font, BLACK, 104, 12, 88);
    if (key)
        lv_obj_set_style_text_align(key, LV_TEXT_ALIGN_LEFT, 0);
    if (value)
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_update_layout(row);
    lv_obj_set_height(row, (key && value ?
        LV_MAX(lv_obj_get_height(key), lv_obj_get_height(value)) : 0) + sy(ui, 24));
    return y + lv_obj_get_height(row);
}

bool soundbox_ui_show_history(soundbox_ui_t *ui, uint64_t today_count,
    uint64_t today_total, const soundbox_ui_history_item_t *items,
    size_t count, const char *message)
{
    char count_text[32], formatted[32];
    lv_coord_t y;
    lv_obj_t *heading, *divider, *amount_label, *issuer, *date;
    size_t i;
    if (count > SOUNDBOX_UI_MAX_ITEMS || (count && !items))
        return invalid();
    if (!fixed_page(ui, "Riwayat Transaksi", true, false))
        return false;
    bounds(ui, ui->body, 0, 46, 212, 262);
    lv_obj_add_flag(ui->body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ui->body, LV_DIR_VER);
    heading = placed_label(ui, ui->body, "Transaksi Hari Ini",
        &spacemonorbold10, BLACK, 8, 10, 197);
    lv_obj_update_layout(ui->body);
    y = heading ? lv_obj_get_y(heading) + lv_obj_get_height(heading) + sy(ui, 8) : sy(ui, 38);
    money(today_total, formatted);
    snprintf(count_text, sizeof(count_text), "%llu", (unsigned long long)today_count);
    y = history_row(ui, y, "Total Trx", count_text, &spacemonoreg9);
    y = history_row(ui, y, "Total", formatted, &spacemonorbold9);
    divider = object(ui, ui->body);
    if (divider) {
        lv_obj_set_pos(divider, sx(ui, 18), y + sy(ui, 6));
        lv_obj_set_size(divider, sx(ui, 177), 1);
        lv_obj_set_style_bg_color(divider, lv_color_hex(0xdddddd), 0);
        lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    }
    y += sy(ui, 12);
    heading = placed_label(ui, ui->body, "Transaksi Terakhir",
        &spacemonorbold10, BLACK, 8, 0, 197);
    if (heading) {
        lv_obj_set_y(heading, y);
        lv_obj_update_layout(heading);
        y += lv_obj_get_height(heading) + sy(ui, 8);
    }
    heading = placed_label(ui, ui->body, message, &spacemonorbold7, BLACK, 8, 0, 197);
    if (heading) {
        lv_obj_set_y(heading, y + sy(ui, 8));
        lv_obj_update_layout(heading);
        y += lv_obj_get_height(heading) + sy(ui, 16);
    }
    for (i = 0; i < count; ++i) {
        lv_obj_t *card = flat_button(ui, ui->body, "", 3, 0, 207, 70,
            SOUNDBOX_UI_HISTORY_ITEM, (uint32_t)i, 0, false);
        if (card) {
            lv_obj_set_y(card, y);
            lv_obj_set_style_radius(card, 0, 0);
            lv_obj_set_style_bg_color(card, lv_color_hex(i % 2 ? 0xf0f0f0 : WHITE), 0);
            amount_label = placed_label(ui, card, items[i].amount,
                &spacemonoreg11, BLACK, 109, 5, 83);
            issuer = placed_label(ui, card, items[i].title,
                &spacemonoreg10, BLACK, 15, 5, 86);
            date = placed_label(ui, card, items[i].subtitle,
                &spacemonoreg7, BLACK, 15, 0, 177);
            if (amount_label)
                lv_obj_set_style_text_align(amount_label, LV_TEXT_ALIGN_RIGHT, 0);
            if (issuer)
                lv_obj_set_style_text_align(issuer, LV_TEXT_ALIGN_LEFT, 0);
            if (date && issuer) {
                lv_obj_update_layout(card);
                lv_obj_set_y(date, lv_obj_get_y(issuer) + lv_obj_get_height(issuer) + sy(ui, 2));
                lv_obj_set_style_text_align(date, LV_TEXT_ALIGN_LEFT, 0);
            }
            lv_obj_update_layout(card);
            if (amount_label && date)
                lv_obj_set_height(card, LV_MAX(sy(ui, 70),
                    LV_MAX(lv_obj_get_y(amount_label) + lv_obj_get_height(amount_label),
                        lv_obj_get_y(date) + lv_obj_get_height(date)) + sy(ui, 5)));
            y += lv_obj_get_height(card);
        }
    }
    return ui->ok;
}

bool soundbox_ui_show_detail(soundbox_ui_t *ui, const char *heading,
    const soundbox_ui_detail_row_t *rows, size_t count)
{
    size_t i;
    lv_coord_t y;
    lv_obj_t *title;
    if (count > SOUNDBOX_UI_MAX_ITEMS || (count && !rows))
        return invalid();
    if (!fixed_page(ui, "Detail Transaksi", true, false))
        return false;
    bounds(ui, ui->body, 0, 51, 212, 262);
    lv_obj_add_flag(ui->body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ui->body, LV_DIR_VER);
    title = placed_label(ui, ui->body, heading, &spacemonorbold10, BLACK, 8, 5, 197);
    lv_obj_update_layout(ui->body);
    y = title ? lv_obj_get_y(title) + lv_obj_get_height(title) + sy(ui, 5) : sy(ui, 30);
    for (i = 0; i < count; ++i)
        y = history_row(ui, y, rows[i].label, rows[i].value, &spacemonoreg7);
    return ui->ok;
}
