#include "soundbox_app.h"

#include <stdio.h>
#include <string.h>

#define AUTO_RETURN_MS 3000U
#define MOCK_PRINT_MS 500U
#define SCAN_TIMEOUT_MS 60000U
#define MPM_TIMEOUT_MS 300000U

struct soundbox_app {
    soundbox_ui_t *ui;
    soundbox_app_config_t config;
    soundbox_page_t page;
    soundbox_ui_method_t method;
    soundbox_ui_print_state_t print_state;
    uint32_t token;
    uint32_t amount;
    uint32_t started;
    uint32_t return_started;
    uint32_t print_started;
    uint32_t countdown;
    const char *vehicle;
    bool manual;
    bool success;
    bool returning;
    bool has_record;
    bool updates_lost;
    int store_status;
    struct soundbox_record stored;
    soundbox_ui_vehicle_t stored_vehicles[SOUNDBOX_PRICE_COUNT];
    size_t stored_vehicle_count;
    soundbox_submit_cb submit;
    void *backend_user_data;
    struct soundbox_attempt attempt;
    struct print_receipt receipt;
    char receipt_event_type[64];
    struct soundbox_history_entry history[SOUNDBOX_HISTORY_MAX];
    size_t history_count;
    bool history_truncated;
    bool mqtt_connected;
    bool backend_pending;
    enum soundbox_job_kind pending_kind;
    char payment_qr[QR_REQUEST_CONTENT_MAX + 1U];
};

static bool rendered(soundbox_app_t *app, bool result)
{
    if (!result) {
        LV_LOG_ERROR("Soundbox page could not be rendered");
        soundbox_ui_set_status(app->ui, "Kesalahan tampilan");
    }
    return result;
}

static void payment_countdown(soundbox_app_t *app, uint32_t now)
{
    uint32_t elapsed = now - app->started;
    rendered(app, soundbox_ui_set_countdown(app->ui,
        elapsed >= MPM_TIMEOUT_MS ? 0 : (MPM_TIMEOUT_MS - elapsed + 999U) / 1000U));
}

bool soundbox_app_home(soundbox_app_t *app)
{
    char merchant[193];
    if (!app) {
        LV_LOG_ERROR("Missing soundbox application");
        return false;
    }
    ++app->token;
    app->backend_pending = false;
    app->page = SOUNDBOX_HOME;
    app->returning = false;
    app->amount = 0;
    app->manual = false;
    if (app->has_record && (app->stored.settings.merchant_name[0] ||
        app->stored.settings.merchant_name_l2[0])) {
        snprintf(merchant, sizeof(merchant), "%s%s%s",
            app->stored.settings.merchant_name,
            app->stored.settings.merchant_name[0] && app->stored.settings.merchant_name_l2[0] ? "\n" : "",
            app->stored.settings.merchant_name_l2);
    } else {
        snprintf(merchant, sizeof(merchant), "%s", app->submit ? "Parkir" : "Parkir (pratinjau)");
    }
    return rendered(app, soundbox_ui_show_home(app->ui,
        merchant,
        app->has_record && app->stored.settings.merchant_nmid[0] ?
            app->stored.settings.merchant_nmid : "NMID belum dikonfigurasi",
        app->has_record ? app->stored_vehicles : app->config.vehicles,
        app->has_record ? app->stored_vehicle_count : app->config.vehicle_count,
        app->config.manual));
}

static bool show_payment(soundbox_app_t *app)
{
    app->page = SOUNDBOX_PAYMENT;
    return rendered(app, soundbox_ui_show_payment(app->ui, app->vehicle, app->amount));
}

static bool show_vehicles(soundbox_app_t *app)
{
    if (!app->config.vehicle_count) {
        app->vehicle = "Lainnya";
        return show_payment(app);
    }
    app->page = SOUNDBOX_VEHICLES;
    return rendered(app, soundbox_ui_show_vehicles(app->ui, app->amount,
        app->has_record ? app->stored_vehicles : app->config.vehicles,
        app->has_record ? app->stored_vehicle_count : app->config.vehicle_count));
}

static bool show_history(soundbox_app_t *app)
{
    static const soundbox_ui_history_item_t examples[] = {
        {"SIMULASI - Motor", "Contoh, bukan transaksi", "Rp 2.000"}
    };
    char dated_message[128];
    soundbox_ui_history_item_t items[SOUNDBOX_HISTORY_MAX];
    size_t i;
    app->page = SOUNDBOX_HISTORY;
    snprintf(dated_message, sizeof(dated_message),
        "Total lokal %04lu-%02lu-%02lu (UTC+7).\nDaftar HTTPS belum aktif.",
        (unsigned long)(app->stored.date / 10000U),
        (unsigned long)(app->stored.date / 100U % 100U),
        (unsigned long)(app->stored.date % 100U));
    for (i = 0; i < app->history_count; ++i)
        items[i] = (soundbox_ui_history_item_t){app->history[i].title,
            app->history[i].subtitle, app->history[i].amount};
    return rendered(app, soundbox_ui_show_history(app->ui,
        app->has_record ? app->stored.count : 0,
        app->has_record ? app->stored.total : 0,
        app->submit ? items : app->config.simulation ? examples : NULL,
        app->submit ? app->history_count : app->config.simulation ? 1 : 0,
        app->store_status < 0 ? "Penyimpanan gagal. Total tidak terjamin." :
        app->updates_lost ? "Pembaruan MQTT hilang; minta broadcast ulang." :
        app->has_record && app->stored.reconciliation ?
            "Pembayaran tersimpan untuk rekonsiliasi. Periksa data lokal." :
        app->has_record && app->stored.pending ?
            "Ada pembayaran belum selesai. Periksa data lokal." :
        app->has_record && !app->stored.date ? "Jam belum valid; total belum aktif." :
        app->submit ? (app->history_truncated ? "16 transaksi pertama. Daftar dibatasi." :
            app->history_count ? "Riwayat dari layanan." : "Tidak ada transaksi.") :
        app->has_record ? dated_message :
            "Riwayat HTTPS dan total lokal belum aktif."));
}

static bool notice(soundbox_app_t *app, const char *heading, const char *message)
{
    app->page = SOUNDBOX_RESULT;
    app->success = false;
    app->returning = false;
    app->print_state = SOUNDBOX_PRINT_FAILED;
    return rendered(app, soundbox_ui_show_notice(app->ui, heading, message));
}

static bool submit_job(soundbox_app_t *app, struct soundbox_backend_job *job)
{
    job->token = app->token;
    if (!app->submit(job, app->backend_user_data)) {
        LV_LOG_ERROR("Parking worker rejected request");
        notice(app, "Permintaan tidak dikirim", "Layanan sibuk / tidak tersedia. Coba dari Home.");
        return false;
    }
    app->backend_pending = true;
    app->pending_kind = job->kind;
    return true;
}

static void request_history(soundbox_app_t *app)
{
    struct soundbox_backend_job job = {0};
    ++app->token;
    app->page = SOUNDBOX_HISTORY;
    job.kind = SB_JOB_HISTORY;
    rendered(app, soundbox_ui_show_network_wait(app->ui, "Memuat riwayat transaksi..."));
    submit_job(app, &job);
}

bool soundbox_app_set_record(soundbox_app_t *app,
    const struct soundbox_record *record, int status)
{
    size_t i;
    char status_message[96];
    const char *prefix;
    if (!app || (record && !soundbox_settings_valid(&record->settings))) {
        LV_LOG_ERROR("Invalid persisted soundbox snapshot");
        return false;
    }
    app->store_status = status;
    if (record) {
        for (i = 0; i < app->config.vehicle_count; ++i) {
            if (app->config.vehicles[i].id < 1U ||
                app->config.vehicles[i].id > SOUNDBOX_PRICE_COUNT) {
                LV_LOG_ERROR("Vehicle ID has no persisted price");
                return false;
            }
        }
        app->stored = *record;
        app->has_record = true;
        app->stored_vehicle_count = app->config.vehicle_count;
        for (i = 0; i < app->stored_vehicle_count; ++i) {
            app->stored_vehicles[i] = app->config.vehicles[i];
            app->stored_vehicles[i].price =
                record->settings.prices[app->stored_vehicles[i].id - 1U];
        }
    }
    prefix = app->config.simulation ? "SIMULASI" : app->submit ? "STAGING" : "PRATINJAU";
    snprintf(status_message, sizeof(status_message), "%s%s | %s", prefix,
        app->config.simulation && status >= 0 && status != SB_STORE_RECOVERED &&
            !app->updates_lost ? " OFFLINE" : "",
        status < 0 ? "Penyimpanan gagal" : status == SB_STORE_RECOVERED ? "Data dipulihkan" :
            app->updates_lost ? "Pembaruan MQTT hilang" : "Data lokal");
    if (!soundbox_ui_set_status(app->ui, status_message))
        return false;
    if (app->page == SOUNDBOX_HOME)
        return soundbox_app_home(app);
    if (app->page == SOUNDBOX_HISTORY && !app->backend_pending)
        return show_history(app);
    return true;
}

void soundbox_app_updates_lost(soundbox_app_t *app)
{
    if (!app) {
        LV_LOG_ERROR("Missing soundbox application");
        return;
    }
    app->updates_lost = true;
    soundbox_app_set_record(app, NULL, app->store_status);
}

static void back(soundbox_app_t *app)
{
    switch (app->page) {
    case SOUNDBOX_HOME:
        break;
    case SOUNDBOX_VEHICLES:
        app->page = SOUNDBOX_AMOUNT;
        rendered(app, soundbox_ui_show_amount(app->ui));
        break;
    case SOUNDBOX_PAYMENT:
        if (app->manual) {
            if (app->config.vehicle_count)
                show_vehicles(app);
            else {
                app->page = SOUNDBOX_AMOUNT;
                rendered(app, soundbox_ui_show_amount(app->ui));
            }
        } else
            soundbox_app_home(app);
        break;
    case SOUNDBOX_MPM_WAIT:
    case SOUNDBOX_TAP_CAPTURE:
    case SOUNDBOX_CPM_START:
    case SOUNDBOX_CPM_SCAN:
    case SOUNDBOX_REQUEST:
        ++app->token;
        if (app->submit)
            notice(app, "Status belum diketahui",
                "Permintaan mungkin sudah terkirim. Periksa riwayat sebelum membayar ulang.");
        else
            show_payment(app);
        break;
    case SOUNDBOX_RESULT:
        if (app->print_state != SOUNDBOX_PRINT_PREPARING &&
            app->print_state != SOUNDBOX_PRINT_SENDING)
            soundbox_app_home(app);
        break;
    case SOUNDBOX_DETAIL:
        ++app->token;
        app->backend_pending = false;
        show_history(app);
        break;
    default:
        soundbox_app_home(app);
        break;
    }
}

bool soundbox_app_payment_complete(soundbox_app_t *app, uint32_t token,
    bool success, const char *message, uint32_t now)
{
    if (!app || token != app->token ||
        (app->page != SOUNDBOX_MPM_WAIT && app->page != SOUNDBOX_REQUEST)) {
        LV_LOG_WARN("Discarding stale or unexpected soundbox payment completion");
        return false;
    }
    app->page = SOUNDBOX_RESULT;
    app->success = success;
    app->print_state = SOUNDBOX_PRINT_READY;
    app->return_started = now;
    app->returning = true;
    app->countdown = 3;
    if (!rendered(app, soundbox_ui_show_result(app->ui, success, app->amount, message)))
        return false;
    return rendered(app, soundbox_ui_set_result_state(app->ui,
        SOUNDBOX_PRINT_READY, "Kembali ke Home dalam 3 detik"));
}

bool soundbox_app_print_complete(soundbox_app_t *app, uint32_t token,
    soundbox_ui_print_state_t state, const char *message, uint32_t now)
{
    if (!app || token != app->token || app->page != SOUNDBOX_RESULT ||
        app->print_state != SOUNDBOX_PRINT_SENDING ||
        (state != SOUNDBOX_PRINT_SENT && state != SOUNDBOX_PRINT_FAILED &&
         state != SOUNDBOX_PRINT_UNCERTAIN)) {
        LV_LOG_WARN("Discarding stale or unexpected soundbox print completion");
        return false;
    }
    app->print_state = state;
    app->returning = state == SOUNDBOX_PRINT_SENT;
    app->return_started = now;
    app->countdown = 0;
    return rendered(app, soundbox_ui_set_result_state(app->ui, state, message));
}

static void event_received(soundbox_ui_t *ui,
    const soundbox_ui_event_t *event, void *user_data)
{
    soundbox_app_t *app = user_data;
    uint32_t now = lv_tick_get();
    size_t i;
    (void)ui;
    switch (event->action) {
    case SOUNDBOX_UI_BACK:
        back(app);
        break;
    case SOUNDBOX_UI_SETTINGS:
        if (app->page == SOUNDBOX_HOME) {
            if (app->config.settings)
                app->config.settings(app->config.user_data);
            else {
                LV_LOG_ERROR("Soundbox settings handler unavailable");
                soundbox_ui_set_status(app->ui, "Layanan pengaturan belum aktif");
            }
        }
        break;
    case SOUNDBOX_UI_HISTORY:
        if (app->page == SOUNDBOX_HOME) {
            if (app->submit)
                request_history(app);
            else
                show_history(app);
        }
        break;
    case SOUNDBOX_UI_MANUAL:
        if (app->page == SOUNDBOX_HOME && app->config.manual) {
            app->manual = true;
            app->page = SOUNDBOX_AMOUNT;
            rendered(app, soundbox_ui_show_amount(app->ui));
        }
        break;
    case SOUNDBOX_UI_AMOUNT:
        if (app->page == SOUNDBOX_AMOUNT &&
            event->amount >= 1000 && event->amount <= 10000000) {
            app->amount = event->amount;
            show_vehicles(app);
        }
        break;
    case SOUNDBOX_UI_VEHICLE: {
        const soundbox_ui_vehicle_t *vehicles;
        size_t count;
        if (app->page != SOUNDBOX_HOME && app->page != SOUNDBOX_VEHICLES)
            break;
        vehicles = app->has_record ? app->stored_vehicles :
            app->config.vehicles;
        count = app->has_record ? app->stored_vehicle_count : app->config.vehicle_count;
        for (i = 0; i < count; ++i) {
            if (vehicles[i].id == event->value) {
                app->vehicle = vehicles[i].name;
                if (!app->manual)
                    app->amount = vehicles[i].price;
                show_payment(app);
                break;
            }
        }
        break;
    }
    case SOUNDBOX_UI_PAYMENT:
        if (app->page != SOUNDBOX_PAYMENT || event->value > SOUNDBOX_UI_CPM)
            break;
        app->method = (soundbox_ui_method_t)event->value;
        ++app->token;
        app->started = now;
        if (app->submit) {
            struct soundbox_backend_job job = {0};
            if (app->method != SOUNDBOX_UI_MPM) {
                notice(app, "Capture belum tersedia",
                    "Tap / CPM belum terhubung ke NFC / kamera. Tidak ada permintaan dikirim.");
                break;
            }
            if (!app->has_record || app->store_status < 0 ||
                !app->stored.date || !app->stored.settings.merchant_nmid[0]) {
                notice(app, "Permintaan tidak dikirim",
                    "Periksa penyimpanan, jam dan konfigurasi NMID.");
                break;
            }
            memset(&app->attempt, 0, sizeof(app->attempt));
            memset(&app->receipt, 0, sizeof(app->receipt));
            app->receipt_event_type[0] = 0;
            snprintf(app->receipt.merchant_name, sizeof(app->receipt.merchant_name),
                "%s", app->stored.settings.merchant_name);
            snprintf(app->receipt.merchant_name_l2, sizeof(app->receipt.merchant_name_l2),
                "%s", app->stored.settings.merchant_name_l2);
            snprintf(app->receipt.nmid, sizeof(app->receipt.nmid),
                "%s", app->stored.settings.merchant_nmid);
            snprintf(app->receipt.vehicle, sizeof(app->receipt.vehicle), "%s", app->vehicle);
            snprintf(app->receipt.method, sizeof(app->receipt.method), "QRIS MPM");
            app->receipt.amount = app->amount;
            app->page = SOUNDBOX_REQUEST;
            job.kind = SB_JOB_QR;
            job.amount = app->amount;
            job.type = 1;
            job.attempt.merchant_generation = app->stored.merchant_generation;
            job.attempt.date = app->stored.date;
            snprintf(job.attempt.nmid, sizeof(job.attempt.nmid), "%s",
                app->stored.settings.merchant_nmid);
            rendered(app, soundbox_ui_show_network_wait(app->ui, "Membuat QR pembayaran..."));
            submit_job(app, &job);
            break;
        }
        app->page = app->method == SOUNDBOX_UI_MPM ? SOUNDBOX_MPM_WAIT :
            app->method == SOUNDBOX_UI_TAP ? SOUNDBOX_TAP_CAPTURE : SOUNDBOX_CPM_START;
        rendered(app, soundbox_ui_show_waiting(app->ui, app->method, app->amount));
        break;
    case SOUNDBOX_UI_SCAN:
        if (app->page == SOUNDBOX_CPM_START) {
            app->page = SOUNDBOX_CPM_SCAN;
            app->started = now;
            rendered(app, soundbox_ui_show_scanner(app->ui, app->amount));
        }
        break;
    case SOUNDBOX_UI_SIM_SUCCESS:
    case SOUNDBOX_UI_SIM_FAILURE:
        if (!app->config.simulation)
            break;
        if (app->page == SOUNDBOX_CPM_SCAN || app->page == SOUNDBOX_TAP_CAPTURE) {
            app->page = SOUNDBOX_REQUEST;
            if (event->action == SOUNDBOX_UI_SIM_SUCCESS) {
                rendered(app, soundbox_ui_show_processing(app->ui, app->amount));
                break;
            }
        }
        if (app->page == SOUNDBOX_REQUEST || app->page == SOUNDBOX_MPM_WAIT)
            soundbox_app_payment_complete(app, app->token,
                event->action == SOUNDBOX_UI_SIM_SUCCESS,
                "SIMULASI - tidak ada pembayaran nyata", now);
        break;
    case SOUNDBOX_UI_PRINT_RECEIPT:
        if (app->page != SOUNDBOX_RESULT || !app->success ||
            app->print_state != SOUNDBOX_PRINT_READY)
            break;
        /* Disarm before any queue submission or UI work. */
        app->returning = false;
        app->print_state = SOUNDBOX_PRINT_PREPARING;
        rendered(app, soundbox_ui_set_result_state(app->ui, app->print_state,
            "Menyiapkan struk..."));
        app->print_started = now;
        app->print_state = SOUNDBOX_PRINT_SENDING;
        rendered(app, soundbox_ui_set_result_state(app->ui, app->print_state,
            app->config.simulation ? "SIMULASI cetak - Bluetooth tidak dipakai" :
            "Memuat data dan mencetak struk..."));
        if (!app->config.simulation) {
            struct soundbox_backend_job job = {0};
            job.kind = SB_JOB_RECEIPT;
            job.token = app->token;
            job.receipt = app->receipt;
            job.test_receipt = SOUNDBOX_TEST_RECEIPT != 0;
            snprintf(job.transaction_id, sizeof(job.transaction_id), "%s",
                app->receipt.transaction_id);
            snprintf(job.event_type, sizeof(job.event_type), "%s", app->receipt_event_type);
            if (!app->submit ||
                (!job.test_receipt && (!job.transaction_id[0] || !job.event_type[0]))) {
                LV_LOG_ERROR("Receipt unavailable: missing transaction identifiers");
                soundbox_app_print_complete(app, app->token, SOUNDBOX_PRINT_FAILED,
                    "Data struk kurang:\nID tidak tersedia.", now);
            } else if (!app->submit(&job, app->backend_user_data)) {
                LV_LOG_ERROR("Receipt worker rejected request");
                soundbox_app_print_complete(app, app->token, SOUNDBOX_PRINT_FAILED,
                    "Layanan cetak sibuk /\ntidak tersedia.", now);
            } else {
                app->backend_pending = true;
                app->pending_kind = job.kind;
                if (job.test_receipt)
                    rendered(app, soundbox_ui_set_result_state(app->ui, app->print_state,
                        "Mencetak struk uji.\nBukan struk resmi."));
            }
        }
        break;
    case SOUNDBOX_UI_DONE:
        if (app->page == SOUNDBOX_RESULT &&
            app->print_state != SOUNDBOX_PRINT_PREPARING &&
            app->print_state != SOUNDBOX_PRINT_SENDING)
            soundbox_app_home(app);
        break;
    case SOUNDBOX_UI_HISTORY_ITEM:
        if (app->page == SOUNDBOX_HISTORY && app->submit &&
            event->value < app->history_count) {
            struct soundbox_backend_job job = {0};
            const struct soundbox_history_entry *entry = &app->history[event->value];
            ++app->token;
            app->page = SOUNDBOX_DETAIL;
            job.kind = SB_JOB_DETAIL;
            snprintf(job.transaction_id, sizeof(job.transaction_id), "%s", entry->transaction_id);
            snprintf(job.event_type, sizeof(job.event_type), "%s", entry->event_type);
            rendered(app, soundbox_ui_show_network_wait(app->ui, "Memuat detail transaksi..."));
            submit_job(app, &job);
            break;
        }
        if (app->page == SOUNDBOX_HISTORY && app->config.simulation &&
            event->value == 0) {
            static const soundbox_ui_detail_row_t rows[] = {
                {"Status", "SIMULASI - bukan pembayaran"},
                {"Kendaraan", "Motor"}, {"Nominal", "Rp 2.000"},
                {"NMID", "Belum dikonfigurasi"}, {"RRN / issuer", "Contoh saja"}
            };
            app->page = SOUNDBOX_DETAIL;
            rendered(app, soundbox_ui_show_detail(app->ui, "Contoh transaksi",
                rows, sizeof(rows) / sizeof(rows[0])));
        }
        break;
    default:
        LV_LOG_ERROR("Unknown soundbox UI action");
        break;
    }
}

bool soundbox_app_set_backend(soundbox_app_t *app, soundbox_submit_cb submit,
    void *user_data)
{
    if (!app || !submit || app->config.simulation || app->page != SOUNDBOX_HOME) {
        LV_LOG_ERROR("Invalid live backend binding");
        return false;
    }
    app->submit = submit;
    app->backend_user_data = user_data;
    return soundbox_app_set_record(app, NULL, app->store_status);
}

bool soundbox_app_waiting_payment(const soundbox_app_t *app)
{
    return app && app->submit && app->page == SOUNDBOX_MPM_WAIT;
}

void soundbox_app_mqtt_status(soundbox_app_t *app, bool connected)
{
    if (!app || !app->submit)
        return;
    if (connected != app->mqtt_connected) {
        app->mqtt_connected = connected;
        if (soundbox_app_waiting_payment(app) &&
            rendered(app, soundbox_ui_show_payment_qr(app->ui, app->amount,
                app->payment_qr, connected)))
            payment_countdown(app, lv_tick_get());
    }
}

bool soundbox_app_acknowledge(soundbox_app_t *app, const struct soundbox_event *event)
{
    struct soundbox_backend_job job = {0};
    if (!soundbox_app_waiting_payment(app) || !app->attempt.id ||
        !event || event->type != SB_EVENT_PAYMENT) {
        LV_LOG_WARN("Discarding unexpected payment acknowledgement");
        return false;
    }
    job.kind = SB_JOB_ACCOUNT;
    job.attempt = app->attempt;
    job.paid_amount = event->money;
    if (!submit_job(app, &job))
        return false;
    if (event->money)
        app->receipt.amount = event->money;
    if (memchr(event->transaction_id, 0, sizeof(event->transaction_id)) &&
        memchr(event->event_type, 0, sizeof(event->event_type))) {
        snprintf(app->receipt.transaction_id, sizeof(app->receipt.transaction_id),
            "%s", event->transaction_id);
        snprintf(app->receipt_event_type, sizeof(app->receipt_event_type),
            "%s", event->event_type);
    } else {
        LV_LOG_ERROR("Receipt identifiers are not terminated");
    }
    app->page = SOUNDBOX_REQUEST;
    return rendered(app, soundbox_ui_show_network_wait(app->ui,
        "Pembayaran diakui. Menyimpan total..."));
}

bool soundbox_app_backend_complete(soundbox_app_t *app,
    const struct soundbox_backend_result *result, uint32_t now)
{
    if (!app || !result || !app->submit || !app->backend_pending ||
        result->kind != app->pending_kind || result->token != app->token ||
        (result->kind == SB_JOB_QR && app->page != SOUNDBOX_REQUEST) ||
        (result->kind == SB_JOB_ACCOUNT && app->page != SOUNDBOX_REQUEST) ||
        (result->kind == SB_JOB_RECEIPT && app->page != SOUNDBOX_RESULT) ||
        (result->kind == SB_JOB_HISTORY && app->page != SOUNDBOX_HISTORY) ||
        (result->kind == SB_JOB_DETAIL && app->page != SOUNDBOX_DETAIL)) {
        LV_LOG_WARN("Discarding stale backend completion");
        return false;
    }
    app->backend_pending = false;
    if (result->kind == SB_JOB_RECEIPT) {
        if (result->status != SB_BACKEND_OK) {
            const char *message;
            LV_LOG_ERROR("Receipt preparation failed: %d / HTTP %d",
                result->status, result->http_status);
            switch (result->status) {
            case SB_BACKEND_INVALID:
                message = "Data / format struk\ntidak valid / lengkap."; break;
            case SB_BACKEND_NETWORK:
                message = "HTTPS struk gagal.\nPeriksa jaringan / CA."; break;
            case SB_BACKEND_AUTH:
                message = "Akses struk ditolak.\nPeriksa konfigurasi."; break;
            case SB_BACKEND_UNAVAILABLE:
                message = "Layanan struk\nbelum tersedia."; break;
            default:
                message = "Layanan struk gagal.\nPeriksa log HTTP."; break;
            }
            return soundbox_app_print_complete(app, result->token,
                SOUNDBOX_PRINT_FAILED, message, now);
        }
        return soundbox_app_print_complete(app, result->token,
            result->delivery == SB_RECEIPT_SENT ? SOUNDBOX_PRINT_SENT :
            result->delivery == SB_RECEIPT_UNCERTAIN ? SOUNDBOX_PRINT_UNCERTAIN :
                SOUNDBOX_PRINT_FAILED,
            result->delivery == SB_RECEIPT_SENT ? "Struk telah dikirim ke printer" :
            result->delivery == SB_RECEIPT_UNCERTAIN ?
                "Mungkin tercetak.\nJangan ulangi." :
                "Cetak gagal.\nPeriksa printer.", now);
    }
    if (result->kind == SB_JOB_ACCOUNT) {
        bool ok = soundbox_app_payment_complete(app, app->token, true,
            result->status == SB_BACKEND_OK ?
                "Diakui melalui MQTT (DQ72)." :
                "Diakui melalui MQTT. Total gagal disimpan; perlu rekonsiliasi.", now);
        if (result->status != SB_BACKEND_OK) {
            app->returning = false;
            return ok && rendered(app, soundbox_ui_set_result_state(app->ui,
                SOUNDBOX_PRINT_READY, "Total belum terjamin. Periksa riwayat."));
        }
        return ok;
    }
    if (result->status != SB_BACKEND_OK) {
        char message[256];
        bool uncertain = result->kind == SB_JOB_QR && result->attempt.id;
        LV_LOG_ERROR("Parking backend failed: %d / HTTP %d", result->status, result->http_status);
        snprintf(message, sizeof(message), "%s%s", soundbox_backend_message(result->status),
            uncertain ? "\nPeriksa riwayat sebelum membayar ulang." : "");
        return notice(app, uncertain ?
            "Status belum diketahui" : "Permintaan gagal",
            message);
    }
    if (result->kind == SB_JOB_QR) {
        app->attempt = result->attempt;
        snprintf(app->payment_qr, sizeof(app->payment_qr), "%s", result->data.qr_content);
        if (!app->payment_qr[0])
            return notice(app, "Status belum diketahui", "Layanan tidak mengirim QR. Periksa riwayat.");
        app->page = SOUNDBOX_MPM_WAIT;
        app->started = now;
        if (!soundbox_ui_show_payment_qr(app->ui, app->amount,
                app->payment_qr, app->mqtt_connected))
            return notice(app, "Status belum diketahui", "QR tidak dapat ditampilkan. Periksa riwayat.");
        return true;
    }
    if (result->kind == SB_JOB_HISTORY) {
        if (result->data.history.count > SOUNDBOX_HISTORY_MAX) {
            LV_LOG_ERROR("Invalid backend history count");
            return notice(app, "Permintaan gagal", "Respons riwayat terlalu besar.");
        }
        app->history_count = result->data.history.count;
        app->history_truncated = result->data.history.truncated;
        memcpy(app->history, result->data.history.entries, sizeof(app->history));
        return show_history(app);
    }
    if (result->kind == SB_JOB_DETAIL) {
        soundbox_ui_detail_row_t rows[SOUNDBOX_DETAIL_MAX];
        size_t i, count = 0;
        for (i = 0; i < SOUNDBOX_DETAIL_MAX && result->data.detail[i].label; ++i)
            rows[count++] = (soundbox_ui_detail_row_t){result->data.detail[i].label,
                result->data.detail[i].value};
        return rendered(app, soundbox_ui_show_detail(app->ui, "Detail transaksi", rows, count));
    }
    LV_LOG_ERROR("Unexpected backend completion kind");
    return false;
}

soundbox_app_t *soundbox_app_create(lv_obj_t *parent,
    const soundbox_app_config_t *config)
{
    soundbox_app_t *app;
    soundbox_ui_config_t ui_config;
    size_t i;
    if (!parent || !config || config->vehicle_count > SOUNDBOX_UI_MAX_ITEMS ||
        (config->vehicle_count && !config->vehicles) ||
        (!config->vehicle_count && !config->manual)) {
        LV_LOG_ERROR("Invalid soundbox application configuration");
        return NULL;
    }
    for (i = 0; i < config->vehicle_count; ++i) {
        size_t j;
        if (!config->vehicles[i].name || config->vehicles[i].price < 1000 ||
            config->vehicles[i].price > 10000000) {
            LV_LOG_ERROR("Invalid soundbox vehicle configuration");
            return NULL;
        }
        for (j = 0; j < i; ++j) {
            if (config->vehicles[i].id == config->vehicles[j].id) {
                LV_LOG_ERROR("Duplicate soundbox vehicle ID");
                return NULL;
            }
        }
    }
    app = lv_mem_alloc(sizeof(*app));
    if (!app) {
        LV_LOG_ERROR("Soundbox application allocation failed");
        return NULL;
    }
    memset(app, 0, sizeof(*app));
    app->config = *config;
    ui_config = (soundbox_ui_config_t){event_received, app, config->simulation};
    app->ui = soundbox_ui_create(parent, &ui_config);
    if (!app->ui) {
        soundbox_app_destroy(app);
        return NULL;
    }
    if (!soundbox_ui_set_status(app->ui, config->simulation ?
        "SIMULASI OFFLINE" : "PRATINJAU - belum aktif") || !soundbox_app_home(app)) {
        soundbox_app_destroy(app);
        return NULL;
    }
    return app;
}

void soundbox_app_destroy(soundbox_app_t *app)
{
    if (app) {
        soundbox_ui_destroy(app->ui);
        lv_mem_free(app);
    }
}

bool soundbox_app_key(soundbox_app_t *app, uint32_t key)
{
    if (!app) {
        LV_LOG_ERROR("Missing soundbox application");
        return false;
    }
    if (key == LV_KEY_ESC && app->page == SOUNDBOX_HOME)
        return false;
    return soundbox_ui_key(app->ui, key);
}

void soundbox_app_tick(soundbox_app_t *app, uint32_t now)
{
    if (!app) {
        LV_LOG_ERROR("Missing soundbox application");
        return;
    }
    if (app->page == SOUNDBOX_CPM_SCAN &&
        (uint32_t)(now - app->started) >= SCAN_TIMEOUT_MS) {
        app->page = SOUNDBOX_REQUEST;
        soundbox_app_payment_complete(app, app->token, false,
            "Waktu pindai habis", now);
    } else if (app->page == SOUNDBOX_MPM_WAIT &&
        (uint32_t)(now - app->started) >= MPM_TIMEOUT_MS) {
        if (app->submit) {
            ++app->token;
            notice(app, "Status belum diketahui",
                "Waktu tunggu habis. Periksa riwayat; jangan bayar ulang tanpa pemeriksaan.");
        } else
            soundbox_app_payment_complete(app, app->token, false,
                "Waktu pembayaran habis", now);
    }
    if (app->submit && app->page == SOUNDBOX_MPM_WAIT)
        payment_countdown(app, now);
    if (app->page != SOUNDBOX_RESULT)
        return;
    if (app->config.simulation && app->print_state == SOUNDBOX_PRINT_SENDING &&
        (uint32_t)(now - app->print_started) >= MOCK_PRINT_MS) {
        soundbox_app_print_complete(app, app->token, SOUNDBOX_PRINT_SENT,
            "SIMULASI selesai - struk tidak dikirim", now);
    }
    if (app->returning) {
        uint32_t elapsed = now - app->return_started;
        uint32_t seconds;
        char countdown[80];
        if (elapsed >= AUTO_RETURN_MS) {
            soundbox_app_home(app);
            return;
        }
        seconds = (AUTO_RETURN_MS - elapsed + 999U) / 1000U;
        if (seconds == app->countdown)
            return;
        app->countdown = seconds;
        snprintf(countdown, sizeof(countdown), "%sHome dalam %lu detik",
            app->print_state == SOUNDBOX_PRINT_SENT ?
                (app->config.simulation ? "SIMULASI cetak selesai. " : "Struk terkirim. ") : "",
            (unsigned long)seconds);
        rendered(app, soundbox_ui_set_result_state(app->ui, app->print_state, countdown));
    }
}

soundbox_page_t soundbox_app_page(const soundbox_app_t *app)
{
    return app->page;
}

uint32_t soundbox_app_token(const soundbox_app_t *app)
{
    return app->token;
}
