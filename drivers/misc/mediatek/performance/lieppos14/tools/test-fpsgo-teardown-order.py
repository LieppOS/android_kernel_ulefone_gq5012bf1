#!/usr/bin/env python3
"""Assert source-level FPSGO rollback/teardown dependency ordering.

This reads actual production functions and checks publication, producer closure,
callback withdrawal, work draining and child destruction order. It is a static
lifecycle witness, not a native concurrency proof.
"""
from pathlib import Path
import re

root=Path(__file__).resolve().parents[1]
v3=root/'public/drivers/misc/mediatek/performance/fpsgo_v3'

def body(path,name):
    text=path.read_text()
    m=re.search(r'^(?:static )?(?:int|void)\s+(?:__init\s+)?'+name+r'\([^)]*\)\n\{',text,re.M)
    assert m,name
    i=m.end();depth=1
    while depth:
        depth+=(text[i]=='{')-(text[i]=='}');i+=1
    return text[m.start():i]

def ordered(text,*needles):
    pos=-1
    for needle in needles:
        nxt=text.find(needle,pos+1)
        assert nxt>=0,(needle,needles)
        assert nxt>pos,(needle,pos,nxt)
        pos=nxt

main=body(v3/'fpsgo_main.c','fpsgo_init')
fbt=body(v3/'fbt/src/fbt_cpu.c','fbt_cpu_exit')
fbt_init=body(v3/'fbt/src/fbt_cpu.c','fbt_cpu_init')
xgf=body(v3/'fbt/src/xgf.c','exit_xgf')
composer=body(v3/'composer/src/fps_composer.c','fpsgo_composer_exit')
fstb=body(v3/'fstb/fstb.c','mtk_fstb_exit')
common=body(v3/'base/src/fpsgo_base.c','exit_fpsgo_common')

# No public producer starts until all fallible child construction has committed.
ordered(main,'FPSGO_INIT_CHILD(fbt, fbt_cpu_init())','FPSGO_INIT_CHILD(gbe, init_gbe_common())',
        'smp_store_release(&fpsgo_runtime, FPSGO_READY)','fbt_cpu_publish()',
        'gbe_publish_callbacks()','fpsgo_composer_start()','perf_ioctl_dispatch_publish()')
# Rollback first revokes ingress/worker, then unwinds callback dependencies.
ordered(main,'rollback:','perf_ioctl_dispatch_revoke()','fpsgo_sysfs_quiesce()',
        'adpf_unregister_callback','fpsgo_stop_worker()','exit_gbe_common()',
        'fbt_cpu_exit()','fpsgo_composer_exit()','mtk_fstb_exit()',
        'exit_fpsgo_common()')
# Target C owns native EAS callback publication only after native dependency
# LIVE; it joins SRCU and drains EAS sessions on every reversible failure.
ordered(main,'fpsgo_sysfs_prepare_commit()',
        'adpf_register_callback(fpsgo_notify_native_adpf)',
        'register_fpsgo_android_cpufreq_transition_hook()',
        'smp_store_release(&fpsgo_runtime, FPSGO_READY)')
ordered(main,'rollback:','adpf_unregister_callback(fpsgo_eas_slot)',
        'eas_adpf_release_sessions()', 'adpf_unregister_callback(fpsgo_adpf_slot)',
        'fpsgo_stop_worker()')
for name in ['fpsgo_notify_adpf_hint','fpsgo_notify_native_adpf']:
    callback=body(v3/'fpsgo_main.c',name)
    assert 'smp_load_acquire(&fpsgo_runtime) != FPSGO_READY' in callback
    assert 'session->cmd != ADPF_CLOSE' in callback
# A failed FBT constructor owns and unwinds its partially committed children.
ordered(fbt_init,'fail:','fbt_cpu_exit()','return ret;')
# Reclaim joins per-render jerk work before XGF/caches/workqueue/platform teardown.
ordered(fbt,'fpsgo_drain_render_reclaims()','fbt_cpu_ctrl_exit()',
        'minitop_exit()','exit_xgf()','cancel_work_sync(&sjerk.work)',
        'cancel_work_sync(&llf_switch_policy_work)','destroy_workqueue(wq_jerk)',
        'fbt_cpu_ux_exit()')
# XGF withdraws trace producers and its composer callback before composer closes.
ordered(xgf,'clean_xgf_tp()','xgf_reset_all_renders()',
        'unregister_get_fpsgo_is_boosting','fpsgo_sysfs_remove_dir')
# Each producer closes admission before joining work and destroying its queue.
ordered(composer,'fpsgo_composer_close_callbacks()','composer_stopping, true',
        'hrtimer_cancel(&recycle_hrt)','cancel_work_sync(&do_recycle_work)',
        'destroy_workqueue(composer_wq)')
ordered(fstb,'fpsgo_fstb_close_readers()','fstb_stopping, true',
        'disable_fstb_timer()','cancel_work_sync(&fps_stats_work)',
        'destroy_workqueue(wq)')
ordered(common,'fpsgo_base_close_readers()','destroy_workqueue(render_reclaim_wq)')

print('PASS actual FPSGO lifecycle source order: publication follows commit; rollback closes ingress, drains render/jerk work, withdraws XGF/composer callbacks, then destroys FBT, composer, FSTB and common providers in dependency order.')
