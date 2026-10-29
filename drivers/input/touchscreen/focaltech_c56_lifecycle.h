/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * C56 FT3680 singleton lifecycle gate. Include after Linux locking/wait headers.
 * No FocalTech firmware or reconstructed vendor implementation is stored here.
 *
 * Lock order: stop_lock -> publish stopping under lock -> drop lock -> drain.
 * The gate lock is NEVER held while waiting for IRQs/work/users, doing SPI, or
 * unregistering notifiers. A counted operation may nest: unlike a rwsem this
 * cannot deadlock when a stop races an inner operation. New operations fail
 * once stopping is published; the last existing operation wakes the drainer.
 */
#ifndef FOCALTECH_C56_LIFECYCLE_H
#define FOCALTECH_C56_LIFECYCLE_H

struct fts_c56_lifecycle {
	spinlock_t lock;
	wait_queue_head_t idle;
	struct mutex stop_lock;
	struct mutex pm_lock;
	struct mutex ta_lock;
	unsigned int ta_open_count;
	unsigned int active;
	bool stopping;
	bool quiesced;
	bool irq_registered;
	bool probe_ready;
	bool notifier_registered;
	bool irq_wake;
	bool dvdd_requested;
	bool dvdd_high;
	bool vdd_enabled;
	bool iovcc_enabled;
	int stop_error;
};

#define FTS_C56_LIFECYCLE_INITIALIZER(n)                                       \
	{                                                                      \
		.lock = __SPIN_LOCK_UNLOCKED(n.lock),                          \
		.idle = __WAIT_QUEUE_HEAD_INITIALIZER(n.idle),                 \
		.stop_lock = __MUTEX_INITIALIZER(n.stop_lock),                 \
		.pm_lock = __MUTEX_INITIALIZER(n.pm_lock),                     \
		.ta_lock = __MUTEX_INITIALIZER(n.ta_lock),                     \
	}

extern struct fts_c56_lifecycle fts_c56_life;

static inline bool fts_c56_stopping(void)
{
	return READ_ONCE(fts_c56_life.stopping);
}

static inline bool fts_c56_get(void)
{
	unsigned long flags;
	bool accepted;

	spin_lock_irqsave(&fts_c56_life.lock, flags);
	accepted = !fts_c56_life.stopping;
	if (accepted)
		WRITE_ONCE(fts_c56_life.active, fts_c56_life.active + 1);
	spin_unlock_irqrestore(&fts_c56_life.lock, flags);
	return accepted;
}

static inline void fts_c56_put(void)
{
	unsigned long flags;
	bool wake;

	spin_lock_irqsave(&fts_c56_life.lock, flags);
	/* Every accepted get is paired exactly once, including error returns. */
	if (WARN_ON_ONCE(!fts_c56_life.active)) {
		spin_unlock_irqrestore(&fts_c56_life.lock, flags);
		return;
	}
	WRITE_ONCE(fts_c56_life.active, fts_c56_life.active - 1);
	wake = fts_c56_life.stopping && !fts_c56_life.active;
	spin_unlock_irqrestore(&fts_c56_life.lock, flags);
	if (wake)
		wake_up_all(&fts_c56_life.idle);
}

static inline void fts_c56_begin_stop(void)
{
	unsigned long flags;

	spin_lock_irqsave(&fts_c56_life.lock, flags);
	WRITE_ONCE(fts_c56_life.stopping, true);
	spin_unlock_irqrestore(&fts_c56_life.lock, flags);
}

static inline void fts_c56_wait_users(void)
{
	wait_event(fts_c56_life.idle, !READ_ONCE(fts_c56_life.active));
}
#endif
