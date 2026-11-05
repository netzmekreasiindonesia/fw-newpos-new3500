#include "soundbox_backend.h"
#include "https_client.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <cJSON.h>

static const char *response;
static int transport_result, response_status = 200, calls;
static char request[4096], requested_path[128];

int https_client_post_json_verified(const char *host, const char *path,
    const char *ca_file, const char *body, uint8_t *out, size_t capacity,
    size_t *length, int *http_status, int *submission_started)
{
    assert(strcmp(host, SOUNDBOX_BACKEND_HOST) == 0);
    assert(strcmp(ca_file, SOUNDBOX_BACKEND_CA) == 0);
    assert(strlen(body) < sizeof(request));
    strcpy(request, body);
    snprintf(requested_path, sizeof(requested_path), "%s", path);
    ++calls;
    *submission_started = 1;
    *http_status = response_status;
    *length = strlen(response);
    assert(*length <= capacity);
    memcpy(out, response, *length);
    return transport_result;
}

static struct soundbox_backend_result result;

static void execute(struct soundbox_backend_job *job, const char *body,
    enum soundbox_backend_status expected)
{
    response = body;
    soundbox_backend_execute(job, &result);
    if (result.status != expected)
        fprintf(stderr, "Expected status %d, got %d for fixture: %s\n",
            expected, result.status, body);
    assert(result.status == expected);
    assert(result.token == job->token && result.kind == job->kind);
}

static void test_receipts(void)
{
    static const char body[] = "{\"data\":{\"transaction\":{"
        "\"transaction_id\":\"T001\",\"event_type\":\"qr\",\"merchant_nmid\":\"M1\","
        "\"amount\":2000,\"paid_at_long\":1700000000,\"rrn\":\"000012345678\","
        "\"issuer_name\":\"Actual issuer\",\"payment_method\":\"QRIS\","
        "\"merchant_name\":\"Later merchant\",\"customer_pan\":\"DO NOT PRINT\"}}}";
    static const struct { const char *key; const char *value; } invalid[] = {
        {"transaction_id", "\"OTHER\""}, {"event_type", "\"other\""},
        {"merchant_nmid", "\"OTHER\""}, {"amount", "3000"}, {"amount", "2000.5"},
        {"paid_at_long", "1700000000000"}, {"paid_at_long", "0"},
        {"rrn", "1234"}, {"rrn", "\"\""}, {"rrn", "\"bad\\u001b\""},
        {"issuer_name", "\"\""}, {"payment_method", "\"QRIS TAP\""},
        {"transaction_id", "\"OTHER\\u0000T001\""}
    };
    static const char *required[] = {
        "transaction_id", "amount", "paid_at_long", "rrn", "issuer_name", "payment_method"
    };
    struct soundbox_backend_job job = {0};
    cJSON *root, *item;
    char *text;
    size_t i;
    int before;
    job.kind = SB_JOB_RECEIPT;
    job.token = 27;
    strcpy(job.transaction_id, "T001");
    strcpy(job.event_type, "qr");
    strcpy(job.receipt.transaction_id, "T001");
    strcpy(job.receipt.nmid, "M1");
    strcpy(job.receipt.merchant_name, "Original merchant");
    strcpy(job.receipt.method, "QRIS MPM");
    job.receipt.amount = 2000;
    execute(&job, body, SB_BACKEND_OK);
    assert(!strcmp(requested_path, "/api/v2/device/transaction/detail"));
    assert(!strcmp(result.data.receipt.merchant_name, "Original merchant"));
    assert(!strcmp(result.data.receipt.nmid, "M1"));
    assert(!strcmp(result.data.receipt.rrn, "000012345678"));
    assert(!strcmp(result.data.receipt.issuer, "Actual issuer"));
    assert(!strcmp(result.data.receipt.method, "QRIS MPM"));
    assert(result.data.receipt.amount == 2000 && result.data.receipt.paid_at == 1700000000);
    assert(result.delivery == SB_RECEIPT_SENT);
    assert(!strstr(request, "customer_pan") && !strstr(request, "qr_content"));
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        root = cJSON_Parse(body);
        assert(root);
        item = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "data"), "transaction");
        cJSON_ReplaceItemInObject(item, invalid[i].key, cJSON_Parse(invalid[i].value));
        text = cJSON_PrintUnformatted(root);
        assert(text);
        execute(&job, text, SB_BACKEND_INVALID);
        cJSON_free(text);
        cJSON_Delete(root);
    }
    for (i = 0; i < sizeof(required) / sizeof(required[0]); ++i) {
        root = cJSON_Parse(body);
        assert(root);
        item = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "data"), "transaction");
        cJSON_DeleteItemFromObject(item, required[i]);
        text = cJSON_PrintUnformatted(root);
        assert(text);
        execute(&job, text, SB_BACKEND_INVALID);
        cJSON_free(text);
        cJSON_Delete(root);
    }
    execute(&job, "{\"data\":{\"transaction\":{\"transaction_id\":\"T001\","
        "\"transaction_id\":\"OTHER\"}}}", SB_BACKEND_INVALID);
    execute(&job, "{\"data\":{},\"data\":{}}", SB_BACKEND_INVALID);
    execute(&job, "{\"data\":{\"transaction\":{},\"transaction\":{}}}", SB_BACKEND_INVALID);
    execute(&job, "{\"data\":{\"transaction\":{\"transaction_id\":\"T001\\u0000OTHER\","
        "\"amount\":2000,\"paid_at_long\":1700000000,\"rrn\":\"000012345678\","
        "\"issuer_name\":\"Issuer\",\"payment_method\":\"QRIS\"}}}", SB_BACKEND_INVALID);
    before = calls;
    job.transaction_id[0] = 0;
    execute(&job, body, SB_BACKEND_INVALID);
    assert(calls == before);
}

int main(void)
{
    struct soundbox_backend_job job = {0};
    cJSON *json;
    char many[6000];
    size_t offset, i;
    int before;
    job.kind = SB_JOB_QR;
    job.token = 23;
    job.amount = 5000;
    job.type = 1;
    execute(&job, "{\"status\":200,\"data\":{\"qr_content\":\"000201test\"}}", SB_BACKEND_OK);
    assert(!strcmp(result.data.qr_content, "000201test"));
    assert(!strcmp(requested_path, SOUNDBOX_BACKEND_QR_PATH));
    json = cJSON_Parse(request);
    assert(json);
    assert(!strcmp(cJSON_GetObjectItem(json, "device_id")->valuestring, SOUNDBOX_BACKEND_DEVICE_ID));
    assert(cJSON_GetObjectItem(json, "request_data")->valuedouble == 5000.0);
    assert(cJSON_GetObjectItem(json, "type")->valuedouble == 1.0);
    cJSON_Delete(json);
    execute(&job, "{\"status\":200,\"data\":{\"qr_content\":\"\"}}", SB_BACKEND_OK);
    assert(!result.data.qr_content[0]);
    execute(&job, "{\"data\":{\"qr_content\":\"x\"}}", SB_BACKEND_INVALID);
    execute(&job, "{\"status\":\"200\",\"data\":{\"qr_content\":\"x\"}}", SB_BACKEND_INVALID);
    execute(&job, "{\"status\":200,\"data\":{}}", SB_BACKEND_INVALID);
    execute(&job, "{\"status\":200,\"data\":{\"qr_content\":\"bad\\nqr\"}}", SB_BACKEND_INVALID);
    execute(&job, "{\"status\":400}", SB_BACKEND_DECLINED);
    execute(&job, "{\"status\":401}", SB_BACKEND_AUTH);
    execute(&job, "{\"status\":403}", SB_BACKEND_AUTH);
    execute(&job, "{\"status\":500}", SB_BACKEND_SERVER);
    execute(&job, "{\"status\":201}", SB_BACKEND_INVALID);
    execute(&job, "{} trailing", SB_BACKEND_INVALID);
    response_status = 503;
    execute(&job, "{}", SB_BACKEND_SERVER);
    response_status = 401;
    execute(&job, "{}", SB_BACKEND_AUTH);
    response_status = 400;
    execute(&job, "{}", SB_BACKEND_DECLINED);
    response_status = 302;
    execute(&job, "{}", SB_BACKEND_HTTP);
    response_status = 200;
    transport_result = -1;
    execute(&job, "{}", SB_BACKEND_NETWORK);
    transport_result = 0;
    before = calls;
    job.amount = 999;
    execute(&job, "{}", SB_BACKEND_INVALID);
    assert(calls == before);
    job.amount = 10000000;
    job.type = 2;
    execute(&job, "{}", SB_BACKEND_INVALID);
    assert(calls == before);
    strcpy(job.qr_content, "captured-\\\"qr\\\\token");
    execute(&job, "{\"status\":200,\"data\":{\"qr_content\":\"\"}}", SB_BACKEND_OK);
    assert(strstr(request, "\"type\":2"));
    json = cJSON_Parse(request);
    assert(json);
    assert(strcmp(cJSON_GetObjectItem(json, "qr_content")->valuestring,
        "captured-\\\"qr\\\\token") == 0);
    cJSON_Delete(json);
    job.type = 3;
    strcpy(job.qr_content, "00A1B2FF");
    execute(&job, "{\"status\":200,\"data\":{\"qr_content\":\"accepted\"}}", SB_BACKEND_OK);
    assert(strstr(request, "\"type\":3"));
    json = cJSON_Parse(request);
    assert(json);
    assert(strcmp(cJSON_GetObjectItem(json, "qr_content")->valuestring,
        "00A1B2FF") == 0);
    cJSON_Delete(json);
    /* The backend acknowledges Tap with an empty qr_content */
    execute(&job, "{\"status\":200,\"data\":{\"qr_content\":\"\"}}", SB_BACKEND_OK);
    execute(&job, "{\"status\":200,\"data\":{}}", SB_BACKEND_INVALID);
    strcpy(job.qr_content, "not-base64");
    before = calls;
    execute(&job, "{}", SB_BACKEND_INVALID);
    assert(calls == before);

    job.kind = SB_JOB_HISTORY;
    execute(&job, "{\"data\":{\"transactions\":[]}}", SB_BACKEND_OK);
    assert(!result.data.history.count && !result.data.history.truncated);
    assert(!strcmp(requested_path, "/api/v2/device/transaction/list"));
    execute(&job, "{\"data\":{\"transactions\":[{\"transaction_id\":\"T1\",\"event_type\":\"qr\",\"issuer_name\":\"Test issuer\",\"amount\":5000,\"paid_at_long\":1700000000}]}}", SB_BACKEND_OK);
    assert(result.data.history.count == 1);
    assert(!strcmp(result.data.history.entries[0].subtitle, "2023-11-15 05:13 (UTC+7)"));
    assert(!strcmp(result.data.history.entries[0].amount, "Rp 5000"));
    execute(&job, "{\"data\":{\"transactions\":[{\"transaction_id\":\"T1\",\"amount\":1}]}}", SB_BACKEND_INVALID);
    execute(&job, "{\"data\":{\"transactions\":[{\"transaction_id\":\"T1\",\"event_type\":\"qr\",\"amount\":1.5}]}}", SB_BACKEND_INVALID);
    execute(&job, "{\"data\":{\"transactions\":{}}}", SB_BACKEND_INVALID);
    offset = (size_t)snprintf(many, sizeof(many), "{\"data\":{\"transactions\":[");
    for (i = 0; i < 17; ++i)
        offset += (size_t)snprintf(many + offset, sizeof(many) - offset,
            "%s{\"transaction_id\":\"T%u\",\"event_type\":\"qr\",\"amount\":1000}",
            i ? "," : "", (unsigned)i);
    snprintf(many + offset, sizeof(many) - offset, "]}}");
    execute(&job, many, SB_BACKEND_OK);
    assert(result.data.history.count == 16 && result.data.history.truncated);

    job.kind = SB_JOB_DETAIL;
    strcpy(job.transaction_id, "T\"1");
    strcpy(job.event_type, "qr");
    execute(&job, "{\"data\":{\"transaction\":{\"transaction_id\":\"T1\",\"amount\":5000,\"customer_pan\":\"REDACTED\",\"merchant_pan\":\"REDACTED\"}}}", SB_BACKEND_OK);
    assert(!strcmp(requested_path, "/api/v2/device/transaction/detail"));
    json = cJSON_Parse(request);
    assert(json);
    assert(!strcmp(cJSON_GetObjectItem(json, "transaction_id")->valuestring, "T\"1"));
    cJSON_Delete(json);
    assert(!strcmp(result.data.detail[0].value, "T1"));
    assert(!strcmp(result.data.detail[3].value, "Rp 5000"));
    for (i = 0; i < SOUNDBOX_DETAIL_MAX && result.data.detail[i].label; ++i)
        assert(!strstr(result.data.detail[i].value, "REDACTED"));
    execute(&job, "{\"data\":{\"transaction\":{\"amount\":-1}}}", SB_BACKEND_INVALID);
    execute(&job, "{\"data\":{\"transaction\":[]}}", SB_BACKEND_INVALID);
    test_receipts();
    puts("Parking backend request/parser tests passed.");
    return 0;
}
