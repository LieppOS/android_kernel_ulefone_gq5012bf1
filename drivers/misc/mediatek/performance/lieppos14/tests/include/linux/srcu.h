/* SPDX-License-Identifier: GPL-2.0 */
/* Host-only lease API model for the actual ioctl translation unit.
 * pthread rwlocks model read-section/grace-period ordering, NOT kernel SRCU,
 * module-loader lifetime, real RCU memory ordering or scheduler behavior.
 */
#ifndef LIEPPOS_TEST_SRCU_H
#define LIEPPOS_TEST_SRCU_H
#include <pthread.h>
#include <assert.h>
struct srcu_struct { pthread_rwlock_t lock; };
#define DEFINE_STATIC_SRCU(name) \
	static struct srcu_struct name = { PTHREAD_RWLOCK_INITIALIZER }
static int srcu_read_lock(struct srcu_struct *s)
{
	assert(pthread_rwlock_rdlock(&s->lock) == 0);
	return 0;
}
static void srcu_read_unlock(struct srcu_struct *s, int index)
{
	(void)index;
	assert(pthread_rwlock_unlock(&s->lock) == 0);
}
static void synchronize_srcu(struct srcu_struct *s)
{
	assert(pthread_rwlock_wrlock(&s->lock) == 0);
	assert(pthread_rwlock_unlock(&s->lock) == 0);
}
#define smp_store_release(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#define smp_load_acquire(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define WARN_ON(x) (!!(x))
#endif
