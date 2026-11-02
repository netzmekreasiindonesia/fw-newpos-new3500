#ifndef PRINTER_TRANSPORT_H
#define PRINTER_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>
#include <bluetooth.h>

struct printer_connection {
    int verified;
    uint8_t address[6];
    int address_type;
    uint16_t service;
    uint16_t characteristic;
    unsigned int pace_ms;
    int fd;
};

enum printer_status {
    PRINTER_SENT,
    PRINTER_CONFIG_REQUIRED,
    PRINTER_OPEN_FAILED,
    PRINTER_CONNECT_FAILED,
    PRINTER_NOT_READY,
    PRINTER_WRITE_FAILED,
    PRINTER_DEADLINE,
    PRINTER_CLEANUP_FAILED,
    PRINTER_SCAN_FAILED,
    PRINTER_NOT_FOUND,
    PRINTER_PAIR_FAILED,
    PRINTER_PAIR_INTERACTION_REQUIRED,
    PRINTER_PAIRED
};

struct printer_report {
    enum printer_status status;
    size_t accepted_bytes;
    int write_attempted;
    int sdk_error;
};

struct printer_report printer_scan_pair(struct printer_connection *connection);
struct printer_report printer_connect(struct printer_connection *connection);
int printer_release(struct printer_connection *connection);
struct printer_report printer_send(struct printer_connection *connection,
    const uint8_t *data, size_t length);

/* Scan only — no connect, no pair.
   Returns number of devices found (0..capacity), or -1 on BT open/scan error. */
int printer_scan_list(ble_device_info_t *devices, int capacity);

#endif