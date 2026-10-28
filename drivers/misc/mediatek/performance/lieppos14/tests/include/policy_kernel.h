/* SPDX-License-Identifier: GPL-2.0 */
/* Minimal deterministic API model for executing the production policy TU. */
#ifndef POLICY_KERNEL_TEST_H
#define POLICY_KERNEL_TEST_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#define EPROBE_DEFER 517
#define FREQ_QOS_MIN_DEFAULT_VALUE 0
#define FREQ_QOS_MAX_DEFAULT_VALUE INT_MAX
#define FREQ_QOS_MIN 1
#define FREQ_QOS_MAX 2
#define CPUFREQ_POLICY_NOTIFIER 0
#define CPUFREQ_CREATE_POLICY 1
#define CPUFREQ_REMOVE_POLICY 2
#define NOTIFY_OK 1
#define DEFINE_MUTEX(x) pthread_mutex_t x=PTHREAD_MUTEX_INITIALIZER
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
#define lockdep_assert_held(x) ((void)0)
#define pr_warn_ratelimited(...) ((void)0)
#define pr_err(...) ((void)0)
#define clamp_t(t,x,lo,hi) ((t)(x)<(t)(lo)?(t)(lo):(t)(x)>(t)(hi)?(t)(hi):(t)(x))
typedef uint64_t cpumask_t;
struct freq_constraints {int external_min,external_max;};
struct cpufreq_policy {unsigned int cpu; cpumask_t mask,*related_cpus;
 struct {unsigned int min_freq,max_freq;} cpuinfo;
 struct freq_constraints constraints; int refs; bool present;};
struct freq_qos_request {struct freq_constraints *qos; int type,value; bool active;};
struct notifier_block {int (*notifier_call)(struct notifier_block *,unsigned long,void *);};
static struct cpufreq_policy model[2];
static struct notifier_block *registered;
static int fail_add_after=-1,fail_update_after=-1,active_requests;
static cpumask_t possible_mask=(1ULL<<0)|(1ULL<<2)|(1ULL<<7);
#define for_each_possible_cpu(cpu) for((cpu)=0;(cpu)<8;(cpu)++) if(possible_mask&(1ULL<<(cpu)))
static bool cpumask_equal(const cpumask_t *a,const cpumask_t *b){return *a==*b;}
static void cpumask_copy(cpumask_t *a,const cpumask_t *b){*a=*b;}
static void cpus_read_lock(void){}
static void cpus_read_unlock(void){}
static struct cpufreq_policy *cpufreq_cpu_get(unsigned int cpu){
 for(int i=0;i<2;i++) if(model[i].present&&(model[i].mask&(1ULL<<cpu))){model[i].refs++;return &model[i];}
 return NULL;
}
static void cpufreq_cpu_put(struct cpufreq_policy *p){assert(p->refs>0);p->refs--;}
static int cpufreq_register_notifier(struct notifier_block *nb,int kind){assert(!registered);registered=nb;return 0;}
static int cpufreq_unregister_notifier(struct notifier_block *nb,int kind){assert(registered==nb);registered=NULL;return 0;}
static bool freq_qos_request_active(struct freq_qos_request *r){return r->active;}
static int freq_qos_add_request(struct freq_constraints *q,struct freq_qos_request *r,int type,int value){
 if(fail_add_after==0){fail_add_after=-1;return -ENOMEM;}
 if(fail_add_after>0)fail_add_after--;
 assert(!r->active);*r=(struct freq_qos_request){q,type,value,true};active_requests++;return 1;
}
static int freq_qos_remove_request(struct freq_qos_request *r){assert(r->active);r->active=false;active_requests--;return 1;}
static int freq_qos_update_request(struct freq_qos_request *r,int value){
 assert(r->active);
 if(fail_update_after==0){fail_update_after=-1;return -EIO;}
 if(fail_update_after>0)fail_update_after--;
 r->value=value;return 1;
}
#endif
