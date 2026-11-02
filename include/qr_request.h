#ifndef QR_REQUEST_H
#define QR_REQUEST_H

#include <stddef.h>

#define QR_REQUEST_CONTENT_MAX 512U

enum qr_request_result {
    QR_REQUEST_OK,
    QR_REQUEST_NETWORK_FAILED,
    QR_REQUEST_SERVER_FAILED,
    QR_REQUEST_INVALID_RESPONSE
};

enum qr_request_result qr_request_generate(char *qr_content, size_t capacity,
    int *http_status);

#endif
