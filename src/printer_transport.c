#include "printer_transport.h"

#include <bluetooth.h>
#include <fcntl.h>
#include <string.h>
#ifdef SIM_BUILD
#include "sim_sdk.h"
#else
#include <syscall.h>
#endif

#define PRINTER_CHUNK_BYTES 20U
#define PRINTER_JOB_MS 60000U
#define PRINTER_SCAN_MS 10000U
#define PRINTER_PAIR_MS 30000U

int printer_release(struct printer_connection *connection)
{
    int fd;
    int result;
    int closed;
    if (!connection || connection->fd < 0)
        return 0;
    fd = connection->fd;
    connection->fd = -1;
    result = bt_disconnect(fd);
    closed = bt_close(fd);
    return result < 0 ? result : closed < 0 ? closed : 0;
}

static enum printer_status wait_ready(int fd, uint32_t started, int *error)
{
    for (;;) {
        int result;
        if ((uint32_t)(syscall_get_tick_count() - started) >= PRINTER_JOB_MS)
            return PRINTER_DEADLINE;
        result = bt_get_status(fd);
        if (result < 0) {
            *error = result;
            return PRINTER_NOT_READY;
        }
        if (result & (1 << 5))
            return PRINTER_SENT;
        syscall_msleep(50);
    }
}

struct printer_report printer_scan_pair(struct printer_connection *connection)
{
    struct printer_report report = {PRINTER_CONFIG_REQUIRED, 0, 0, 0};
    ble_device_info_t devices[MAX_DEVICE_NUM];
    ble_device_info_t selected;
    uint32_t started;
    int scanning = 0;
    int found = 0;
    int result;
    static const uint8_t empty[6] = {0};

    if (!connection || memcmp(connection->address, empty, sizeof(empty)) == 0)
        return report;
    result = printer_release(connection);
    if (result < 0) {
        report.status = PRINTER_CLEANUP_FAILED;
        report.sdk_error = result;
        return report;
    }
    connection->fd = bt_open("/dev/bt", O_RDWR);
    if (connection->fd < 0) {
        report.status = PRINTER_OPEN_FAILED;
        report.sdk_error = connection->fd;
        return report;
    }
    result = bt_scan_start(connection->fd);
    if (result < 0) {
        report.status = PRINTER_SCAN_FAILED;
        report.sdk_error = result;
        goto cleanup;
    }
    scanning = 1;
    started = syscall_get_tick_count();
    while (!found && (uint32_t)(syscall_get_tick_count() - started) < PRINTER_SCAN_MS) {
        int count = 0;
        int index;
        memset(devices, 0, sizeof(devices));
        result = bt_get_scan_list(connection->fd, &count, devices, sizeof(devices));
        if (result != 0 || count < 0 || count > MAX_DEVICE_NUM) {
            report.status = PRINTER_SCAN_FAILED;
            report.sdk_error = result;
            goto cleanup;
        }
        for (index = 0; index < count; ++index) {
            if (memcmp(devices[index].mac_addr, connection->address, sizeof(connection->address)) != 0)
                continue;
            if (found && selected.addr_type != devices[index].addr_type) {
                report.status = PRINTER_SCAN_FAILED;
                goto cleanup;
            }
            selected = devices[index];
            found = 1;
        }
        if (!found)
            syscall_msleep(250);
    }
    result = bt_scan_stop(connection->fd);
    if (result < 0) {
        report.status = PRINTER_SCAN_FAILED;
        report.sdk_error = result;
        goto cleanup;
    }
    scanning = 0;
    if (!found) {
        report.status = PRINTER_NOT_FOUND;
        goto cleanup;
    }
    memcpy(connection->address, selected.mac_addr, sizeof(connection->address));
    connection->address_type = selected.addr_type;
    started = syscall_get_tick_count();
    result = bt_ble_connect(connection->fd, selected.mac_addr, selected.addr_type);
    if (result != 0) {
        report.status = PRINTER_CONNECT_FAILED;
        report.sdk_error = result;
        goto cleanup;
    }
    report.status = wait_ready(connection->fd, started, &report.sdk_error);
    if (report.status != PRINTER_SENT)
        goto cleanup;
    result = bt_ble_start_pairing(connection->fd);
    if (result != 0) {
        report.status = PRINTER_PAIR_FAILED;
        report.sdk_error = result;
        goto cleanup;
    }
    started = syscall_get_tick_count();
    while ((uint32_t)(syscall_get_tick_count() - started) < PRINTER_PAIR_MS) {
        uint32_t status = PAIRING_IDLE;
        uint32_t nv_count = 0;
        result = bt_get_pairing_status(connection->fd, &status, &nv_count);
        if (result != 0 || status == PAIRING_FAIL) {
            report.status = PRINTER_PAIR_FAILED;
            report.sdk_error = result;
            goto cleanup;
        }
        if (status == PAIRING_SUCCESS) {
            report.status = PRINTER_PAIRED;
            return report;
        }
        if (status != PAIRING_IDLE) {
            report.status = PRINTER_PAIR_INTERACTION_REQUIRED;
            report.sdk_error = (int)status;
            goto cleanup;
        }
        syscall_msleep(50);
    }
    report.status = PRINTER_DEADLINE;

cleanup:
    if (scanning)
        bt_scan_stop(connection->fd);
    printer_release(connection);
    return report;
}

struct printer_report printer_send(struct printer_connection *connection,
    const uint8_t *data, size_t length)
{
    struct printer_report report = {PRINTER_CONFIG_REQUIRED, 0, 0, 0};
    uint32_t started;
    int result;
    uint8_t address[6];
    static const uint8_t empty[6] = {0};

    if (!connection || !connection->verified || !data || !length ||
        connection->address_type < 0 || connection->address_type > 255 ||
        !connection->service || !connection->characteristic ||
        connection->pace_ms < 20 || connection->pace_ms > 1000 ||
        memcmp(connection->address, empty, sizeof(empty)) == 0)
        return report;

    started = syscall_get_tick_count();
    if (connection->fd < 0) {
        connection->fd = bt_open("/dev/bt", O_RDWR);
        if (connection->fd < 0) {
            report.status = PRINTER_OPEN_FAILED;
            report.sdk_error = connection->fd;
            return report;
        }
        memcpy(address, connection->address, sizeof(address));
        result = bt_ble_connect(connection->fd, address, connection->address_type);
        if (result != 0) {
            report.status = PRINTER_CONNECT_FAILED;
            report.sdk_error = result;
            goto cleanup;
        }
    }
    report.status = wait_ready(connection->fd, started, &report.sdk_error);
    if (report.status != PRINTER_SENT)
        goto cleanup;
    result = bt_ble_set_gattc_write(connection->fd, connection->service, connection->characteristic);
    if (result != 0) {
        report.status = PRINTER_NOT_READY;
        report.sdk_error = result;
        goto cleanup;
    }
    while (report.accepted_bytes < length) {
        size_t chunk = length - report.accepted_bytes;
        if ((uint32_t)(syscall_get_tick_count() - started) >= PRINTER_JOB_MS) {
            report.status = PRINTER_DEADLINE;
            goto cleanup;
        }
        if (chunk > PRINTER_CHUNK_BYTES)
            chunk = PRINTER_CHUNK_BYTES;
        report.write_attempted = 1;
        result = bt_ble_write_data(connection->fd, connection->service, connection->characteristic,
            data + report.accepted_bytes, (uint32_t)chunk);
        if (result != 0) {
            report.status = PRINTER_WRITE_FAILED;
            report.sdk_error = result;
            goto cleanup;
        }
        report.accepted_bytes += chunk;
        syscall_msleep(connection->pace_ms);
    }
    report.status = PRINTER_SENT;
    return report;

cleanup:
    printer_release(connection);
    return report;
}


struct printer_report printer_connect(struct printer_connection *connection)
{
    struct printer_report report = {PRINTER_CONFIG_REQUIRED, 0, 0, 0};
    uint32_t started;
    int result;
    size_t i;
    static const uint8_t empty[6] = {0};
    static const struct { uint16_t svc; uint16_t chr; } candidates[] = {
        { 0, 0 },             /* configured connection->service & characteristic */
        { 0x4953, 0x8841 },   /* ISSC transparent UART (RPP02N / Bleak 49535343-8841-...) */
        { 0xfe7d, 0x8841 },   /* ISSC 16-bit service + write characteristic */
        { 0xff00, 0xff02 },   /* ESC/POS standard BLE service */
        { 0xffe0, 0xffe1 },   /* Common thermal printer service */
        { 0x18f0, 0x2af1 },   /* Standard POS printer service */
    };

    if (!connection || memcmp(connection->address, empty, sizeof(empty)) == 0)
        return report;

    result = printer_release(connection);
    if (result < 0) {
        report.status = PRINTER_CLEANUP_FAILED;
        report.sdk_error = result;
        return report;
    }

    connection->fd = bt_open("/dev/bt", O_RDWR);
    if (connection->fd < 0) {
        report.status = PRINTER_OPEN_FAILED;
        report.sdk_error = connection->fd;
        return report;
    }

    started = syscall_get_tick_count();
    result = bt_ble_connect(connection->fd, connection->address, connection->address_type);
    if (result != 0) {
        report.status = PRINTER_CONNECT_FAILED;
        report.sdk_error = result;
        goto cleanup;
    }

    report.status = wait_ready(connection->fd, started, &report.sdk_error);
    if (report.status != PRINTER_SENT)
        goto cleanup;

    /* Discover / set working GATT write characteristic */
    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        uint16_t svc = candidates[i].svc ? candidates[i].svc : connection->service;
        uint16_t chr = candidates[i].chr ? candidates[i].chr : connection->characteristic;
        if (svc && chr && bt_ble_set_gattc_write(connection->fd, svc, chr) == 0) {
            connection->service = svc;
            connection->characteristic = chr;
            break;
        }
    }

    /* Standard BLE printers (like RPP02N) do not require or support SMP pairing.
       Attempt pairing if supported, but if not (returns -1), accept active BLE connection. */
    result = bt_ble_start_pairing(connection->fd);
    if (result == 0) {
        started = syscall_get_tick_count();
        while ((uint32_t)(syscall_get_tick_count() - started) < PRINTER_PAIR_MS) {
            uint32_t status = PAIRING_IDLE;
            uint32_t nv_count = 0;
            result = bt_get_pairing_status(connection->fd, &status, &nv_count);
            if (result != 0 || status == PAIRING_FAIL)
                break; /* Fallback to active unpaired BLE connection */
            if (status == PAIRING_SUCCESS)
                break;
            if (status != PAIRING_IDLE) {
                report.status = PRINTER_PAIR_INTERACTION_REQUIRED;
                report.sdk_error = (int)status;
                goto cleanup;
            }
            syscall_msleep(50);
        }
    }

    report.status = PRINTER_PAIRED;
    return report;

cleanup:
    printer_release(connection);
    return report;
}

int printer_scan_list(ble_device_info_t *devices, int capacity)
{
    ble_device_info_t buf[MAX_DEVICE_NUM];
    int fd, count = 0, n, result;
    uint32_t started;

    if (!devices || capacity <= 0)
        return -1;
    fd = bt_open("/dev/bt", O_RDWR);
    if (fd < 0)
        return -1;
    if (bt_scan_start(fd) < 0) {
        bt_close(fd);
        return -1;
    }
    started = syscall_get_tick_count();
    while ((uint32_t)(syscall_get_tick_count() - started) < PRINTER_SCAN_MS) {
        n = 0;
        memset(buf, 0, sizeof(buf));
        result = bt_get_scan_list(fd, &n, buf, sizeof(buf));
        if (result == 0 && n > 0 && n <= MAX_DEVICE_NUM) {
            int copy = n < capacity ? n : capacity;
            memcpy(devices, buf, (size_t)copy * sizeof(*devices));
            count = copy;
        }
        syscall_msleep(250);
    }
    bt_scan_stop(fd);
    bt_close(fd);
    return count;
}
