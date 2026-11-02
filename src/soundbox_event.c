#include "soundbox_event.h"

#include <cJSON.h>
#include <stdlib.h>
#include <string.h>

#define LOGGER_MODULE_NAME "soundbox-event"
#include "libs/logger/logger.h"

static int rejected(const char *reason)
{
    logger_warn("Soundbox broadcast rejected: %s\n", reason);
    return -1;
}

static bool unique(const cJSON *object)
{
    const cJSON *a, *b;
    for (a = object->child; a; a = a->next) {
        for (b = a->next; b; b = b->next) {
            if (!a->string || !b->string || strcmp(a->string, b->string) == 0)
                return false;
        }
    }
    return true;
}

static bool bounded_depth(const unsigned char *payload, size_t length)
{
    size_t i;
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (i = 0; i < length; ++i) {
        unsigned char c = payload[i];
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
        } else if (c == '"') {
            quoted = true;
        } else if (c == '{' || c == '[') {
            if (++depth > 16)
                return false;
        } else if (c == '}' || c == ']') {
            if (!depth)
                return false;
            --depth;
        }
    }
    return !depth && !quoted;
}

static bool integer(const cJSON *field, uint64_t *value)
{
    const char *digit;
    uint64_t number = 0;
    if (cJSON_IsNumber(field)) {
        double d = field->valuedouble;
        if (!(d >= 0 && d <= 9007199254740991.0))
            return false;
        number = (uint64_t)d;
        if (d > (double)number)
            return false;
    } else if (cJSON_IsString(field) && field->valuestring[0]) {
        for (digit = field->valuestring; *digit; ++digit) {
            unsigned d = (unsigned char)*digit - '0';
            if (d > 9 || number > (UINT64_MAX - d) / 10U)
                return false;
            number = number * 10U + d;
        }
    } else {
        return false;
    }
    *value = number;
    return true;
}

static bool string_field(const cJSON *root, const char *key,
    char *out, size_t capacity, uint32_t *mask, uint32_t bit)
{
    cJSON *field = cJSON_GetObjectItemCaseSensitive(root, key);
    const unsigned char *c;
    if (!field)
        return true;
    if (!cJSON_IsString(field) || strlen(field->valuestring) >= capacity)
        return false;
    for (c = (const unsigned char *)field->valuestring; *c; ++c) {
        if (*c < 32 || *c == 127)
            return false;
    }
    strcpy(out, field->valuestring);
    *mask |= bit;
    return true;
}

static int product(const cJSON *item)
{
    static const char *const codes[] = {"-MOTOR-", "-MOBIL-", "-SEPEDA-", "-ELF-", "-BUS-"};
    static const char *const names[] = {"Motor", "Mobil", "Sepeda", "Elf", "Bus"};
    cJSON *code = cJSON_GetObjectItemCaseSensitive(item, "code");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
    unsigned i;
    for (i = 0; i < SOUNDBOX_PRICE_COUNT; ++i) {
        if (cJSON_IsString(code) && strstr(code->valuestring, codes[i]))
            return (int)i;
    }
    for (i = 0; i < SOUNDBOX_PRICE_COUNT; ++i) {
        if (cJSON_IsString(name) && strcmp(name->valuestring, names[i]) == 0)
            return (int)i;
    }
    return -1;
}

int soundbox_event_parse(const unsigned char *payload, size_t length,
    struct soundbox_event *event)
{
    char *json;
    cJSON *root, *field;
    uint64_t type;
    bool valid = true;
    if (!event || !payload || !length || length > 4096 || memchr(payload, 0, length) ||
        !bounded_depth(payload, length))
        return rejected("length/NUL/depth");
    memset(event, 0, sizeof(*event));
    json = malloc(length + 1U);
    if (!json)
        return rejected("memory unavailable");
    memcpy(json, payload, length);
    json[length] = '\0';
    if (strstr(json, "\\u0000")) {
        free(json);
        return rejected("escaped NUL");
    }
    root = cJSON_ParseWithOpts(json, NULL, 1);
    free(json);
    if (!cJSON_IsObject(root) || !unique(root)) {
        cJSON_Delete(root);
        return rejected("JSON or duplicate keys");
    }
    field = cJSON_GetObjectItemCaseSensitive(root, "broadcast_type");
    if (!integer(field, &type) || (type != 1 && type != 4 && type != 6) ||
        (cJSON_IsString(field) && strcmp(field->valuestring, "1") != 0 &&
         strcmp(field->valuestring, "4") != 0 && strcmp(field->valuestring, "6") != 0)) {
        cJSON_Delete(root);
        return rejected("unsupported broadcast_type");
    }
    event->type = (enum soundbox_event_type)type;
    if (type == SB_EVENT_PAYMENT) {
        uint32_t identifiers = 0;
        field = cJSON_GetObjectItemCaseSensitive(root, "money");
        valid = !field || integer(field, &event->money);
        if (!string_field(root, "transaction_id", event->transaction_id,
                sizeof(event->transaction_id), &identifiers, 1U) ||
            !string_field(root, "event_type", event->event_type,
                sizeof(event->event_type), &identifiers, 2U) ||
            identifiers != 3U || !event->transaction_id[0] || !event->event_type[0]) {
            event->transaction_id[0] = event->event_type[0] = '\0';
#if SOUNDBOX_TEST_RECEIPT
            logger_info("Receipt identifiers unavailable; flow-test receipt uses amount only\n");
#else
            logger_warn("Payment receipt identifiers missing/invalid; payment acknowledgement retained\n");
#endif
        }
    } else if (type == SB_EVENT_MERCHANT) {
        valid = string_field(root, "merchant_name", event->settings.merchant_name,
            sizeof(event->settings.merchant_name), &event->fields, SB_MERCHANT_NAME) &&
            string_field(root, "merchant_name_l2", event->settings.merchant_name_l2,
            sizeof(event->settings.merchant_name_l2), &event->fields, SB_MERCHANT_NAME_L2) &&
            string_field(root, "merchant_nmid", event->settings.merchant_nmid,
            sizeof(event->settings.merchant_nmid), &event->fields, SB_MERCHANT_NMID) &&
            event->fields;
    } else {
        cJSON *products = cJSON_GetObjectItemCaseSensitive(root, "products");
        int i, count = cJSON_GetArraySize(products);
        valid = cJSON_IsArray(products) && count > 0 && count <= 32;
        for (i = 0; valid && i < count; ++i) {
            cJSON *item = cJSON_GetArrayItem(products, i);
            uint64_t price;
            int index;
            if (!cJSON_IsObject(item) || !unique(item)) {
                valid = false;
                break;
            }
            index = product(item);
            if (index < 0) {
                logger_warn("Ignoring unrecognized parking product\n");
                continue;
            }
            if ((event->fields & (1U << index)) ||
                !integer(cJSON_GetObjectItemCaseSensitive(item, "price"), &price) ||
                price < 1000 || price > 10000000) {
                valid = false;
                break;
            }
            event->fields |= 1U << index;
            event->settings.prices[index] = (uint32_t)price;
        }
        valid = valid && event->fields;
    }
    cJSON_Delete(root);
    return valid ? 0 : rejected("invalid merchant/amount/product fields");
}

int soundbox_event_apply(struct soundbox_store *store, const struct soundbox_event *event)
{
    struct soundbox_settings next;
    unsigned i;
    if (!store || !store->ready || !event)
        return rejected("store unavailable");
    next = store->record.settings;
    if (event->type == SB_EVENT_MERCHANT) {
        if (!event->fields || (event->fields & ~7U))
            return rejected("merchant field mask");
        if (event->fields & SB_MERCHANT_NAME)
            memcpy(next.merchant_name, event->settings.merchant_name, sizeof(next.merchant_name));
        if (event->fields & SB_MERCHANT_NAME_L2)
            memcpy(next.merchant_name_l2, event->settings.merchant_name_l2, sizeof(next.merchant_name_l2));
        if (event->fields & SB_MERCHANT_NMID)
            memcpy(next.merchant_nmid, event->settings.merchant_nmid, sizeof(next.merchant_nmid));
    } else if (event->type == SB_EVENT_PRICES) {
        if (!event->fields || (event->fields & ~((1U << SOUNDBOX_PRICE_COUNT) - 1U)))
            return rejected("price field mask");
        for (i = 0; i < SOUNDBOX_PRICE_COUNT; ++i) {
            if (event->fields & (1U << i))
                next.prices[i] = event->settings.prices[i];
        }
    } else {
        return rejected("payment accounting requires an attempt snapshot");
    }
    return soundbox_store_settings(store, &next);
}
