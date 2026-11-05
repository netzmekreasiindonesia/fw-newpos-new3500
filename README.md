# PrinterDemo

PrinterDemo is a NEW3500 parking soundbox demo with staging MPM payments,
transaction history, and Bluetooth printer tools. The Home settings icon opens
the **Alat & Tes** diagnostic menu.

> **Staging only:** Tap and CPM submission are enabled by default in `config/conf.h`
> and can send real payment requests. Backend, acquirer, printer, and
> live-payment qualification is still required.
> Do not treat this demo as production-ready.

## Requirements

This repository contains only PrinterDemo. Place it in an authorized NEW3500
SDK workspace with the following siblings:

```text
NEW3500_DEMO/
  app/
    PrinterDemo/   # this repository
    NetzmeSB/      # shared source and headers
  sim/             # simulator, LVGL, and host-test support
  toolchain/       # vendor SDK, NDK, and tools
```

Obtain the SDK, toolchain, simulator, and sibling project separately; they are
not downloaded automatically. Host tests and the simulator require a C
compiler, Make, SDL2 development tools (`sdl2-config`), and OpenSSL development
libraries.

## Build and run

Run these commands from the `NEW3500_DEMO` workspace root:

```sh
# Run host tests
make -C app/PrinterDemo check

# Build and run the offline simulator
make -C sim APP=PrinterDemo -j4
make -C sim APP=PrinterDemo run

# Build the device package (requires the certificates below)
make -C app/PrinterDemo
```

Install `output/system/PrinterDemo.npk` using the normal NEW3500 installation
workflow. Rebuild the device app and simulator after changing configuration.
Host tests and simulator results do not qualify real hardware or live services.

## Configuration

Edit [config/conf.h](config/conf.h) before building:

- Set `SOUNDBOX_BACKEND_DEVICE_ID` to the authorized identity. It also controls
  the MQTT client ID and topics; it is not automatically set to the terminal
  serial number. Provision matching broker credentials and permissions.
- `SOUNDBOX_CPM_LIVE_ENABLED` (default `1`) enables device CPM payment
  submission. Set it to `0` to disable it until backend
  success/reconciliation semantics and the terminal scanner are qualified.
- `SOUNDBOX_TAP_LIVE_ENABLED` (default `1`) enables Tap payment submission.
  Set it to `0` to disable it until the backend's type-3 payload, success, and
  reconciliation contract have been qualified for production.
- Set the `SOUNDBOX_MENU_*` switches to `1` or `0` to show or hide Home options;
  at least one must be enabled.
- `SOUNDBOX_TEST_RECEIPT=1` enables the current flow-test receipt, which is not
  an official receipt or independent payment confirmation.
- `SOUNDBOX_TELEMETRY_VERIFIED=0` keeps telemetry disabled until its topic,
  permissions, and payload are qualified.

### Certificates and keys

Before building the device package, securely provision these nonempty files in
`resources/certs/stg/`:

```text
ca.pem  backend-ca.pem  starfield-g2.pem  starfield-class2.pem
amazon-cross.pem  cil.crt  pri.key
```

Use approved staging credentials and never commit or force-add them. Anyone
with the package or terminal file access can read the included private key;
production requires secure provisioning and restricted access. The simulator
also copies app resources into its filesystem, so protect that copy.

## Using the demo

- Use touch or the terminal keys to select a vehicle, enter an amount, and
  navigate. **CANCEL** goes back or exits from idle Home.
- Device MPM uses the staging backend and MQTT. CPM has a native scanner and
  submits the captured customer QR only when `SOUNDBOX_CPM_LIVE_ENABLED=1`;
  it does not wait for an MQTT acknowledgement. Tap reads an NFC wallet payload
  and submits it as type 3 only when `SOUNDBOX_TAP_LIVE_ENABLED=1`; captured
  payloads are sent as base64 text (up to 1024 characters; wallet text that is
  already base64 is sent unchanged). Both live switches default to on for
  staging; disable them for anything else. The simulator is
  offline and does not scan or send payments, connect to MQTT or Bluetooth, or
  print receipts.
- In **Alat & Tes**, use **Scan & Pair** to select the configured printer and
  access the sample print, QR, and HTTPS tools. The diagnostic Generate QRIS
  request uses a fixed sample amount.
- On a device payment-success screen, **Cetak Struk** starts the configured
  receipt print. The current flow-test receipt is marked as a test; delivery
  to Bluetooth does not prove that paper was printed.

## Safety and qualification

- Qualify the printer address and type, BLE service and characteristic, write
  pacing, and native/raster QR output on the actual device before printing.
  With `PRINTER_BLE_VERIFIED=0`, sending is blocked; verification flags are not
  proof of hardware qualification. Never retry an uncertain print
  automatically; check the paper first.
- MPM MQTT payment acknowledgements are not correlated to a specific QR, and
  QoS 0 notifications can be missed. Do not treat an acknowledgement or a
  diagnostic Success popup as independently verified payment confirmation.
- CPM treats a valid backend success response as confirmation under the
  current integration contract. Obtain backend/acquirer approval that this
  means final payment success before enabling live CPM. A lost response may
  leave the payment outcome unknown; do not retry until reconciled.
- Tap reads the built-in NFC reader through the SDK `appiccard` API (slot
  `SLOT_INRF`): select PPSE (a `6A82` reply is tolerated), select the QRIS AID,
  then read records 1 and 2. It was verified on a staging terminal with one
  phone wallet: the type-3 request got HTTP 200 with an empty `qr_content`,
  which the app treats as accepted. Final-success semantics, other wallets, and
  the reconciliation path are not qualified; do not retry an uncertain outcome.
  The temporary NFC/APDU and Tap request/response logs print wallet and request
  data; remove them before release.
- Mutable state is stored in `/home/user0/PrinterDemo/data/` as checksummed
  snapshots. Do not edit or delete them to bypass a blocked payment. Uncertain
  attempts can persist and block new payments; automatic reconciliation is not
  implemented. A valid device clock, NMID, and readable/writable storage are
  required to generate a parking QR.
- Parking HTTPS verifies certificates and hostnames. The diagnostic HTTPS
  probe and Generate QRIS request do not; use them only in controlled staging.
  MQTT logs can include merchant and payment data, so keep logs private.
- Physical display, touch targets, keys, printer behavior, live networking,
  payment recovery, and telemetry still need device qualification.

Confirm redistribution rights for the application, artwork, and branding, and
preserve the [font license](src/fonts/OFL.txt); no vendor SDK license is implied.

## More information

The [technical reference](REFERENCE.md) contains the detailed
[backend and recovery](REFERENCE.md#parking-backend),
[receipt modes](REFERENCE.md#transaction-receipts), and
[printer qualification](REFERENCE.md#hardware-qualification-required) guides.
