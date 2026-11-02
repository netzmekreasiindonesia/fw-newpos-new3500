# NEW3500 Parking Soundbox / Printer Tools

## Repository Scope And Setup

This repository contains **PrinterDemo only**, not a standalone copy of the
NEW3500 SDK workspace. Clone it at `app/PrinterDemo` inside an authorized SDK
workspace with this layout:

```text
NEW3500_DEMO/
  app/
    PrinterDemo/       # this repository
    NetzmeSB/          # shared logger, initialization and SDK-facing headers
  sim/                # simulator, LVGL, SDK stubs and host-test support
  toolchain/
    ndk/
    sdk/
    app_c_sdk/
    tools/
```

Device build rules use paths relative to this layout; host tests also require
the sibling simulator and NetzmeSB files. Obtain those dependencies separately
from their authorized source. The SDK/toolchain and sibling projects are not
included or downloaded automatically. Host UI tests require a C compiler, Make,
SDL2 development tools (`sdl2-config`), and OpenSSL development libraries.

The Git exclusions keep `docs/`, generated `graphify-out/` directories,
certificates/keys under `resources/certs/`, build output, and mutable `data/`
out of the repository. Local design documents are optional reference material,
not checkout requirements; the feature behavior and build commands are
documented below. Generated LVGL image descriptors and font sources remain
versioned because they are required to compile without regenerating assets.

Before a device build, provision approved certificates and credentials locally:
the `pack` target requires `resources/certs/stg/ca.pem`, `backend-ca.pem`,
`starfield-g2.pem`, `starfield-class2.pem`, `amazon-cross.pem`, `cil.crt`, and
`pri.key`. Obtain client credentials through an approved secure channel; never
add them with `git add -f`. See [Staging setup](#staging-setup) and
[Existing Diagnostic Tools](#existing-diagnostic-tools) for runtime requirements.
Use [config/conf.h](config/conf.h) to configure the authorized device identity
and hardware settings; the checked-in staging values are not production defaults.

Before publishing, confirm permission to redistribute the application sources,
DQ72 artwork, branding, and bundled fonts. Preserve the font license in
[src/fonts/OFL.txt](src/fonts/OFL.txt). No license for vendor SDK files or artwork
is implied by this repository.

## Parking UI - Milestone 1

The app opens an adaptive parking UI, built against SDK LVGL 8.3.7
and simulator LVGL 8.3.9. It includes the vehicle grid, manual amount entry,
MPM/Tap/CPM selection, Tap waiting, CPM **Pindai**/scanner placeholder, history
and detail navigation, and payment result footer. Merchant names, NMID, rates,
and daily summaries come from persisted local state. Device builds now use the
staging backend for MPM and history/detail; the simulator remains offline.
See [Parking Backend](#parking-backend) for setup and limits.

- Use touch, or the terminal's UP/DOWN and ENTER keys to select controls.
  Physical digits, BACKSPACE, CLEAR, and ENTER share the on-screen amount
  validation (Rp 1.000-10.000.000).
- CANCEL navigates back, returns from diagnostic tools to parking Home, and
  exits the app from parking Home when idle. The test menu's **Kembali ke Home**
  button returns to parking Home without exiting; an active job must finish first.
- Home's settings icon opens the existing **Alat & Tes** diagnostic screen.
  The original five tools remain available and their services are unchanged.
- In the simulator only, **Simulasi OK** / **Simulasi gagal** drive offline
  outcomes. Tap/CPM first simulate capture, then the HTTPS outcome. These
  controls do not generate QR data, send HTTPS, consume payment MQTT events,
  activate NFC/camera, update totals, or use Bluetooth.
- Both result types return Home after three seconds. On success, **Cetak Struk**
  cancels that deadline immediately. Print/Home are disabled while sending;
  repeated taps cannot restart printing. Successful delivery starts a fresh
  three-second return. Failed/uncertain completion requires manual Home and
  never retries or changes payment success.
- Live network/unknown-outcome notices and acknowledged payments with accounting
  errors require manual Home, so the warning is not dismissed by a timer.
- Simulator printing completes after 500 ms without sending a receipt.
  Device builds expose no simulated payment outcome controls. Unwired
  payment/capture screens explicitly say they are previews; the receipt hook
  uses matching transaction detail for receipts rather than the diagnostic
  sample when enriched mode is enabled. The current build uses the explicit
  flow-test receipt described below.

The production port is in [soundbox_ui.c](src/soundbox_ui.c) and
[soundbox_app.c](src/soundbox_app.c); the original documentation reference is
unchanged. Parking screens now use the
local DQ72 layout map (excluded from Git)
as a 212x318 coordinate system, rather than a generic flex layout. Positions
and containers scale to the runtime viewport; original bitmap fonts and artwork
retain their native pixel sizes. Tested viewports are 212x318, 240x320,
320x480, and 480x854. The DQ72 navigation/settings/cancel targets are smaller
than 48 pixels; touch qualification on the physical device is required.

At 212x318, the normal four-choice Home has 101x92 cards starting at y=110.
Payment methods have 100x75 hit areas enclosing 95x75 blue cards, on white,
with CPM centered below. MPM uses a 160x160 QR at (26,100), a real countdown,
and the original cancel artwork. Tap retains the 140x76 illustration and
original instruction typography. Result artwork is full size (144/146x136),
above the original 95-pixel white footer.

Intentional differences from the legacy firmware remain:
- The 16-pixel top strip reports actual app mode/storage state; it does not
  fabricate cellular, battery, or clock information.
- Simulator controls and explicit capture/preview warnings remain visible.
  Tap/CPM are still previews, not active payment capture.
- The MPM time instruction is below, not over, the QR. Its instruction/timer
  use the 13-pixel font at y=262/275, avoiding the map's eight-pixel QR overlap.
- Receipt/Home buttons remain in the result footer (28 pixels high at 212x318).
  They move the footer logos down relative to DQ72; print locking, warning
  messages, and automatic return behavior are unchanged.
- Dense menus use taller, scrolling cards instead of DQ72's overflowing
  60-pixel cards. Long merchant names also expand the header and scroll.
- History/detail use the existing backend's fields and warnings, with DQ72
  typography, paired rows and scroll viewports, not invented legacy fields.
- Diagnostic tools and their generic loading/error screens are not a port of
  DQ72 device settings.

### DQ72 visual assets

[assets/dq72/](assets/dq72/) contains 20 unchanged PNGs copied from the supplied
DQ72 project's `assets/` directory: all five vehicles, calculator, navigation,
Netzme/Toko Netzme and QRIS branding, NFC waiting, cancellation, and result art.
Vehicle cards follow the DQ72 image / `Rp. 2.000` / vehicle-name order; payment
methods keep MPM/Tap side by side and CPM centered below. Tap waiting uses the
original NFC illustration, and results use the original success/failure images.
Preview warnings and the new receipt controls remain explicit.

[soundbox_images.c](src/soundbox_images.c) contains checked-in LVGL 8 descriptors:
RGB565 plus alpha for the SDK and BGRA32 for the simulator. No runtime PNG
decoder, filesystem asset load, or new target dependency is required. To
regenerate them after editing the source PNGs, run
`python3 assets/generate.py` from this directory; `--check` detects stale output.
Only that regeneration tool needs Pillow, which is already installed locally.
Do not edit the generated C/header directly.

[src/fonts/](src/fonts/) reuses the supplied LVGL 8 ports of the original
Space Mono bitmaps and metrics, with the included SIL Open Font License.
The only font-port adjustment is a constant Montserrat-14 fallback: the SDK's
`LV_FONT_DEFAULT` is a runtime pointer and cannot initialize a static descriptor.
Navigation/keypad use SDK Montserrat 12/16, as mapped. Vehicle icons remain
40 pixels. Persisted prices and enabled menu entries are not changed to match
screenshots.

[Workflow tests](tests/soundbox_test.c) cover input limits, navigation, all
three methods, CPM timeout, MPM timeout/cancellation, print/return races,
stale completions, clock wraparound, manual-only menus, and the device-preview
gates, plus long merchant names and the 32-item menu limit. All controller/UI
calls and future service completions must run on the
LVGL thread; completion timestamps use `lv_tick_get()`, with generation tokens
invalidated on cancellation/navigation.
The tests also assert actual image sources, image/price/name ordering, centered
CPM placement, button-child/result-image bounds, and exact 212x318 coordinates
for Home configurations with one through four choices, payment hit areas,
QR canvas, Tap art, CPM/scanner and result/footer geometry. Native host tests using
both SDK and simulator headers verify 16-/32-bit descriptor sizes and pixel/
alpha samples from the source PNGs. For rendered evidence, set
`SOUNDBOX_CAPTURE_DIR` to an existing directory when running
`sim/build/PrinterDemo/tests/soundbox_full_test` from the workspace root; it writes
real LVGL framebuffer captures as PPM files at all four sizes, including
`dq72-reference-*` screens. `make -C app/PrinterDemo/tests check` builds and runs
this explicit all-menu workflow even when the configured menu disables items.

**Still pending:** physical viewport/touch/key and visual qualification,
live backend/hardware qualification, live Tap/CPM capture, bearer
recovery, physical receipt qualification, and telemetry qualification. See the
local implementation plan (excluded from Git).

## Local State And MQTT - Milestone 2

[soundbox_store.c](src/soundbox_store.c) uses the NewPOS `app_file_*` API.
Mutable files live under `/home/user0/PrinterDemo/data/`, not packaged resources.
Run the simulator from `sim/` (as `make -C sim APP=PrinterDemo run` does); its
data directory is `sim/fsroot/home/user0/PrinterDemo/data/` and survives rebuilds.

`data/setting.ini` and `data/dtotal.ini` are **alternating complete snapshots**,
not independent settings/total files. Each contains `[set]`, `[dtotal]`,
attempt metadata, a sequence number, and CRC32. This deliberate adaptation
commits merchant configuration, totals, and duplicate protection together.
Preserved setting names are `merchant_name`, `merchant_name_l2`,
`merchant_nmid`, and all five `menu_price_*` keys. Initial prices are Motor
2000, Mobil 5000, Sepeda 1000, Elf 10000, and Bus 15000; merchant fields start
empty. No production credentials are stored in these records.

Writes target the inactive slot, handle short writes, then reopen and verify
the entire record before publishing it. Startup selects the newest valid slot
and visibly reports recovery from a torn slot. Unreadable records fail closed;
two corrupt records are **not** treated as first boot. Missing-file detection
requires `ENOENT` from open or an explicit not-found result from the exported
access syscall. The device's libc access wrapper can replace the syscall's
original errno with `1`; startup bypasses that wrapper for the existence check.
Existing but unreadable files remain errors. The data
directory is opened (and created if needed), avoiding the device SDK's
boolean directory-check return convention that differs from its documentation.
Do not hand-edit these checksummed files or copy a legacy DQ72 INI into them.
Schema changes require migration; this release has no legacy INI importer.
Readback/CRC is not proof of durable flash writes: qualify close, power-cut
recovery, writable permissions, and upgrade retention on the terminal.

The existing worker loads state before MQTT/UI startup and serializes all
updates. Home shows both merchant-name lines and current prices; visible Home
and History refresh after committed updates. An already-selected amount is
not rewritten. History shows 64-bit local count/total and the stored UTC+7 date;
device builds also load the remote list/detail through the parking backend.
Rollover runs at startup, on entering
History, and every 30 seconds when the worker is idle.

Totals reset when NMID changes or the UTC+7 date advances. Same-NMID, name, and
price updates do not reset them. Invalid/backward clocks preserve the bucket.
The date conversion expects UTC epoch seconds from `time(NULL)`; qualify the
terminal's RTC timebase/synchronization before live accounting.

The store API issues persisted, single-flight attempt IDs and checks unsigned
64-bit accounting. Counting an acknowledged attempt twice cannot increment it
twice, including after reload. A changed merchant/date, invalid accounting
clock, or overflow retains the acknowledgement amount and original NMID/date
for reconciliation instead of charging the new bucket. One such retained
reconciliation blocks new attempts until a resolution workflow is implemented.
Uncertain submissions remain pending across reboot; cancellation is only for
attempts known not to have charged. Live parking payment binding is Milestone 3:
**neither preview success nor the diagnostic popup increments these totals.**

[soundbox_event.c](src/soundbox_event.c) implements the approved DQ72 broadcasts:

After the first successful MQTT subscription, PrinterDemo publishes
`{"state":1}` with QoS 0 to
`/ota/device/connection/<SOUNDBOX_BACKEND_DEVICE_ID>/state`. This announcement
is independent of the telemetry gate and the payment-consumption pause.
After a successful socket write it is not repeated on reconnect or listener
restart within the same app process. Restarting the app permits another announcement.
A failed write is logged and retried after reconnect; a partial QoS 0 write
can therefore produce a duplicate, and socket success is not broker acknowledgement.
The broker policy must allow publishing to this state topic.
Startup logs identify the configured client/topics and each successful stage:
worker start, TLS ready, accepted CONNACK, and accepted SUBACK. Changing the
device ID does not change the bundled client certificate or its broker policy.
If CONNECT or subscription is rejected after an identity change, provision
authorization for the new client ID and topics; do not disable TLS verification.
Incoming business messages log topic and byte count, then the full payload in
96-byte chunks labeled with byte offsets, followed by accepted type, field mask
and queue depth. Non-ASCII/control bytes are printed as `\xNN` to keep UART lines
readable. Payload logging includes merchant/payment data: keep diagnostic logs
private and remove payload logging before production deployment. The
storage worker logs commit outcome and whether a committed NMID is present.
No incoming-message log means no update was observed by the listener, even if
the boot-state socket write succeeded.

Storage startup logs each failed slot's absolute path, SDK return and captured
`errno`, plus both slot classifications. On ambiguous open failures, startup
logs a direct access-syscall existence check (raw result and errno); generic
failures, including `errno=1`, cannot establish absence. Existing unreadable snapshots block
initialization to avoid overwriting accounting. In that state merchant updates can queue, but
cannot be committed. Do not delete the snapshots to troubleshoot this.

Merchant updates arrive on `/ota/<SOUNDBOX_BACKEND_DEVICE_ID>/update`:

```json
{"broadcast_type":4,"merchant_name":"Test parking","merchant_name_l2":"Gate A","merchant_nmid":"TEST-NMID"}
```

The existing storage worker persists the supplied fields and refreshes Home;
omitted fields are unchanged. The configured identity is used for both topics,
not the physical terminal serial unless explicitly configured that way.

- Numeric `broadcast_type: 1` or string `"1"` acknowledges a watched MPM once;
  numeric/string `money` is retained, absent money is zero. `biz_type` alone
  no longer acknowledges anything.
- Type 4 patches the three merchant fields. An actual NMID change resets the
  total bucket in the same commit.
- Type 6 maps product codes/names Motor, Mobil, Sepeda, Elf, and Bus to their
  price keys. Prices must be whole rupiah in the current UI range 1000-10000000.
  Invalid recognized prices/duplicates reject the entire update; unknown
  products are logged and skipped.

JSON is bounded to 4096 bytes and 16 nesting levels. Duplicate keys, control
characters in merchant fields, malformed amounts, and overflow are rejected
with logs, rather than inheriting DQ72's permissive/truncating conversions.
JSON numeric amounts must be exactly representable integers up to 2^53-1;
decimal strings can represent the full unsigned 64-bit range.

Eight owned configuration events are buffered separately from a reserved
one-shot payment slot. Overflow is logged and shown in the parking UI; request
a fresh configuration broadcast. The queues are RAM-only and MQTT remains
QoS 0, so offline/unapplied updates are not guaranteed to replay.
The existing QRIS HTTPS operation acquires a generation-checked pause token
and releases it on success or error. During the pause neither payment nor
configuration events are consumed, while transport reads and keepalive continue.

Outbound QoS 0 telemetry uses that same MQTT connection/thread; no new client
or driver is added. `SOUNDBOX_TELEMETRY_VERIFIED=0` in
[config/conf.h](config/conf.h) keeps publishing disabled until broker policy
and payload qualification. The implemented subset is `v`, `sn`, `up`, `model`,
and `fw`, sent after subscription and every 60 seconds to
`/staging/telemetry/3521519013/status`. Its configured identity matches the
physical terminal serial number and the subscription/client ID.
Unavailable battery, radio, or heap metrics are not fabricated. Tests enable
the publisher and verify telemetry/keepalive continue during a business pause.

[Store tests](tests/store_test.c) cover 416 interrupted settings writes and
425 interrupted accounting writes, short I/O, corruption, unreadable files,
uncertain readback, duplicate recovery, NMID/date resets, overflow,
reconciliation, and broadcast validation. UI tests additionally exercise the
worker-to-UI snapshot path and 64-bit summary rendering at all four viewports.

## Parking Backend

Milestone 3 is partially implemented.

Device builds bind [soundbox_backend.c](src/soundbox_backend.c) to the existing
serialized worker, not to an LVGL callback. Inputs and results own their strings;
generation tokens reject late/duplicate completions after navigation.

- **MPM:** sends the selected integer amount and `type: 1` to the QR endpoint.
  The returned QR is displayed on the parking screen. MQTT must be connected
  before submission. An active displayed QR consumes one typed
  `broadcast_type: 1` acknowledgement, then the worker applies its `money` once
  to persisted accounting. An absent `money` remains zero, as on DQ72.
- **History:** posts the device ID to `/api/v2/device/transaction/list`.
  Displays up to 16 entries, explicitly marking truncation; daily totals remain
  local. Empty history and failed requests are separate outcomes.
- **Detail:** posts the selected `transaction_id` and `event_type` to
  `/api/v2/device/transaction/detail`. Displays bounded fields and UTC+7 dates;
  customer/merchant PAN fields are deliberately not displayed or logged.
- **Tap/CPM:** the client supports explicit backend types `3`/`2`, but the
  device UI blocks these methods before submission until NFC/camera capture
  is integrated. It never sends empty capture data or claims a simulated
  device payment succeeded. The offline simulator retains its preview flows.
- **Receipts:** transaction-bound printing is still unavailable. The success
  button does not send the diagnostic specimen receipt.

### Staging setup

Configure these values in [config/conf.h](config/conf.h):

| Switch | Default |
| --- | --- |
| `SOUNDBOX_BACKEND_HOST` | `soundbox-notify-stg.netzme.com` (HTTPS port 443) |
| `SOUNDBOX_BACKEND_DEVICE_ID` | Physical terminal identity `3521519013` |
| `SOUNDBOX_BACKEND_QR_PATH` | `/api/v2/device/generate-qr` |
| `SOUNDBOX_BACKEND_CA` | App-local `resources/certs/stg/backend-ca.pem` (DigiCert Global Root G2 and DigiCert Global Root CA) |

The device ID also determines the MQTT client ID, update topic and staging
telemetry topic. It is not automatically replaced with the physical serial.
Provision matching authorized MQTT credentials and broker policy for another
identity. Diagnostic **Generate QRIS** retains its original fixed sample request.

Parking HTTPS uses peer-chain verification, hostname verification, SNI and
TLS 1.2. There is **no insecure fallback** if the CA is missing or does not trust
the backend chain. The backend's observed DigiCert Global G2 TLS RSA SHA256 2020
CA1 chain includes Global Root G2 cross-signed by DigiCert Global Root CA.
The backend bundle includes both self-signed roots for clients that follow
that cross-sign rather than terminating at G2. These public roots come from
[DigiCert G2](https://cacerts.digicert.com/DigiCertGlobalRootG2.crt.pem) and
[DigiCert Global Root CA](https://cacerts.digicert.com/DigiCertGlobalRootCA.crt.pem);
MQTT trust files remain unchanged. HTTPS logs the loaded bundle path.
Error `-188` (`ASN_NO_SIGNER_E`) means no trusted signer
was found. Qualify the supplied CA against the actual endpoint/firmware,
and set a valid UTC device clock. If changing the CA path, package/provision that
public CA file too. The diagnostic HTTPS probe remains explicitly unverified.
HTTPS globals remain initialized across requests and are released only at app
shutdown, after the serialized worker and MQTT owner stop.

A valid stored NMID, business date and readable/writable local storage are
required before generating a parking QR. Merchant/date changes between selection
and worker execution reject the request before submission. Configure the
terminal's existing Wi-Fi/cellular connection; this change adds no bearer
selector, Wi-Fi setup screen, or automatic route failover.

Requests are unsigned staging requests. If the backend requires RFC 9421 signing
or additional authorization, it reports authentication failure; there is no
silent signing fallback. Production signing/provisioning remains a separate gate.

### Uncertain payments and limits

MPM acknowledgement follows DQ72 behavior, **not transaction-correlated proof**.
Unrelated/late broadcasts can acknowledge an active QR. A disconnected MQTT
screen warns that QoS 0 notifications may be missed; reconnecting does not prove
an unpaid or paid outcome.

The worker persists an attempt before submission. A failure before the first
HTTP write, or an explicit invalid-request/authentication rejection, releases
that attempt if persistence succeeds. Once a write might have occurred, network,
server or malformed-response failures retain the pending attempt. Cancelling
or timing out also shows **Status belum diketahui**, without automatic replay
or automatic failure. Even an empty successful QR response is treated as unknown,
not payment success.

**A retained pending/reconciliation record blocks new payment attempts.**
History can still be fetched, but it does not automatically clear that record.
An operator/backend reconciliation workflow is not implemented yet; do not
delete or hand-edit checksummed storage to bypass the block.

Response bodies are bounded to 14,000 bytes; QR/captured content to 512 bytes.
History dates use the existing UTC+7 helper's 2020-2099 range. Oversize/malformed
responses fail explicitly. DNS, TCP connect and some vendor TLS operations can
still block beyond the existing between-call timeout. No live payment is retried
automatically, and no response payload or credentials are logged.

`make -C app/PrinterDemo check` covers typed request bodies, response parsing,
verified TLS setup/failures, pre-write vs uncertain-write classification, live
controller navigation, QR rendering, history/detail, stale/duplicate completions,
MQTT accounting and the existing offline/menu suites. Mocked tests are not proof
of live backend acceptance or real-terminal TLS/QR/payment qualification.

## Transaction Receipts

### Enabled flow-test receipt

`SOUNDBOX_TEST_RECEIPT` is currently **1** in [config/conf.h](config/conf.h).
After the payment-success screen appears, tap **Cetak Struk** before the
three-second Home timeout. It prints a predefined **STRUK UJI COBA** /
**BUKAN STRUK RESMI** receipt containing the MQTT `money` amount formatted as
rupiah (for example **TOTAL BAYAR: Rp 2.000**). As in the existing acknowledgement
flow, absent/zero `money` uses the selected payment amount.

This mode does not request transaction detail or require `transaction_id`,
`event_type`, merchant name, date, issuer or RRN. It invents none of those fields.
Printing uses the same worker, selected printer, BLE verification, pacing,
duplicate-tap protection and uncertain-delivery handling. It never changes
payment accounting. The UI and worker log explicitly identify flow-test mode;
the simulator remains mocked.

Install the rebuilt device package, select/connect the printer in **Alat & Tes**,
return Home, complete a staging payment and tap **Cetak Struk**. A sent message
means Bluetooth accepted the bytes, not confirmed paper output.

Set `SOUNDBOX_TEST_RECEIPT` to **0** and rebuild to restore the enriched-receipt
requirements below. Test receipts are not official receipts or independently
verified payment confirmation.

### Enriched mode (`SOUNDBOX_TEST_RECEIPT=0`)

On an acknowledged payment's success screen, **Cetak Struk** queues one job on
the existing worker. It cancels the three-second Home deadline immediately and
blocks Print/Home/CANCEL until completion. Payment success and local totals never
change because of a print failure.

The optional payment-broadcast extension is:

```json
{"broadcast_type":1,"money":2000,"transaction_id":"T001","event_type":"qr"}
```

`transaction_id` and `event_type` must be nonempty strings of at most 95 and 63
bytes, without control characters. Missing/invalid identifiers do not reject the
payment acknowledgement, but printing reports incomplete receipt data. The
minimal DQ72 broadcast remains supported. This extension must be supplied by the
backend; it is not guaranteed by the existing minimal broadcast contract.

Print fetches `/api/v2/device/transaction/detail` using those exact identifiers.
It never searches history or guesses by amount/latest transaction. Detail must
contain a matching textual `transaction_id`, integer `amount` matching the
acknowledged `money` (or selected amount when money is absent/zero),
`paid_at_long` in Unix seconds, textual `rrn` (including leading zeroes),
`issuer_name`, and `payment_method`. If returned, `event_type` and
`merchant_nmid` must also match. For the currently live MPM flow, accepted
method spellings are `QRIS`, `MPM`, and `QRIS MPM`, printed as **QRIS MPM**.
Incomplete, mismatched, duplicate-key, or unsupported receipt data fails before
Bluetooth is invoked. Network/authentication failures are shown separately.

Merchant name, NMID, vehicle and method are copied when the payment request is
queued; later merchant/price broadcasts cannot rewrite them. The detail's paid
time is converted to UTC+7 once. The formatter uses 32-column wrapped printable
ASCII, rejecting control bytes, non-ASCII text and overflow rather than silently
corrupting names or dropping NMID/RRN/reference characters. At least one merchant
name line and the NMID are required. Qualify a different encoding before
supporting non-ASCII receipts. No PAN, customer details, credentials, raw QR,
sample authenticity URL or hardcoded issuer/reference is included.

The existing BLE transport retains its 20-byte chunks, pacing and deadlines.
All accepted bytes produce **Struk telah dikirim ke printer**, not proof of
paper output, and start a fresh three-second Home timer. A failure before writing
requires manual Home; any attempted write with uncertain completion warns
**Mungkin tercetak. Jangan ulangi.** There is no automatic retry or reprint action.
The simulator still completes a mock print without Bluetooth or HTTPS.

Pair/select and qualify the printer using **Alat & Tes** before live printing.
The existing endpoint/address verification gate is not bypassed. Physical paper
width, encoding, BLE pacing, printer-off/paper-out behavior and end-to-end staging
payments still need terminal qualification. Broadcast identifiers do not add
independent payment correlation or replay protection; the existing DQ72
acknowledgement limitations above still apply.

## Existing Diagnostic Tools

Standalone NEW3500 C/LVGL app: **Scan & Pair**, a sample receipt, a static QR,
and **Generate QRIS**. Generate QRIS sends a staging API request, displays the
returned QR code on a separate screen, and prints it only when the QR on screen
is tapped.
The request uses the configured device ID `3521519013`, amount `3666`, and type
`1`; change these values before using a different staging device or amount.

The app also starts an app-local MQTT 3.1.1 client for
`acbkcrhucmhqq-ats.iot.ap-southeast-1.amazonaws.com:8883` and subscribes to
`/ota/3521519013/update`. While the generated QR screen is open, a JSON
message with numeric `broadcast_type: 1` or string `"1"` displays a **Success**
popup once. The screen shows when MQTT is disconnected. Payment messages
outside that screen are ignored; merchant/price broadcasts are processed
independently by the storage worker.
The subscriber resolves the hostname and uses TLS 1.2 with CA and hostname
verification, client authentication, a clean session, and QoS 0. It reconnects
after a connection failure. To support AWS's cross-signed chain, it also trusts
the AWS-published Starfield Services Root CA G2 and cross-signed Amazon Root
CA 1, following [AWS server authentication guidance](https://docs.aws.amazon.com/iot/latest/developerguide/server-authentication.html).
The NPK bundles these public CA certificates in `starfield-g2.pem` and
`amazon-cross.pem`. It also trusts the legacy Starfield Class 2 root in
`starfield-class2.pem`, because the device's WolfSSL verifies the server's
cross-signed G2 certificate through that root. This expands the trusted CAs;
hostname verification remains enabled. Class 2 expires on June 29, 2034 and
should be removed when the SDK can validate the shorter AWS chain. Alongside
these public certificates, the NPK bundles the
staging CA, client certificate and private key from `resources/certs/stg/` at
`/home/user0/PrinterDemo/resources/certs/stg/ca.pem`, `cil.crt`, and `pri.key`,
respectively. The build requires all six nonempty files and does not include
production credentials. For this staging demo, the archive stores credential
directories as `755` and the private key as `644`, so the app can read it even
when the installer owns the files. The NPK itself is `600` on the build host.
**Anyone with the NPK or terminal file access can read the private key.** Use
only approved staging credentials. For production, provision credentials securely
and restrict ownership and access instead. A missing credential is logged and shown as MQTT
unavailable; restart the app after correcting the installation. The client ID
is `qris_pos&3521519013`, so another connection using that ID can displace it.
The simulator does not connect to MQTT.
If credentials are unavailable, the log identifies each failed path, operation,
SDK return value, and `errno` (which may be zero if the SDK does not set it).
The app sets the SDK file working directory to `/home/user0/PrinterDemo/` and
checks the credentials using paths relative to it; WolfSSL receives absolute paths.
Each connection attempt logs the broker's resolved IPv4 address. A TLS error
`-188` means WolfSSL could not find a signer for a certificate in the server
chain; check the resolved endpoint, the installed CA and the chain presented
to the terminal. Do not disable certificate verification to bypass this error.
See [MQTT certificate issue](./MQTT_CERTIFICATES.md) for the problem and fix.

**This popup is a notification, not verified payment confirmation:** `broadcast_type`
alone does not correlate the event to the displayed QR or amount, and an old or
unrelated notification may show Success. The sample event's amount (2000) differs
from the request amount (3666). Confirm the provider's correlation and replay
rules before relying on this flow for payments.

The **HTTPS (unverified)** button probes
`https://valid-isrgrootx1.letsencrypt.org/` as a separate connectivity check.
Physical CANCEL returns to parking Home when idle. A single background worker serializes jobs;
all buttons stay disabled during scanning, pairing, printing, or the QR request.
The worker uses a 48 KiB stack for the synchronous raw-socket WolfSSL request,
matching the stack size used by the Basic app's worker thread.
At startup the device build logs whether Wi-Fi is connected and its local IP;
this is a connection snapshot only and does not verify DNS or internet access.

## Scan And Pair

On the real device, press **Scan & Pair** to scan for up to 10 seconds for the
exact configured `PRINTER_ADDRESS_BYTES`. It does not select arbitrary nearby
devices by advertised name. The scan list is bounded to `MAX_DEVICE_NUM` and
its buffer size is passed in bytes; verify that convention with your firmware.
The discovered SDK-native address bytes and `addr_type` are passed directly to
`bt_ble_connect`, without address reversal or a guessed public/random type.

After the link is ready, the worker calls `bt_ble_start_pairing` and polls
`bt_get_pairing_status` for up to 30 seconds. Only `PAIRING_SUCCESS` is reported
as paired. The app does not change pairing mode, erase existing bonds, force a
PIN or automatically accept a passkey/confirmation request. Interactive
PIN/passkey/numeric-confirmation modes are currently reported as unsupported;
use a supported firmware pairing flow instead. A printer that allows only an
unpaired BLE link is not falsely reported as paired.

The connection is retained after successful pairing and printing, reused by the
next print job, and released on error, an explicit new scan, or app exit.
A later print can reconnect to the learned address/type, but never replays
data automatically after a failed write. Pairing does **not** verify the print
service/characteristic or disable any existing print-format safety gates.
Discovery/connection state lasts for the app session; device bonding persistence
is firmware-managed. The simulator reports Bluetooth unavailable.

## Hardware Qualification Required

Printing is intentionally gated in [config/conf.h](config/conf.h).
The supplied logs confirm BLE connection and completed host writes, not paper
output or NEW3500 compatibility. No endpoint is yet qualified.

Before enabling printing:

1. Physically print a tiny ESC/POS specimen through a supported 16-bit service
   and characteristic. The initial candidate is `FF00/FF02`; `18F0/2AF1` is
   another candidate to test separately. Never replay jobs across endpoints.
   The existing Python script's `49535343-8841-43f4-a8d4-ecbe34729bb3`
   characteristic requires a 128-bit UUID, which this SDK cannot pass through
   its `uint16_t` BLE write parameters. Do not truncate it.
2. Confirm `PRINTER_ADDRESS_BYTES` in the **SDK's native address order**.
   **Scan & Pair** obtains the address type from the matching device; alternatively
   a qualified fixed `PRINTER_ADDRESS_TYPE` can be configured for direct printing.
   The displayed
   `06:02:31:98:50:95` is not proof of byte order or public/random type.
   Default address bytes reflect display order only; scanning matches those exact
   bytes. Type `-1` blocks direct sending until scanning learns the actual type.
   Verify the address remains stable after power cycling. Configure the proven
   service/characteristic and set `PRINTER_BLE_VERIFIED` to `1` only after
   qualification of address, endpoint and SDK call behavior. Scanning and pairing
   are available with this flag off, but printing remains blocked. No guessed
   address reversal, bond deletion or automatic endpoint fallback.
3. Verify native ESC/POS `GS ( k` model-2 QR commands, module size 4, ECC M,
   printable width and quiet zone. Scan the paper QR with a phone; only then
   set `PRINTER_NATIVE_QR_VERIFIED` to `1`. QRIS uses a raster image command
   (`GS v 0`) instead of relying on the printer's native QR encoder for long
   payloads; confirm raster command support and scan the printed QR on the
   target printer before relying on it.

The worker uses the SDK `bt_ble_connect`, BLE status bit 5,
`bt_ble_set_gattc_write` and `bt_ble_write_data`; successful writes return **0**,
not a byte count. Payloads use at most 20-byte chunks with `PRINTER_PACE_MS`
(default 50 ms, uncalibrated). No MTU increase, notification assumptions or
unverified printer-status commands. Qualify pacing/write mode on real hardware.
Scan, link/print (60 seconds) and pairing deadlines are checked **between** SDK
calls; these APIs expose no
per-call timeout/cancellation. A firmware call that blocks indefinitely will
keep the worker busy, although LVGL remains responsive. Qualify SDK scan, pairing
and print call bounds on hardware. Exit is deferred during a job.

A failed write may already have delivered bytes. Jobs are never automatically
retried; inspect the paper before deliberately pressing again. Success means
"Sent to printer", not confirmed paper output. No PINs are stored or logged.

On the device, QRIS and diagnostic HTTPS requests use direct SDK TCP sockets and
WolfSSL through `src/https_client.c`, rather than the SDK HTTP request functions.
The client uses TLS 1.2 and SNI. Server certificate verification is disabled
for both requests, as explicitly selected for this staging integration. The app
does not use the bundled MQTT CA or GlobalSign Root CA R1. **Without certificate
verification, a network attacker could impersonate the staging server; do not
use this configuration for production payments.**

The QRIS request is a JSON POST to
`https://soundbox-notify-stg.netzme.com/api/v2/device/generate-qr`. The app
accepts only HTTP 200 and JSON `status: 200` responses containing a printable
`data.qr_content` value. That value is displayed in an LVGL QR view; tapping the
displayed QR sends it to the printer as a raster image generated from the same
QR data. The static QR button continues to use native ESC/POS QR commands. The
HTTPS probe shows the transport result and HTTP status; the HTTP library
version is logged for reference only.
The 10-second deadline is checked between retryable TLS/response calls. Blocking
DNS, TCP connect, or WolfSSL operations can exceed it; a hard end-to-end deadline
would require nonblocking I/O.
Automatic power saving is disabled only during the HTTPS request and the
previous mode is restored immediately afterward.
The app initializes SDK system services before checking Wi-Fi or starting
worker infrastructure. If either initialization fails,
the QRIS request is disabled while local print jobs remain available. Response
content is length-bounded and rejects printer control characters before
building the ESC/POS payload. The simulator reports network unavailable; the
QR request unit test mocks the raw TCP/WolfSSL exchange.

## Build And Run

### Choosing active Home menus

Set the `SOUNDBOX_MENU_*` switches in [config/conf.h](config/conf.h) to `1`
(shown) or `0` (hidden), then rebuild. The current configuration enables Motor,
Mobil, Sepeda and Elf; Bus and Input Nominal are disabled.
For the DQ72 four-tile layout, use:

```c
#define SOUNDBOX_MENU_MOTOR 1
#define SOUNDBOX_MENU_MOBIL 1
#define SOUNDBOX_MENU_SEPEDA 1
#define SOUNDBOX_MENU_ELF 0
#define SOUNDBOX_MENU_BUS 1
#define SOUNDBOX_MENU_MANUAL 0
```

`SOUNDBOX_MENU_MANUAL` controls **Input Nominal**. Any nonempty combination is
supported; an all-disabled or non-0/1 configuration fails compilation.
The manual vehicle picker uses only active vehicles. With manual input alone,
it goes straight to payment as **Lainnya**. Settings and History remain available.
This controls menu visibility, not which payment services are production-ready.

Stored prices and MQTT price updates are retained for all five vehicle types;
loading or updating them never re-enables a hidden menu. Re-enabling a vehicle
restores its stored price. Do not edit or delete the checksummed data files.
The switches apply to both device and simulator builds; no on-device toggle
or storage migration is needed. `make -C app/PrinterDemo/tests check-menus`
tests four-tile and manual-only builds, including all 63 nonempty menu
combinations before/after persisted state updates at all four viewports.

From the SDK workspace root:

```sh
make -C app/PrinterDemo check
make -C app/PrinterDemo
make -C sim APP=PrinterDemo -j4
make -C sim APP=PrinterDemo run
```

Device output: `output/system/PrinterDemo.npk`. Install through the normal
NEW3500 NPK installation workflow. The launcher uses
`/home/user0/PrinterDemo/PrinterDemo.bin`. Its icon is reused from Basic.
App-local `make_8850` templates support root `make app-PrinterDemo` without
discarding custom build rules; the direct app build avoids root regeneration.

The device build follows the repository's `-fno-short-enums` application ABI.
Some shipped vendor/newlib objects advertise variable-size enums, so the linker
reports enum-size mismatch warnings. They are not suppressed. A successful
link is not ABI certification; qualify the app and SDK calls on real firmware.

The simulator never uses a real Bluetooth adapter and explicitly reports
Bluetooth unavailable. QRIS networking is unavailable in the simulator; a
separate unit test mocks the raw TCP/WolfSSL path used on-device. Tests also cover SDK calls,
including HTTP framing, zero-success, chunk boundaries, errors/uncertain
delivery, scan/pair deadlines and cleanup,
address-type propagation, failed/interactive pairing and connection reuse. UI
tests check all five diagnostic controls and mutually exclusive actions at four
resolutions; parking workflow tests exercise the new UI at those same sizes.
Neither
host test success nor successful GATT writes qualify actual printing.

After any configuration change rebuild the device app and simulator. On the
device verify all three outputs, QR scanning, repeated taps, power cycle,
Scan & Pair with the printer on/off/out of range, firmware scan buffer/address
conventions, pairing/bond behavior, paper-out behavior, and disconnect mid-job
with no automatic replay.