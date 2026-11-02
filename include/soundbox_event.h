#ifndef PRINTER_DEMO_SOUNDBOX_EVENT_H
#define PRINTER_DEMO_SOUNDBOX_EVENT_H

#include <stddef.h>
#include "soundbox_store.h"

enum soundbox_event_type { SB_EVENT_PAYMENT = 1, SB_EVENT_MERCHANT = 4, SB_EVENT_PRICES = 6 };
#define SB_MERCHANT_NAME 1U
#define SB_MERCHANT_NAME_L2 2U
#define SB_MERCHANT_NMID 4U

struct soundbox_event {
    enum soundbox_event_type type;
    unsigned int generation;
    uint64_t money;
    char transaction_id[96];
    char event_type[64];
    uint32_t fields;
    struct soundbox_settings settings;
};

int soundbox_event_parse(const unsigned char *payload, size_t length,
    struct soundbox_event *event);
int soundbox_event_apply(struct soundbox_store *store, const struct soundbox_event *event);

#endif
