// SPDX-License-Identifier: GPL-2.0
/* Execute real source, substitute only kernel-facing policy/QoS services.
 * This checks ownership/error/state logic, not real scheduler timing or R5.
 */
#include <stdio.h>
#include "../public/drivers/misc/mediatek/performance/powerhal_cpu_ctrl/powerhal_policy.c"
static void reset_model(void){
 assert(!registered&&active_requests==0);
 model[0]=(struct cpufreq_policy){.cpu=0,.mask=(1ULL<<0)|(1ULL<<2),
  .cpuinfo={100,1000},.constraints={77,888},.present=true};
 model[1]=(struct cpufreq_policy){.cpu=7,.mask=1ULL<<7,
  .cpuinfo={200,2000},.constraints={88,1777},.present=true};
 for(int i=0;i<2;i++)model[i].related_cpus=&model[i].mask;
}
int main(void){
 struct powerhal_policy_state state;
 reset_model();
 fail_add_after=1; /* Fail min request after max attached; complete unwind. */
 assert(powerhal_policy_init()==-ENOMEM);
 assert(!registered&&active_requests==0&&model[0].refs==0&&model[1].refs==0);
 reset_model(); assert(powerhal_policy_init()==0);
 assert(powerhal_policy_count()==2&&active_requests==4);
 assert(model[0].refs==1&&model[1].refs==1); /* sparse CPUs, shared domain */
 assert(powerhal_policy_read(-1,&state)==-ENODEV);
 assert(powerhal_policy_set(0,900,200)==-EINVAL);
 assert(powerhal_policy_set(0,250,700)==0);
 assert(powerhal_policy_read(0,&state)==0&&state.min==250&&state.max==700);
 assert(model[0].constraints.external_min==77&&model[0].constraints.external_max==888);
 fail_update_after=1; /* max succeeds, min fails, max restored. */
 assert(powerhal_policy_set(0,300,800)==-EIO);
 assert(policies[0].min_request.value==250&&policies[0].max_request.value==700);
 registered->notifier_call(registered,CPUFREQ_REMOVE_POLICY,&model[0]);
 model[0].present=false;
 assert(model[0].refs==0&&active_requests==2);
 assert(powerhal_policy_set(0,200,600)==-ENODEV);
 /* Same policy domain recreated with smaller hardware range. */
 model[0].cpuinfo.max_freq=500;model[0].present=true;
 registered->notifier_call(registered,CPUFREQ_CREATE_POLICY,&model[0]);
 assert(model[0].refs==1&&active_requests==4);
 assert(powerhal_policy_read(0,&state)==0&&state.min==250&&state.max==500);
 assert(powerhal_policy_set(0,-1,-1)==0);
 assert(powerhal_policy_read(0,&state)==0&&state.min==0&&state.max==INT_MAX);
 powerhal_policy_exit();assert(active_requests==0&&!registered);
 assert(model[0].refs==0&&model[1].refs==0);
 reset_model();model[0].present=model[1].present=false;
 assert(powerhal_policy_init()==-EPROBE_DEFER&&active_requests==0&&!registered);
 puts("PASS: production policy refs, sparse domains, QoS unwind/rollback, remove/recreate, limits and owner isolation");
 return 0;
}
