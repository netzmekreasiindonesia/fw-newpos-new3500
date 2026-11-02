#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "soundbox_event.h"

struct memory_file {
    unsigned char data[2048];
    int exists;
    int length;
    int position;
};
static struct memory_file files[2];
static int write_budget = -1;
static int read_error = -1;
static int directory_error;
static int directory_exists;
static int access_error;
static int access_negative_errno;
static int open_error = -1;
static int missing_errno = ENOENT;
static int directory_token;

int logger_level_printf(int level, int config, const char *module,
    const char *file, int line, const char *format, ...)
{
    (void)level; (void)config; (void)module; (void)file;
    (void)line; (void)format;
    return 0;
}

DIR *app_file_opendir(const char *path)
{
    assert(strcmp(path, "data") == 0);
    if (directory_error || !directory_exists) {
        errno = EPERM;
        return NULL;
    }
    return (DIR *)&directory_token;
}
int app_file_exists(const char *path)
{
    int slot = strcmp(path, "data/setting.ini") == 0 ? 0 : 1;
    assert(slot == 0 || strcmp(path, "data/dtotal.ini") == 0);
    if (access_error || !files[slot].exists) {
        int error = access_error ? access_error : ENOENT;
        if (access_negative_errno)
            return -error;
        errno = error;
        return -1;
    }
    return 0;
}
void app_file_closedir(DIR *dir)
{
    assert(dir == (DIR *)&directory_token);
}
int app_file_creatappdir(const char *path)
{
    assert(strcmp(path, "data") == 0);
    if (directory_error || directory_exists) {
        errno = EPERM;
        return -1;
    }
    directory_exists = 1;
    return 0;
}
int app_file_open(const char *path, int flags)
{
    int slot = strcmp(path, "data/setting.ini") == 0 ? 0 : 1;
    assert(slot == 0 || strcmp(path, "data/dtotal.ini") == 0);
    assert(directory_exists);
    if (slot == open_error) {
        errno = EPERM;
        return -1;
    }
    if (!files[slot].exists && !(flags & O_CREAT)) {
        errno = missing_errno;
        return -1;
    }
    files[slot].exists = 1;
    files[slot].position = 0;
    if (flags & O_TRUNC)
        files[slot].length = 0;
    return slot;
}
int app_file_close(int fd) { (void)fd; return 0; }
int app_file_read(int fd, void *buffer, int length)
{
    struct memory_file *file = &files[fd];
    if (fd == read_error) {
        errno = EIO;
        return -1;
    }
    if (length > 5)
        length = 5;
    if (length > file->length - file->position)
        length = file->length - file->position;
    memcpy(buffer, file->data + file->position, (size_t)length);
    file->position += length;
    return length;
}
int app_file_write(int fd, const void *buffer, int length)
{
    struct memory_file *file = &files[fd];
    if (!write_budget) {
        errno = EIO;
        return -1;
    }
    if (length > 7)
        length = 7;
    if (write_budget > 0 && length > write_budget)
        length = write_budget;
    assert(file->position + length <= (int)sizeof(file->data));
    memcpy(file->data + file->position, buffer, (size_t)length);
    file->position += length;
    file->length = file->position;
    if (write_budget > 0)
        write_budget -= length;
    return length;
}

static void reset(void)
{
    memset(files, 0, sizeof(files));
    write_budget = -1;
    read_error = -1;
    directory_error = 0;
    directory_exists = 0;
    access_error = 0;
    access_negative_errno = 0;
    open_error = -1;
    missing_errno = ENOENT;
}

static void merchant(struct soundbox_store *store, const char *nmid)
{
    struct soundbox_settings settings = store->record.settings;
    snprintf(settings.merchant_name, sizeof(settings.merchant_name), "Parkir Uji");
    snprintf(settings.merchant_nmid, sizeof(settings.merchant_nmid), "%s", nmid);
    assert(soundbox_store_settings(store, &settings) == SB_STORE_OK);
}

static void test_sdk_filesystem(void)
{
    const int64_t day = INT64_C(1577836800);
    struct soundbox_store store, reloaded;
    struct memory_file baseline[2];
    reset();
    missing_errno = EPERM;
    assert(soundbox_store_open(&store, day) == SB_STORE_OK);
    assert(directory_exists && store.ready);
    assert(soundbox_store_open(&reloaded, day) == SB_STORE_OK);
    merchant(&store, "SDK");
    memcpy(baseline, files, sizeof(files));
    open_error = store.slot;
    assert(soundbox_store_open(&reloaded, day) == SB_STORE_IO);
    assert(!reloaded.ready && memcmp(baseline, files, sizeof(files)) == 0);
    open_error = -1;
    assert(soundbox_store_open(&reloaded, day) == SB_STORE_OK);
    assert(strcmp(reloaded.record.settings.merchant_nmid, "SDK") == 0);

    reset();
    missing_errno = EPERM;
    access_error = EIO;
    assert(soundbox_store_open(&store, day) == SB_STORE_IO);
    assert(!store.ready && !files[0].exists && !files[1].exists);
    access_error = 0;
    assert(soundbox_store_open(&store, day) == SB_STORE_OK);
    reset();
    missing_errno = EPERM;
    access_error = EACCES;
    assert(soundbox_store_open(&store, day) == SB_STORE_IO);
    assert(!store.ready && !files[0].exists && !files[1].exists);
    access_error = EPERM;
    assert(soundbox_store_open(&store, day) == SB_STORE_IO);
    assert(!store.ready && !files[0].exists && !files[1].exists);
    access_error = 0;
    access_negative_errno = 1;
    assert(soundbox_store_open(&store, day) == SB_STORE_OK);
}

static int parse(const char *json, struct soundbox_event *event)
{
    return soundbox_event_parse((const unsigned char *)json, strlen(json), event);
}

static void test_accounting(void)
{
    const int64_t day = INT64_C(1577836800);
    struct soundbox_store store, reloaded;
    struct soundbox_attempt attempt, old;
    struct soundbox_settings settings;
    reset();
    assert(soundbox_business_date(0) == 0);
    assert(soundbox_business_date(INT64_MAX) == 0);
    assert(soundbox_business_date(1709139599) == 20240228);
    assert(soundbox_business_date(1709139600) == 20240229);
    assert(soundbox_store_open(&store, day) == SB_STORE_OK);
    assert(store.record.date == 20200101 && store.record.total == 0);
    assert(soundbox_store_begin(&store, &attempt) == SB_STORE_INVALID);
    merchant(&store, "A");
    assert(soundbox_store_begin(&store, &attempt) == SB_STORE_OK);
    assert(soundbox_store_begin(&store, &old) == SB_STORE_BUSY);
    assert(soundbox_store_account(&store, &attempt, UINT64_C(4294967296), day) == SB_STORE_OK);
    assert(store.record.count == 1 && store.record.total == UINT64_C(4294967296));
    assert(soundbox_store_account(&store, &attempt, 1, day) == SB_STORE_DUPLICATE);
    assert(soundbox_store_open(&reloaded, day) == SB_STORE_OK);
    assert(soundbox_store_account(&reloaded, &attempt, 1, day) == SB_STORE_DUPLICATE);
    assert(reloaded.record.total == UINT64_C(4294967296));
    settings = store.record.settings;
    strcpy(settings.merchant_name, "Nama baru");
    settings.prices[SB_MOTOR] = 3000;
    assert(soundbox_store_settings(&store, &settings) == SB_STORE_OK);
    assert(store.record.count == 1);
    merchant(&store, "A");
    assert(store.record.count == 1);
    assert(soundbox_store_rollover(&store, 0) == SB_STORE_OK);
    assert(soundbox_store_rollover(&store, day - 86400) == SB_STORE_OK);
    assert(store.record.count == 1);
    assert(soundbox_store_rollover(&store, day + 17 * 3600 - 1) == SB_STORE_OK);
    assert(store.record.date == 20200101 && store.record.count == 1);
    assert(soundbox_store_rollover(&store, day + 17 * 3600) == SB_STORE_OK);
    assert(store.record.date == 20200102 && store.record.total == 0);
    assert(soundbox_store_begin(&store, &attempt) == SB_STORE_OK);
    merchant(&store, "B");
    assert(soundbox_store_account(&store, &attempt, 2000, day + 86400) == SB_STORE_RECONCILE);
    assert(store.record.total == 0 && store.record.reconciliation_amount == 2000);
    assert(soundbox_store_open(&reloaded, day + 86400) == SB_STORE_OK);
    assert(reloaded.record.reconciliation && strcmp(reloaded.record.attempt.nmid, "A") == 0);
    assert(soundbox_store_begin(&reloaded, &attempt) == SB_STORE_BUSY);

    reset();
    assert(soundbox_store_open(&store, day) == SB_STORE_OK);
    merchant(&store, "A");
    assert(soundbox_store_begin(&store, &attempt) == SB_STORE_OK);
    assert(soundbox_store_account(&store, &attempt, 0, day) == SB_STORE_OK);
    assert(store.record.count == 1 && store.record.total == 0);
    assert(soundbox_store_begin(&store, &attempt) == SB_STORE_OK);
    assert(soundbox_store_account(&store, &attempt, UINT64_MAX, day) == SB_STORE_OK);
    assert(store.record.total == UINT64_MAX);
    assert(soundbox_store_begin(&store, &attempt) == SB_STORE_OK);
    assert(soundbox_store_account(&store, &attempt, 1, day) == SB_STORE_OVERFLOW);
    assert(store.record.total == UINT64_MAX && store.record.reconciliation_amount == 1);
    assert(store.record.reconciliation && !store.record.pending);
    assert(soundbox_store_open(&reloaded, day) == 0 && reloaded.record.reconciliation);
    reset();
    assert(soundbox_store_open(&store, day) == 0);
    merchant(&store, "A");
    assert(soundbox_store_begin(&store, &attempt) == 0);
    assert(soundbox_store_cancel(&store, attempt.id) == SB_STORE_OK);
    assert(soundbox_store_account(&store, &attempt, 1, day) == SB_STORE_INVALID);
    assert(soundbox_store_begin(&store, &old) == SB_STORE_OK);
    assert(old.id > attempt.id);
    assert(soundbox_store_account(&store, &old, 1, day + 86400) == SB_STORE_RECONCILE);
    assert(store.record.date == 20200102 && store.record.total == 0);
}

static void test_broadcasts(void)
{
    struct soundbox_store store;
    struct soundbox_event event;
    const int64_t day = INT64_C(1577836800);
    reset();
    assert(soundbox_store_open(&store, day) == 0);
    assert(parse("{\"broadcast_type\":\"4\",\"merchant_name\":\"Parkir\","
        "\"merchant_name_l2\":\"Utara\",\"merchant_nmid\":\"M1\"}", &event) == 0);
    assert(soundbox_event_apply(&store, &event) == 0);
    assert(strcmp(store.record.settings.merchant_nmid, "M1") == 0);
    assert(strcmp(store.record.settings.merchant_name_l2, "Utara") == 0);
    assert(parse("{\"broadcast_type\":6,\"products\":["
        "{\"code\":\"P-MOTOR-01\",\"price\":\"6000\"},{\"name\":\"Bus\",\"price\":7000},"
        "{\"name\":\"Tidak dikenal\",\"price\":999}]}", &event) == 0);
    assert(soundbox_event_apply(&store, &event) == 0);
    assert(store.record.settings.prices[SB_MOTOR] == 6000);
    assert(store.record.settings.prices[SB_BUS] == 7000);
    assert(store.record.settings.prices[SB_MOBIL] == 5000);
    assert(parse("{\"broadcast_type\":6,\"products\":[{\"name\":\"Motor\",\"price\":1}]}", &event) < 0);
    assert(parse("{\"broadcast_type\":6,\"products\":[{\"name\":\"Motor\",\"price\":3000},"
        "{\"name\":\"Motor\",\"price\":4000}]}", &event) < 0);
    assert(parse("{\"broadcast_type\":4,\"merchant_name\":\"bad\\nname\"}", &event) < 0);
    assert(parse("{\"broadcast_type\":4,\"merchant_nmid\":false}", &event) < 0);
    assert(parse("{\"broadcast_type\":4}", &event) < 0);
    assert(parse("{\"biz_type\":1}", &event) < 0);
    assert(parse("{\"broadcast_type\":1,\"money\":\"-2\"}", &event) < 0);
    assert(parse("{\"broadcast_type\":1,\"money\":\"18446744073709551616\"}", &event) < 0);
    assert(parse("{\"broadcast_type\":1,\"money\":\"2000\"}", &event) == 0 && event.money == 2000);
    assert(soundbox_event_apply(&store, &event) < 0);
    assert(parse("{\"broadcast_type\":\"1\",\"money\":2000,"
        "\"transaction_id\":\"T001\",\"event_type\":\"qr\"}", &event) == 0);
    assert(!strcmp(event.transaction_id, "T001") && !strcmp(event.event_type, "qr"));
    assert(parse("{\"broadcast_type\":1,\"transaction_id\":\"T001\"}", &event) == 0);
    assert(!event.transaction_id[0] && !event.event_type[0]);
    assert(parse("{\"broadcast_type\":1,\"transaction_id\":42,\"event_type\":\"qr\"}",
        &event) == 0);
    assert(!event.transaction_id[0] && !event.event_type[0]);
    assert(parse("{\"broadcast_type\":1,\"transaction_id\":\"bad\\u001b\","
        "\"event_type\":\"qr\"}", &event) == 0);
    assert(!event.transaction_id[0] && !event.event_type[0]);
    {
        char nested[128];
        unsigned depth;
        size_t prefix;
        for (depth = 15; depth <= 16; ++depth) {
            strcpy(nested, "{\"broadcast_type\":1,\"data\":");
            prefix = strlen(nested);
            memset(nested + prefix, '[', depth);
            nested[prefix + depth] = '0';
            memset(nested + prefix + depth + 1U, ']', depth);
            strcpy(nested + prefix + 2U * depth + 1U, "}");
            assert((parse(nested, &event) == 0) == (depth == 15));
        }
    }
}

static void test_torn_writes(void)
{
    const int64_t day = INT64_C(1577836800);
    struct soundbox_store store, reloaded;
    struct soundbox_settings settings;
    struct memory_file baseline[2];
    int length, cut, slot;
    reset();
    assert(soundbox_store_open(&store, day) == 0);
    merchant(&store, "OLD");
    settings = store.record.settings;
    strcpy(settings.merchant_nmid, "NEW");
    memcpy(baseline, files, sizeof(files));
    assert(soundbox_store_settings(&store, &settings) == 0);
    length = files[store.slot].length;
    for (cut = 0; cut < length; ++cut) {
        memcpy(files, baseline, sizeof(files));
        write_budget = -1;
        assert(soundbox_store_open(&store, day) == 0);
        write_budget = cut;
        assert(soundbox_store_settings(&store, &settings) == SB_STORE_IO);
        assert(strcmp(store.record.settings.merchant_nmid, "OLD") == 0);
        write_budget = -1;
        assert(soundbox_store_open(&reloaded, day) == SB_STORE_RECOVERED);
        assert(strcmp(reloaded.record.settings.merchant_nmid, "OLD") == 0);
    }
    printf("Recovered each of %d interrupted INI write boundaries\n", length);
    memcpy(files, baseline, sizeof(files));
    assert(soundbox_store_open(&store, day) == 0);
    slot = store.slot;
    read_error = 1 - slot;
    assert(soundbox_store_open(&reloaded, day) == SB_STORE_IO);
    read_error = -1;
    files[0].data[0] = files[1].data[0] = '!';
    assert(soundbox_store_open(&reloaded, day) == SB_STORE_CORRUPT);
    assert(!reloaded.ready);
    reset();
    directory_error = 1;
    assert(soundbox_store_open(&store, day) == SB_STORE_IO);
}

static void test_accounting_recovery(void)
{
    const int64_t day = INT64_C(1577836800);
    struct soundbox_store store, reloaded;
    struct soundbox_attempt attempt;
    struct memory_file baseline[2];
    int length, cut;
    reset();
    assert(soundbox_store_open(&store, day) == 0);
    merchant(&store, "A");
    assert(soundbox_store_begin(&store, &attempt) == 0);
    memcpy(baseline, files, sizeof(files));
    assert(soundbox_store_account(&store, &attempt, 2000, day) == 0);
    length = files[store.slot].length;
    for (cut = 0; cut < length; ++cut) {
        memcpy(files, baseline, sizeof(files));
        write_budget = -1;
        assert(soundbox_store_open(&store, day) == 0);
        write_budget = cut;
        assert(soundbox_store_account(&store, &attempt, 2000, day) == SB_STORE_IO);
        write_budget = -1;
        assert(soundbox_store_open(&reloaded, day) == SB_STORE_RECOVERED);
        assert(reloaded.record.pending && reloaded.record.count == 0);
        assert(soundbox_store_account(&reloaded, &attempt, 2000, day) == 0);
        assert(soundbox_store_account(&reloaded, &attempt, 2000, day) == SB_STORE_DUPLICATE);
        assert(reloaded.record.count == 1 && reloaded.record.total == 2000);
    }
    memcpy(files, baseline, sizeof(files));
    assert(soundbox_store_open(&store, day) == 0);
    read_error = 1 - store.slot;
    assert(soundbox_store_account(&store, &attempt, 2000, day) == SB_STORE_IO);
    assert(!store.ready);
    read_error = -1;
    assert(soundbox_store_open(&reloaded, day) == 0);
    assert(soundbox_store_account(&reloaded, &attempt, 2000, day) == SB_STORE_DUPLICATE);
    assert(reloaded.record.total == 2000);
    printf("Recovered each of %d interrupted accounting write boundaries\n", length);
}

int main(void)
{
    test_sdk_filesystem();
    test_accounting();
    test_broadcasts();
    test_torn_writes();
    test_accounting_recovery();
    puts("Soundbox persistence/broadcast tests passed.");
    return 0;
}
