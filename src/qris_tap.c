#include "qris_tap.h"

#include <string.h>

#ifndef SIM_BUILD
#define LOGGER_MODULE_NAME "qris-tap"
#include "libs/logger/logger.h"
#endif

#define APDU_RESPONSE_MAX 512U

static const uint8_t select_ppse[] = {
    0x00, 0xA4, 0x04, 0x00, 0x0C, '1', 'P', 'A', 'Y', '.', 'S', 'Y', 'S',
    '.', 'D', 'D', 'D', 0x00
};
static const uint8_t select_aid[] = {
    0x00, 0xA4, 0x04, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x06, 0x02, 0x20, 0x20
};
static const uint8_t read_record_1[] = {
    0x00, 0xB2, 0x01, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x06, 0x02, 0x20, 0x20
};
static const uint8_t read_record_2[] = {
    0x00, 0xB2, 0x02, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x06, 0x02, 0x20, 0x20
};

static int exchange(struct qris_tap_reader *reader, const uint8_t *command,
    size_t command_length, uint8_t response[APDU_RESPONSE_MAX], size_t *length)
{
    int ok;
    *length = APDU_RESPONSE_MAX;
    ok = reader->exchange(reader->context, command, command_length,
        response, length) == 0 && *length <= APDU_RESPONSE_MAX &&
        *length >= 2U;
#ifndef SIM_BUILD
    if (ok)
        logger_error("NFC APDU INS=%02X response length=%u SW=%02X%02X\n",
            command[1], (unsigned)*length, response[*length - 2U],
            response[*length - 1U]);
#endif
    return ok;
}

static int append(uint8_t *output, size_t *length, const uint8_t *response,
    size_t response_length)
{
    size_t data_length = response_length - 2U;
    if (data_length > QRIS_TAP_RAW_MAX - *length)
        return 0;
    memcpy(output + *length, response, data_length);
    *length += data_length;
    return 1;
}

static const char base64_digits[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void encode_base64(const uint8_t *input, size_t length, char *output)
{
    size_t i, out = 0;
    for (i = 0; i < length; i += 3U) {
        uint32_t value = (uint32_t)input[i] << 16;
        if (i + 1U < length)
            value |= (uint32_t)input[i + 1U] << 8;
        if (i + 2U < length)
            value |= input[i + 2U];
        output[out++] = base64_digits[(value >> 18) & 0x3FU];
        output[out++] = base64_digits[(value >> 12) & 0x3FU];
        output[out++] = i + 1U < length ? base64_digits[(value >> 6) & 0x3FU] : '=';
        output[out++] = i + 2U < length ? base64_digits[value & 0x3FU] : '=';
    }
    output[out] = '\0';
}

static enum qris_tap_result exchange_apdus(struct qris_tap_reader *reader,
    char *content, size_t capacity)
{
    uint8_t response[APDU_RESPONSE_MAX];
    uint8_t payload[QRIS_TAP_RAW_MAX];
    size_t length, payload_length = 0;
    int more;

    if (capacity < QR_REQUEST_CONTENT_MAX + 1U)
        return QRIS_TAP_INVALID;
    if (!exchange(reader, select_ppse, sizeof(select_ppse), response, &length)) {
#ifndef SIM_BUILD
        logger_error("NFC SELECT PPSE exchange failed\n");
#endif
        return QRIS_TAP_READER_ERROR;
    }
    /* 6A82: wallet has no PPSE; DQ72 and NetzmeSB continue to the AID select */
    if (!(response[length - 2U] == 0x6A && response[length - 1U] == 0x82) &&
        (response[length - 2U] != 0x90 || response[length - 1U] != 0x00)) {
#ifndef SIM_BUILD
        logger_error("NFC SELECT PPSE rejected: SW=%02X%02X\n",
            response[length - 2U], response[length - 1U]);
#endif
        return QRIS_TAP_READER_ERROR;
    }
    if (!exchange(reader, select_aid, sizeof(select_aid), response, &length)) {
#ifndef SIM_BUILD
        logger_error("NFC SELECT AID exchange failed\n");
#endif
        return QRIS_TAP_READER_ERROR;
    }
    if (response[length - 2U] != 0x90 || response[length - 1U] != 0x00) {
#ifndef SIM_BUILD
        logger_error("NFC SELECT AID rejected: SW=%02X%02X\n",
            response[length - 2U], response[length - 1U]);
#endif
        return QRIS_TAP_READER_ERROR;
    }
    if (!exchange(reader, read_record_1, sizeof(read_record_1), response, &length)) {
#ifndef SIM_BUILD
        logger_error("NFC READ RECORD 1 exchange failed\n");
#endif
        return QRIS_TAP_READER_ERROR;
    }

    more = response[length - 2U] == 0x61;
    if (!more && (response[length - 2U] != 0x90 ||
        response[length - 1U] != 0x00)) {
#ifndef SIM_BUILD
        logger_error("NFC READ RECORD 1 rejected: SW=%02X%02X\n",
            response[length - 2U], response[length - 1U]);
#endif
        return QRIS_TAP_READER_ERROR;
    }
    if (!append(payload, &payload_length, response, length)) {
#ifndef SIM_BUILD
        logger_error("NFC payload too large\n");
#endif
        return QRIS_TAP_INVALID;
    }
    if (more) {
        if (!exchange(reader, read_record_2, sizeof(read_record_2), response, &length)) {
#ifndef SIM_BUILD
            logger_error("NFC READ RECORD 2 exchange failed\n");
#endif
            return QRIS_TAP_READER_ERROR;
        }
        if (response[length - 2U] != 0x90 || response[length - 1U] != 0x00) {
#ifndef SIM_BUILD
            logger_error("NFC READ RECORD 2 rejected: SW=%02X%02X\n",
                response[length - 2U], response[length - 1U]);
#endif
            return QRIS_TAP_READER_ERROR;
        }
        if (!append(payload, &payload_length, response, length))
            return QRIS_TAP_INVALID;
    }
    if (!payload_length) {
#ifndef SIM_BUILD
        logger_error("NFC payload empty\n");
#endif
        return QRIS_TAP_INVALID;
    }
#ifndef SIM_BUILD
    {
        char preview[65];
        size_t i, n = payload_length < 64U ? payload_length : 64U;
        for (i = 0; i < n; ++i)
            preview[i] = payload[i] >= 0x20 && payload[i] < 0x7F ? (char)payload[i] : '.';
        preview[n] = '\0';
        logger_error("NFC payload %u bytes, starts: %s\n", (unsigned)payload_length, preview);
    }
#endif
    /* Some wallets already return base64 text; encoding it again breaks the backend */
    memcpy(content, payload, payload_length);
    content[payload_length] = '\0';
    if (memchr(payload, 0, payload_length) ||
        !qris_tap_base64_valid(content, capacity))
        encode_base64(payload, payload_length, content);
    return QRIS_TAP_OK;
}

enum qris_tap_result qris_tap_capture(struct qris_tap_reader *reader,
    char *content, size_t capacity)
{
    enum qris_tap_result result = QRIS_TAP_READER_ERROR;
    uint32_t started;
    int opened = 0, close_result;

    if (!content || capacity < QR_REQUEST_CONTENT_MAX + 1U) {
        return QRIS_TAP_INVALID;
    }
    content[0] = '\0';
    if (!reader || !reader->open || !reader->poll || !reader->exchange ||
        !reader->close || !reader->ticks || !reader->sleep || !reader->cancelled)
        return QRIS_TAP_INVALID;
    if (reader->open(reader->context) != 0)
        return QRIS_TAP_READER_ERROR;
    opened = 1;
    started = reader->ticks(reader->context);
    for (;;) {
        int polled;
        if (reader->cancelled(reader->context)) {
            result = QRIS_TAP_CANCELLED;
            break;
        }
        if ((uint32_t)(reader->ticks(reader->context) - started) >=
            QRIS_TAP_TIMEOUT_MS) {
            result = QRIS_TAP_TIMEOUT;
            break;
        }
        polled = reader->poll(reader->context);
        if (polled == QRIS_TAP_POLL_CARD) {
            result = exchange_apdus(reader, content, capacity);
            break;
        }
        if (polled != QRIS_TAP_POLL_NO_CARD) {
            result = QRIS_TAP_READER_ERROR;
            break;
        }
        reader->sleep(reader->context, QRIS_TAP_POLL_INTERVAL_MS);
    }
    if (opened) {
        close_result = reader->close(reader->context);
        if (close_result != 0 && result == QRIS_TAP_OK)
            result = QRIS_TAP_READER_ERROR;
    }
    if (result != QRIS_TAP_OK)
        content[0] = '\0';
    return result;
}

int qris_tap_base64_valid(const char *content, size_t capacity)
{
    size_t length = 0, i, padding = 0;
    if (!content)
        return 0;
    while (length < capacity && content[length])
        ++length;
    if (!length || length == capacity || (length & 3U) != 0 ||
        length > QR_REQUEST_CONTENT_MAX)
        return 0;
    for (i = 0; i < length; ++i) {
        char c = content[i];
        if (c == '=') {
            if (i < length - 2U)
                return 0;
            ++padding;
        } else if (padding || !strchr(base64_digits, c)) {
            return 0;
        }
    }
    return 1;
}

#ifndef SIM_BUILD
#include <appiccard.h>
#include <syscall.h>

struct native_reader {
    int open;
    int (*cancelled)(void *);
    void *cancel_context;
};

static int native_open(void *context)
{
    struct native_reader *reader = context;
    int result = app_icc_open(SLOT_INRF);
    if (result != ICC_OK) {
        logger_error("NFC slot open failed: result=%d\n", result);
        return -1;
    }
    reader->open = 1;
    return 0;
}

static int native_poll(void *context)
{
    char uid[32] = {0};
    (void)context;
    /* The SDK gives no way to tell "no card" from a reader fault here */
    return app_icc_ctls_powerup_seek(ICC_CPUCARD, SLOT_INRF, uid) == ICC_OK ?
        QRIS_TAP_POLL_CARD : QRIS_TAP_POLL_NO_CARD;
}

static int native_exchange(void *context, const uint8_t *command,
    size_t command_length, uint8_t *response, size_t *response_length)
{
    static const int card_types[] = {ICC_CPUCARD, ICC_RFCPU_1356};
    size_t capacity = *response_length;
    int result = -1;
    unsigned i;
    (void)context;
    for (i = 0; i < sizeof(card_types) / sizeof(card_types[0]); i++) {
        uint32_t length = (uint32_t)capacity;
        result = app_icc_comm_raw(card_types[i], SLOT_INRF,
            (uint32_t)command_length, command, &length, response);
        if (result == ICC_OK && length >= 2) {
            *response_length = length;
            return 0;
        }
        logger_error("NFC exchange failed: type=%d result=%d length=%u\n",
            card_types[i], result, (unsigned)length);
    }
    return -1;
}

static int native_close(void *context)
{
    struct native_reader *reader = context;
    int result = 0;
    if (reader->open && app_icc_close(SLOT_INRF) != ICC_OK) {
        logger_error("NFC slot close failed\n");
        result = -1;
    }
    reader->open = 0;
    return result;
}

static uint32_t native_ticks(void *context)
{
    (void)context;
    return syscall_get_tick_count();
}

static void native_sleep(void *context, uint32_t milliseconds)
{
    (void)context;
    syscall_msleep(milliseconds);
}

static int native_cancelled(void *context)
{
    struct native_reader *reader = context;
    return reader->cancelled && reader->cancelled(reader->cancel_context);
}

enum qris_tap_result qris_tap_native_capture(char *content, size_t capacity,
    int (*cancelled)(void *), void *context)
{
    struct native_reader native = {0, cancelled, context};
    struct qris_tap_reader reader = {
        &native, native_open, native_poll, native_exchange, native_close,
        native_ticks, native_sleep, native_cancelled
    };
    return qris_tap_capture(&reader, content, capacity);
}
#else
enum qris_tap_result qris_tap_native_capture(char *content, size_t capacity,
    int (*cancelled)(void *), void *context)
{
    (void)cancelled;
    (void)context;
    if (content && capacity)
        content[0] = '\0';
    return QRIS_TAP_READER_ERROR;
}
#endif
