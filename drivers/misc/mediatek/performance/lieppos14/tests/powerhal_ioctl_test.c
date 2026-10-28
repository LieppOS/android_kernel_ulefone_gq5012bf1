// SPDX-License-Identifier: GPL-2.0
/* Execute the production ioctl TU with only its kernel services substituted.
 * Tests parser/ABI/error behavior, NOT kernel lifetime or scheduler behavior.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include "../public/drivers/misc/mediatek/performance/perf_ioctl/powerhal_uapi.h"
#define LIEPPOS_IOCTL_POWERHAL_H
#define CONFIG_COMPAT 1
#define static_assert _Static_assert
#define __aligned(n) __attribute__((aligned(n)))
#define __user
#define __init
#define __exit
#define EXPORT_SYMBOL_GPL(x)
#define MODULE_LICENSE(x)
#define MODULE_DESCRIPTION(x)
#define MODULE_AUTHOR(x)
#define module_init(x)
#define module_exit(x)
#define GFP_KERNEL 0
#define READ_ONCE(x) (x)
#define ERESTARTSYS 512
#define array_size(a,b) ((size_t)(a)*(b))
#define PTR_ERR(x) ((long)(intptr_t)(x))
#define IS_ERR(x) ((uintptr_t)(x) >= (uintptr_t)-4095L)
#define ERR_PTR(x) ((void *)(intptr_t)(x))
struct file { int unused; };
struct proc_dir_entry { int unused; };
struct proc_ops {
	long (*proc_ioctl)(struct file *, unsigned int, unsigned long);
	long (*proc_compat_ioctl)(struct file *, unsigned int, unsigned long);
};
static int alloc_fail, user_fault, proc_fail, roots, removes;
static void *memdup_user(const void *p, size_t n) {
	void *q;
	if (!p || user_fault) return ERR_PTR(-EFAULT);
	if (alloc_fail) return ERR_PTR(-ENOMEM);
	q=malloc(n); if (!q) return ERR_PTR(-ENOMEM); memcpy(q,p,n); return q;
}
static void *kcalloc(size_t n, size_t s, int flags) { return alloc_fail ? NULL : calloc(n,s); }
#define kfree free
static unsigned long copy_from_user(void *d,const void *s,size_t n) {
	if (!s || user_fault) return n; memcpy(d,s,n); return 0;
}
#define copy_to_user copy_from_user
static int signal_pending(void *task) { return 0; }
#define current NULL
static struct proc_dir_entry root;
static struct proc_dir_entry *proc_mkdir(const char *name, void *parent) {
	if (proc_fail==1) return NULL; roots++; return &root;
}
static struct proc_dir_entry *proc_create(const char *name, int mode, void *parent,
		const struct proc_ops *ops) { return proc_fail==2 ? NULL : &root; }
static void proc_remove(struct proc_dir_entry *p) { if(p) {roots--; removes++;} }
#include "../public/drivers/misc/mediatek/performance/perf_ioctl/ioctl_powerhal.c"
#include "../public/drivers/misc/mediatek/performance/powerhal_cpu_ctrl/adpf_session_snapshot.h"

static int calls, seen_count;
static long seen_timestamp, seen_duration;
static int create_session(unsigned int sid, unsigned int tgid, unsigned int uid,
		int *threads, int count, long duration) {
	calls++; seen_count=count; assert(threads[0]==123); assert(duration==1000); return -EIO;
}
static int report(unsigned int sid, struct _ADPF_WORK_DURATION *d, int count) {
	calls++; seen_count=count; seen_timestamp=d[0].timeStampNanos;
	seen_duration=d[0].durationNanos; return -ERANGE;
}
static int preferred(long long *rate) { *rate=777; return 0; }
static int sport(unsigned int mode) { return -EOPNOTSUPP; }

/* Exercise production wake-before-drain and admission logic with an explicitly
 * modelled grace period. This is not a real-kernel SRCU/module-unload test.
 */
static pthread_mutex_t wait_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wait_cond = PTHREAD_COND_INITIALIZER;
static bool reader_entered, reader_closed;
static int stop_calls, clear_calls;
static long reader_result;
static void blocked_boost(int *cmd, int *value) {
	pthread_mutex_lock(&wait_lock);
	reader_entered = true;
	pthread_cond_broadcast(&wait_cond);
	while (!reader_closed)
		pthread_cond_wait(&wait_cond, &wait_lock);
	*cmd = *value = -1;
	pthread_mutex_unlock(&wait_lock);
}
static void stop_readers(void) {
	pthread_mutex_lock(&wait_lock);
	stop_calls++;
	reader_closed = true;
	pthread_cond_broadcast(&wait_cond);
	pthread_mutex_unlock(&wait_lock);
}
static void clear_provider(void) {
	clear_calls++;
	powerhal_adpf_create_session_hint_fp = NULL;
	boost_get_cmd_fp = NULL;
}
static void *read_boost(void *unused) {
	struct _CPU_CTRL_PACKAGE p = {};
	(void)unused;
	reader_result = powerhal_device_ioctl(NULL, NOTIFY_BOOST, (unsigned long)&p);
	return NULL;
}
int main(void) {
	int threads[32]={123};
	struct _ADPF_PACKAGE p={.cmd=ADPF_CREATE_HINT_SESSION,.sid=1,.tgid=1,
		.threadIds=threads,.threadIds_size=1,.durationNanos=1000};
	struct _ADPF_WORK_DURATION d={99,55};
	struct _POWERHAL_PACKAGE mode={1};
	long r;
	pthread_t reader;
	struct _SESSION session={.threadIds_size=1,.work_duration_size=1};
	struct _SESSION_WORK_DURATION sample={101,202};
	struct adpf_session_snapshot snapshot;
	session.threadIds[0]=77; session.workDuration[0]=&sample;
	assert(adpf_copy_session(&snapshot,&session)==0);
	sample.durationNanos=999; session.threadIds[0]=88;
	assert(snapshot.session.threadIds[0]==77);
	assert(snapshot.session.workDuration[0]==&snapshot.duration[0]);
	assert(snapshot.session.workDuration[0]->durationNanos==202);
	assert(snapshot.session.workDuration[31]==&snapshot.duration[31]);
	session.work_duration_size=33; assert(adpf_copy_session(&snapshot,&session)==-EINVAL);
	session.work_duration_size=-1; assert(adpf_copy_session(&snapshot,&session)==-EINVAL);
	session.work_duration_size=1; session.workDuration[0]=NULL;
	assert(adpf_copy_session(&snapshot,&session)==-EINVAL);
	assert(sizeof(p)==56);
	assert(adpf_device_ioctl(NULL,POWERHAL_SET_ADPF_DATA,(unsigned long)&p)==-ESHUTDOWN);
	assert(powerhal_ioctl_provider_bind(NULL, clear_provider)==-EINVAL);
	assert(powerhal_ioctl_provider_bind(stop_readers, clear_provider)==0);
	assert(powerhal_ioctl_provider_bind(stop_readers, clear_provider)==-EBUSY);
	powerhal_ioctl_provider_publish();
	assert(adpf_device_ioctl(NULL,0,(unsigned long)&p)==-ENOTTY);
	user_fault=1; assert(adpf_device_ioctl(NULL,POWERHAL_SET_ADPF_DATA,(unsigned long)&p)==-EFAULT); user_fault=0;
	assert(adpf_device_ioctl(NULL,POWERHAL_SET_ADPF_DATA,(unsigned long)&p)==-EAGAIN);
	powerhal_adpf_create_session_hint_fp=create_session;
	p.threadIds_size=-1; assert(adpf_device_ioctl(NULL,POWERHAL_SET_ADPF_DATA,(unsigned long)&p)==-EINVAL);
	p.threadIds_size=33; assert(adpf_device_ioctl(NULL,POWERHAL_SET_ADPF_DATA,(unsigned long)&p)==-EINVAL);
	p.threadIds_size=1; alloc_fail=1;
	assert(adpf_device_ioctl(NULL,POWERHAL_SET_ADPF_DATA,(unsigned long)&p)==-ENOMEM); alloc_fail=0;
	assert(adpf_device_ioctl(NULL,POWERHAL_SET_ADPF_DATA,(unsigned long)&p)==-EIO); assert(calls==1 && seen_count==1);
	p.cmd=ADPF_REPORT_ACTUAL_WORK_DURATION; p.workDuration=&d; p.work_duration_size=1;
	powerhal_adpf_report_actual_work_duration_fp=report;
	assert(adpf_device_ioctl(NULL,POWERHAL_SET_ADPF_DATA,(unsigned long)&p)==-ERANGE);
	assert(seen_timestamp==99 && seen_duration==55);
	p.work_duration_size=33; assert(adpf_device_ioctl(NULL,POWERHAL_SET_ADPF_DATA,(unsigned long)&p)==-EINVAL);
	p.cmd=ADPF_GET_HINT_SESSION_PREFERED_RATE;
	assert(adpf_device_ioctl(NULL,POWERHAL_GET_ADPF_DATA,(unsigned long)&p)==-EAGAIN);
	powerhal_adpf_get_hint_session_preferred_rate_fp=preferred;
	assert(adpf_device_ioctl(NULL,POWERHAL_GET_ADPF_DATA,(unsigned long)&p)==0 && p.preferredRate==777);
	powerhal_dsu_sport_mode_fp=sport;
	assert(powerhal_device_ioctl(NULL,DSU_CCI_SPORT_MODE,(unsigned long)&mode)==-EOPNOTSUPP);

	/* Real 32-bit pointers and long fields, not a native-layout stand-in. */
	void *low=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
	assert(low!=MAP_FAILED && (uintptr_t)low<=UINT32_MAX-4096);
	struct powerhal_compat_adpf *cp=low;
	struct powerhal_compat_duration *cd=(void *)((char *)low+128);
	*cd=(struct powerhal_compat_duration){-7,19};
	*cp=(struct powerhal_compat_adpf){.cmd=ADPF_REPORT_ACTUAL_WORK_DURATION,
		.sid=1,.durations=(uintptr_t)cd,.duration_count=1};
	r=adpf_compat_ioctl(NULL,POWERHAL_COMPAT_SET,(uintptr_t)cp);
	assert(r==-ERANGE && seen_timestamp==-7 && seen_duration==19);
	cp->cmd=ADPF_GET_HINT_SESSION_PREFERED_RATE;
	assert(adpf_compat_ioctl(NULL,POWERHAL_COMPAT_GET,(uintptr_t)cp)==0 && cp->value.rate==777);
	munmap(low,4096);
	proc_fail=1; assert(powerhal_ioctl_init()==-ENOMEM && roots==0);
	proc_fail=2; assert(powerhal_ioctl_init()==-ENOMEM && roots==0 && removes==1);
	proc_fail=0; assert(powerhal_ioctl_init()==0 && roots==1);
	powerhal_ioctl_exit(); assert(roots==0 && removes==2);
	boost_get_cmd_fp = blocked_boost;
	assert(pthread_create(&reader, NULL, read_boost, NULL)==0);
	pthread_mutex_lock(&wait_lock);
	while (!reader_entered)
		pthread_cond_wait(&wait_cond, &wait_lock);
	pthread_mutex_unlock(&wait_lock);
	powerhal_ioctl_provider_revoke();
	assert(pthread_join(reader, NULL)==0);
	assert(reader_result==-ESHUTDOWN && stop_calls==1 && clear_calls==1);
	assert(!boost_get_cmd_fp && !powerhal_adpf_create_session_hint_fp);
	assert(adpf_device_ioctl(NULL,POWERHAL_SET_ADPF_DATA,(unsigned long)&p)==-ESHUTDOWN);
	powerhal_ioctl_provider_revoke();
	assert(stop_calls==1 && clear_calls==1);
	puts("PASS: production ADPF parser/proc rollback plus provider admission and wake-before-drain under host lease model (not kernel SRCU proof)");
	return 0;
}
