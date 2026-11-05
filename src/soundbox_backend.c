#include "soundbox_backend.h"
#include "https_client.h"
#include "qris_tap.h"

#include <cJSON.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SIM_BUILD
#define LOGGER_MODULE_NAME "backend"
#include "libs/logger/logger.h"
#endif

#define RESPONSE_MAX 14000U

static bool copy_text(const cJSON *object, const char *key, char *out,
    size_t capacity, bool required)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    size_t i, length;
    if (!value) {
        snprintf(out, capacity, "%s", required ? "" : "-");
        return !required;
    }
    if (!cJSON_IsString(value) || !value->valuestring)
        return false;
    length = strlen(value->valuestring);
    if (length >= capacity || (required && !length))
        return false;
    for (i = 0; i < length; ++i)
        if ((unsigned char)value->valuestring[i] < 0x20U ||
            (unsigned char)value->valuestring[i] == 0x7fU)
            return false;
    memcpy(out, value->valuestring, length + 1U);
    return true;
}

static bool number(const cJSON *object, const char *key, double maximum,
    uint64_t *out, bool required)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!value && !required) {
        *out = 0;
        return true;
    }
    if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) ||
        value->valuedouble < 0 || value->valuedouble > maximum ||
        value->valuedouble - floor(value->valuedouble) > 0.0)
        return false;
    *out = (uint64_t)value->valuedouble;
    return true;
}

static bool display_amount(const cJSON *object, char out[32], bool required)
{
    uint64_t amount;
    if (!number(object, "amount", 100000000.0, &amount, required))
        return false;
    snprintf(out, 32, "Rp %llu", (unsigned long long)amount);
    return true;
}

static bool display_date(const cJSON *object, char out[96])
{
    uint64_t seconds;
    uint32_t date;
    if (!number(object, "paid_at_long", 4102419599.0, &seconds, false))
        return false;
    if (!seconds) {
        strcpy(out, "-");
        return true;
    }
    date = soundbox_business_date((int64_t)seconds);
    if (!date)
        return false;
    seconds += 7U * 3600U;
    snprintf(out, 96, "%04u-%02u-%02u %02u:%02u (UTC+7)",
        (unsigned)(date / 10000U), (unsigned)(date / 100U % 100U),
        (unsigned)(date % 100U), (unsigned)(seconds / 3600U % 24U),
        (unsigned)(seconds / 60U % 60U));
    return true;
}

static bool parse_history(const cJSON *data, struct soundbox_backend_result *result)
{
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(data, "transactions");
    const cJSON *item;
    if (!cJSON_IsArray(array))
        return false;
    cJSON_ArrayForEach(item, array) {
        struct soundbox_history_entry *entry;
        if (result->data.history.count == SOUNDBOX_HISTORY_MAX) {
            result->data.history.truncated = true;
            break;
        }
        entry = &result->data.history.entries[result->data.history.count];
        if (!cJSON_IsObject(item) ||
            !copy_text(item, "transaction_id", entry->transaction_id, sizeof(entry->transaction_id), true) ||
            !copy_text(item, "event_type", entry->event_type, sizeof(entry->event_type), true) ||
            !copy_text(item, "issuer_name", entry->title, sizeof(entry->title), false) ||
            !display_amount(item, entry->amount, true) ||
            !display_date(item, entry->subtitle))
            return false;
        ++result->data.history.count;
    }
    return true;
}

static bool parse_detail(const cJSON *data, struct soundbox_backend_result *result)
{
    static const struct { const char *key; const char *label; } fields[] = {
        {"transaction_id", "ID transaksi"}, {"issuer_name", "Penerbit"},
        {"customer_name", "Pelanggan"}, {"amount", "Nominal"},
        {"paid_at_long", "Waktu"}, {"rrn", "RRN"}, {"terminal_id", "Terminal"},
        {"acquirer_name", "Acquirer"}, {"merchant_name", "Merchant"},
        {"merchant_location", "Lokasi"}, {"product_indicator", "Produk"},
        {"payment_method", "Metode"}, {"reference_id", "Referensi"}
    };
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(data, "transaction");
    size_t i;
    if (!cJSON_IsObject(item))
        return false;
    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        struct soundbox_detail_entry *entry = &result->data.detail[i];
        entry->label = fields[i].label;
        if (i == 3) {
            if (!display_amount(item, entry->value, false))
                return false;
        } else if (i == 4) {
            if (!display_date(item, entry->value))
                return false;
        } else if (!copy_text(item, fields[i].key, entry->value, sizeof(entry->value), false))
            return false;
    }
    return true;
}

static bool receipt_object_valid(const cJSON *object)
{
    const cJSON *a, *b;
    if (!cJSON_IsObject(object))
        return false;
    for (a = object->child; a; a = a->next)
        for (b = a->next; b; b = b->next)
            if (!a->string || !b->string || strcmp(a->string, b->string) == 0)
                return false;
    return true;
}

static bool parse_receipt(const cJSON *data, const struct soundbox_backend_job *job,
    struct soundbox_backend_result *result)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(data, "transaction");
    struct print_receipt *receipt = &result->data.receipt;
    char transaction_id[96], event_type[64];
    char method[32], nmid[64];
    uint64_t amount;
    if (!receipt_object_valid(item) ||
        !copy_text(item, "transaction_id", transaction_id, sizeof(transaction_id), true) ||
        strcmp(transaction_id, job->transaction_id) != 0 ||
        strcmp(transaction_id, job->receipt.transaction_id) != 0 ||
        !number(item, "amount", 100000000.0, &amount, true) ||
        amount != job->receipt.amount)
        return false;
    if (cJSON_GetObjectItemCaseSensitive(item, "event_type") &&
        (!copy_text(item, "event_type", event_type, sizeof(event_type), true) ||
         strcmp(event_type, job->event_type) != 0))
        return false;
    if (cJSON_GetObjectItemCaseSensitive(item, "merchant_nmid") &&
        (!copy_text(item, "merchant_nmid", nmid, sizeof(nmid), true) ||
         strcmp(nmid, job->receipt.nmid) != 0))
        return false;
    if (!copy_text(item, "payment_method", method, sizeof(method), true) ||
        (strcmp(method, job->receipt.method) != 0 &&
         strcmp(method, "QRIS") != 0 &&
         !(strcmp(method, "MPM") == 0 && strcmp(job->receipt.method, "QRIS MPM") == 0)))
        return false;
    *receipt = job->receipt;
    return number(item, "paid_at_long", 4102419599.0, &receipt->paid_at, true) &&
        soundbox_business_date((int64_t)receipt->paid_at) != 0 &&
        copy_text(item, "rrn", receipt->rrn, sizeof(receipt->rrn), true) &&
        copy_text(item, "issuer_name", receipt->issuer, sizeof(receipt->issuer), true);
}

void soundbox_backend_execute(const struct soundbox_backend_job *job,
    struct soundbox_backend_result *result)
{
    cJSON *request = NULL, *root = NULL;
    const cJSON *status, *data;
    char *body = NULL;
    uint8_t *response = NULL;
    size_t length = 0;
    uint64_t code;
    const char *path;
    bool valid = false;
    memset(result, 0, sizeof(*result));
    result->kind = job->kind;
    result->token = job->token;
    result->attempt = job->attempt;
    result->status = SB_BACKEND_INVALID;
    if (job->kind != SB_JOB_QR && job->kind != SB_JOB_HISTORY &&
        job->kind != SB_JOB_DETAIL && job->kind != SB_JOB_RECEIPT)
        return;
    request = cJSON_CreateObject();
    if (!request || !cJSON_AddStringToObject(request, "device_id", SOUNDBOX_BACKEND_DEVICE_ID))
        goto cleanup;
    path = job->kind == SB_JOB_QR ? SOUNDBOX_BACKEND_QR_PATH :
        job->kind == SB_JOB_HISTORY ? "/api/v2/device/transaction/list" :
            "/api/v2/device/transaction/detail";
    if (job->kind == SB_JOB_QR) {
        if (job->amount < 1000U || job->amount > 10000000U ||
            job->type < 1U || job->type > 3U ||
            !memchr(job->qr_content, 0, sizeof(job->qr_content)) ||
            (job->type != 1 && !job->qr_content[0]) ||
            (job->type == 3 &&
             !qris_tap_base64_valid(job->qr_content, sizeof(job->qr_content))) ||
            !cJSON_AddNumberToObject(request, "request_data", job->amount) ||
            !cJSON_AddNumberToObject(request, "type", job->type) ||
            !cJSON_AddStringToObject(request, "qr_content", job->qr_content) ||
            (job->type != 3 &&
             (!cJSON_AddStringToObject(request, "invoice_trx_id", "") ||
              !cJSON_AddStringToObject(request, "qr_content_id", ""))))
            goto cleanup;
    } else if (job->kind == SB_JOB_DETAIL || job->kind == SB_JOB_RECEIPT) {
        if (!memchr(job->transaction_id, 0, sizeof(job->transaction_id)) ||
            !memchr(job->event_type, 0, sizeof(job->event_type)) ||
            !job->transaction_id[0] || !job->event_type[0] ||
            !cJSON_AddStringToObject(request, "transaction_id", job->transaction_id) ||
            !cJSON_AddStringToObject(request, "event_type", job->event_type))
            goto cleanup;
        if (job->kind == SB_JOB_RECEIPT &&
            (!memchr(job->receipt.transaction_id, 0, sizeof(job->receipt.transaction_id)) ||
             !memchr(job->receipt.method, 0, sizeof(job->receipt.method)) ||
             !memchr(job->receipt.nmid, 0, sizeof(job->receipt.nmid))))
            goto cleanup;
    }
    body = cJSON_PrintUnformatted(request);
    response = malloc(RESPONSE_MAX + 1U);
    if (!body || !response)
        goto cleanup;
    result->status = SB_BACKEND_NETWORK;
#ifndef SIM_BUILD
    if (job->kind == SB_JOB_QR && job->type == 3) {
        size_t offset, total = strlen(body);
        logger_error("Tap request POST %s, %u bytes\n", path, (unsigned)total);
        for (offset = 0; offset < total; offset += 200U)
            logger_error("Tap request body[%u]: %.200s\n", (unsigned)offset, body + offset);
    }
#endif
    if (https_client_post_json_verified(SOUNDBOX_BACKEND_HOST, path,
        SOUNDBOX_BACKEND_CA, body, response, RESPONSE_MAX, &length,
        &result->http_status, &result->submission_started) != 0)
        goto cleanup;
    result->status = result->http_status == 400 ? SB_BACKEND_DECLINED :
        result->http_status == 401 || result->http_status == 403 ? SB_BACKEND_AUTH :
        result->http_status >= 500 ? SB_BACKEND_SERVER : SB_BACKEND_HTTP;
#ifndef SIM_BUILD
    if (job->kind == SB_JOB_QR && result->http_status != 200) {
        char text[257];
        size_t i, n = length < sizeof(text) - 1U ? length : sizeof(text) - 1U;
        for (i = 0; i < n; ++i)
            text[i] = response[i] >= 0x20 && response[i] < 0x7F ? (char)response[i] : '.';
        text[n] = '\0';
        logger_error("QR request HTTP %d, %u bytes: %s\n", result->http_status,
            (unsigned)length, text);
    }
#endif
    if (result->http_status != 200)
        goto cleanup;
    result->status = SB_BACKEND_INVALID;
    if (!length || length > RESPONSE_MAX || memchr(response, 0, length))
        goto cleanup;
    response[length] = 0;
    if (job->kind == SB_JOB_RECEIPT && strstr((char *)response, "\\u0000"))
        goto cleanup;
    root = cJSON_ParseWithOpts((char *)response, NULL, 1);
    if (!cJSON_IsObject(root))
        goto cleanup;
    status = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (status) {
        if (!number(root, "status", 99999.0, &code, true))
            goto cleanup;
        if (code != 200U) {
            if (code == 400U)
                result->status = SB_BACKEND_DECLINED;
            else if (code == 401U || code == 403U)
                result->status = SB_BACKEND_AUTH;
            else if (code >= 500U)
                result->status = SB_BACKEND_SERVER;
            goto cleanup;
        }
    }
    data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsObject(data))
        goto cleanup;
    if (job->kind == SB_JOB_RECEIPT &&
        (!receipt_object_valid(root) || !receipt_object_valid(data)))
        goto cleanup;
    if (job->kind == SB_JOB_QR)
        valid = status && copy_text(data, "qr_content", result->data.qr_content,
            sizeof(result->data.qr_content), false) &&
            cJSON_IsString(cJSON_GetObjectItemCaseSensitive(data, "qr_content"));
    else if (job->kind == SB_JOB_HISTORY)
        valid = parse_history(data, result);
    else if (job->kind == SB_JOB_RECEIPT) {
        valid = parse_receipt(data, job, result);
        if (valid)
            result->delivery = SB_RECEIPT_SENT;
    } else
        valid = parse_detail(data, result);
    if (valid)
        result->status = SB_BACKEND_OK;
cleanup:
    cJSON_Delete(root);
    cJSON_Delete(request);
    cJSON_free(body);
    free(response);
}

const char *soundbox_backend_message(enum soundbox_backend_status status)
{
    switch (status) {
    case SB_BACKEND_OK: return "Permintaan selesai.";
    case SB_BACKEND_NETWORK: return "Koneksi HTTPS gagal. Periksa jaringan, CA dan jam.";
    case SB_BACKEND_HTTP: return "Layanan mengembalikan kesalahan HTTP.";
    case SB_BACKEND_DECLINED: return "Permintaan ditolak.";
    case SB_BACKEND_AUTH: return "Autentikasi ditolak.";
    case SB_BACKEND_SERVER: return "Layanan sedang bermasalah.";
    case SB_BACKEND_STORAGE: return "Penyimpanan gagal / transaksi sebelumnya belum selesai.";
    case SB_BACKEND_UNAVAILABLE: return "Layanan jaringan belum tersedia.";
    default: return "Respons layanan tidak valid.";
    }
}
