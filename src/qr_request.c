#include "qr_request.h"

#include <stdint.h>
#include <string.h>

#include <cJSON.h>
#include <httprequest.h>

#include "https_client.h"

#define QR_REQUEST_HOST "soundbox-notify-stg.netzme.com"
#define QR_REQUEST_PATH "/api/v2/device/generate-qr"
#define QR_RESPONSE_MAX 2048U

static const char request_body[] =
    "{\"device_id\":\"" SOUNDBOX_BACKEND_DEVICE_ID "\",\"request_data\":3666,\"type\":1,"
    "\"qr_content\":\"\",\"invoice_trx_id\":\"\",\"qr_content_id\":\"\"}";

static int parse_response(const uint8_t *body, size_t body_length,
    char *qr_content, size_t capacity)
{
    char json[QR_RESPONSE_MAX + 1U];
    cJSON *root;
    cJSON *status;
    cJSON *data;
    cJSON *content;
    size_t length;
    size_t index;
    int valid = 0;

    if (!body || !body_length || body_length > QR_RESPONSE_MAX ||
        memchr(body, '\0', body_length))
        return 0;
    memcpy(json, body, body_length);
    json[body_length] = '\0';
    root = cJSON_ParseWithOpts(json, NULL, 1);
    if (!root)
        return 0;

    status = cJSON_GetObjectItemCaseSensitive(root, "status");
    data = cJSON_GetObjectItemCaseSensitive(root, "data");
    content = cJSON_GetObjectItemCaseSensitive(data, "qr_content");
    if (!cJSON_IsObject(root) || !cJSON_IsNumber(status) ||
        status->valueint != 200 || status->valuedouble < 200.0 ||
        status->valuedouble > 200.0 ||
        !cJSON_IsString(content) || !content->valuestring)
        goto cleanup;

    length = strlen(content->valuestring);
    if (!length || length > QR_REQUEST_CONTENT_MAX || length >= capacity)
        goto cleanup;
    for (index = 0; index < length; ++index) {
        unsigned char character = (unsigned char)content->valuestring[index];
        if (character < 0x20 || character > 0x7e)
            goto cleanup;
    }
    memcpy(qr_content, content->valuestring, length + 1U);
    valid = 1;

cleanup:
    cJSON_Delete(root);
    return valid;
}

enum qr_request_result qr_request_generate(char *qr_content, size_t capacity,
    int *http_status)
{
    uint8_t response[QR_RESPONSE_MAX];
    size_t response_length = 0;
    int result;

    if (!qr_content || !capacity || !http_status)
        return QR_REQUEST_INVALID_RESPONSE;
    qr_content[0] = '\0';
    *http_status = 0;
    result = https_client_post_json(QR_REQUEST_HOST, QR_REQUEST_PATH,
        request_body, response, sizeof(response), &response_length, http_status);
    if (result != 0)
        return QR_REQUEST_NETWORK_FAILED;
    if (*http_status != 200)
        return QR_REQUEST_SERVER_FAILED;
    if (!parse_response(response, response_length, qr_content, capacity))
        return QR_REQUEST_INVALID_RESPONSE;
    return QR_REQUEST_OK;
}
