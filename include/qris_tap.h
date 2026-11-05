#ifndef PRINTER_DEMO_QRIS_TAP_H
#define PRINTER_DEMO_QRIS_TAP_H

#include <stddef.h>
#include <stdint.h>

#include "qr_request.h"

#define QRIS_TAP_RAW_MAX 512U
#define QRIS_TAP_TIMEOUT_MS 60000U
#define QRIS_TAP_POLL_INTERVAL_MS 50U

enum qris_tap_result {
    QRIS_TAP_OK,
    QRIS_TAP_CANCELLED,
    QRIS_TAP_TIMEOUT,
    QRIS_TAP_READER_ERROR,
    QRIS_TAP_INVALID
};

enum qris_tap_poll_result {
    QRIS_TAP_POLL_ERROR = -1,
    QRIS_TAP_POLL_NO_CARD,
    QRIS_TAP_POLL_CARD
};

struct qris_tap_reader {
    void *context;
    int (*open)(void *context);
    int (*poll)(void *context);
    int (*exchange)(void *context, const uint8_t *command,
        size_t command_length, uint8_t *response, size_t *response_length);
    int (*close)(void *context);
    uint32_t (*ticks)(void *context);
    void (*sleep)(void *context, uint32_t milliseconds);
    int (*cancelled)(void *context);
};

enum qris_tap_result qris_tap_capture(struct qris_tap_reader *reader,
    char *content, size_t capacity);
enum qris_tap_result qris_tap_native_capture(char *content, size_t capacity,
    int (*cancelled)(void *), void *context);
int qris_tap_base64_valid(const char *content, size_t capacity);

#endif
