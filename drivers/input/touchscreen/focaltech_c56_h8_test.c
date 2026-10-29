/* SPDX-License-Identifier: GPL-2.0-only */
/* Production H8 functions with HOST-ONLY kernel/controller mocks.
 * Not a hardware, power, thermal or kernel-scheduler test.
 */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef uint8_t u8;
typedef uint32_t u32;
struct device { int unused; };
struct work_struct { void (*func)(struct work_struct *); };
struct fts_ts_data {
    struct device *dev;
    bool suspended, fw_loading, gesture_mode, pocket_mode, irq_disabled;
    bool c56_pm_blocked;
    u8 Fod_support;
    int fw_is_running, irq;
    void *ts_workqueue;
    struct { bool is_incell; struct { u8 chip_idh; } ids; } ic_info;
};
struct fts_upgrade {
    struct fts_ts_data *ts_data;
    struct { u8 upgsts_boot; } *setting_nf;
    u8 *fw;
    struct work_struct fwrecover_work;
};
static struct device dev;
static struct fts_ts_data data, *fts_data = &data;
static typeof(*((struct fts_upgrade *)0)->setting_nf) setting;
static struct fts_upgrade upgrade, *fwupgrade = &upgrade;
static struct { pthread_mutex_t pm_lock; bool irq_wake; } fts_c56_life = {PTHREAD_MUTEX_INITIALIZER, false};
static bool stopping, ram_running, esd_running, irq_wake_live;
static int users, tpgesture_status, yft_aod_state, fault;
static char tpgesture_value[10];
static unsigned downloads, writes, resets, irq_enables, drains, esd_starts, queued;
static u8 gesture_reg;
static bool slow_download, download_started;
static u8 firmware[128];
enum { NONE, POWER_ON, POWER_OFF, RESET, DOWNLOAD, READY, ID_IO, FLAG,
       GESTURE_WRITE, GESTURE_READ, GESTURE_STATE, WAKE_ON, WAKE_OFF,
       SLEEP_WRITE, STOP_AFTER_DOWNLOAD, POCKET_WRITE };
#define READ_ONCE(v) __atomic_load_n(&(v), __ATOMIC_ACQUIRE)
#define WRITE_ONCE(v,x) __atomic_store_n(&(v), (x), __ATOMIC_RELEASE)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
#define FTS_FUNC_ENTER() ((void)0)
#define FTS_FUNC_EXIT() ((void)0)
static void log_sink(const char *fmt, ...) { (void)fmt; }
#define FTS_INFO(...) log_sink(__VA_ARGS__)
#define FTS_ERROR(...) log_sink(__VA_ARGS__)
#define FTS_DEBUG(...) log_sink(__VA_ARGS__)
#define FTS_POWER_SOURCE_CUST_EN 1
#define FTS_REG_POWER_MODE 0xa5
#define FTS_REG_POWER_MODE_SLEEP 3
#define FTS_REG_CHIP_ID 0xa3
#define FTS_REG_BOOT_STATE 0xd0
#define FTS_REG_GESTURE_EN 0xd0
#define FTS_REG_FOD_EN 0xcf
#define FTS_REG_FOD_VALUE 1
#define FTS_FOD_DISABLE 0
#define FTS_MIN_LEN 8
#define INTERVAL_READ_REG_RESUME 100
#define TIMEOUT_READ_REG 1000
#define FW_NORMAL 0
#define FW_AUTO 2
#define ENABLE 1
#define DISABLE 0
static bool fts_c56_stopping(void) { return READ_ONCE(stopping); }
static bool fts_c56_get(void) { if (fts_c56_stopping()) return false; __atomic_add_fetch(&users,1,__ATOMIC_SEQ_CST); return true; }
static void fts_c56_put(void) { assert(__atomic_sub_fetch(&users,1,__ATOMIC_SEQ_CST) >= 0); }
static void mutex_lock(pthread_mutex_t *p) { assert(!pthread_mutex_lock(p)); }
static void mutex_unlock(pthread_mutex_t *p) { assert(!pthread_mutex_unlock(p)); }
static void msleep(unsigned n) { (void)n; }
static void fts_msleep(unsigned n) { msleep(n); }
static int fts_check_cid(struct fts_ts_data *ts, u8 id) { return id == ts->ic_info.ids.chip_idh ? 0 : -EINVAL; }
static int fts_read_reg(u8 reg, u8 *value)
{
    if (reg == FTS_REG_CHIP_ID) {
        *value = fault == ID_IO ? 0x56 : (ram_running ? 0x56 : 0);
        if (fault == ID_IO) return -EIO; /* Failed IO must not accept valid-looking data. */
    } else if (reg == FTS_REG_GESTURE_EN) {
        if (fault == GESTURE_READ) return -EIO;
        *value = ram_running ? gesture_reg : setting.upgsts_boot;
        if (fault == GESTURE_STATE) *value = 0xff;
    } else *value = 0;
    return 0;
}
static int fts_write_reg(u8 reg, u8 value)
{
    writes++;
    if ((fault == GESTURE_WRITE && reg >= 0xd0) ||
        (fault == SLEEP_WRITE && reg == FTS_REG_POWER_MODE && value == 3) ||
        (fault == POCKET_WRITE && reg == FTS_REG_POWER_MODE && value == 0)) return -EIO;
    if (!ram_running) return -EIO;
    if (reg == FTS_REG_GESTURE_EN) gesture_reg = value;
    return 0;
}
static int enable_irq_wake(int irq) { (void)irq; if (fault == WAKE_ON) return -EIO; assert(!irq_wake_live); irq_wake_live = true; return 0; }
static int disable_irq_wake(int irq) { (void)irq; if (fault == WAKE_OFF) return -EIO; assert(irq_wake_live); irq_wake_live = false; return 0; }
static void fts_irq_disable(void) { data.irq_disabled = true; }
static void fts_irq_enable(void)
{
    if (fts_c56_stopping() || READ_ONCE(data.c56_pm_blocked)) return;
    assert(ram_running); data.irq_disabled = false; irq_enables++;
}
static void synchronize_irq(int irq) { (void)irq; assert(data.irq_disabled && data.c56_pm_blocked); drains++; }
static void fts_esdcheck_switch(struct fts_ts_data *ts, bool enable)
{
    if (enable && READ_ONCE(ts->c56_pm_blocked)) return;
    esd_running = enable;
}
static bool fts_esdcheck_is_running(void) { return esd_running; }
static void fts_esdcheck_suspend(struct fts_ts_data *ts) { fts_esdcheck_switch(ts, false); }
static void fts_esdcheck_resume(struct fts_ts_data *ts) { assert(ram_running && !ts->suspended && !ts->c56_pm_blocked); fts_esdcheck_switch(ts,true); esd_starts++; }
static int fts_power_source_suspend(struct fts_ts_data *ts) { (void)ts; ram_running = false; return fault == POWER_OFF ? -EIO : 0; }
static int fts_power_source_resume(struct fts_ts_data *ts) { (void)ts; return fault == POWER_ON ? -EIO : 0; }
static int fts_reset_proc(struct fts_ts_data *ts, bool force, unsigned delay) { (void)ts; (void)force; (void)delay; resets++; ram_running = false; return fault == RESET ? -EIO : 0; }
static void fts_release_all_finger(void) {}
static void fts_ex_mode_recovery(struct fts_ts_data *ts) { (void)ts; assert(ram_running); }
static void fts_tp_state_recovery(struct fts_ts_data *ts) { (void)ts; }
static int fts_check_bootid(void) { return ram_running ? -EIO : 0; }
static void queue_work(void *q, struct work_struct *w) { (void)q; (void)w; queued++; }
static int fts_fw_write_start(u8 *buf, u32 len, bool reset)
{
    (void)buf; (void)len; (void)reset;
    downloads++;
    assert(!esd_running); /* PM stopped ESD before entering the downloader. */
    if (slow_download) { WRITE_ONCE(download_started,true); usleep(50000); }
    data.fw_is_running = false;
    if (fault == DOWNLOAD) return -EIO;
    ram_running = fault != READY;
    data.fw_is_running = fault != FLAG;
    if (fault == STOP_AFTER_DOWNLOAD) WRITE_ONCE(stopping,true);
    return 0;
}
int fts_fw_download(u8 *, u32, bool);
static int fts_fw_resume(bool reset, int type) { assert(type == FW_NORMAL || type == FW_AUTO); return fts_fw_download(firmware,sizeof(firmware),reset); }
int fts_enter_normal_fw(void);
#include C56_H8_ACTUAL_FUNCTIONS
#include "focaltech_c56_pm_gesture.h"
#include "focaltech_c56_resume_fw.h"
#include "focaltech_c56_pm.h"

static void init(void)
{
    assert(!users);
    memset(&data,0,sizeof(data));
    data.dev = &dev; data.irq = 52; data.ic_info.ids.chip_idh = 0x56;
    data.fw_is_running = true; data.ts_workqueue = &data;
    upgrade.ts_data = &data; upgrade.setting_nf = &setting; upgrade.fw = firmware;
    upgrade.fwrecover_work.func = fts_fwrecover_work;
    setting.upgsts_boot = 1;
    stopping = slow_download = download_started = false;
    ram_running = esd_running = true;
    fts_c56_life.irq_wake = irq_wake_live = false;
    tpgesture_status = yft_aod_state = fault = gesture_reg = 0;
    downloads = writes = resets = irq_enables = drains = esd_starts = queued = 0;
}
static void balanced(void) { assert(!users && !pthread_mutex_trylock(&fts_c56_life.pm_lock)); mutex_unlock(&fts_c56_life.pm_lock); }
static void failed(void) { assert(data.suspended && data.c56_pm_blocked && data.irq_disabled && !esd_running && !data.fw_is_running); balanced(); }
static void prepare_sleep(int gesture) { init(); tpgesture_status = gesture; assert(!fts_ts_suspend(&dev)); assert(data.suspended && !esd_running); }
static void *resume_thread(void *unused) { (void)unused; assert(!fts_ts_resume(&dev)); return NULL; }
static void *suspend_thread(void *unused) { (void)unused; assert(!fts_ts_suspend(&dev)); return NULL; }
int main(void)
{
    unsigned cycles = 0;
    for (int gesture = 0; gesture <= 1; gesture++) {
        init(); tpgesture_status = gesture;
        for (int n = 0; n < 100; n++) {
            assert(!fts_ts_suspend(&dev));
            assert(data.suspended && !esd_running);
            assert(gesture ? (!data.c56_pm_blocked && irq_wake_live) : data.c56_pm_blocked);
            assert(!fts_ts_resume(&dev));
            assert(ram_running && data.fw_is_running && !data.suspended && !data.c56_pm_blocked);
            assert(!data.irq_disabled && esd_running && !irq_wake_live && !fts_c56_life.irq_wake);
            balanced(); cycles++;
        }
        assert(downloads == 100);
    }
    int faults[] = {POWER_ON, RESET, DOWNLOAD, READY, ID_IO, FLAG, STOP_AFTER_DOWNLOAD, POCKET_WRITE};
    for (unsigned i = 0; i < ARRAY_SIZE(faults); i++) {
        prepare_sleep(0); data.pocket_mode = faults[i] == POCKET_WRITE; fault = faults[i];
        assert(fts_ts_resume(&dev) < 0); failed();
        if (fault == DOWNLOAD) assert(downloads == 3);
        unsigned attempts = downloads;
        if (!stopping) {
            assert(fts_fw_recovery() == -EHOSTDOWN);
            fts_fwrecover_work(&upgrade.fwrecover_work);
            struct work_struct w = {0}; fts_fwload_work(&w);
            assert(downloads == attempts && !queued);
            fault = NONE;
            assert(!fts_ts_resume(&dev));
            assert(ram_running && !data.suspended && !data.c56_pm_blocked && esd_running);
        }
        balanced();
    }
    int gesture_faults[] = {GESTURE_WRITE,GESTURE_READ,GESTURE_STATE,WAKE_ON};
    for (unsigned i = 0; i < ARRAY_SIZE(gesture_faults); i++) {
        init(); tpgesture_status = 1; fault = gesture_faults[i];
        assert(fts_ts_suspend(&dev) < 0); failed(); assert(!irq_wake_live);
        fault = NONE; assert(!fts_ts_resume(&dev)); balanced();
    }
    for (unsigned i = 0; i < ARRAY_SIZE(gesture_faults); i++) {
        if (gesture_faults[i] == WAKE_ON) continue;
        prepare_sleep(1); fault = gesture_faults[i];
        assert(fts_ts_resume(&dev) < 0); failed(); assert(!irq_wake_live);
    }
    prepare_sleep(1); fault = WAKE_OFF;
    assert(fts_ts_resume(&dev) < 0); failed(); assert(irq_wake_live && fts_c56_life.irq_wake);
    fault = NONE; assert(!fts_ts_resume(&dev)); assert(!irq_wake_live); balanced();
    for (int f = POWER_OFF; f <= SLEEP_WRITE; f += SLEEP_WRITE-POWER_OFF) {
        init(); fault = f; assert(fts_ts_suspend(&dev) < 0); failed();
    }
    init(); data.fw_loading = true; assert(fts_ts_suspend(&dev) == -EBUSY); balanced();
    prepare_sleep(0); data.fw_loading = true; assert(fts_ts_resume(&dev) == -EBUSY); balanced();
    data.fw_loading = false; yft_aod_state = 1; assert(!fts_ts_resume(&dev) && data.suspended); balanced();
    yft_aod_state = 0; WRITE_ONCE(stopping,true); assert(fts_ts_resume(&dev) == -ESHUTDOWN); balanced();
    init(); assert(!fts_ts_resume(&dev) && !downloads); balanced();
    init(); fault = ID_IO; assert(fts_wait_tp_to_valid() == -EIO); balanced();
    init(); data.suspended = true; data.c56_pm_blocked = true; esd_running = false;
    data.ic_info.is_incell = true; assert(!fts_ts_resume(&dev) && !resets && !downloads); balanced();
    /* Real pthread mutex serializes concurrent PM callers; mocked IRQ/work/IO. */
    prepare_sleep(0); slow_download = true;
    pthread_t a,b; assert(!pthread_create(&a,NULL,resume_thread,NULL));
    unsigned wait;
    for (wait = 0; wait < 1000 && !READ_ONCE(download_started); wait++) usleep(1000);
    assert(wait < 1000); assert(!pthread_create(&b,NULL,suspend_thread,NULL));
    assert(!pthread_join(a,NULL)); assert(!pthread_join(b,NULL));
    assert(data.suspended && data.c56_pm_blocked && data.irq_disabled && !esd_running); balanced();
    printf("H8 PRODUCTION HOST PASS: %u lock/wake cycles, bounded download/ready/gesture/power failures, deferred-work gates, explicit retry, guards and concurrent PM serialization. Mocks only; no thermal/hardware claim.\n",cycles);
    return 0;
}
