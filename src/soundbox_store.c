#include "soundbox_store.h"

#include <appfile.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef SIM_BUILD
#include <syscall.h>
#endif

#define LOGGER_MODULE_NAME "soundbox-store"
#include "libs/logger/logger.h"

#define RECORD_MAX 2048U

static const char *const paths[] = {"data/setting.ini", "data/dtotal.ini"};

enum field_type { FIELD_TEXT, FIELD_U32, FIELD_U64 };
struct field {
    const char *prefix;
    size_t offset;
    size_t size;
    enum field_type type;
};

#define FIELD(prefix, member, type) \
    {prefix, offsetof(struct soundbox_record, member), \
     sizeof(((struct soundbox_record *)0)->member), type}
static const struct field fields[] = {
    FIELD("[meta]\nversion=1\nsequence=", sequence, FIELD_U64),
    FIELD("merchant_generation=", merchant_generation, FIELD_U64),
    FIELD("next_attempt=", next_attempt, FIELD_U64),
    FIELD("last_accounted=", last_accounted, FIELD_U64),
    FIELD("[set]\nmerchant_name=", settings.merchant_name, FIELD_TEXT),
    FIELD("merchant_name_l2=", settings.merchant_name_l2, FIELD_TEXT),
    FIELD("merchant_nmid=", settings.merchant_nmid, FIELD_TEXT),
    FIELD("menu_price_motor=", settings.prices[SB_MOTOR], FIELD_U32),
    FIELD("menu_price_mobil=", settings.prices[SB_MOBIL], FIELD_U32),
    FIELD("menu_price_sepeda=", settings.prices[SB_SEPEDA], FIELD_U32),
    FIELD("menu_price_elf=", settings.prices[SB_ELF], FIELD_U32),
    FIELD("menu_price_bus=", settings.prices[SB_BUS], FIELD_U32),
    FIELD("[dtotal]\ndate=", date, FIELD_U32),
    FIELD("count=", count, FIELD_U64),
    FIELD("total=", total, FIELD_U64),
    FIELD("[attempt]\nid=", attempt.id, FIELD_U64),
    FIELD("merchant_generation=", attempt.merchant_generation, FIELD_U64),
    FIELD("date=", attempt.date, FIELD_U32),
    FIELD("nmid=", attempt.nmid, FIELD_TEXT),
    FIELD("pending=", pending, FIELD_U32),
    FIELD("reconciliation=", reconciliation, FIELD_U32),
    FIELD("reconciliation_amount=", reconciliation_amount, FIELD_U64)
};
#undef FIELD

static int failed(int result, const char *operation)
{
    logger_error("Soundbox storage %s failed (%d, errno=%d)\n", operation, result, errno);
    return result;
}

static bool safe_text(const char *text, size_t capacity)
{
    size_t i;
    for (i = 0; i < capacity; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (!c)
            return true;
        if (c < 32 || c == 127)
            return false;
    }
    return false;
}

bool soundbox_settings_valid(const struct soundbox_settings *settings)
{
    size_t i;
    if (!settings || !safe_text(settings->merchant_name, sizeof(settings->merchant_name)) ||
        !safe_text(settings->merchant_name_l2, sizeof(settings->merchant_name_l2)) ||
        !safe_text(settings->merchant_nmid, sizeof(settings->merchant_nmid)))
        return false;
    for (i = 0; i < SOUNDBOX_PRICE_COUNT; ++i) {
        if (settings->prices[i] < 1000 || settings->prices[i] > 10000000)
            return false;
    }
    return true;
}

static bool leap(unsigned year)
{
    return year % 4U == 0 && (year % 100U != 0 || year % 400U == 0);
}

uint32_t soundbox_business_date(int64_t utc_seconds)
{
    static const unsigned month_days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    unsigned year = 1970, month = 0;
    int64_t days;
    if (utc_seconds < INT64_C(1577836800) || utc_seconds > INT64_C(4102419599))
        return 0;
    days = (utc_seconds + 7 * 3600) / 86400;
    while (days >= (leap(year) ? 366 : 365)) {
        days -= leap(year) ? 366 : 365;
        ++year;
    }
    for (;;) {
        unsigned count = month_days[month] + (month == 1 && leap(year) ? 1U : 0U);
        if (days < count)
            break;
        days -= count;
        ++month;
    }
    return year * 10000U + (month + 1U) * 100U + (unsigned)days + 1U;
}

static bool valid_date(uint32_t date)
{
    static const unsigned month_days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    unsigned year = date / 10000U, month = date / 100U % 100U, day = date % 100U;
    return !date || (year >= 2020 && year <= 2099 && month >= 1 && month <= 12 &&
        day >= 1 && day <= month_days[month - 1] + (month == 2 && leap(year) ? 1U : 0U));
}

static uint32_t checksum(const char *data, size_t length)
{
    uint32_t crc = UINT32_MAX;
    size_t i;
    unsigned bit;
    for (i = 0; i < length; ++i) {
        crc ^= (unsigned char)data[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? UINT32_C(0xedb88320) : 0);
    }
    return ~crc;
}

static bool record_valid(const struct soundbox_record *record)
{
    return record->sequence && record->merchant_generation &&
        soundbox_settings_valid(&record->settings) && valid_date(record->date) &&
        valid_date(record->attempt.date) &&
        safe_text(record->attempt.nmid, sizeof(record->attempt.nmid)) &&
        record->pending <= 1 && record->reconciliation <= 1 &&
        !(record->pending && record->reconciliation) &&
        record->attempt.id == record->next_attempt &&
        record->last_accounted <= record->next_attempt &&
        record->attempt.merchant_generation <= record->merchant_generation &&
        (record->date || (!record->count && !record->total)) &&
        (record->count || !record->total) &&
        (record->reconciliation || !record->reconciliation_amount) &&
        (!record->attempt.id || (record->attempt.date &&
         record->attempt.merchant_generation && record->attempt.nmid[0])) &&
        (!record->pending || record->attempt.id > record->last_accounted) &&
        (!record->reconciliation || (record->attempt.id &&
         record->attempt.id == record->last_accounted));
}

static int encode(const struct soundbox_record *record, char out[RECORD_MAX])
{
    size_t i, used = 0;
    int count;
    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        const struct field *field = &fields[i];
        const void *value = (const unsigned char *)record + field->offset;
        if (field->type == FIELD_TEXT) {
            count = snprintf(out + used, RECORD_MAX - used, "%s%s\n",
                field->prefix, (const char *)value);
        } else {
            uint64_t number = 0;
            if (field->type == FIELD_U32) {
                uint32_t small;
                memcpy(&small, value, sizeof(small));
                number = small;
            } else {
                memcpy(&number, value, sizeof(number));
            }
            count = snprintf(out + used, RECORD_MAX - used, "%s%llu\n",
                field->prefix, (unsigned long long)number);
        }
        if (count < 0 || (size_t)count >= RECORD_MAX - used)
            return -1;
        used += (size_t)count;
    }
    count = snprintf(out + used, RECORD_MAX - used, "checksum=%08lx\n",
        (unsigned long)checksum(out, used));
    if (count < 0 || (size_t)count >= RECORD_MAX - used)
        return -1;
    return (int)(used + (size_t)count);
}

static bool decode(char *data, size_t length, struct soundbox_record *record)
{
    char *cursor = data;
    char canonical[RECORD_MAX];
    size_t i;
    memset(record, 0, sizeof(*record));
    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        const struct field *field = &fields[i];
        char *end;
        size_t size = strlen(field->prefix);
        void *value = (unsigned char *)record + field->offset;
        if (strncmp(cursor, field->prefix, size) != 0)
            return false;
        cursor += size;
        end = strchr(cursor, '\n');
        if (!end)
            return false;
        if (field->type == FIELD_TEXT) {
            size = (size_t)(end - cursor);
            if (size >= field->size)
                return false;
            memcpy(value, cursor, size);
        } else {
            uint64_t number = 0;
            char *digit;
            if (end == cursor)
                return false;
            for (digit = cursor; digit < end; ++digit) {
                unsigned d = (unsigned char)*digit - '0';
                if (d > 9 || number > (UINT64_MAX - d) / 10U)
                    return false;
                number = number * 10U + d;
            }
            if (field->type == FIELD_U32) {
                uint32_t small;
                if (number > UINT32_MAX)
                    return false;
                small = (uint32_t)number;
                memcpy(value, &small, sizeof(small));
            } else {
                memcpy(value, &number, sizeof(number));
            }
        }
        cursor = end + 1;
    }
    if (!record_valid(record))
        return false;
    return encode(record, canonical) == (int)length &&
        memcmp(canonical, data, length) == 0;
}

static bool slot_missing(int slot)
{
    char path[sizeof(CFG_APP_PATH) + sizeof("data/setting.ini")];
    int result, error;
    snprintf(path, sizeof(path), "%s%s", CFG_APP_PATH, paths[slot]);
    errno = 0;
#ifdef SIM_BUILD
    result = app_file_exists(paths[slot]);
#else
    /* The libc access wrapper replaces syscall errno with -result. */
    result = syscall_access(path, F_OK);
#endif
    error = errno;
    logger_info("Soundbox slot existence: %s (raw result=%d, errno=%d)\n",
        path, result, error);
    return result < 0 && (result == -ENOENT || error == ENOENT);
}

/* 1 means missing, 2 means torn/corrupt, negative means unreadable. */
static int read_slot(int slot, struct soundbox_record *record)
{
    char data[RECORD_MAX];
    size_t used = 0;
    int fd, count;
    errno = 0;
    fd = app_file_open(paths[slot], O_RDONLY);
    if (fd < 0) {
        int error = errno;
        if (error == ENOENT || slot_missing(slot))
            return 1;
        logger_error("Soundbox slot open failed: %s%s (result=%d, errno=%d); not assuming missing file\n",
            CFG_APP_PATH, paths[slot], fd, error);
        errno = error;
        return SB_STORE_IO;
    }
    while (used < sizeof(data) - 1U) {
        count = app_file_read(fd, data + used, (int)(sizeof(data) - 1U - used));
        if (count < 0 || (size_t)count > sizeof(data) - 1U - used) {
            int error = errno;
            logger_error("Soundbox slot read failed: %s%s (fd=%d, offset=%u, result=%d, errno=%d)\n",
                CFG_APP_PATH, paths[slot], fd, (unsigned)used, count, error);
            app_file_close(fd);
            errno = error;
            return SB_STORE_IO;
        }
        if (!count)
            break;
        used += (size_t)count;
    }
    app_file_close(fd);
    data[used] = '\0';
    if (!used || used == sizeof(data) - 1U || memchr(data, '\0', used) ||
        !decode(data, used, record))
        return 2;
    return 0;
}

static int commit(struct soundbox_store *store, struct soundbox_record *next)
{
    char data[RECORD_MAX];
    struct soundbox_record verified;
    int slot = store->slot == 0 ? 1 : 0;
    int fd, length, written = 0;
    if (store->record.sequence == UINT64_MAX)
        return failed(SB_STORE_OVERFLOW, "sequence");
    next->sequence = store->record.sequence + 1U;
    if (!record_valid(next) || (length = encode(next, data)) < 0)
        return failed(SB_STORE_INVALID, "encode");
    fd = app_file_open(paths[slot], O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0)
        return failed(SB_STORE_IO, "open-write");
    while (written < length) {
        int count = app_file_write(fd, data + written, length - written);
        if (count <= 0 || count > length - written) {
            app_file_close(fd);
            return failed(SB_STORE_IO, "write");
        }
        written += count;
    }
    app_file_close(fd);
    if (read_slot(slot, &verified) != 0 || verified.sequence != next->sequence) {
        store->ready = false;
        return failed(SB_STORE_IO, "readback; reload required");
    }
    {
        char check[RECORD_MAX];
        if (encode(&verified, check) != length || memcmp(data, check, (size_t)length) != 0) {
            store->ready = false;
            return failed(SB_STORE_IO, "readback mismatch");
        }
    }
    store->record = *next;
    store->slot = slot;
    return SB_STORE_OK;
}

int soundbox_store_open(struct soundbox_store *store, int64_t utc_seconds)
{
    struct soundbox_record records[2];
    DIR *dir;
    int status[2], result;
    if (!store)
        return failed(SB_STORE_INVALID, "context");
    memset(store, 0, sizeof(*store));
    store->slot = -1;
    /* Opening avoids the SDK's boolean checkappdir/documentation mismatch. */
    dir = app_file_opendir("data");
    if (!dir) {
        if (app_file_creatappdir("data") != 0)
            return failed(SB_STORE_IO, "create data directory");
        dir = app_file_opendir("data");
        if (!dir)
            return failed(SB_STORE_IO, "open data directory");
        logger_info("Created soundbox data directory\n");
    }
    app_file_closedir(dir);
    status[0] = read_slot(0, &records[0]);
    status[1] = read_slot(1, &records[1]);
    logger_info("Soundbox startup slots: setting=%d dtotal=%d (0=valid, 1=missing, 2=corrupt, negative=I/O)\n",
        status[0], status[1]);
    if (status[0] < 0 || status[1] < 0)
        return failed(SB_STORE_IO, "read");
    if (!status[0] || !status[1]) {
        store->slot = status[0] ? 1 : status[1] ? 0 :
            records[1].sequence > records[0].sequence ? 1 : 0;
        store->record = records[store->slot];
        if (!status[0] && !status[1] && records[0].sequence == records[1].sequence) {
            char a[RECORD_MAX], b[RECORD_MAX];
            int n = encode(&records[0], a);
            if (encode(&records[1], b) != n || memcmp(a, b, (size_t)n))
                return failed(SB_STORE_CORRUPT, "conflicting generations");
        }
        store->ready = true;
        result = soundbox_store_rollover(store, utc_seconds);
        if (result < 0)
            return result;
        if (status[0] == 2 || status[1] == 2) {
            logger_warn("Recovered soundbox record from intact slot\n");
            return SB_STORE_RECOVERED;
        }
        return SB_STORE_OK;
    }
    if (status[0] != 1 || status[1] != 1)
        return failed(SB_STORE_CORRUPT, "both records invalid; not first boot");
    store->record.merchant_generation = 1;
    store->record.date = soundbox_business_date(utc_seconds);
    memcpy(store->record.settings.prices,
        (const uint32_t[]){2000,5000,1000,10000,15000}, sizeof(store->record.settings.prices));
    store->ready = true;
    {
        struct soundbox_record first = store->record;
        result = commit(store, &first);
    }
    if (result < 0)
        store->ready = false;
    else
        logger_info("Initialized first-run soundbox settings and totals\n");
    return result;
}

int soundbox_store_rollover(struct soundbox_store *store, int64_t utc_seconds)
{
    uint32_t date = soundbox_business_date(utc_seconds);
    struct soundbox_record next;
    if (!store || !store->ready)
        return failed(SB_STORE_INVALID, "not ready");
    if (!date) {
        logger_warn("Soundbox clock invalid; preserving daily totals\n");
        return SB_STORE_OK;
    }
    if (date <= store->record.date)
        return SB_STORE_OK;
    next = store->record;
    next.date = date;
    next.count = next.total = 0;
    return commit(store, &next);
}

int soundbox_store_settings(struct soundbox_store *store,
    const struct soundbox_settings *settings)
{
    struct soundbox_record next;
    if (!store || !store->ready || !soundbox_settings_valid(settings))
        return failed(SB_STORE_INVALID, "settings");
    next = store->record;
    if (strcmp(settings->merchant_nmid, next.settings.merchant_nmid) != 0) {
        if (next.merchant_generation == UINT64_MAX)
            return failed(SB_STORE_OVERFLOW, "merchant generation");
        ++next.merchant_generation;
        next.count = next.total = 0;
    }
    next.settings = *settings;
    if (memcmp(&next.settings, &store->record.settings, sizeof(next.settings)) == 0)
        return SB_STORE_OK;
    return commit(store, &next);
}

int soundbox_store_begin(struct soundbox_store *store, struct soundbox_attempt *attempt)
{
    struct soundbox_record next;
    int result;
    if (!store || !store->ready || !attempt || !store->record.date ||
        !store->record.settings.merchant_nmid[0])
        return failed(SB_STORE_INVALID, "begin");
    if (store->record.pending || store->record.reconciliation)
        return failed(SB_STORE_BUSY, "unresolved attempt");
    if (store->record.next_attempt == UINT64_MAX)
        return failed(SB_STORE_OVERFLOW, "attempt sequence");
    next = store->record;
    next.attempt.id = ++next.next_attempt;
    next.attempt.merchant_generation = next.merchant_generation;
    next.attempt.date = next.date;
    snprintf(next.attempt.nmid, sizeof(next.attempt.nmid), "%s", next.settings.merchant_nmid);
    next.pending = 1;
    result = commit(store, &next);
    if (result == SB_STORE_OK)
        *attempt = next.attempt;
    return result;
}

int soundbox_store_cancel(struct soundbox_store *store, uint64_t attempt_id)
{
    struct soundbox_record next;
    if (!store || !store->ready || !store->record.pending ||
        store->record.attempt.id != attempt_id)
        return failed(SB_STORE_INVALID, "cancel");
    next = store->record;
    next.pending = 0;
    return commit(store, &next);
}

int soundbox_store_account(struct soundbox_store *store,
    const struct soundbox_attempt *attempt, uint64_t amount, int64_t utc_seconds)
{
    struct soundbox_record next;
    uint32_t date = soundbox_business_date(utc_seconds);
    int result;
    bool overflow;
    if (!store || !store->ready || !attempt || !attempt->id)
        return failed(SB_STORE_INVALID, "account");
    if (attempt->id == store->record.last_accounted)
        return SB_STORE_DUPLICATE;
    if (!store->record.pending || attempt->id != store->record.attempt.id ||
        attempt->merchant_generation != store->record.attempt.merchant_generation ||
        attempt->date != store->record.attempt.date ||
        !safe_text(attempt->nmid, sizeof(attempt->nmid)) ||
        strcmp(attempt->nmid, store->record.attempt.nmid) != 0)
        return failed(SB_STORE_INVALID, "stale attempt");
    next = store->record;
    if (date > next.date) {
        next.date = date;
        next.count = next.total = 0;
    }
    next.pending = 0;
    next.last_accounted = attempt->id;
    overflow = next.count == UINT64_MAX || amount > UINT64_MAX - next.total;
    if (!date || date < next.date || attempt->date != next.date ||
        attempt->merchant_generation != next.merchant_generation || overflow) {
        /* shortcut: one retained reconciliation blocks new payments until a resolution workflow is added. */
        next.reconciliation = 1;
        next.reconciliation_amount = amount;
        result = commit(store, &next);
        if (result < 0)
            return result;
        logger_error("Acknowledged payment retained for reconciliation; totals unchanged (overflow=%d)\n",
            overflow);
        return overflow ? SB_STORE_OVERFLOW : SB_STORE_RECONCILE;
    }
    ++next.count;
    next.total += amount;
    return commit(store, &next);
}
