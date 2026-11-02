#define _POSIX_C_SOURCE 200809L
#include "print_jobs.h"

#include <lvgl/src/extra/libs/qrcode/qrcodegen.h>

#include <string.h>
#include <stdio.h>
#include <time.h>

#define QR_QUIET_ZONE_MODULES 4
#define QR_DOTS_PER_MODULE 3

static int append(uint8_t *buffer, size_t capacity, size_t *used,
    const void *data, size_t length)
{
    if (length > capacity - *used)
        return 0;
    memcpy(buffer + *used, data, length);
    *used += length;
    return 1;
}

static int valid_qr_content(const char *text, size_t *text_length)
{
    size_t index;
    size_t length;

    if (!text || !text_length)
        return 0;
    length = strlen(text);
    if (!length || length > PRINT_QR_MAX_LENGTH)
        return 0;
    for (index = 0; index < length; ++index) {
        unsigned char character = (unsigned char)text[index];
        if (character < 0x20 || character > 0x7e)
            return 0;
    }
    *text_length = length;
    return 1;
}

static int append_qr(uint8_t *buffer, size_t capacity, size_t *used,
    const char *text)
{
    static const uint8_t setup[] = {
        0x1d, 0x28, 0x6b, 4, 0, 49, 65, 50, 0,
        0x1d, 0x28, 0x6b, 3, 0, 49, 67, 4,
        0x1d, 0x28, 0x6b, 3, 0, 49, 69, 49
    };
    static const uint8_t print[] = {0x1d, 0x28, 0x6b, 3, 0, 49, 81, 48};
    size_t text_length = strlen(text);
    uint16_t payload_length = (uint16_t)(text_length + 3U);
    uint8_t store_header[] = {
        0x1d, 0x28, 0x6b,
        (uint8_t)(payload_length & 0xff),
        (uint8_t)(payload_length >> 8),
        49, 80, 48
    };

    return append(buffer, capacity, used, setup, sizeof(setup)) &&
        append(buffer, capacity, used, store_header, sizeof(store_header)) &&
        append(buffer, capacity, used, text, text_length) &&
        append(buffer, capacity, used, print, sizeof(print));
}

enum print_result print_qr_build(const char *qr_content, uint8_t *buffer,
    size_t capacity, size_t *length)
{
    static const uint8_t reset[] = {0x1b, 0x40};
    static const uint8_t center[] = {0x1b, 0x61, 1};
    static const uint8_t finish[] = {0x0a, 0x0a, 0x0a, 0x1b, 0x61, 0};
    size_t used = 0;
    size_t text_length;

    if (length)
        *length = 0;
    if (!qr_content || !buffer || !length)
        return PRINT_INVALID;
    if (!valid_qr_content(qr_content, &text_length))
        return PRINT_INVALID;
    if (!append(buffer, capacity, &used, reset, sizeof(reset)) ||
        !append(buffer, capacity, &used, center, sizeof(center)) ||
        !append_qr(buffer, capacity, &used, qr_content) ||
        !append(buffer, capacity, &used, finish, sizeof(finish)))
        return PRINT_NO_SPACE;
    *length = used;
    return PRINT_OK;
}

enum print_result print_qr_raster_build(const char *qr_content,
    uint8_t *buffer, size_t capacity, size_t *length)
{
    static const uint8_t reset[] = {0x1b, 0x40};
    static const uint8_t center[] = {0x1b, 0x61, 1};
    static const uint8_t finish[] = {0x0a, 0x0a, 0x0a, 0x1b, 0x61, 0};
    uint8_t data_and_temp[qrcodegen_BUFFER_LEN_MAX];
    uint8_t qrcode[qrcodegen_BUFFER_LEN_MAX];
    uint8_t raster_header[8];
    size_t used = 0;
    size_t text_length;
    size_t row_bytes;
    size_t raster_length;
    int version;
    int modules;
    int pixels;
    int x;
    int y;

    if (length)
        *length = 0;
    if (!buffer || !length || !valid_qr_content(qr_content, &text_length))
        return PRINT_INVALID;

    version = qrcodegen_getMinFitVersion(qrcodegen_Ecc_MEDIUM, text_length);
    if (version < qrcodegen_VERSION_MIN)
        return PRINT_INVALID;
    memcpy(data_and_temp, qr_content, text_length);
    if (!qrcodegen_encodeBinary(data_and_temp, text_length, qrcode,
            qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, version,
            qrcodegen_Mask_AUTO, true))
        return PRINT_INVALID;

    modules = qrcodegen_getSize(qrcode);
    pixels = (modules + 2 * QR_QUIET_ZONE_MODULES) * QR_DOTS_PER_MODULE;
    row_bytes = ((size_t)pixels + 7U) / 8U;
    raster_length = row_bytes * (size_t)pixels;
    if (row_bytes > 0xffffU || pixels > 0xffff ||
        sizeof(reset) + sizeof(center) + sizeof(raster_header) +
            raster_length + sizeof(finish) > capacity)
        return PRINT_NO_SPACE;

    raster_header[0] = 0x1d;
    raster_header[1] = 0x76;
    raster_header[2] = 0x30;
    raster_header[3] = 0;
    raster_header[4] = (uint8_t)(row_bytes & 0xffU);
    raster_header[5] = (uint8_t)(row_bytes >> 8);
    raster_header[6] = (uint8_t)(pixels & 0xff);
    raster_header[7] = (uint8_t)(pixels >> 8);
    append(buffer, capacity, &used, reset, sizeof(reset));
    append(buffer, capacity, &used, center, sizeof(center));
    append(buffer, capacity, &used, raster_header, sizeof(raster_header));

    memset(buffer + used, 0, raster_length);
    for (y = 0; y < pixels; ++y) {
        int module_y = y / QR_DOTS_PER_MODULE - QR_QUIET_ZONE_MODULES;
        if (module_y < 0 || module_y >= modules)
            continue;
        for (x = 0; x < pixels; ++x) {
            int module_x = x / QR_DOTS_PER_MODULE - QR_QUIET_ZONE_MODULES;
            if (module_x >= 0 && module_x < modules &&
                qrcodegen_getModule(qrcode, module_x, module_y)) {
                size_t offset = used + (size_t)y * row_bytes +
                    (size_t)x / 8U;
                buffer[offset] |= (uint8_t)(0x80U >> (x & 7));
            }
        }
    }
    used += raster_length;
    append(buffer, capacity, &used, finish, sizeof(finish));
    *length = used;
    return PRINT_OK;
}

static int receipt_text_valid(const char *text, size_t capacity, int required)
{
    const char *end = memchr(text, 0, capacity);
    if (!end || (required && end == text))
        return 0;
    while (text != end) {
        unsigned char c = (unsigned char)*text++;
        if (c < 0x20U || c > 0x7eU)
            return 0;
    }
    return 1;
}

static int receipt_line(uint8_t *buffer, size_t capacity, size_t *used,
    const char *text)
{
    size_t remaining = strlen(text);
    do {
        size_t count = remaining > 32U ? 32U : remaining;
        if (!append(buffer, capacity, used, text, count) ||
            !append(buffer, capacity, used, "\n", 1))
            return 0;
        text += count;
        remaining -= count;
    } while (remaining);
    return 1;
}

enum print_result print_receipt_build(const struct print_receipt *receipt,
    uint8_t *buffer, size_t capacity, size_t *length)
{
    static const uint8_t reset[] = {0x1b, 0x40, 0x1b, 0x21, 0, 0x1d, 0x21, 0,
        0x1b, 0x61, 0};
    static const char rule[] = "--------------------------------";
    const char *fields[8];
    size_t sizes[8], i, used = 0, digits_length, offset;
    char date[32], digits[24], amount[40] = "Rp ";
    time_t local;
    struct tm calendar;
    if (length)
        *length = 0;
    if (!receipt || !buffer || !length)
        return PRINT_INVALID;
    fields[0] = receipt->merchant_name; sizes[0] = sizeof(receipt->merchant_name);
    fields[1] = receipt->merchant_name_l2; sizes[1] = sizeof(receipt->merchant_name_l2);
    fields[2] = receipt->nmid; sizes[2] = sizeof(receipt->nmid);
    fields[3] = receipt->vehicle; sizes[3] = sizeof(receipt->vehicle);
    fields[4] = receipt->rrn; sizes[4] = sizeof(receipt->rrn);
    fields[5] = receipt->issuer; sizes[5] = sizeof(receipt->issuer);
    fields[6] = receipt->method; sizes[6] = sizeof(receipt->method);
    fields[7] = receipt->transaction_id; sizes[7] = sizeof(receipt->transaction_id);
    for (i = 0; i < 8; ++i)
        if (!receipt_text_valid(fields[i], sizes[i],
                i != 1 && i != 3 && (i != 0 || !receipt->merchant_name_l2[0])))
            return PRINT_INVALID;
    if (!receipt->amount || receipt->amount > UINT64_C(100000000) ||
        receipt->paid_at < UINT64_C(1577836800) ||
        receipt->paid_at > UINT64_C(4102419599))
        return PRINT_INVALID;
    local = (time_t)(receipt->paid_at + 7U * 3600U);
    if ((uint64_t)local != receipt->paid_at + 7U * 3600U ||
        !gmtime_r(&local, &calendar) ||
        !strftime(date, sizeof(date), "%d/%m/%Y %H:%M:%S WIB", &calendar))
        return PRINT_INVALID;
    snprintf(digits, sizeof(digits), "%llu", (unsigned long long)receipt->amount);
    digits_length = strlen(digits);
    offset = 3;
    for (i = 0; i < digits_length; ++i) {
        if (i && (digits_length - i) % 3U == 0)
            amount[offset++] = '.';
        amount[offset++] = digits[i];
    }
    amount[offset] = 0;
    if (!append(buffer, capacity, &used, reset, sizeof(reset)) ||
        !receipt_line(buffer, capacity, &used, "             NETZME") ||
        !receipt_line(buffer, capacity, &used, receipt->merchant_name) ||
        (receipt->merchant_name_l2[0] &&
         !receipt_line(buffer, capacity, &used, receipt->merchant_name_l2)) ||
        !receipt_line(buffer, capacity, &used, "NMID:") ||
        !receipt_line(buffer, capacity, &used, receipt->nmid) ||
        !receipt_line(buffer, capacity, &used, rule) ||
        !receipt_line(buffer, capacity, &used, "     PEMBAYARAN BERHASIL") ||
        !receipt_line(buffer, capacity, &used, rule) ||
        !receipt_line(buffer, capacity, &used, "Tanggal:") ||
        !receipt_line(buffer, capacity, &used, date) ||
        !receipt_line(buffer, capacity, &used, "Nominal:") ||
        !receipt_line(buffer, capacity, &used, amount) ||
        !receipt_line(buffer, capacity, &used, "RRN:") ||
        !receipt_line(buffer, capacity, &used, receipt->rrn) ||
        !receipt_line(buffer, capacity, &used, "Penerbit:") ||
        !receipt_line(buffer, capacity, &used, receipt->issuer) ||
        !receipt_line(buffer, capacity, &used, "Metode:") ||
        !receipt_line(buffer, capacity, &used, receipt->method) ||
        (receipt->vehicle[0] &&
         (!receipt_line(buffer, capacity, &used, "Kendaraan:") ||
          !receipt_line(buffer, capacity, &used, receipt->vehicle))) ||
        !receipt_line(buffer, capacity, &used, "Referensi:") ||
        !receipt_line(buffer, capacity, &used, receipt->transaction_id) ||
        !receipt_line(buffer, capacity, &used, rule) ||
        !receipt_line(buffer, capacity, &used, "        Terima kasih") ||
        !append(buffer, capacity, &used, "\n\n\n", 3))
        return PRINT_NO_SPACE;
    *length = used;
    return PRINT_OK;
}

enum print_result print_test_receipt_build(uint64_t paid_amount,
    uint8_t *buffer, size_t capacity, size_t *length)
{
    static const uint8_t reset[] = {0x1b, 0x40, 0x1b, 0x21, 0, 0x1d, 0x21, 0,
        0x1b, 0x61, 0};
    static const char header[] =
        "             NETZME\n"
        "         STRUK UJI COBA\n"
        "--------------------------------\n"
        "     PEMBAYARAN BERHASIL\n\n";
    static const char footer[] =
        "--------------------------------\n"
        "          Terima kasih\n"
        "       BUKAN STRUK RESMI\n\n\n\n";
    char digits[24], total[48] = "TOTAL BAYAR: Rp ";
    size_t i, count, used = 0, offset = strlen(total);
    if (length)
        *length = 0;
    if (!buffer || !length || !paid_amount || paid_amount > UINT64_C(100000000))
        return PRINT_INVALID;
    snprintf(digits, sizeof(digits), "%llu", (unsigned long long)paid_amount);
    count = strlen(digits);
    for (i = 0; i < count; ++i) {
        if (i && (count - i) % 3U == 0)
            total[offset++] = '.';
        total[offset++] = digits[i];
    }
    total[offset++] = '\n';
    total[offset] = 0;
    if (!append(buffer, capacity, &used, reset, sizeof(reset)) ||
        !append(buffer, capacity, &used, header, sizeof(header) - 1U) ||
        !append(buffer, capacity, &used, total, offset) ||
        !append(buffer, capacity, &used, footer, sizeof(footer) - 1U))
        return PRINT_NO_SPACE;
    *length = used;
    return PRINT_OK;
}

enum print_result print_job_build(enum print_job job,
    const struct print_format *format, uint8_t *buffer, size_t capacity,
    size_t *length)
{
    static const uint8_t reset[] = {0x1b, 0x40, 0x1b, 0x21, 0, 0x1d, 0x21, 0};
    static const uint8_t center[] = {0x1b, 0x61, 1};
    static const uint8_t finish[] = {0x0a, 0x0a, 0x0a, 0x1b, 0x61, 0};
    static const char receipt_header[] =
        "================================\n"
        "             NETZME             \n"
        "         WARUNG BAROKAH         \n"
        "     NMID: ID2020025319142      \n"
        "     Jakarta Timur (alamat)     \n"
        "--------------------------------\n"
        "  *** PEMBAYARAN BERHASIL ***   \n"
        "--------------------------------\n"
        "Tanggal         16/07/2026 08:08\n"
        "Ref No.          NTZ260716000123\n"
        "RRN                 019876543210\n"
        "Metode                      QRIS\n"
        "Penerbit                     BCA\n"
        "Acquirer                  NETZME\n"
        "Pelanggan              BUD***NTO\n"
        "CPAN         xxxx-xxxx-xxxx-5678\n"
        "--------------------------------\n"
        "Tagihan             Rp    10.000\n"
        "Tip                 Rp         0\n"
        "--------------------------------\n"
        "TOTAL               Rp    10.000\n"
        "--------------------------------\n"
        "   Rp10.000 berhasil dibayar    \n"
        "  oleh pembeli. Cek rinciannya  \n"
        "     di riwayat transaksi.      \n"
        "--------------------------------\n";
    static const char receipt_footer[] =
        "  Scan untuk cek keaslian struk \n"
        "\n"
        "       Terima kasih atas        \n"
        "        pembayaran Anda!        \n"
        " Powered by Netzme - CS 1500-xxx\n"
        "================================\n";
    size_t used = 0;

    if (length)
        *length = 0;
    if (!format || !buffer || !length ||
        (job != PRINT_QR && job != PRINT_RECEIPT))
        return PRINT_INVALID;
    if (job == PRINT_QR) {
        if (!format->native_qr_verified)
            return PRINT_QR_UNSUPPORTED;
        return print_qr_build("https://netzme.com", buffer, capacity, length);
    }

    if (!append(buffer, capacity, &used, reset, sizeof(reset)) ||
        !append(buffer, capacity, &used, center, sizeof(center)) ||
        !append(buffer, capacity, &used, receipt_header, strlen(receipt_header)))
        return PRINT_NO_SPACE;
    if (format->native_qr_verified &&
        !append_qr(buffer, capacity, &used,
            "https://netzme.com/qris/NTZ260716000123"))
        return PRINT_NO_SPACE;
    if (!append(buffer, capacity, &used, receipt_footer, strlen(receipt_footer)) ||
        !append(buffer, capacity, &used, finish, sizeof(finish)))
        return PRINT_NO_SPACE;
    *length = used;
    return PRINT_OK;
}
