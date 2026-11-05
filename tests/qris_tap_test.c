#include "qris_tap.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

struct fake_reader {
    uint32_t now;
    unsigned polls;
    unsigned exchanges;
    unsigned closes;
    unsigned card_after;
    int open_result;
    int poll_error;
    int cancel_after_sleep;
    int cancelled;
    int close_result;
    int response_case;
};

static const uint8_t expected_commands[][18] = {
    {0x00, 0xA4, 0x04, 0x00, 0x0C, '1', 'P', 'A', 'Y', '.', 'S', 'Y', 'S',
     '.', 'D', 'D', 'D', 0x00},
    {0x00, 0xA4, 0x04, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x06, 0x02, 0x20, 0x20},
    {0x00, 0xB2, 0x01, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x06, 0x02, 0x20, 0x20},
    {0x00, 0xB2, 0x02, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x06, 0x02, 0x20, 0x20}
};

static int fake_open(void *context)
{
    return ((struct fake_reader *)context)->open_result;
}

static int fake_poll(void *context)
{
    struct fake_reader *reader = context;
    if (reader->poll_error)
        return QRIS_TAP_POLL_ERROR;
    return reader->polls++ >= reader->card_after ?
        QRIS_TAP_POLL_CARD : QRIS_TAP_POLL_NO_CARD;
}

static int fake_exchange(void *context, const uint8_t *command,
    size_t command_length, uint8_t *response, size_t *response_length)
{
    struct fake_reader *reader = context;
    unsigned index = reader->exchanges++;
    size_t expected_length = index == 0 ? 18U : 12U;
    assert(index < 4U);
    assert(command_length == expected_length);
    assert(memcmp(command, expected_commands[index], expected_length) == 0);
    assert(*response_length >= 5U);
    if (index == 0U && reader->response_case == 4) {
        response[0] = 0x6A;
        response[1] = 0x82;
        *response_length = 2;
    } else if (index < 2U) {
        response[0] = 0x90;
        response[1] = 0x00;
        *response_length = 2;
    } else if (index == 2U && reader->response_case == 1) {
        response[0] = 0x00;
        response[1] = 0x22;
        response[2] = 0xAB;
        response[3] = 0x61;
        response[4] = 0x00;
        *response_length = 5;
    } else if (reader->response_case == 3) {
        /* two 510-byte records exceed the raw payload cap */
        memset(response, 0x5A, 510U);
        response[510] = index == 2U ? 0x61 : 0x90;
        response[511] = 0x00;
        *response_length = 512;
    } else if (index == 3U) {
        response[0] = 0xFF;
        response[1] = 0x10;
        response[2] = 0x90;
        response[3] = 0x00;
        *response_length = 4;
    } else if (index == 2U && reader->response_case == 2) {
        response[0] = 0x6A;
        response[1] = 0x82;
        *response_length = 2;
    } else if (reader->response_case == 5) {
        memcpy(response, "MzM=\x90\x00", 6);
        *response_length = 6;
    } else {
        response[0] = 0x33;
        response[1] = 0x90;
        response[2] = 0x00;
        *response_length = 3;
    }
    return 0;
}

static int fake_close(void *context)
{
    struct fake_reader *reader = context;
    ++reader->closes;
    return reader->close_result;
}

static uint32_t fake_ticks(void *context)
{
    return ((struct fake_reader *)context)->now;
}

static void fake_sleep(void *context, uint32_t milliseconds)
{
    struct fake_reader *reader = context;
    assert(milliseconds == QRIS_TAP_POLL_INTERVAL_MS);
    reader->now += milliseconds;
    if (reader->cancel_after_sleep)
        reader->cancelled = 1;
}

static int fake_cancelled(void *context)
{
    return ((struct fake_reader *)context)->cancelled;
}

static enum qris_tap_result capture(struct fake_reader *state, char *content,
    size_t capacity)
{
    struct qris_tap_reader reader = {
        state, fake_open, fake_poll, fake_exchange, fake_close,
        fake_ticks, fake_sleep, fake_cancelled
    };
    return qris_tap_capture(&reader, content, capacity);
}

int main(void)
{
    struct fake_reader state = {0};
    char content[QR_REQUEST_CONTENT_MAX + 1U];

    state.response_case = 1;
    state.card_after = 1;
    assert(capture(&state, content, sizeof(content)) == QRIS_TAP_OK);
    assert(strcmp(content, "ACKr/xA=") == 0);
    assert(state.polls == 2 && state.exchanges == 4 && state.closes == 1);

    memset(&state, 0, sizeof(state));
    assert(capture(&state, content, sizeof(content)) == QRIS_TAP_OK);
    assert(strcmp(content, "Mw==") == 0);
    assert(state.exchanges == 3 && state.closes == 1);

    memset(&state, 0, sizeof(state));
    state.response_case = 4;
    assert(capture(&state, content, sizeof(content)) == QRIS_TAP_OK);
    assert(strcmp(content, "Mw==") == 0 && state.exchanges == 3);

    memset(&state, 0, sizeof(state));
    state.response_case = 5;
    assert(capture(&state, content, sizeof(content)) == QRIS_TAP_OK);
    assert(strcmp(content, "MzM=") == 0);

    memset(&state, 0, sizeof(state));
    state.response_case = 2;
    assert(capture(&state, content, sizeof(content)) == QRIS_TAP_READER_ERROR);
    assert(state.exchanges == 3 && state.closes == 1 && !content[0]);

    memset(&state, 0, sizeof(state));
    state.response_case = 3;
    assert(capture(&state, content, sizeof(content)) == QRIS_TAP_INVALID);
    assert(state.closes == 1 && !content[0]);

    memset(&state, 0, sizeof(state));
    state.poll_error = 1;
    assert(capture(&state, content, sizeof(content)) == QRIS_TAP_READER_ERROR);
    assert(state.exchanges == 0 && state.closes == 1);

    memset(&state, 0, sizeof(state));
    state.card_after = UINT_MAX;
    state.cancel_after_sleep = 1;
    assert(capture(&state, content, sizeof(content)) == QRIS_TAP_CANCELLED);
    assert(state.closes == 1 && !content[0]);

    memset(&state, 0, sizeof(state));
    assert(capture(&state, content, sizeof(content) - 1U) == QRIS_TAP_INVALID);
    assert(!state.closes);

    memset(&state, 0, sizeof(state));
    state.card_after = UINT_MAX;
    assert(capture(&state, content, sizeof(content)) == QRIS_TAP_TIMEOUT);
    assert(state.closes == 1 && !content[0]);

    memset(&state, 0, sizeof(state));
    state.open_result = -1;
    assert(capture(&state, content, sizeof(content)) == QRIS_TAP_READER_ERROR);
    assert(!state.closes);

    assert(qris_tap_base64_valid("AKH/", 5));
    assert(qris_tap_base64_valid("MzM=", 5));
    assert(!qris_tap_base64_valid("", 1));
    assert(!qris_tap_base64_valid("ABC", 4));
    assert(!qris_tap_base64_valid("AB=C", 5));
    assert(!qris_tap_base64_valid("AB-C", 5));
    puts("QRIS Tap APDU and lifecycle tests passed.");
    return 0;
}
