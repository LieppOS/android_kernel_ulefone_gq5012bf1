/* SPDX-License-Identifier: GPL-2.0-only */
/* Host tests for EXTRACTED H3 routines, not a kernel/hardware simulator.
 * c56_extracted.h is generated privately from the exact compiled module inputs.
 * IRQ/work/regulator APIs below are instrumented mocks with real pthread waits.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define READ_ONCE(x) __atomic_load_n(&(x), __ATOMIC_SEQ_CST)
#define WRITE_ONCE(x, v) __atomic_store_n(&(x), (v), __ATOMIC_SEQ_CST)
#define WARN_ON_ONCE(x) ((x) ? (abort(), 1) : 0)
typedef unsigned int u32;
typedef uint8_t u8;
typedef uint64_t u64;
struct mutex {
	pthread_mutex_t m;
};
typedef pthread_mutex_t spinlock_t;
typedef struct {
	pthread_mutex_t m;
	pthread_cond_t c;
} wait_queue_head_t;
#define __SPIN_LOCK_UNLOCKED(n) PTHREAD_MUTEX_INITIALIZER
#define __MUTEX_INITIALIZER(n) { PTHREAD_MUTEX_INITIALIZER }
#define __WAIT_QUEUE_HEAD_INITIALIZER(n)                                       \
	{ PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER }
#define mutex_init(x) pthread_mutex_init(&(x)->m, NULL)
#define mutex_lock(x) pthread_mutex_lock(&(x)->m)
#define mutex_unlock(x) pthread_mutex_unlock(&(x)->m)
#define spin_lock_init(x) pthread_mutex_init((x), NULL)
#define spin_lock_irqsave(x, f)                                                \
	do {                                                                   \
		(f) = 0;                                                       \
		pthread_mutex_lock(x);                                         \
	} while (0)
#define spin_unlock_irqrestore(x, f)                                           \
	do {                                                                   \
		(void)(f);                                                     \
		pthread_mutex_unlock(x);                                       \
	} while (0)
#define init_waitqueue_head(q)                                                 \
	do {                                                                   \
		pthread_mutex_init(&(q)->m, NULL);                             \
		pthread_cond_init(&(q)->c, NULL);                              \
	} while (0)
#define wake_up_all(q)                                                         \
	do {                                                                   \
		pthread_mutex_lock(&(q)->m);                                   \
		pthread_cond_broadcast(&(q)->c);                               \
		pthread_mutex_unlock(&(q)->m);                                 \
	} while (0)
#define wait_event(q, pred)                                                    \
	do {                                                                   \
		pthread_mutex_lock(&(q).m);                                    \
		while (!(pred))                                                \
			pthread_cond_wait(&(q).c, &(q).m);                     \
		pthread_mutex_unlock(&(q).m);                                  \
	} while (0)
#define wait_event_interruptible(q, pred)                                      \
	({                                                                     \
		wait_event(q, pred);                                           \
		0;                                                             \
	})
#define wait_event_interruptible_timeout(q, pred, t)                           \
	({                                                                     \
		(void)(t);                                                     \
		wait_event(q, pred);                                           \
		1;                                                             \
	})
#include "focaltech_c56_lifecycle.h"
struct fts_c56_lifecycle fts_c56_life =
	FTS_C56_LIFECYCLE_INITIALIZER(fts_c56_life);

#define EPROBE_DEFER 517
#define IRQF_TRIGGER_FALLING 1
#define IRQF_ONESHOT 2
#define IRQF_NO_AUTOEN 4
#define IRQ_HANDLED 1
#define CONFIG_DRM_MEDIATEK_V2 1
#define CONFIG_FB 0
#define CONFIG_PM 0
#define FTS_PATCH_COMERR_PM 0
#define IS_ENABLED(x) (x)
#define FTS_PEN_EN 0
#define FTS_PINCTRL_EN 0
#define FTS_POWER_SOURCE_CUST_EN 1
#define FTS_DRIVER_NAME "fts_ts"
#define FTS_DRIVER_VERSION "mock"
#define FTS_VTG_MIN_UV 3300000
#define FTS_VTG_MAX_UV 3300000
#define FTS_I2C_VTG_MIN_UV 1800000
#define FTS_I2C_VTG_MAX_UV 1800000
#define FTS_FUNC_ENTER() ((void)0)
#define FTS_FUNC_EXIT() ((void)0)
#define FTS_INFO(...) ((void)0)
#define FTS_DEBUG(...) ((void)0)
static atomic_int errors;
#define FTS_ERROR(...) ((void)atomic_fetch_add(&errors, 1))
#define GFP_KERNEL 0
#define __user
#define ENABLE 1
#define FTS_MAX_TOUCH_BUF 4096
#define FHP_QUEUE_BUF_SIZE 0x8000
#define FHP_FRAME_SIZE_MAX 0xFF8
#define FTS_FHP_FIX_ENQUEUE_OVERRUN 1
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
struct work_struct {
	void (*func)(struct work_struct *);
	int id;
	atomic_bool pending, running;
};
struct delayed_work {
	struct work_struct work;
};
struct device {
	int unused;
	struct {
		int unused;
	} kobj;
};
struct spi_device {
	struct fts_ts_data *data;
};
struct inode {
	void *data;
};
struct file {
	int unused;
};
static void *pde_data(struct inode *inode)
{
	return inode->data;
}
struct notifier_block {
	int unused;
};
struct regulator {
	int id, refs, enabled;
};
struct fts_ts_platform_data {
	int irq_gpio, reset_gpio, dvdd_gpio, avdd_gpio;
	unsigned irq_gpio_flags;
};
struct fts_ts_data {
	struct device *dev;
	struct spi_device *spi;
	struct fts_ts_platform_data *pdata;
	void *input_dev, *pen_dev, *ts_workqueue, *ws, *ta_buf, *touch_buf,
		*bus_tx_buf, *bus_rx_buf;
	struct work_struct resume_work;
	struct delayed_work esdcheck_work, prc_work, fwdbg_work;
	wait_queue_head_t ts_waitqueue;
	spinlock_t irq_lock;
	struct mutex report_mutex, bus_lock;
	struct notifier_block disp_notifier;
	int irq, ta_flag;
	bool irq_disabled, power_disabled, fw_loading, suspended, gesture_mode,
		pocket_mode;
	struct {
		bool is_incell;
	} ic_info;
	struct regulator *vdd, *vcc_i2c;
	unsigned long intr_jiffies;
	bool esd_support, prc_support, prc_mode, touch_analysis_support;
};
struct fhp_queue {
	struct mutex mutexq;
	u8 *buffer;
	unsigned int count, sizeq, maxq, wridx, rdidx;
};
struct fhp_data {
	struct fts_ts_data *ts_data;
	int state, timeout;
	struct fhp_queue q;
	wait_queue_head_t frame_waitq;
	int fhp_dev, fhp_input_dev;
};
struct fts_fwdbg_data {
	void *proc, *regfa_val, *regfb_val, *read_buf;
	struct {
		void *buffer;
	} q;
	wait_queue_head_t frame_waitq;
};
struct fts_upgrade {
	struct work_struct upgrade_work, fwrecover_work, fwload_work;
};
static struct fts_upgrade *fwupgrade;
static struct fts_fwdbg_data *fts_fwdbg_data;
static struct fhp_data *fts_fhp_data;
static bool c56_fhp_main_registered, c56_fhp_input_registered;
static struct fts_ts_data *fts_data;
static const int fts_fwdbg_group;
#define FHP_STATE_IDLE 0
#define FHP_STATE_POLL 1
#define FHP_STATE_WAIT_FRAME 2
#define FHP_STATE_FRAME_READY 3
#define ESDCHECK_WAIT_TIME 100
#define LCD_ESD_PATCH 0
static struct {
	bool mode;
} fts_esdcheck_data;
static bool yft_aod_state;
static int tpgesture_status = 1;
static char tpgesture_value[10];
#define FTS_REG_POWER_MODE 0xA5
#define FTS_REG_POWER_MODE_SLEEP 3
#define DISABLE 0
static atomic_int gesture_suspends;
static atomic_bool pm_entered;
#define msecs_to_jiffies(x) (x)
#define INIT_WORK(w, fn)                                                       \
	do {                                                                   \
		(w)->func = (fn);                                              \
		(w)->pending = false;                                          \
		(w)->running = false;                                          \
	} while (0)

static struct regulator rv = { .id = 1 }, ri = { .id = 2 };
static bool gpio_owned[256], irq_freed, notifier_live;
static atomic_bool irq_enabled, stop_finished;
static atomic_int free_count;
static int input_frees, ws_frees, workqueue_frees, writes, wake_disables,
	irq_syncs, order_violations;
static int ui_count, registrations, fault, fault_once;
static atomic_bool irq_running, algorithm_running, release_algorithm;
static pthread_mutex_t evlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t evcond = PTHREAD_COND_INITIALIZER;
enum {
	F_NONE,
	F_DT,
	F_WQ,
	F_BUS,
	F_INPUT,
	F_BUFFER,
	F_GPIO,
	F_VDD_GET,
	F_VDD_VOLT,
	F_IO_GET,
	F_IO_VOLT,
	F_DVDD,
	F_VDD_ENABLE,
	F_IO_ENABLE,
	F_DVDD_HIGH,
	F_IRQ,
	F_FHP_MAIN,
	F_FHP_INPUT,
	F_RESET,
	F_VDD_DISABLE,
	F_NOTIFIER,
	F_TA_ALLOC
};
static bool fail(int n)
{
	return fault == n && (!fault_once || fault_once-- > 0);
}
static void *zalloc(size_t n)
{
	if (n == FTS_MAX_TOUCH_BUF && fail(F_TA_ALLOC))
		return NULL;
	void *p = calloc(1, n);
	assert(p);
	return p;
}
#define kzalloc(n, g) ((void)(g), zalloc(n))
static void kfree(void *p)
{
	if (p) {
		free_count++;
		free(p);
	}
}
static void vfree(void *p)
{
	kfree(p);
}
#define kfree_safe(p)                                                          \
	do {                                                                   \
		kfree(p);                                                      \
		(p) = NULL;                                                    \
	} while (0)
#define IS_ERR(p) ((intptr_t)(p) < 0 && (intptr_t)(p) > -4096)
#define IS_ERR_OR_NULL(p) (!(p) || IS_ERR(p))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(n) ((void *)(intptr_t)(n))
static int gpio_is_valid(int n)
{
	return n >= 0 && n < 256;
}
static int gpio_request(int n, const char *name)
{
	(void)name;
	if (n == 183 && fail(F_DVDD))
		return -EIO;
	assert(!gpio_owned[n]);
	gpio_owned[n] = true;
	return 0;
}
static void gpio_free(int n)
{
	if (!gpio_owned[n])
		order_violations++;
	gpio_owned[n] = false;
}
static int gpio_direction_output(int n, int v)
{
	assert(gpio_owned[n]);
	writes++;
	if ((n == 6 && v == 0 && fail(F_RESET)) ||
	    (n == 183 && v && fail(F_DVDD_HIGH)))
		return -EIO;
	return 0;
}
static int gpio_to_irq(int n)
{
	assert(n == 7);
	return 52;
}
static void msleep(int n)
{
	(void)n;
}
static void fts_msleep(unsigned long n)
{
	(void)n;
}
static void usleep_range(unsigned long a, unsigned long b)
{
	(void)a;
	(void)b;
}
static int fts_set_reset(struct fts_ts_data *ts, int value)
{
	(void)ts;
	(void)value;
	return fail(F_RESET) ? -EIO : 0;
}
void fts_irq_enable(void);
void fts_irq_disable(void);
static struct regulator *regulator_get(struct device *d, const char *n)
{
	(void)d;
	bool main = !strcmp(n, "vdd");
	if (fail(main ? F_VDD_GET : F_IO_GET))
		return ERR_PTR(-EPROBE_DEFER);
	struct regulator *r = main ? &rv : &ri;
	r->refs++;
	return r;
}
static int regulator_count_voltages(struct regulator *r)
{
	(void)r;
	return 1;
}
static int regulator_set_voltage(struct regulator *r, int lo, int hi)
{
	(void)hi;
	if (lo && fail(r->id == 1 ? F_VDD_VOLT : F_IO_VOLT))
		return -EIO;
	return 0;
}
static int regulator_enable(struct regulator *r)
{
	if (fail(r->id == 1 ? F_VDD_ENABLE : F_IO_ENABLE))
		return -EIO;
	r->enabled++;
	return 0;
}
static int regulator_disable(struct regulator *r)
{
	assert(r->enabled > 0);
	if (r->id == 1 && fail(F_VDD_DISABLE))
		return -EIO;
	r->enabled--;
	return 0;
}
static void regulator_put(struct regulator *r)
{
	assert(r->refs > 0);
	assert(r->enabled == 0);
	r->refs--;
}
static int disable_irq_wake(int n)
{
	assert(n == 52);
	wake_disables++;
	return 0;
}
static void disable_irq_nosync(int n)
{
	assert(n == 52);
	irq_enabled = false;
}
static void enable_irq(int n)
{
	assert(n == 52);
	assert(fts_c56_life.irq_registered);
	irq_enabled = true;
}
static void sync_event(void)
{
	pthread_mutex_lock(&evlock);
	pthread_cond_broadcast(&evcond);
	pthread_mutex_unlock(&evlock);
}
static void synchronize_irq(int n)
{
	assert(n == 52);
	assert(!irq_enabled);
	irq_syncs++;
	assert(!pthread_mutex_trylock(&fts_data->bus_lock.m));
	pthread_mutex_unlock(&fts_data->bus_lock.m);
	pthread_mutex_lock(&evlock);
	while (atomic_load(&irq_running))
		pthread_cond_wait(&evcond, &evlock);
	pthread_mutex_unlock(&evlock);
}
static void free_irq(int n, void *p)
{
	(void)p;
	assert(n == 52);
	assert(!atomic_load(&irq_running));
	irq_freed = true;
}
static void cancel_work_sync(struct work_struct *w)
{
	assert(w->func);
	assert(!pthread_mutex_trylock(&fts_data->bus_lock.m));
	pthread_mutex_unlock(&fts_data->bus_lock.m);
	pthread_mutex_lock(&evlock);
	while (atomic_load(&w->running))
		pthread_cond_wait(&evcond, &evlock);
	atomic_store(&w->pending, false);
	pthread_mutex_unlock(&evlock);
}
static void cancel_delayed_work_sync(struct delayed_work *w)
{
	cancel_work_sync(&w->work);
}
static void queue_delayed_work(void *q, struct delayed_work *w,
			       unsigned long delay)
{
	(void)delay;
	assert(q);
	assert(w->work.func);
	atomic_store(&w->work.pending, true);
}
static void input_unregister_device(void *p)
{
	if (fts_c56_life.irq_registered && !irq_freed)
		order_violations++;
	input_frees++;
	kfree(p);
}
static void wakeup_source_unregister(void *p)
{
	if (fts_c56_life.irq_registered && !fts_c56_stopping())
		order_violations++;
	ws_frees++;
	kfree(p);
}
static void *wakeup_source_register(struct device *d, const char *n)
{
	(void)d;
	(void)n;
	return zalloc(1);
}
static void destroy_workqueue(void *p)
{
	assert(!notifier_live);
	workqueue_frees++;
	kfree(p);
}
static void proc_remove(void *p)
{
	kfree(p);
}
static void sysfs_remove_group(void *p, const void *g)
{
	(void)p;
	(void)g;
}
static void misc_deregister(void *p)
{
	(void)p;
	assert(registrations > 0);
	registrations--;
}
static int fhpq_dequeue_userspace(struct fhp_queue *q, void *p)
{
	(void)q;
	(void)p;
	assert(!fts_c56_stopping());
	return 0;
}
static int fhp_miscdev_init(struct fhp_data *p)
{
	(void)p;
	if (fail(F_FHP_MAIN))
		return -EIO;
	fts_irq_enable();
	assert(!irq_enabled);
	registrations++;
	return 0;
}
static int fhp_input_miscdev_init(struct fhp_data *p)
{
	(void)p;
	if (fail(F_FHP_INPUT))
		return -EIO;
	registrations++;
	return 0;
}
static int mtk_disp_notifier_unregister(struct notifier_block *b)
{
	(void)b;
	notifier_live = false;
	return fail(F_NOTIFIER) ? -ENOENT : 0;
}
static int fts_notifier_callback_init(struct fts_ts_data *ts)
{
	(void)ts;
	notifier_live = true;
	return 0;
}
static void dummy_work(struct work_struct *w)
{
	(void)w;
}
static void fts_resume_work(struct work_struct *w)
{
	(void)w;
}
static int fts_irq_handler(int n, void *p)
{
	(void)n;
	(void)p;
	return IRQ_HANDLED;
}
static int request_threaded_irq(int n, void *top, void *fn, unsigned flags,
				const char *name, void *data)
{
	(void)top;
	(void)fn;
	(void)name;
	(void)data;
	assert(n == 52);
	assert(flags & IRQF_NO_AUTOEN);
	assert(ui_count == 0);
	if (fail(F_IRQ))
		return -EIO;
	irq_enabled = false;
	return 0;
}
static void *create_singlethread_workqueue(const char *n)
{
	(void)n;
	if (fail(F_WQ))
		return NULL;
	return zalloc(1);
}
static int fts_parse_dt(struct device *d, struct fts_ts_platform_data *p)
{
	(void)d;
	p->irq_gpio = 7;
	p->reset_gpio = 6;
	p->dvdd_gpio = 183;
	if (fail(F_DT))
		return -EINVAL;
	return 0;
}
static int fts_bus_init(struct fts_ts_data *ts)
{
	if (fail(F_BUS))
		return -ENOMEM;
	ts->bus_rx_buf = zalloc(32);
	ts->bus_tx_buf = zalloc(32);
	return 0;
}
static int fts_input_init(struct fts_ts_data *ts)
{
	if (fail(F_INPUT))
		return -ENOMEM;
	ts->input_dev = zalloc(1);
	return 0;
}
static int fts_buffer_init(struct fts_ts_data *ts)
{
	if (fail(F_BUFFER))
		return -ENOMEM;
	ts->touch_buf = zalloc(32);
	return 0;
}
static int fts_gpio_configure(struct fts_ts_data *ts)
{
	if (fail(F_GPIO))
		return -EINVAL;
	gpio_request(ts->pdata->reset_gpio, "rst");
	gpio_request(ts->pdata->irq_gpio, "irq");
	return 0;
}
static int fts_get_ic_information(struct fts_ts_data *ts)
{
	(void)ts;
	return 0;
}
static int fts_create_apk_debug_channel(struct fts_ts_data *ts)
{
	(void)ts;
	ui_count++;
	return 0;
}
static int fts_create_sysfs(struct fts_ts_data *ts)
{
	(void)ts;
	ui_count++;
	return 0;
}
static void fts_remove_sysfs(struct fts_ts_data *ts)
{
	(void)ts;
}
static void fts_release_apk_debug_channel(struct fts_ts_data *ts)
{
	(void)ts;
}
static int fts_fwdbg_init(struct fts_ts_data *ts)
{
	fts_fwdbg_data = zalloc(sizeof(*fts_fwdbg_data));
	init_waitqueue_head(&fts_fwdbg_data->frame_waitq);
	INIT_WORK(&ts->fwdbg_work.work, dummy_work);
	return 0;
}
static int fts_point_report_check_init(struct fts_ts_data *ts)
{
	INIT_WORK(&ts->prc_work.work, dummy_work);
	return 0;
}
static int fts_esdcheck_init(struct fts_ts_data *ts)
{
	INIT_WORK(&ts->esdcheck_work.work, dummy_work);
	return 0;
}
static void fts_release_all_finger(void)
{
}
static int fts_write_reg(int reg, int value)
{
	(void)reg;
	(void)value;
	return fts_c56_stopping() ? -ESHUTDOWN : 0;
}
static int fts_wait_tp_to_valid(void)
{
	return 0;
}
static int fts_ex_mode_recovery(struct fts_ts_data *ts)
{
	(void)ts;
	return 0;
}
static int fts_power_source_suspend(struct fts_ts_data *ts)
{
	(void)ts;
	return 0;
}
static int fts_power_source_resume(struct fts_ts_data *ts)
{
	(void)ts;
	return 0;
}
static int fts_gesture_suspend(struct fts_ts_data *ts)
{
	(void)ts;
	atomic_fetch_add(&gesture_suspends, 1);
	fts_c56_life.irq_wake = true;
	atomic_store(&pm_entered, true);
	sync_event();
	usleep(10000);
	return 0;
}
static int fts_gesture_resume(struct fts_ts_data *ts)
{
	(void)ts;
	fts_c56_life.irq_wake = false;
	return 0;
}
static void fts_esdcheck_suspend(struct fts_ts_data *ts)
{
	(void)ts;
}
static void fts_esdcheck_resume(struct fts_ts_data *ts)
{
	(void)ts;
}
static int fts_ex_mode_init(struct fts_ts_data *ts)
{
	(void)ts;
	return 0;
}
static int fts_gesture_init(struct fts_ts_data *ts)
{
	(void)ts;
	return 0;
}
static int fts_fwupg_init(struct fts_ts_data *ts)
{
	(void)ts;
	fwupgrade = zalloc(sizeof(*fwupgrade));
	INIT_WORK(&fwupgrade->upgrade_work, dummy_work);
	INIT_WORK(&fwupgrade->fwrecover_work, dummy_work);
	INIT_WORK(&fwupgrade->fwload_work, dummy_work);
	return 0;
}
static void fts_fwupg_exit(struct fts_ts_data *ts)
{
	(void)ts;
	kfree(fwupgrade);
	fwupgrade = NULL;
}
static void fts_esdcheck_exit(struct fts_ts_data *ts)
{
	(void)ts;
}
static void fts_gesture_exit(struct fts_ts_data *ts)
{
	(void)ts;
}
static void fts_ex_mode_exit(struct fts_ts_data *ts)
{
	(void)ts;
}
static void fts_point_report_check_exit(struct fts_ts_data *ts)
{
	(void)ts;
}
static struct fts_ts_data *spi_get_drvdata(struct spi_device *s)
{
	return s->data;
}
static void spi_set_drvdata(struct spi_device *s, struct fts_ts_data *d)
{
	s->data = d;
}
static int esdcheck_algorithm(struct fts_ts_data *ts)
{
	(void)ts;
	atomic_store(&algorithm_running, true);
	sync_event();
	pthread_mutex_lock(&evlock);
	while (!atomic_load(&release_algorithm))
		pthread_cond_wait(&evcond, &evlock);
	pthread_mutex_unlock(&evlock);
	return 0;
}

static bool freeze_clock;
static atomic_bool enqueue_clock_entered, release_clock;
static u64 ktime_get_real(void)
{
	if (freeze_clock) {
		atomic_store(&enqueue_clock_entered, true);
		sync_event();
		pthread_mutex_lock(&evlock);
		while (!atomic_load(&release_clock))
			pthread_cond_wait(&evcond, &evlock);
		pthread_mutex_unlock(&evlock);
	}
	return 123456;
}
#define ktime_to_us(x) (x)
#include "focaltech_c56_power.h"
/* Exact generated driver functions, not copies maintained in this test file. */
#include "c56_extracted.h"

static struct device dev;
static struct fts_ts_data *make_ts(void)
{
	struct fts_ts_data *t = zalloc(sizeof(*t));
	t->dev = &dev;
	t->spi = zalloc(sizeof(*t->spi));
	t->spi->data = t;
	fts_data = t;
	return t;
}
static void finish(struct fts_ts_data *t)
{
	struct spi_device *s = t->spi;
	fts_ts_remove(s);
	assert(s->data == NULL);
	free(s);
	assert(registrations == 0);
	assert(rv.refs == 0 && ri.refs == 0);
	assert(!rv.enabled && !ri.enabled);
	assert(!gpio_owned[6] && !gpio_owned[7] && !gpio_owned[183]);
	assert(order_violations == 0);
}
static void join(pthread_t t)
{
	struct timespec deadline;
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += 4;
	assert(!pthread_timedjoin_np(t, NULL, &deadline));
}
static void *stop_thread(void *arg)
{
	int ret = fts_c56_quiesce(arg);
	atomic_store(&stop_finished, true);
	return (void *)(intptr_t)ret;
}
static void *debug_exit_thread(void *arg)
{
	fts_fwdbg_exit(arg);
	return NULL;
}
static void *esd_thread(void *arg)
{
	struct fts_ts_data *t = arg;
	esdcheck_func(&t->esdcheck_work.work);
	return NULL;
}
static void *frame_thread(void *arg)
{
	struct fhp_data *f = arg;
	assert(fts_c56_get());
	int ret = fhp_ioctl_get_frame(f, 0);
	assert(ret == -ESHUTDOWN);
	fts_c56_put();
	return NULL;
}
static void wait_flag(atomic_bool *f)
{
	pthread_mutex_lock(&evlock);
	while (!atomic_load(f))
		pthread_cond_wait(&evcond, &evlock);
	pthread_mutex_unlock(&evlock);
}
static void test_shutdown(void)
{
	struct fts_ts_data *t = make_ts();
	assert(!fts_ts_probe_entry(t));
	int before = free_count;
	fts_ts_shutdown(t->spi);
	assert(free_count == before && input_frees == 0 && ws_frees == 0 &&
	       workqueue_frees == 0);
	assert(!irq_enabled && irq_syncs == 1);
	assert(rv.enabled == 1 && ri.enabled == 1);
	assert(t->spi->data == t && fts_data == t);
	assert(!fts_c56_get());
	fts_ts_shutdown(t->spi);
	assert(irq_syncs == 1);
	finish(t);
}
static void test_remove(void)
{
	struct fts_ts_data *t = make_ts();
	assert(!fts_ts_probe_entry(t));
	finish(t);
	assert(input_frees == 1 && ws_frees == 1 && workqueue_frees == 1);
}
static void test_fault_probe(void)
{
	struct fts_ts_data *t = make_ts();
	int r = fts_ts_probe_entry(t);
	assert(r < 0);
	assert(ui_count == 0);
	assert(rv.refs == 0 && ri.refs == 0 && !rv.enabled && !ri.enabled);
	assert(!gpio_owned[6] && !gpio_owned[7] && !gpio_owned[183]);
	assert(order_violations == 0);
	free(t->spi);
	free(t);
}
static void test_partial_fhp(void)
{
	struct fts_ts_data *t = make_ts();
	assert(!fts_ts_probe_entry(t));
	assert(atomic_load(&errors) > 0);
	assert((fault == F_FHP_MAIN && !fts_fhp_data && registrations == 0) ||
	       (fault == F_FHP_INPUT && fts_fhp_data && registrations == 1));
	fault = 0;
	finish(t);
}
static void test_reset_error(void)
{
	struct fts_ts_data *t = make_ts();
	assert(!fts_ts_probe_entry(t));
	fault = F_RESET;
	assert(fts_c56_quiesce(t) == -EIO);
	assert(fts_c56_quiesce(t) == -EIO);
	assert(t->spi->data == t && input_frees == 0);
	fault = 0;
	finish(t);
}
static void test_notifier_absent(void)
{
	struct fts_ts_data *t = make_ts();
	assert(!fts_ts_probe_entry(t));
	fault = F_NOTIFIER;
	assert(!fts_c56_quiesce(t));
	fault = 0;
	finish(t);
}
static void test_rearm(void)
{
	struct fts_ts_data *t = make_ts();
	assert(!fts_ts_probe_entry(t));
	t->esd_support = true;
	fts_esdcheck_data.mode = true;
	pthread_t w, s;
	pthread_create(&w, NULL, esd_thread, t);
	wait_flag(&algorithm_running);
	pthread_create(&s, NULL, stop_thread, t);
	while (!fts_c56_stopping())
		sched_yield();
	assert(!atomic_load(&stop_finished));
	atomic_store(&release_algorithm, true);
	sync_event();
	join(w);
	join(s);
	assert(!atomic_load(&t->esdcheck_work.work.pending));
	finish(t);
}
static void test_frame_wait(void)
{
	struct fts_ts_data *t = make_ts();
	assert(!fts_ts_probe_entry(t));
	fts_fhp_data->timeout = -1;
	pthread_t w;
	pthread_create(&w, NULL, frame_thread, fts_fhp_data);
	while (!READ_ONCE(fts_c56_life.active))
		sched_yield();
	assert(!fts_c56_quiesce(t));
	join(w);
	finish(t);
}
static void test_irq_drain(void)
{
	struct fts_ts_data *t = make_ts();
	assert(!fts_ts_probe_entry(t));
	atomic_store(&irq_running, true);
	pthread_t s;
	pthread_create(&s, NULL, stop_thread, t);
	while (!fts_c56_stopping())
		sched_yield();
	usleep(10000);
	assert(!atomic_load(&stop_finished) && free_count == 0);
	atomic_store(&irq_running, false);
	sync_event();
	join(s);
	finish(t);
}
static void test_nested_user(void)
{
	struct fts_ts_data *t = make_ts();
	assert(!fts_ts_probe_entry(t));
	assert(fts_c56_get());
	pthread_t s;
	pthread_create(&s, NULL, stop_thread, t);
	while (!fts_c56_stopping())
		sched_yield();
	assert(!fts_c56_get());
	assert(!atomic_load(&stop_finished));
	fts_c56_life.irq_wake = true;
	fts_c56_put();
	join(s);
	assert(wake_disables == 1 && !fts_c56_life.irq_wake);
	finish(t);
}
static void test_disable_failure(void)
{
	struct fts_ts_data *t = make_ts();
	assert(!fts_ts_probe_entry(t));
	fault = F_VDD_DISABLE;
	int ret = fts_power_source_exit(t);
	assert(ret == -EIO && rv.enabled == 1 && rv.refs == 1 &&
	       fts_c56_life.vdd_enabled);
	assert(ri.refs == 0);
	fault = 0;
	finish(t);
}
static void test_partial_quiesce(void)
{
	struct fts_ts_data *t = make_ts();
	mutex_init(&t->bus_lock);
	spin_lock_init(&t->irq_lock);
	init_waitqueue_head(&t->ts_waitqueue);
	assert(!fts_c56_quiesce(t));
	assert(!irq_syncs && !free_count);
	free(t->spi);
	free(t);
}
static void test_gate_race(void)
{ /* each accepted pre-stop get drains; no post-stop get */
	for (int i = 0; i < 10000; i++) {
		assert(fts_c56_get());
		assert(fts_c56_get());
		fts_c56_put();
		fts_c56_put();
	}
	fts_c56_begin_stop();
	for (int i = 0; i < 10000; i++)
		assert(!fts_c56_get());
	assert(fts_c56_life.active == 0);
	fts_c56_wait_users();
}

static void test_debug_drain(void)
{
	struct fts_ts_data *t = make_ts();
	assert(!fts_ts_probe_entry(t));
	fts_c56_begin_stop();
	fts_irq_disable();
	synchronize_irq(t->irq);
	atomic_store(&t->fwdbg_work.work.running, true);
	int before = free_count;
	pthread_t d;
	pthread_create(&d, NULL, debug_exit_thread, t);
	usleep(10000);
	assert(fts_fwdbg_data && free_count == before);
	atomic_store(&t->fwdbg_work.work.running, false);
	sync_event();
	join(d);
	assert(!fts_fwdbg_data);
	finish(t);
}
static void test_firmware_drain(void)
{
	struct fts_ts_data *t = make_ts();
	assert(!fts_ts_probe_entry(t));
	atomic_store(&fwupgrade->upgrade_work.running, true);
	pthread_t s;
	pthread_create(&s, NULL, stop_thread, t);
	while (!fts_c56_stopping())
		sched_yield();
	usleep(10000);
	assert(!atomic_load(&stop_finished));
	fts_irq_enable();
	assert(!atomic_load(&irq_enabled));
	atomic_store(&fwupgrade->upgrade_work.running, false);
	sync_event();
	join(s);
	finish(t);
}
static atomic_int accepted;
static void *gate_racer(void *arg)
{
	(void)arg;
	for (int i = 0; i < 2000; i++) {
		if (fts_c56_get()) {
			atomic_fetch_add(&accepted, 1);
			sched_yield();
			fts_c56_put();
		}
	}
	return NULL;
}
static void test_parallel_gate(void)
{
	pthread_t t[4];
	for (int i = 0; i < 4; i++)
		pthread_create(&t[i], NULL, gate_racer, NULL);
	usleep(1000);
	fts_c56_begin_stop();
	fts_c56_wait_users();
	for (int i = 0; i < 4; i++)
		join(t[i]);
	assert(!READ_ONCE(fts_c56_life.active));
	assert(!fts_c56_get());
}

static void test_reset_error_propagation(void)
{
	struct fts_ts_data *t = make_ts();
	assert(!fts_ts_probe_entry(t));
	fault = F_RESET;
	assert(fts_reset_proc(t, true, 0) == -EIO);
	fault = 0;
	fts_c56_begin_stop();
	assert(fts_reset_proc(t, true, 0) == -ESHUTDOWN);
	finish(t);
}

static void *pm_thread(void *arg)
{
	struct fts_ts_data *t = arg;
	int ret = fts_ts_suspend(t->dev);
	assert(ret == 0 || ret == -ESHUTDOWN);
	return NULL;
}
static void test_pm_serialization(void)
{
	struct fts_ts_data *t = make_ts();
	assert(!fts_ts_probe_entry(t));
	t->ic_info.is_incell = true;
	pthread_t a, b;
	pthread_create(&a, NULL, pm_thread, t);
	wait_flag(&pm_entered);
	pthread_create(&b, NULL, pm_thread, t);
	join(a);
	join(b);
	assert(gesture_suspends == 1 && t->suspended);
	assert(!fts_ts_resume(t->dev) && !t->suspended);
	finish(t);
}
static void test_pm_stop(void)
{
	struct fts_ts_data *t = make_ts();
	assert(!fts_ts_probe_entry(t));
	t->ic_info.is_incell = true;
	pthread_t a, s;
	pthread_create(&a, NULL, pm_thread, t);
	wait_flag(&pm_entered);
	pthread_create(&s, NULL, stop_thread, t);
	join(a);
	join(s);
	assert(gesture_suspends == 1 && wake_disables == 1 &&
	       !fts_c56_life.irq_wake);
	assert(fts_ts_resume(t->dev) == -ESHUTDOWN);
	finish(t);
}

static void setup_ring(struct fhp_queue *q)
{
	memset(q, 0, sizeof(*q));
	mutex_init(&q->mutexq);
	q->buffer = zalloc(FHP_QUEUE_BUF_SIZE);
	q->sizeq = 16;
	q->maxq = FHP_QUEUE_BUF_SIZE / 16;
	q->wridx = q->maxq - 1;
}
static void test_ring_boundary(void)
{
	struct fhp_queue q;
	u8 val[16];
	memset(val, 0x5a, sizeof(val));
	setup_ring(&q);
	assert(!fhpq_enqueue(&q, val));
	assert(q.wridx == 0);
	assert(!memcmp(q.buffer + FHP_QUEUE_BUF_SIZE - 8, val, 8));
	u64 stamp;
	memcpy(&stamp, q.buffer + FHP_QUEUE_BUF_SIZE - 16, 8);
	assert(stamp == 123456);
	assert(!fhpq_enqueue(&q, val));
	memcpy(&stamp, q.buffer + FHP_QUEUE_BUF_SIZE - 16, 8);
	assert(stamp == 123456);
	fhpq_close(&q);
}
static void test_closed_queue(void)
{
	struct fhp_queue q;
	u8 val[16] = { 0 };
	setup_ring(&q);
	fhpq_close(&q);
	assert(fhpq_enqueue(&q, val) == -EINVAL);
	q.buffer = zalloc(FHP_QUEUE_BUF_SIZE);
	q.sizeq = 4;
	assert(fhpq_enqueue(&q, val) == -EINVAL);
	fhpq_close(&q);
}
static void *ta_open_thread(void *arg)
{
	assert(!fts_ta_open(arg, NULL));
	return NULL;
}
static void test_ta_lifetime(void)
{
	struct fts_ts_data *t = make_ts();
	assert(!fts_ts_probe_entry(t));
	t->touch_analysis_support = true;
	struct inode node = { .data = t };
	pthread_t a, b;
	pthread_create(&a, NULL, ta_open_thread, &node);
	pthread_create(&b, NULL, ta_open_thread, &node);
	join(a);
	join(b);
	assert(fts_c56_life.ta_open_count == 2 && t->ta_buf);
	void *saved = t->ta_buf;
	int frees = free_count;
	assert(!fts_ta_release(&node, NULL) && fts_c56_life.ta_open_count == 1);
	assert(t->ta_buf == saved && free_count == frees);
	assert(!fts_c56_quiesce(t));
	assert(fts_ta_open(&node, NULL) == -ESHUTDOWN);
	assert(!fts_ta_release(&node, NULL) && !fts_c56_life.ta_open_count);
	assert(t->ta_buf == saved && free_count == frees);
	finish(t);
}
static void test_old_queue(void)
{
	struct fhp_queue q;
	u8 val[16] = { 0 };
	setup_ring(&q);
	c56_old_fhpq_enqueue(&q, val);
	abort();
}
struct ring_arg {
	struct fhp_queue *q;
	u8 val[16];
	int result;
};
static void *enqueue_thread(void *arg)
{
	struct ring_arg *r = arg;
	r->result = fhpq_enqueue(r->q, r->val);
	return NULL;
}
static void test_queue_close_race(void)
{
	struct fhp_queue q;
	setup_ring(&q);
	freeze_clock = true;
	struct ring_arg a = { .q = &q };
	pthread_t p;
	pthread_create(&p, NULL, enqueue_thread, &a);
	wait_flag(&enqueue_clock_entered);
	fhpq_close(&q);
	atomic_store(&release_clock, true);
	sync_event();
	join(p);
	assert(a.result == -EINVAL);
}

static void test_ta_alloc_failure(void)
{
	struct fts_ts_data *t = make_ts();
	assert(!fts_ts_probe_entry(t));
	t->touch_analysis_support = true;
	struct inode node = { .data = t };
	fault = F_TA_ALLOC;
	assert(fts_ta_open(&node, NULL) == -ENOMEM);
	assert(!t->ta_buf && !fts_c56_life.ta_open_count &&
	       !READ_ONCE(fts_c56_life.active));
	fault = 0;
	finish(t);
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	alarm(8);
	fault = atoi(argv[1]);
	if (fault >= F_DT && fault <= F_IRQ)
		test_fault_probe();
	else if (fault == F_FHP_MAIN || fault == F_FHP_INPUT)
		test_partial_fhp();
	else if (fault == 100)
		test_shutdown();
	else if (fault == 101)
		test_remove();
	else if (fault == 102)
		test_reset_error();
	else if (fault == 103)
		test_notifier_absent();
	else if (fault == 104)
		test_rearm();
	else if (fault == 105)
		test_frame_wait();
	else if (fault == 106)
		test_irq_drain();
	else if (fault == 107)
		test_nested_user();
	else if (fault == 108)
		test_disable_failure();
	else if (fault == 109)
		test_partial_quiesce();
	else if (fault == 110)
		test_gate_race();
	else if (fault == 111) {
		struct fts_ts_data *t = make_ts();
		assert(!fts_ts_probe_entry(t));
		c56_old_remove_entry(t);
		assert(order_violations == 0);
	} else if (fault == 112)
		test_debug_drain();
	else if (fault == 113)
		test_firmware_drain();
	else if (fault == 114)
		test_parallel_gate();
	else if (fault == 115)
		test_reset_error_propagation();
	else if (fault == 116)
		test_pm_serialization();
	else if (fault == 117)
		test_pm_stop();
	else if (fault == 118)
		test_ring_boundary();
	else if (fault == 119)
		test_closed_queue();
	else if (fault == 120)
		test_ta_lifetime();
	else if (fault == 121)
		test_old_queue();
	else if (fault == 122)
		test_queue_close_race();
	else if (fault == 123)
		test_ta_alloc_failure();
	else
		abort();
	printf("PASS %d\n", fault);
	return 0;
}
