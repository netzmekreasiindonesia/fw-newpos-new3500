#ifndef PRINTER_DEMO_SOUNDBOX_BACKEND_H
#define PRINTER_DEMO_SOUNDBOX_BACKEND_H

#include "soundbox_store.h"
#include "qr_request.h"
#include "print_jobs.h"
#include <stddef.h>

#define SOUNDBOX_HISTORY_MAX 16U
#define SOUNDBOX_DETAIL_MAX 16U

enum soundbox_job_kind { SB_JOB_QR, SB_JOB_HISTORY, SB_JOB_DETAIL, SB_JOB_ACCOUNT,
    SB_JOB_RECEIPT };
enum soundbox_receipt_delivery { SB_RECEIPT_FAILED, SB_RECEIPT_SENT, SB_RECEIPT_UNCERTAIN };
enum soundbox_backend_status {
    SB_BACKEND_OK, SB_BACKEND_NETWORK, SB_BACKEND_HTTP, SB_BACKEND_INVALID,
    SB_BACKEND_DECLINED, SB_BACKEND_AUTH, SB_BACKEND_SERVER, SB_BACKEND_STORAGE,
    SB_BACKEND_UNAVAILABLE
};

struct soundbox_backend_job {
    enum soundbox_job_kind kind;
    uint32_t token;
    uint32_t amount;
    unsigned type;
    char qr_content[QR_REQUEST_CONTENT_MAX + 1U];
    char transaction_id[96];
    char event_type[64];
    struct soundbox_attempt attempt;
    uint64_t paid_amount;
    struct print_receipt receipt;
    bool test_receipt;
};

struct soundbox_history_entry {
    char transaction_id[96];
    char event_type[64];
    char title[96];
    char subtitle[96];
    char amount[32];
};

struct soundbox_detail_entry {
    const char *label;
    char value[192];
};

struct soundbox_backend_result {
    enum soundbox_job_kind kind;
    uint32_t token;
    enum soundbox_backend_status status;
    int accounting_status;
    int http_status;
    int submission_started;
    struct soundbox_attempt attempt;
    enum soundbox_receipt_delivery delivery;
    union {
        char qr_content[QR_REQUEST_CONTENT_MAX + 1U];
        struct {
            size_t count;
            bool truncated;
            struct soundbox_history_entry entries[SOUNDBOX_HISTORY_MAX];
        } history;
        struct soundbox_detail_entry detail[SOUNDBOX_DETAIL_MAX];
        struct print_receipt receipt;
    } data;
};

/* Called only by the serialized worker; outputs own all response strings. */
void soundbox_backend_execute(const struct soundbox_backend_job *job,
    struct soundbox_backend_result *result);
const char *soundbox_backend_message(enum soundbox_backend_status status);

#endif
