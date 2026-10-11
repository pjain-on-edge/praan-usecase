#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "freertos/semphr.h"
#include "storage.h"
#include "storage_ring.h"
#include "record.h"
#include "app_events.h"
#include "app_nvs.h"
#include "health.h"

#define STORAGE_STACK_SIZE      4096
#define STORAGE_PRIO            8
#define STORAGE_CORE            1

#define SD_BASE                 "/sdcard"
#define SD_MAX_FILES            3       /* segment writer, segment reader, meta slot */
#define SD_ALLOC_UNIT           (16 * 1024)
#define SD_MIN_FREE_PCT         5
#define SD_RETRY_US             (30 * 1000000LL)
#define SD_ABANDON_US           (5 * 60 * 1000000LL)
#define FALLBACK_RECORDS        300
#define MAX_DIRS                64
#define PATH_LEN                40

#define NVS_KEY_SEG_FLOOR       "seg_floor"

static const char *TAG = "storage";

static storage_config_t s_cfg;
static StaticTask_t s_tcb;
static StackType_t s_stack[STORAGE_STACK_SIZE];
static TaskHandle_t s_task;
static QueueHandle_t s_queue;
static StaticSemaphore_t s_mtx_buf;
static SemaphoreHandle_t s_mtx;
static esp_pm_lock_handle_t s_pm_lock;

static sdmmc_card_t *s_card;
static bool s_mounted;
static int64_t s_last_mount_try_us;
static int64_t s_fault_since_us;
static bool s_card_abandoned;

static int s_wfd = -1;
static uint32_t s_wseg = UINT32_MAX;
static int s_rfd = -1;
static uint32_t s_rseg = UINT32_MAX;
static uint32_t s_rsize;

/* Pending block; its first record has sequence s_blk_seq0. */
static record_t s_blk[RING_BLOCK_RECORDS];
static uint32_t s_blk_seq0;
static uint16_t s_blk_n;

/* RAM fallback while the card is unusable; holds [s_fb_seq0, write_idx). */
static record_t s_fb[FALLBACK_RECORDS];
static uint32_t s_fb_seq0;
static uint16_t s_fb_head;
static uint16_t s_fb_n;

/* Records in [s_sd_lo, s_sd_hi) are on the card (minus padding and gaps). */
static uint32_t s_sd_lo;
static uint32_t s_sd_hi;
static uint32_t s_oldest_seg;
static uint32_t s_seg_floor;

static _Atomic uint32_t s_write_idx;
static _Atomic uint32_t s_read_idx;
static uint32_t s_meta_gen;
static uint8_t s_pad_sector[RING_BLOCK_BYTES];
/* All 0xFF: a sealed sample can never look like this, so it is a safe in-band
 * "power failing" marker on the record queue. */
static DRAM_ATTR record_t s_power_fail_marker;

static void lock(void)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_mtx);
}

static void pm_hold(bool on)
{
    if (s_pm_lock) {
        on ? esp_pm_lock_acquire(s_pm_lock) : esp_pm_lock_release(s_pm_lock);
    }
}

static bool card_present(void)
{
    return gpio_get_level(s_cfg.cd) == 0;
}

static void set_sd_bits(bool ok)
{
    if (ok) {
        xEventGroupClearBits(app_events(), EVT_FAULT_SD);
        xEventGroupSetBits(app_events(), EVT_SD_OK);
    } else {
        xEventGroupClearBits(app_events(), EVT_SD_OK);
        xEventGroupSetBits(app_events(), EVT_FAULT_SD);
        s_fault_since_us = esp_timer_get_time();
        s_card_abandoned = false;
    }
}

/* The NVS floor keeps sequence numbers monotonic across reboots without a card
 * (or with a different one), so (device_id, seq) stays unique for the backend. */
static void seg_floor_bump(uint32_t seq)
{
    uint32_t floor = ring_seg_of(seq) + 1;
    if (floor > s_seg_floor) {
        s_seg_floor = floor;
        esp_err_t err = app_nvs_set_u32(APP_NVS_NS_STORAGE, NVS_KEY_SEG_FLOOR, floor);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "seq floor not saved: %s", esp_err_to_name(err));
        }
    }
}

static void close_fd(int *fd)
{
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

/* ---- fallback ring ---- */

static record_t *fb_at(uint16_t i)
{
    return &s_fb[(s_fb_head + i) % FALLBACK_RECORDS];
}

static void fb_push_back(const record_t *r)
{
    if (s_fb_n == FALLBACK_RECORDS) {
        s_fb_head = (s_fb_head + 1) % FALLBACK_RECORDS;
        s_fb_n--;
        s_fb_seq0++;
        health_inc(HC_RECORDS_LOST);
    }
    record_t *slot = fb_at(s_fb_n++);
    *slot = *r;
    if (!(slot->flags & FLAG_SD_FALLBACK)) {
        slot->flags |= FLAG_SD_FALLBACK;
        record_seal(slot);
    }
}

/* Re-inserts older records ahead of the ring; if it is full, the oldest are lost. */
static void fb_push_front(const record_t *r)
{
    if (s_fb_n == FALLBACK_RECORDS) {
        health_inc(HC_RECORDS_LOST);
        return;
    }
    s_fb_head = (uint16_t)((s_fb_head + FALLBACK_RECORDS - 1) % FALLBACK_RECORDS);
    s_fb_n++;
    s_fb_seq0--;
    record_t *slot = fb_at(0);
    *slot = *r;
    if (!(slot->flags & FLAG_SD_FALLBACK)) {
        slot->flags |= FLAG_SD_FALLBACK;
        record_seal(slot);
    }
}

/* ---- card ---- */

static esp_err_t sd_mount(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = s_cfg.clk;
    slot.cmd = s_cfg.cmd;
    slot.d0 = s_cfg.d0;
    slot.d1 = s_cfg.d1;
    slot.d2 = s_cfg.d2;
    slot.d3 = s_cfg.d3;
    slot.cd = s_cfg.cd;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    const esp_vfs_fat_sdmmc_mount_config_t mc = {
        .format_if_mount_failed = false,
        .max_files = SD_MAX_FILES,
        .allocation_unit_size = SD_ALLOC_UNIT,
    };
    return esp_vfs_fat_sdmmc_mount(SD_BASE, &host, &slot, &mc, &s_card);
}

static void sd_unmount(void)
{
    close_fd(&s_wfd);
    close_fd(&s_rfd);
    s_wseg = s_rseg = UINT32_MAX;
    if (s_card) {
        esp_vfs_fat_sdcard_unmount(SD_BASE, s_card);
        s_card = NULL;
    }
    s_mounted = false;
}

/* Moves the pending block into the RAM ring and drops the card. */
static void sd_fail(const char *what)
{
    ESP_LOGE(TAG, "SD fault (%s, errno %d), using RAM fallback", what, errno);
    health_inc(HC_SD_ERRORS);
    sd_unmount();
    /* Records held only in RAM may be published and then lost on reboot; make the
     * next boot start beyond them even if the card comes back unchanged. */
    seg_floor_bump(ring_seq_of(ring_seg_of(atomic_load(&s_write_idx)) + 1, 0));

    if (s_fb_n == 0) {
        s_fb_seq0 = s_blk_seq0 + s_blk_n;
    }
    for (int i = s_blk_n - 1; i >= 0 && !record_is_padding(&s_blk[i]); i--) {
        fb_push_front(&s_blk[i]);
    }
    s_blk_n = 0;
    set_sd_bits(false);
}

static void enforce_free_space(uint32_t current_seg)
{
    uint64_t total = 0, free_bytes = 0;
    if (esp_vfs_fat_info(SD_BASE, &total, &free_bytes) != ESP_OK || total == 0) {
        return;
    }
    while (free_bytes * 100 < total * SD_MIN_FREE_PCT && s_oldest_seg < current_seg) {
        char path[PATH_LEN];
        ring_seg_path(path, sizeof(path), SD_BASE, s_oldest_seg);
        if (s_rseg == s_oldest_seg) {
            close_fd(&s_rfd);
            s_rseg = UINT32_MAX;
        }
        if (unlink(path) == 0) {
            ESP_LOGW(TAG, "card above %d%% full, deleted segment %lu", 100 - SD_MIN_FREE_PCT,
                     (unsigned long)s_oldest_seg);
        }
        if (ring_dir_of(s_oldest_seg) != ring_dir_of(s_oldest_seg + 1)) {
            ring_dir_path(path, sizeof(path), SD_BASE, ring_dir_of(s_oldest_seg));
            rmdir(path);
        }
        s_oldest_seg++;
        if (esp_vfs_fat_info(SD_BASE, &total, &free_bytes) != ESP_OK) {
            break;
        }
    }
    uint32_t new_lo = ring_seq_of(s_oldest_seg, 0);
    if (new_lo > s_sd_lo) {
        s_sd_lo = new_lo;
        uint32_t lost = 0;
        uint32_t r = ring_clamp_read_idx(atomic_load(&s_read_idx), s_sd_lo, &lost);
        if (lost) {
            ESP_LOGW(TAG, "%lu unsent records deleted with old segments", (unsigned long)lost);
            health_add(HC_RECORDS_LOST, lost);
            storage_advance_read_idx(r);
        }
    }
}

static bool open_write_segment(uint32_t seg)
{
    if (s_wseg == seg) {
        return true;
    }
    close_fd(&s_wfd);
    s_wseg = UINT32_MAX;

    char path[PATH_LEN];
    ring_dir_path(path, sizeof(path), SD_BASE, ring_dir_of(seg));
    if (mkdir(path, 0775) != 0 && errno != EEXIST) {
        return false;
    }
    ring_seg_path(path, sizeof(path), SD_BASE, seg);
    s_wfd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0664);
    if (s_wfd < 0) {
        return false;
    }
    s_wseg = seg;
    seg_floor_bump(ring_seq_of(seg, 0));
    enforce_free_space(seg);
    return true;
}

/* Appends the full pending block at its sector-aligned offset. A segment whose
 * file is shorter than that offset (gap after a card swap or flush) is padded. */
static bool commit_block(void)
{
    uint32_t seg = ring_seg_of(s_blk_seq0);
    off_t expect = (off_t)ring_index_of(s_blk_seq0) * (off_t)sizeof(record_t);

    pm_hold(true);
    bool ok = open_write_segment(seg);
    struct stat st;
    if (ok) {
        ok = fstat(s_wfd, &st) == 0 && st.st_size <= expect;
    }
    while (ok && st.st_size < expect) {
        ok = write(s_wfd, s_pad_sector, RING_BLOCK_BYTES) == RING_BLOCK_BYTES;
        st.st_size += RING_BLOCK_BYTES;
    }
    if (ok) {
        ok = write(s_wfd, s_blk, RING_BLOCK_BYTES) == RING_BLOCK_BYTES && fsync(s_wfd) == 0;
    }
    pm_hold(false);
    if (!ok) {
        return false;
    }

    s_blk_seq0 += RING_BLOCK_RECORDS;
    s_blk_n = 0;
    s_sd_hi = s_blk_seq0;
    if (ring_index_of(s_blk_seq0) == 0) {
        close_fd(&s_wfd);
        s_wseg = UINT32_MAX;
    }
    return true;
}

/* Places a record at the next SD slot. Returns false after an SD failure. */
static bool sd_put(const record_t *r)
{
    s_blk[s_blk_n++] = *r;
    if (s_blk_n < RING_BLOCK_RECORDS) {
        return true;
    }
    if (!commit_block()) {
        sd_fail("write");
        return false;
    }
    return true;
}

static bool pad_block_to(uint32_t seq)
{
    while (s_blk_seq0 + s_blk_n < seq && s_blk_n < RING_BLOCK_RECORDS) {
        record_make_padding(&s_blk[s_blk_n++]);
    }
    if (s_blk_n == RING_BLOCK_RECORDS && !commit_block()) {
        sd_fail("write");
        return false;
    }
    return true;
}

/* Moves the SD write position forward to seq, leaving padding/holes behind. */
static bool sd_seek(uint32_t seq)
{
    if (seq <= s_blk_seq0 + s_blk_n) {
        return true;
    }
    if (s_blk_n > 0 && !pad_block_to(seq)) {
        return false;
    }
    if (s_blk_n == 0 && seq >= s_blk_seq0 + RING_BLOCK_RECORDS) {
        s_blk_seq0 = seq - seq % RING_BLOCK_RECORDS;
        s_sd_hi = s_blk_seq0;
    }
    return pad_block_to(seq);
}

/* ---- recovery ---- */

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

static bool scan_dir_segments(uint32_t dir, uint32_t *min_seg, uint32_t *max_seg)
{
    char path[PATH_LEN];
    ring_dir_path(path, sizeof(path), SD_BASE, dir);
    DIR *d = opendir(path);
    if (!d) {
        return false;
    }
    bool found = false;
    struct dirent *e;
    uint32_t seg;
    while ((e = readdir(d)) != NULL) {
        if (ring_parse_seg_name(e->d_name, &seg)) {
            *min_seg = found && *min_seg < seg ? *min_seg : seg;
            *max_seg = found && *max_seg > seg ? *max_seg : seg;
            found = true;
        }
    }
    closedir(d);
    return found;
}

/* Finds the oldest and newest segments. Only the outermost non-empty directories
 * are listed, which keeps boot time flat as the card fills. */
static bool scan_segments(uint32_t *oldest, uint32_t *newest)
{
    static uint32_t dirs[MAX_DIRS];
    size_t n = 0;
    DIR *d = opendir(SD_BASE);
    if (!d) {
        return false;
    }
    struct dirent *e;
    uint32_t dir;
    while ((e = readdir(d)) != NULL && n < MAX_DIRS) {
        if (ring_parse_dir_name(e->d_name, &dir)) {
            dirs[n++] = dir;
        }
    }
    closedir(d);
    qsort(dirs, n, sizeof(dirs[0]), cmp_u32);

    uint32_t lo, hi;
    size_t i = 0;
    while (i < n && !scan_dir_segments(dirs[i], &lo, &hi)) {
        i++;
    }
    if (i == n) {
        return false;
    }
    *oldest = lo;
    size_t j = n - 1;
    while (j > i && !scan_dir_segments(dirs[j], &lo, &hi)) {
        j--;
    }
    *newest = hi;
    return true;
}

/* Trims a torn tail off the newest segment and returns the next free sequence.
 * Valid records of a partially bad last block are reloaded into the pending block. */
static uint32_t sd_recover(bool *have_data)
{
    uint32_t oldest, newest;
    s_blk_n = 0;
    *have_data = scan_segments(&oldest, &newest);
    if (!*have_data) {
        return 0;
    }
    s_oldest_seg = oldest;

    char path[PATH_LEN];
    ring_seg_path(path, sizeof(path), SD_BASE, newest);
    struct stat st;
    off_t size = stat(path, &st) == 0 ? st.st_size : 0;
    if (size % RING_BLOCK_BYTES) {
        ESP_LOGW(TAG, "S%06lu: torn sector, truncating %ld -> %ld", (unsigned long)newest,
                 (long)size, (long)(size - size % RING_BLOCK_BYTES));
        size -= size % RING_BLOCK_BYTES;
        truncate(path, size);
    }
    if (size > (off_t)RING_SEG_BYTES) {
        size = RING_SEG_BYTES;
    }

    uint32_t blk_seq0 = ring_seq_of(newest, (uint32_t)(size / sizeof(record_t)));
    if (size > 0) {
        int fd = open(path, O_RDONLY);
        bool read_ok = fd >= 0 && lseek(fd, size - RING_BLOCK_BYTES, SEEK_SET) >= 0 &&
                       read(fd, s_blk, RING_BLOCK_BYTES) == RING_BLOCK_BYTES;
        close_fd(&fd);
        size_t keep = read_ok ? ring_block_keep(s_blk, RING_BLOCK_RECORDS) : RING_BLOCK_RECORDS;
        if (keep < RING_BLOCK_RECORDS) {
            ESP_LOGW(TAG, "S%06lu: dropped %u bad trailing records", (unsigned long)newest,
                     (unsigned)(RING_BLOCK_RECORDS - keep));
            truncate(path, size - RING_BLOCK_BYTES);
            blk_seq0 -= RING_BLOCK_RECORDS;
            s_blk_n = (uint16_t)keep;
        }
    }
    s_blk_seq0 = blk_seq0;
    s_sd_lo = ring_seq_of(oldest, 0);
    s_sd_hi = blk_seq0;
    return blk_seq0 + s_blk_n;
}

/* ---- meta slots ---- */

static void meta_path(char *buf, size_t len, int slot)
{
    snprintf(buf, len, SD_BASE "/META%d.BIN", slot);
}

static bool meta_load(uint32_t *read_idx)
{
    ring_meta_t slots[2];
    bool loaded[2];
    for (int i = 0; i < 2; i++) {
        char path[PATH_LEN];
        meta_path(path, sizeof(path), i);
        int fd = open(path, O_RDONLY);
        loaded[i] = fd >= 0 && read(fd, &slots[i], sizeof(slots[i])) == sizeof(slots[i]);
        close_fd(&fd);
    }
    int pick = ring_meta_pick(slots, loaded);
    if (pick < 0) {
        return false;
    }
    s_meta_gen = slots[pick].gen;
    *read_idx = slots[pick].read_idx;
    return true;
}

static esp_err_t meta_save(uint32_t read_idx)
{
    ring_meta_t m = {.magic = RING_META_MAGIC, .gen = s_meta_gen + 1, .read_idx = read_idx};
    ring_meta_seal(&m);
    char path[PATH_LEN];
    /* Alternate slots so a torn write always leaves the previous one intact. */
    meta_path(path, sizeof(path), (int)(m.gen & 1));

    pm_hold(true);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    bool ok = fd >= 0 && write(fd, &m, sizeof(m)) == sizeof(m) && fsync(fd) == 0;
    close_fd(&fd);
    pm_hold(false);
    if (!ok) {
        health_inc(HC_SD_ERRORS);
        return ESP_FAIL;
    }
    s_meta_gen = m.gen;
    return ESP_OK;
}

/* ---- mount / remount ---- */

static bool sd_attach(bool at_boot)
{
    if (!card_present()) {
        return false;
    }
    esp_err_t err = sd_mount();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(err));
        s_card = NULL;
        return false;
    }

    bool have_data;
    uint32_t sd_end = sd_recover(&have_data);
    uint32_t target;
    if (at_boot) {
        /* Records in the RAM block at power loss may already have been published.
         * Resuming one block past the recovered end guarantees their sequence
         * numbers are never reused. A card that does not continue the last
         * segment we used (swapped, or written while we ran from RAM) restarts
         * at the NVS floor. */
        bool continues = have_data && ring_seg_of(sd_end) + 1 >= s_seg_floor;
        target = continues ? sd_end - sd_end % RING_BLOCK_RECORDS + RING_BLOCK_RECORDS
                           : ring_seq_of(s_seg_floor, 0);
        atomic_store(&s_write_idx, target);
    } else {
        uint32_t write_idx = atomic_load(&s_write_idx);
        if (have_data && sd_end > write_idx) {
            ESP_LOGE(TAG, "card holds sequences ahead of this device (%lu > %lu), not using it",
                     (unsigned long)sd_end, (unsigned long)write_idx);
            sd_unmount();
            return false;
        }
        target = s_fb_n ? s_fb_seq0 : write_idx;
    }
    if (!have_data) {
        s_blk_seq0 = target - target % RING_BLOCK_RECORDS;
        s_blk_n = 0;
        s_sd_lo = s_sd_hi = s_blk_seq0;
        s_oldest_seg = ring_seg_of(target);
    }

    s_mounted = true;
    if (!sd_seek(target)) {
        return false;
    }
    bool replayed = s_fb_n > 0;
    while (s_fb_n > 0 && s_mounted) {
        record_t r = *fb_at(0);
        s_fb_head = (s_fb_head + 1) % FALLBACK_RECORDS;
        s_fb_n--;
        s_fb_seq0++;
        sd_put(&r);
    }
    if (!s_mounted) {
        return false;
    }
    if (replayed) {
        ESP_LOGI(TAG, "card back, RAM fallback written to SD");
    }
    seg_floor_bump(atomic_load(&s_write_idx));
    set_sd_bits(true);
    return true;
}

/* ---- public ---- */

esp_err_t storage_init(const storage_config_t *cfg)
{
    s_cfg = *cfg;
    s_mtx = xSemaphoreCreateMutexStatic(&s_mtx_buf);
    memset(s_pad_sector, 0xFF, sizeof(s_pad_sector));

    const gpio_config_t cd = {
        .pin_bit_mask = 1ULL << cfg->cd,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    esp_err_t err = gpio_config(&cd);
    if (err != ESP_OK) {
        return err;
    }
#if CONFIG_PM_ENABLE
    err = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "storage", &s_pm_lock);
    if (err != ESP_OK) {
        return err;
    }
#endif

    if (app_nvs_get_u32(APP_NVS_NS_STORAGE, NVS_KEY_SEG_FLOOR, &s_seg_floor) != ESP_OK) {
        s_seg_floor = 0;
    }
    uint32_t floor_seq = ring_seq_of(s_seg_floor, 0);
    s_last_mount_try_us = esp_timer_get_time();

    uint32_t read_idx;
    if (sd_attach(true)) {
        if (!meta_load(&read_idx)) {
            read_idx = s_sd_lo;
        }
    } else {
        /* Anything recovered from a card that failed mid-boot is not used this boot. */
        uint32_t start = atomic_load(&s_write_idx);
        if (start < floor_seq) {
            start = floor_seq;
        }
        ESP_LOGW(TAG, "no usable card, records kept in RAM (%d max)", FALLBACK_RECORDS);
        atomic_store(&s_write_idx, start);
        s_fb_n = s_fb_head = 0;
        s_fb_seq0 = s_sd_lo = s_sd_hi = s_blk_seq0 = start;
        s_blk_n = 0;
        read_idx = start;
        seg_floor_bump(start);
        set_sd_bits(false);
    }

    uint32_t write_idx = atomic_load(&s_write_idx);
    uint32_t lost = 0;
    read_idx = ring_clamp_read_idx(read_idx, s_sd_lo, &lost);
    health_add(HC_RECORDS_LOST, lost);
    if (read_idx > write_idx) {
        read_idx = write_idx;
    }
    atomic_store(&s_read_idx, read_idx);
    ESP_LOGI(TAG, "write_idx=%lu read_idx=%lu unsent=%lu", (unsigned long)write_idx,
             (unsigned long)read_idx, (unsigned long)(write_idx - read_idx));
    return ESP_OK;
}

static void append(const record_t *r)
{
    uint32_t seq = atomic_load(&s_write_idx);
    if (s_mounted) {
        atomic_store(&s_write_idx, seq + 1);
        sd_put(r);
    } else {
        fb_push_back(r);
        atomic_store(&s_write_idx, seq + 1);
    }
    if (ring_index_of(seq) == 0) {
        seg_floor_bump(seq);
    }
}

/* After a long SD fault, stop waiting for records that exist only on the card:
 * count them as lost and let the publisher continue from the RAM ring. If the
 * card comes back they stay on it, below read_idx, and are not resent. */
static void abandon_card_backlog(void)
{
    s_card_abandoned = true;
    uint32_t lost = 0;
    uint32_t read_idx = ring_clamp_read_idx(atomic_load(&s_read_idx), s_sd_hi, &lost);
    if (lost) {
        ESP_LOGE(TAG, "SD fault for %lld s: %lu unsent records on the card marked lost",
                 SD_ABANDON_US / 1000000, (unsigned long)lost);
        health_add(HC_RECORDS_LOST, lost);
        storage_advance_read_idx(read_idx);
    }
    s_sd_lo = s_sd_hi;
}

static void check_card(void)
{
    if (s_mounted && !card_present()) {
        errno = ENODEV;
        sd_fail("card removed");
        return;
    }
    int64_t now = esp_timer_get_time();
    if (!s_mounted && now - s_last_mount_try_us >= SD_RETRY_US) {
        s_last_mount_try_us = now;
        esp_task_wdt_reset();
        sd_attach(false);
    }
    if (!s_mounted && !s_card_abandoned && now - s_fault_since_us >= SD_ABANDON_US) {
        abandon_card_backlog();
    }
}

static esp_err_t flush_locked(void);

/* The supply holds 3.3 V for at least 50 ms after power-good drops: enough for
 * the pending block and the meta slot (two fsyncs). Data goes first. */
static void on_power_fail(void)
{
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = flush_locked();
    if (err == ESP_OK && s_mounted) {
        err = meta_save(atomic_load(&s_read_idx));
    }
    ESP_LOGW(TAG, "power fail: SD flushed in %lld ms (%s)", (esp_timer_get_time() - t0) / 1000,
             esp_err_to_name(err));
}

static void IRAM_ATTR power_fail_isr(void *arg)
{
    (void)arg;
    BaseType_t woken = pdFALSE;
    xQueueSendToFrontFromISR(s_queue, &s_power_fail_marker, &woken);
    portYIELD_FROM_ISR(woken);
}

static void storage_task(void *arg)
{
    (void)arg;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    record_t r;
    for (;;) {
        bool got = xQueueReceive(s_queue, &r, pdMS_TO_TICKS(1000)) == pdTRUE;
        esp_task_wdt_reset();
        lock();
        if (got && record_is_padding(&r)) {
            on_power_fail();
        } else if (got) {
            append(&r);
        }
        check_card();
        unlock();
    }
}

esp_err_t storage_start(QueueHandle_t in_queue)
{
    s_queue = in_queue;
    s_task = xTaskCreateStaticPinnedToCore(storage_task, "storage", STORAGE_STACK_SIZE, NULL,
                                           STORAGE_PRIO, s_stack, &s_tcb, STORAGE_CORE);
    if (!s_task) {
        return ESP_FAIL;
    }
    if (s_cfg.pwr_fail == GPIO_NUM_NC) {
        return ESP_OK;
    }
    record_make_padding(&s_power_fail_marker);
    /* Pulled up internally, so an unconnected pin never triggers. */
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << s_cfg.pwr_fail,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    esp_err_t err = gpio_config(&io);
    if (err == ESP_OK) {
        err = gpio_isr_handler_add(s_cfg.pwr_fail, power_fail_isr, NULL);
    }
    return err;
}

TaskHandle_t storage_task_handle(void)
{
    return s_task;
}

/* Reads one run of records from a segment file. *skip counts leading slots
 * without a record; the run stops at the next such slot. */
static esp_err_t sd_read_run(uint32_t seq, uint16_t n, record_t *out, uint16_t *count, uint32_t *skip)
{
    uint32_t seg = ring_seg_of(seq);
    uint32_t idx = ring_index_of(seq);
    uint32_t seg_left = RING_SEG_RECORDS - idx;
    uint32_t max = n;
    if (max > seg_left) {
        max = seg_left;
    }
    if (max > s_sd_hi - seq) {
        max = s_sd_hi - seq;
    }
    *count = 0;
    *skip = 0;

    /* FatFs fixes a file's size at open; reopen when reading past it, which is
     * how the reader picks up blocks appended through the writer's fd. */
    if (s_rseg != seg || (idx + max) * sizeof(record_t) > s_rsize) {
        close_fd(&s_rfd);
        s_rseg = UINT32_MAX;
        char path[PATH_LEN];
        ring_seg_path(path, sizeof(path), SD_BASE, seg);
        s_rfd = open(path, O_RDONLY);
        struct stat st;
        if (s_rfd < 0 || fstat(s_rfd, &st) != 0) {
            close_fd(&s_rfd);
            *skip = seg_left < s_sd_hi - seq ? seg_left : s_sd_hi - seq;
            return ESP_OK;
        }
        s_rseg = seg;
        s_rsize = (uint32_t)st.st_size;
    }

    uint32_t avail = s_rsize / sizeof(record_t) > idx ? s_rsize / sizeof(record_t) - idx : 0;
    if (avail == 0) {
        *skip = seg_left < s_sd_hi - seq ? seg_left : s_sd_hi - seq;
        return ESP_OK;
    }
    if (max > avail) {
        max = avail;
    }
    size_t bytes = max * sizeof(record_t);
    if (lseek(s_rfd, (off_t)idx * sizeof(record_t), SEEK_SET) < 0 || read(s_rfd, out, bytes) != (ssize_t)bytes) {
        health_inc(HC_SD_ERRORS);
        close_fd(&s_rfd);
        s_rseg = UINT32_MAX;
        return ESP_FAIL;
    }

    uint32_t lead = 0;
    while (lead < max && record_is_padding(&out[lead])) {
        lead++;
    }
    uint32_t k = 0;
    while (lead + k < max && !record_is_padding(&out[lead + k])) {
        k++;
    }
    if (lead) {
        memmove(out, &out[lead], k * sizeof(record_t));
    }
    *skip = lead;
    *count = (uint16_t)k;
    return ESP_OK;
}

esp_err_t storage_read(uint32_t *seq, uint16_t n, record_t *out, uint16_t *count)
{
    esp_err_t err = ESP_OK;
    *count = 0;
    if (n == 0) {
        return ESP_OK;
    }
    lock();
    uint32_t s = *seq;
    uint32_t w = atomic_load(&s_write_idx);
    if (s < s_sd_lo) {
        s = s_sd_lo;
    }

    while (*count == 0 && s < w && err == ESP_OK) {
        if (s < s_sd_hi) {
            if (!s_mounted) {
                err = ESP_ERR_INVALID_STATE;
                break;
            }
            uint32_t skip = 0;
            err = sd_read_run(s, n, out, count, &skip);
            if (err != ESP_OK) {
                /* Unreadable sector: skip its block rather than stall the backlog. */
                uint32_t next = s - s % RING_BLOCK_RECORDS + RING_BLOCK_RECORDS;
                health_add(HC_RECORDS_LOST, next - s);
                ESP_LOGW(TAG, "read error at %lu, skipping to %lu", (unsigned long)s, (unsigned long)next);
                s = next;
                err = ESP_OK;
                continue;
            }
            s += skip;
        } else if (s_mounted) {
            uint32_t i = s - s_blk_seq0;
            while (i < s_blk_n && record_is_padding(&s_blk[i])) {
                i++;
                s++;
            }
            while (i < s_blk_n && *count < n) {
                out[(*count)++] = s_blk[i++];
            }
        } else {
            if (s < s_fb_seq0) {
                s = s_fb_seq0;
            }
            for (uint32_t i = s - s_fb_seq0; i < s_fb_n && *count < n; i++) {
                out[(*count)++] = *fb_at((uint16_t)i);
            }
        }
    }
    unlock();

    for (uint16_t i = 0; i < *count; i++) {
        if (!record_crc_ok(&out[i])) {
            health_inc(HC_CRC_ERRORS);
        }
    }
    *seq = s;
    return err;
}

uint32_t storage_write_idx(void)
{
    return atomic_load(&s_write_idx);
}

uint32_t storage_read_idx(void)
{
    return atomic_load(&s_read_idx);
}

void storage_advance_read_idx(uint32_t read_idx)
{
    uint32_t cur = atomic_load(&s_read_idx);
    while ((int32_t)(read_idx - cur) > 0 && !atomic_compare_exchange_weak(&s_read_idx, &cur, read_idx)) {
    }
}

esp_err_t storage_persist_read_idx(void)
{
    lock();
    esp_err_t err = s_mounted ? meta_save(atomic_load(&s_read_idx)) : ESP_ERR_INVALID_STATE;
    unlock();
    return err;
}

static esp_err_t flush_locked(void)
{
    if (!s_mounted || s_blk_n == 0) {
        return ESP_OK;
    }
    while (s_blk_n < RING_BLOCK_RECORDS) {
        record_make_padding(&s_blk[s_blk_n++]);
    }
    if (!commit_block()) {
        sd_fail("flush");
        return ESP_FAIL;
    }
    atomic_store(&s_write_idx, s_blk_seq0);
    return ESP_OK;
}

esp_err_t storage_flush(void)
{
    lock();
    esp_err_t err = flush_locked();
    unlock();
    return err;
}

void storage_prepare_reboot(void)
{
    esp_err_t err = storage_flush();
    if (err == ESP_OK) {
        err = storage_persist_read_idx();
    }
    ESP_LOGI(TAG, "prepared for reboot: %s", esp_err_to_name(err));
}

uint32_t storage_free_mb(void)
{
    uint64_t total = 0, free_bytes = 0;
    lock();
    if (s_mounted) {
        esp_vfs_fat_info(SD_BASE, &total, &free_bytes);
    }
    unlock();
    return (uint32_t)(free_bytes / (1024 * 1024));
}
