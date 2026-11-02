#include "print_jobs.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_jobs(void)
{
    struct print_format format = {0};
    uint8_t buffer[PRINT_JOB_BUFFER_SIZE];
    size_t length = 99;
    size_t index;
    static const char qr_content[] = "00020101021226650013ID.NETZME.WWW";
    static const char qris_content[] =
        "00020101021226650013ID.NETZME.WWW01189360081401000011360215"
        "ID20240000000810303UMI51440014ID.CO.QRIS.WWW0215ID20240000000810303UMI"
        "520441215303360540436665802ID5913Clients Games6015JAKARTA SELATAN"
        "61051281062690703A010812qris_invoice9942000200013235f680d17edd4720aa"
        "4705c48b5f5b2e6304F80C";

    assert(print_job_build(PRINT_QR, &format, buffer, sizeof(buffer), &length) ==
        PRINT_QR_UNSUPPORTED);
    assert(length == 0);
    format.native_qr_verified = 1;
    assert(print_job_build(PRINT_QR, &format, buffer, sizeof(buffer), &length) ==
        PRINT_OK);
    assert(length > strlen("https://netzme.com"));

    assert(print_qr_build(qr_content, buffer, sizeof(buffer), &length) == PRINT_OK);
    assert(length > strlen(qr_content));
    for (index = 0; index + sizeof(qr_content) - 1U <= length; ++index) {
        if (memcmp(buffer + index, qr_content, sizeof(qr_content) - 1U) == 0)
            break;
    }
    assert(index + sizeof(qr_content) - 1U <= length);
    for (index = 0; index < length; ++index) {
        size_t short_length = 99;
        assert(print_qr_build(qr_content, buffer, index, &short_length) ==
            PRINT_NO_SPACE);
        assert(short_length == 0);
    }
    assert(print_qr_build("Bad\033payload", buffer, sizeof(buffer), &length) ==
        PRINT_INVALID);
    assert(length == 0);

    assert(strlen(qris_content) == 284U);
    assert(print_qr_raster_build(qris_content, buffer, sizeof(buffer), &length) ==
        PRINT_OK);
    {
        const size_t header_offset = 5U;
        size_t row_bytes;
        size_t raster_height;
        size_t image_offset = header_offset + 8U;
        size_t raster_length;
        int has_black_pixel = 0;

        assert(buffer[header_offset] == 0x1d);
        assert(buffer[header_offset + 1U] == 0x76);
        assert(buffer[header_offset + 2U] == 0x30);
        row_bytes = buffer[header_offset + 4U] |
            ((size_t)buffer[header_offset + 5U] << 8);
        raster_height = buffer[header_offset + 6U] |
            ((size_t)buffer[header_offset + 7U] << 8);
        raster_length = row_bytes * raster_height;
        assert(row_bytes > 0 && raster_height > 0);
        assert(length == image_offset + raster_length + 6U);
        for (index = image_offset; index < image_offset + raster_length; ++index)
            has_black_pixel |= buffer[index] != 0;
        assert(has_black_pixel);
    }
    {
        size_t short_length = 99;
        assert(print_qr_raster_build(qris_content, buffer, length - 1U,
            &short_length) == PRINT_NO_SPACE);
        assert(short_length == 0);
    }

    assert(print_job_build(PRINT_RECEIPT, &format, buffer, sizeof(buffer), &length) ==
        PRINT_OK);
    assert(length > 500);
    {
        int found_merchant = 0;
        int found_status = 0;
        int found_reference = 0;
        for (index = 0; index < length; ++index) {
            if (index + 14U <= length &&
                memcmp(buffer + index, "WARUNG BAROKAH", 14) == 0)
                found_merchant = 1;
            if (index + 19U <= length &&
                memcmp(buffer + index, "PEMBAYARAN BERHASIL", 19) == 0)
                found_status = 1;
            if (index + 15U <= length &&
                memcmp(buffer + index, "NTZ260716000123", 15) == 0)
                found_reference = 1;
        }
        assert(found_merchant && found_status && found_reference);
    }
    assert(print_job_build((enum print_job)99, &format, buffer, sizeof(buffer),
        &length) == PRINT_INVALID);
    assert(length == 0);
    assert(print_job_build(PRINT_QR, NULL, buffer, sizeof(buffer), &length) ==
        PRINT_INVALID);
    assert(print_job_build(PRINT_RECEIPT, &format, buffer, sizeof(buffer), NULL) ==
        PRINT_INVALID);
}

static int contains(const uint8_t *buffer, size_t length, const char *text)
{
    size_t i, n = strlen(text);
    for (i = 0; i + n <= length; ++i)
        if (!memcmp(buffer + i, text, n))
            return 1;
    return 0;
}

static void test_transaction_receipt(void)
{
    struct print_receipt receipt = {0};
    uint8_t buffer[PRINT_JOB_BUFFER_SIZE];
    size_t length, expected, i, column = 0;
    strcpy(receipt.merchant_name, "Parkir Selatan");
    strcpy(receipt.merchant_name_l2, "Gerbang 2");
    strcpy(receipt.nmid, "ID000012345678901234567890123456789012345678901234567890");
    strcpy(receipt.vehicle, "Motor");
    strcpy(receipt.rrn, "000012345678");
    strcpy(receipt.issuer, "Issuer Aktual");
    strcpy(receipt.method, "QRIS MPM");
    strcpy(receipt.transaction_id, "TRX000123");
    receipt.amount = 1234567;
    receipt.paid_at = 1700000000;
    assert(print_receipt_build(&receipt, buffer, sizeof(buffer), &length) == PRINT_OK);
    expected = length;
    assert(contains(buffer, length, "15/11/2023 05:13:20 WIB"));
    assert(contains(buffer, length, "Rp 1.234.567"));
    assert(contains(buffer, length, "000012345678"));
    assert(contains(buffer, length, "Issuer Aktual"));
    assert(contains(buffer, length, "TRX000123"));
    assert(contains(buffer, length, "ID000012345678901234567890123456\n789012345678901234567890"));
    assert(!contains(buffer, length, "WARUNG") && !contains(buffer, length, "https://"));
    for (i = 11; i < length; ++i) {
        if (buffer[i] == '\n')
            column = 0;
        else
            assert(++column <= 32);
    }
    for (i = 0; i < expected; ++i) {
        assert(print_receipt_build(&receipt, buffer, i, &length) == PRINT_NO_SPACE);
        assert(length == 0);
    }
    strcpy(receipt.issuer, "bad\033payload");
    assert(print_receipt_build(&receipt, buffer, sizeof(buffer), &length) == PRINT_INVALID);
    assert(length == 0);
    strcpy(receipt.issuer, "bad\xc3\xa9");
    assert(print_receipt_build(&receipt, buffer, sizeof(buffer), &length) == PRINT_INVALID);
    memset(receipt.issuer, 'A', sizeof(receipt.issuer));
    assert(print_receipt_build(&receipt, buffer, sizeof(buffer), &length) == PRINT_INVALID);
    strcpy(receipt.issuer, "Issuer");
    receipt.rrn[0] = 0;
    assert(print_receipt_build(&receipt, buffer, sizeof(buffer), &length) == PRINT_INVALID);
    strcpy(receipt.rrn, "000012345678");
    receipt.paid_at = 0;
    assert(print_receipt_build(&receipt, buffer, sizeof(buffer), &length) == PRINT_INVALID);
    receipt.paid_at = 1700067599;
    assert(print_receipt_build(&receipt, buffer, sizeof(buffer), &length) == PRINT_OK);
    assert(contains(buffer, length, "15/11/2023 23:59:59 WIB"));
    ++receipt.paid_at;
    assert(print_receipt_build(&receipt, buffer, sizeof(buffer), &length) == PRINT_OK);
    assert(contains(buffer, length, "16/11/2023 00:00:00 WIB"));
    receipt.amount = 0;
    assert(print_receipt_build(&receipt, buffer, sizeof(buffer), &length) == PRINT_INVALID);
    receipt.amount = 100000000;
    assert(print_receipt_build(&receipt, buffer, sizeof(buffer), &length) == PRINT_OK);
    assert(contains(buffer, length, "Rp 100.000.000"));
    ++receipt.amount;
    assert(print_receipt_build(&receipt, buffer, sizeof(buffer), &length) == PRINT_INVALID);
    receipt.amount = 2000;
    receipt.merchant_name[0] = 0;
    assert(print_receipt_build(&receipt, buffer, sizeof(buffer), &length) == PRINT_OK);
    receipt.merchant_name_l2[0] = 0;
    assert(print_receipt_build(&receipt, buffer, sizeof(buffer), &length) == PRINT_INVALID);
    assert(print_receipt_build(NULL, buffer, sizeof(buffer), &length) == PRINT_INVALID);
    assert(print_receipt_build(&receipt, NULL, sizeof(buffer), &length) == PRINT_INVALID);
    assert(print_receipt_build(&receipt, buffer, sizeof(buffer), NULL) == PRINT_INVALID);
}

static void test_flow_receipt(void)
{
    uint8_t buffer[PRINT_JOB_BUFFER_SIZE];
    size_t length, expected, i;
    assert(print_test_receipt_build(2000, buffer, sizeof(buffer), &length) == PRINT_OK);
    assert(contains(buffer, length, "TOTAL BAYAR: Rp 2.000\n"));
    assert(contains(buffer, length, "STRUK UJI COBA"));
    assert(contains(buffer, length, "BUKAN STRUK RESMI"));
    assert(!contains(buffer, length, "RRN") && !contains(buffer, length, "https://"));
    expected = length;
    for (i = 0; i < expected; ++i) {
        assert(print_test_receipt_build(2000, buffer, i, &length) == PRINT_NO_SPACE);
        assert(length == 0);
    }
    assert(print_test_receipt_build(7000, buffer, sizeof(buffer), &length) == PRINT_OK);
    assert(contains(buffer, length, "TOTAL BAYAR: Rp 7.000\n"));
    assert(!contains(buffer, length, "Rp 2.000"));
    assert(print_test_receipt_build(100000000, buffer, sizeof(buffer), &length) == PRINT_OK);
    assert(contains(buffer, length, "Rp 100.000.000"));
    assert(print_test_receipt_build(100000001, buffer, sizeof(buffer), &length) == PRINT_INVALID);
    assert(length == 0);
    assert(print_test_receipt_build(0, buffer, sizeof(buffer), &length) == PRINT_INVALID);
    assert(print_test_receipt_build(2000, NULL, sizeof(buffer), &length) == PRINT_INVALID);
    assert(print_test_receipt_build(2000, buffer, sizeof(buffer), NULL) == PRINT_INVALID);
}

int main(void)
{
    test_jobs();
    test_transaction_receipt();
    test_flow_receipt();
    puts("PrinterDemo payload tests passed");
    return 0;
}
