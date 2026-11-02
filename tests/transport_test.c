#include "printer_transport.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <bluetooth.h>

static uint32_t ticks;
static int open_calls;
static int connect_error;
static int ready_value;
static int setup_error;
static int write_error;
static int fail_write;
static int writes;
static int close_calls;
static int disconnect_calls;
static int close_error;
static int disconnect_error;
static int open_error;
static int connect_calls;
static int scan_error;
static int scan_list_error;
static int scan_count;
static int scan_start_calls;
static int scan_stop_calls;
static int scan_stop_error;
static int pair_error;
static int pair_status_error;
static uint32_t pair_status;
static int pair_calls;
static int scan_matches;
static int scan_type;
static int pair_pending_polls;
static int scan_ambiguous;
static unsigned int delay_ms;
static uint8_t received[1024];
static size_t received_length;
static struct printer_connection config = {
    1, {6, 2, 0x31, 0x98, 0x50, 0x95}, 1, 0xff00, 0xff02, 50, -1
};

uint32_t syscall_get_tick_count(void) { return ticks; }
int syscall_msleep(int duration) { ticks += (uint32_t)duration; return 0; }

int bt_open(const char *path, int flags)
{
    (void)flags;
    assert(strcmp(path, "/dev/bt") == 0);
    ++open_calls;
    return open_error ? -1 : 7;
}

int bt_ble_connect(int fd, uint8_t *address, int type)
{
    assert(fd == 7 && type == config.address_type);
    assert(memcmp(address, config.address, 6) == 0);
    assert(scan_start_calls == 0 || scan_stop_calls > 0);
    ++connect_calls;
    return connect_error;
}

int bt_scan_start(int fd) { assert(fd == 7); ++scan_start_calls; return scan_error; }
int bt_scan_stop(int fd) { assert(fd == 7); ++scan_stop_calls; return scan_stop_error; }

int bt_get_scan_list(int fd, int *count, ble_device_info_t *list, uint32_t size)
{
    assert(fd == 7 && size == MAX_DEVICE_NUM * sizeof(*list));
    *count = scan_count;
    if (scan_count > 0 && scan_count <= MAX_DEVICE_NUM) {
        memset(list, 0, size);
        if (scan_matches)
            memcpy(list[scan_count - 1].mac_addr, config.address, 6);
        list[scan_count - 1].addr_type = (uint8_t)scan_type;
        if (scan_ambiguous && scan_count > 1) {
            memcpy(list[0].mac_addr, config.address, 6);
            list[0].addr_type = (uint8_t)(scan_type + 1);
        }
    }
    return scan_list_error;
}

int bt_ble_start_pairing(int fd) { assert(fd == 7); ++pair_calls; return pair_error; }

int bt_get_pairing_status(int fd, uint32_t *status, uint32_t *nv_count)
{
    assert(fd == 7);
    *status = pair_pending_polls-- > 0 ? PAIRING_IDLE : pair_status;
    *nv_count = 1;
    return pair_status_error;
}

int bt_get_status(int fd) { assert(fd == 7); return ready_value; }

int bt_ble_set_gattc_write(int fd, uint16_t service, uint16_t characteristic)
{
    assert(fd == 7 && service == 0xff00 && characteristic == 0xff02);
    return setup_error;
}

int bt_ble_write_data(int fd, uint16_t service, uint16_t characteristic,
    const void *data, uint32_t length)
{
    assert(fd == 7 && service == 0xff00 && characteristic == 0xff02);
    assert(length > 0 && length <= 20);
    ++writes;
    ticks += delay_ms;
    if (writes == fail_write)
        return write_error;
    assert(received_length + length <= sizeof(received));
    memcpy(received + received_length, data, length);
    received_length += length;
    return 0;
}

int bt_disconnect(int fd) { assert(fd == 7); ++disconnect_calls; return disconnect_error; }
int bt_close(int fd) { assert(fd == 7); ++close_calls; return close_error; }

static void reset(void)
{
    ticks = 0;
    open_calls = writes = close_calls = disconnect_calls = 0;
    connect_error = setup_error = open_error = close_error = disconnect_error = 0;
    ready_value = 1 << 5;
    write_error = -11;
    fail_write = 0;
    delay_ms = 0;
    received_length = 0;
    config.fd = -1;
    config.address_type = 1;
    connect_calls = scan_start_calls = scan_stop_calls = pair_calls = 0;
    scan_error = scan_list_error = scan_stop_error = pair_error = pair_status_error = 0;
    scan_count = 2;
    scan_matches = 1;
    scan_type = 1;
    pair_status = PAIRING_SUCCESS;
    pair_pending_polls = scan_ambiguous = 0;
}

int main(void)
{
    uint8_t payload[45];
    struct printer_report report;
    size_t index;
    for (index = 0; index < sizeof(payload); ++index)
        payload[index] = (uint8_t)index;
    reset();
    config.verified = 0;
    report = printer_send(&config, payload, sizeof(payload));
    assert(report.status == PRINTER_CONFIG_REQUIRED && open_calls == 0);
    config.verified = 1;
    config.address_type = -1;
    assert(printer_send(&config, payload, sizeof(payload)).status == PRINTER_CONFIG_REQUIRED);
    assert(open_calls == 0);
    config.address_type = 1;
    assert(printer_send(NULL, payload, sizeof(payload)).status == PRINTER_CONFIG_REQUIRED);
    assert(printer_send(&config, NULL, sizeof(payload)).status == PRINTER_CONFIG_REQUIRED);
    assert(printer_send(&config, payload, 0).status == PRINTER_CONFIG_REQUIRED);
    report = printer_send(&config, payload, sizeof(payload));
    assert(report.status == PRINTER_SENT && report.accepted_bytes == sizeof(payload));
    assert(writes == 3 && received_length == sizeof(payload));
    assert(memcmp(payload, received, sizeof(payload)) == 0);
    assert(close_calls == 0 && disconnect_calls == 0 && ticks == 150);
    assert(config.fd == 7);
    assert(printer_send(&config, payload, 1).status == PRINTER_SENT);
    assert(connect_calls == 1 && open_calls == 1);
    assert(printer_release(&config) == 0 && config.fd == -1);
    assert(close_calls == 1 && disconnect_calls == 1);
    reset();
    fail_write = 2;
    report = printer_send(&config, payload, sizeof(payload));
    assert(report.status == PRINTER_WRITE_FAILED && report.accepted_bytes == 20);
    assert(report.write_attempted && report.sdk_error == -11 && writes == 2);
    assert(close_calls == 1 && disconnect_calls == 1);
    reset();
    fail_write = 1;
    write_error = 20;
    report = printer_send(&config, payload, sizeof(payload));
    assert(report.status == PRINTER_WRITE_FAILED && report.accepted_bytes == 0);
    assert(report.write_attempted && writes == 1);
    reset();
    connect_error = -1;
    assert(printer_send(&config, payload, sizeof(payload)).status == PRINTER_CONNECT_FAILED);
    assert(writes == 0 && close_calls == 1 && disconnect_calls == 1);
    reset();
    open_error = 1;
    assert(printer_send(&config, payload, sizeof(payload)).status == PRINTER_OPEN_FAILED);
    assert(close_calls == 0 && disconnect_calls == 0);
    reset();
    setup_error = -1;
    assert(printer_send(&config, payload, sizeof(payload)).status == PRINTER_NOT_READY);
    assert(writes == 0 && close_calls == 1);
    reset();
    ready_value = -1;
    assert(printer_send(&config, payload, sizeof(payload)).status == PRINTER_NOT_READY);
    reset();
    ready_value = 0;
    report = printer_send(&config, payload, sizeof(payload));
    assert(report.status == PRINTER_DEADLINE && ticks == 60000);
    assert(writes == 0 && close_calls == 1 && disconnect_calls == 1);
    reset();
    ticks = UINT32_MAX - 100;
    assert(printer_send(&config, payload, sizeof(payload)).status == PRINTER_SENT);
    reset();
    delay_ms = 60000;
    report = printer_send(&config, payload, sizeof(payload));
    assert(report.status == PRINTER_DEADLINE && writes == 1);
    assert(report.accepted_bytes == 20 && report.write_attempted);
    reset();
    close_error = -1;
    assert(printer_send(&config, payload, sizeof(payload)).status == PRINTER_SENT);
    assert(printer_release(&config) == -1 && config.fd == -1);
    reset();
    disconnect_error = -1;
    assert(printer_send(&config, payload, sizeof(payload)).status == PRINTER_SENT);
    assert(printer_release(&config) == -1 && config.fd == -1);
    reset();
    disconnect_error = 1;
    close_error = 1;
    assert(printer_send(&config, payload, sizeof(payload)).status == PRINTER_SENT);
    assert(printer_release(&config) == 0);
    reset();
    config.verified = 0;
    config.address_type = -1;
    scan_type = 0;
    report = printer_scan_pair(&config);
    assert(report.status == PRINTER_PAIRED && config.fd == 7);
    assert(config.address_type == 0 && scan_start_calls == 1 && scan_stop_calls == 1);
    assert(pair_calls == 1 && writes == 0 && close_calls == 0);
    assert(printer_send(&config, payload, sizeof(payload)).status == PRINTER_CONFIG_REQUIRED);
    assert(writes == 0 && config.fd == 7);
    config.verified = 1;
    assert(printer_send(&config, payload, sizeof(payload)).status == PRINTER_SENT);
    assert(open_calls == 1 && connect_calls == 1);
    assert(printer_release(&config) == 0);
    assert(printer_release(&config) == 0 && close_calls == 1);
    reset();
    scan_matches = 0;
    report = printer_scan_pair(&config);
    assert(report.status == PRINTER_NOT_FOUND && ticks == 10000);
    assert(scan_stop_calls == 1 && connect_calls == 0 && pair_calls == 0 && config.fd == -1);
    reset();
    scan_error = -1;
    assert(printer_scan_pair(&config).status == PRINTER_SCAN_FAILED);
    assert(scan_stop_calls == 0 && close_calls == 1);
    reset();
    scan_list_error = -1;
    assert(printer_scan_pair(&config).status == PRINTER_SCAN_FAILED);
    assert(scan_stop_calls == 1 && close_calls == 1);
    reset();
    scan_count = MAX_DEVICE_NUM + 1;
    assert(printer_scan_pair(&config).status == PRINTER_SCAN_FAILED);
    assert(scan_stop_calls == 1 && connect_calls == 0);
    reset();
    scan_count = -1;
    assert(printer_scan_pair(&config).status == PRINTER_SCAN_FAILED);
    reset();
    scan_stop_error = -1;
    assert(printer_scan_pair(&config).status == PRINTER_SCAN_FAILED);
    assert(close_calls == 1 && connect_calls == 0);
    reset();
    connect_error = -1;
    assert(printer_scan_pair(&config).status == PRINTER_CONNECT_FAILED);
    assert(pair_calls == 0 && close_calls == 1);
    reset();
    pair_error = -1;
    assert(printer_scan_pair(&config).status == PRINTER_PAIR_FAILED);
    assert(pair_calls == 1 && close_calls == 1);
    reset();
    pair_status_error = -1;
    assert(printer_scan_pair(&config).status == PRINTER_PAIR_FAILED);
    reset();
    pair_status = PAIRING_FAIL;
    assert(printer_scan_pair(&config).status == PRINTER_PAIR_FAILED);
    reset();
    pair_status = PAIRING_BLE_COMFIRM;
    assert(printer_scan_pair(&config).status == PRINTER_PAIR_INTERACTION_REQUIRED);
    assert(writes == 0 && close_calls == 1);
    reset();
    pair_status = PAIRING_IDLE;
    assert(printer_scan_pair(&config).status == PRINTER_DEADLINE);
    assert(ticks == 30000 && close_calls == 1);
    reset();
    config.fd = 7;
    assert(printer_scan_pair(&config).status == PRINTER_PAIRED);
    assert(close_calls == 1 && scan_start_calls == 1);
    assert(printer_release(&config) == 0);
    reset();
    scan_error = 1;
    scan_stop_error = 1;
    pair_pending_polls = 2;
    assert(printer_scan_pair(&config).status == PRINTER_PAIRED);
    assert(ticks == 100 && config.fd == 7);
    assert(printer_release(&config) == 0);
    reset();
    scan_ambiguous = 1;
    assert(printer_scan_pair(&config).status == PRINTER_SCAN_FAILED);
    assert(connect_calls == 0 && writes == 0 && close_calls == 1);
    /* printer_scan_list tests */
    {
        ble_device_info_t found[MAX_DEVICE_NUM];
        int n;
        /* NULL / zero-capacity guards */
        reset();
        assert(printer_scan_list(NULL, MAX_DEVICE_NUM) == -1);
        assert(printer_scan_list(found, 0) == -1);
        assert(open_calls == 0);
        /* bt_open failure */
        reset();
        open_error = 1;
        assert(printer_scan_list(found, MAX_DEVICE_NUM) == -1);
        assert(scan_start_calls == 0 && close_calls == 0);
        /* bt_scan_start failure */
        reset();
        scan_error = -1;
        assert(printer_scan_list(found, MAX_DEVICE_NUM) == -1);
        assert(scan_start_calls == 1 && close_calls == 1 && scan_stop_calls == 0);
        /* happy path — returns all devices found */
        reset();
        n = printer_scan_list(found, MAX_DEVICE_NUM);
        assert(n == scan_count);
        assert(scan_start_calls == 1 && scan_stop_calls == 1);
        assert(close_calls == 1 && open_calls == 1);
    }
    /* printer_connect tests (standard BLE without pairing, e.g. RPP02N) */
    {
        reset();
        assert(printer_connect(NULL).status == PRINTER_CONFIG_REQUIRED);
        reset();
        connect_error = -1;
        assert(printer_connect(&config).status == PRINTER_CONNECT_FAILED);
        assert(close_calls == 1 && connect_calls == 1);
        /* standard BLE device where pairing returns -1 should succeed and remain connected */
        reset();
        pair_error = -1;
        assert(printer_connect(&config).status == PRINTER_PAIRED);
        assert(config.fd == 7 && connect_calls == 1 && close_calls == 0);
        assert(printer_release(&config) == 0);
    }
    puts("PrinterDemo transport tests passed");
    return 0;
}