#ifndef PRINTER_DEMO_SOUNDBOX_STORE_H
#define PRINTER_DEMO_SOUNDBOX_STORE_H

#include <stdbool.h>
#include <stdint.h>

#define SOUNDBOX_PRICE_COUNT 5U
enum soundbox_product { SB_MOTOR, SB_MOBIL, SB_SEPEDA, SB_ELF, SB_BUS };
enum soundbox_store_result {
    SB_STORE_OK, SB_STORE_RECOVERED, SB_STORE_DUPLICATE, SB_STORE_RECONCILE,
    SB_STORE_IO = -1, SB_STORE_CORRUPT = -2, SB_STORE_INVALID = -3,
    SB_STORE_OVERFLOW = -4, SB_STORE_BUSY = -5
};

struct soundbox_settings {
    char merchant_name[96];
    char merchant_name_l2[96];
    char merchant_nmid[64];
    uint32_t prices[SOUNDBOX_PRICE_COUNT];
};

struct soundbox_attempt {
    uint64_t id;
    uint64_t merchant_generation;
    uint32_t date;
    char nmid[64];
};

struct soundbox_record {
    uint64_t sequence;
    uint64_t merchant_generation;
    uint64_t next_attempt;
    uint64_t last_accounted;
    struct soundbox_settings settings;
    uint32_t date;
    uint64_t count;
    uint64_t total;
    struct soundbox_attempt attempt;
    uint32_t pending;
    uint32_t reconciliation;
    uint64_t reconciliation_amount;
};

struct soundbox_store {
    struct soundbox_record record;
    int slot;
    bool ready;
};

/* Single worker owns the store. No API performs I/O on the LVGL thread. */
int soundbox_store_open(struct soundbox_store *store, int64_t utc_seconds);
int soundbox_store_settings(struct soundbox_store *store,
    const struct soundbox_settings *settings);
int soundbox_store_rollover(struct soundbox_store *store, int64_t utc_seconds);
int soundbox_store_begin(struct soundbox_store *store, struct soundbox_attempt *attempt);
/* Only abandon attempts known not to have charged; uncertain submissions stay pending. */
int soundbox_store_cancel(struct soundbox_store *store, uint64_t attempt_id);
int soundbox_store_account(struct soundbox_store *store,
    const struct soundbox_attempt *attempt, uint64_t amount, int64_t utc_seconds);
uint32_t soundbox_business_date(int64_t utc_seconds);
bool soundbox_settings_valid(const struct soundbox_settings *settings);

#endif
