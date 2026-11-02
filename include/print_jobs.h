#ifndef PRINT_JOBS_H
#define PRINT_JOBS_H

#include <stddef.h>
#include <stdint.h>

enum print_job {
    PRINT_QR,
    PRINT_RECEIPT
};

enum print_result {
    PRINT_OK,
    PRINT_INVALID,
    PRINT_NO_SPACE,
    PRINT_QR_UNSUPPORTED
};

#define PRINT_QR_MAX_LENGTH 512U
#define PRINT_JOB_BUFFER_SIZE 16384U

struct print_format {
    int native_qr_verified;
};

struct print_receipt {
    char merchant_name[96];
    char merchant_name_l2[96];
    char nmid[64];
    char vehicle[96];
    uint64_t amount;
    uint64_t paid_at;
    char rrn[96];
    char issuer[192];
    char method[32];
    char transaction_id[96];
};

enum print_result print_receipt_build(const struct print_receipt *receipt,
    uint8_t *buffer, size_t capacity, size_t *length);
enum print_result print_test_receipt_build(uint64_t paid_amount,
    uint8_t *buffer, size_t capacity, size_t *length);
enum print_result print_job_build(enum print_job job,
    const struct print_format *format, uint8_t *buffer, size_t capacity,
    size_t *length);
enum print_result print_qr_build(const char *qr_content, uint8_t *buffer,
    size_t capacity, size_t *length);
enum print_result print_qr_raster_build(const char *qr_content,
    uint8_t *buffer, size_t capacity, size_t *length);

#endif