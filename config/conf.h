#ifndef PRINTER_DEMO_CONF_H
#define PRINTER_DEMO_CONF_H

#define CFG_APP_VERSION "1.0.0"

#define CFG_LOGGER_CHANNEL uart
#define CFG_LOGGER_ON 1

#define SOUNDBOX_BACKEND_HOST "soundbox-notify-stg.netzme.com"
#define SOUNDBOX_BACKEND_DEVICE_ID "3521519013"
#define SOUNDBOX_BACKEND_QR_PATH "/api/v2/device/generate-qr"
#define SOUNDBOX_BACKEND_CA CFG_APP_PATH "resources/certs/stg/backend-ca.pem"

#ifndef SOUNDBOX_MENU_MOTOR
#define SOUNDBOX_MENU_MOTOR 1
#endif
#ifndef SOUNDBOX_MENU_MOBIL
#define SOUNDBOX_MENU_MOBIL 1
#endif
#ifndef SOUNDBOX_MENU_SEPEDA
#define SOUNDBOX_MENU_SEPEDA 1
#endif
#ifndef SOUNDBOX_MENU_ELF
#define SOUNDBOX_MENU_ELF 1
#endif
#ifndef SOUNDBOX_MENU_BUS
#define SOUNDBOX_MENU_BUS 0
#endif
#ifndef SOUNDBOX_MENU_MANUAL
#define SOUNDBOX_MENU_MANUAL 0
#endif

#if (SOUNDBOX_MENU_MOTOR != 0 && SOUNDBOX_MENU_MOTOR != 1) || \
    (SOUNDBOX_MENU_MOBIL != 0 && SOUNDBOX_MENU_MOBIL != 1) || \
    (SOUNDBOX_MENU_SEPEDA != 0 && SOUNDBOX_MENU_SEPEDA != 1) || \
    (SOUNDBOX_MENU_ELF != 0 && SOUNDBOX_MENU_ELF != 1) || \
    (SOUNDBOX_MENU_BUS != 0 && SOUNDBOX_MENU_BUS != 1) || \
    (SOUNDBOX_MENU_MANUAL != 0 && SOUNDBOX_MENU_MANUAL != 1)
#error "Soundbox menu switches must be 0 or 1"
#endif
#if !(SOUNDBOX_MENU_MOTOR || SOUNDBOX_MENU_MOBIL || SOUNDBOX_MENU_SEPEDA || \
    SOUNDBOX_MENU_ELF || SOUNDBOX_MENU_BUS || SOUNDBOX_MENU_MANUAL)
#error "Enable at least one soundbox menu tile"
#endif

#define PRINTER_BLE_VERIFIED 0
#define PRINTER_ADDRESS_BYTES {0x06, 0x02, 0x31, 0x98, 0x50, 0x95}
#define PRINTER_ADDRESS_TYPE (-1)
#define PRINTER_SERVICE_UUID 0xff00
#define PRINTER_WRITE_UUID 0xff02
#define PRINTER_PACE_MS 50

#define PRINTER_NATIVE_QR_VERIFIED 1

/* shortcut: flow-test receipts only; disable after backend receipt qualification. */
#ifndef SOUNDBOX_TEST_RECEIPT
#define SOUNDBOX_TEST_RECEIPT 1
#endif
#if SOUNDBOX_TEST_RECEIPT != 0 && SOUNDBOX_TEST_RECEIPT != 1
#error "SOUNDBOX_TEST_RECEIPT must be 0 or 1"
#endif

/* Qualify the staging telemetry topic policy and payload before enabling. */
#define SOUNDBOX_TELEMETRY_VERIFIED 0

#endif