#!/usr/bin/env python3
"""Actual C2PS UAPI + ioctl body, host error-path model; NOT native ABI/runtime proof."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess

root = Path(__file__).resolve().parents[1]
p = root / 'public/drivers/misc/mediatek/c2ps_ioctl'
t = (p / 'c2ps_perf_ioctl.c').read_text()
h = (p / 'c2ps_perf_ioctl.h').read_text()

def function(signature):
    start = t.index(signature)
    i = t.index('{', start) + 1
    level = 1
    while level:
        level += (t[i] == '{') - (t[i] == '}')
        i += 1
    return t[start:i]

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('output', type=Path)
a = ap.parse_args()
a.output.mkdir(exist_ok=False)
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
typedef uint32_t u32;
#define CONFIG_MAX_NR_CPUS 8
#define IS_ENABLED(x) (x)
#define __user
#define EXPORT_SYMBOL_GPL(x)
#define READ_ONCE(x) __atomic_load_n(&(x), __ATOMIC_RELAXED)
#define smp_store_release(p,v) __atomic_store_n(p,v,__ATOMIC_RELEASE)
#define smp_load_acquire(p) __atomic_load_n(p,__ATOMIC_ACQUIRE)
struct file { int unused; };
struct inode { int unused; };
static int session_file_lock;
static struct file *activation_file;
static void mutex_lock(int *p) { assert(*p == 0); *p = 1; }
static void mutex_unlock(int *p) { assert(*p == 1); *p = 0; }
static int single_release(struct inode *i, struct file *f) { return 0; }
static bool callbacks_ready;
static unsigned long copy_from_user(void *to, const void *from, unsigned long size) {
    if (!from) return size;
    memcpy(to, from, size); return 0;
}
'''
# Preserve the real definitions/command encodings. Only kernel includes are
# omitted for host isolation, rather than inventing substitute request layouts.
uapi = re.sub(r'^#include[^\n]*\n', '', h, flags=re.M)
slots = t[t.index('int (*c2ps_notify_init_fp)'):t.index('struct proc_dir_entry *c2ps_ioctl_root')]
macros = t[t.index('#define C2PS_CALL'):t.index('static int device_show')]
body = function('static long device_ioctl(')
publish = function('void c2ps_publish_callbacks(void)')
release = function('static int device_release(')
suffix = r'''
static int calls;
static int fail_init(int a, int b, int c, int d) { calls++; return -ENOMEM; }
static int fail_vsync(void) { calls++; return -EAGAIN; }
static int success_init(int a, int b, int c, int d) { return 0; }
static int close_calls;
static int close_session(void) { close_calls++; return 0; }
static int check_task(u32 a, u32 b, u32 c, int d, u32 e, bool f, bool g, const char *name) {
    assert(name[MAX_TASK_NAME_SIZE-1] == 0); calls++; return -ENOSPC;
}
int main(void) {
    C2PS_INIT_PARAM init = { .camfps = 30000, .max_uclamp_cluster0 = 500 };
    C2PS_TASK_INIT_PARAMS task = {0};
    unsigned long arg = (unsigned long)&init;
    if (!CONFIG_MTK_C2PS) {
        assert(device_ioctl(0,C2PS_ACTIVATE,arg) == -EOPNOTSUPP);
        puts("PASS: disabled native feature returns EOPNOTSUPP, not success");
        return 0;
    }
    assert(device_ioctl(0,C2PS_ACTIVATE,arg) == -ENODEV);
    c2ps_publish_callbacks();
    assert(device_ioctl(0,C2PS_ACTIVATE,arg) == -ENODEV);
    c2ps_notify_init_fp = fail_init;
    assert(device_ioctl(0,C2PS_ACTIVATE,0) == -EFAULT);
    assert(device_ioctl(0,C2PS_ACTIVATE,arg) == -ENOMEM && calls == 1);
    init.max_uclamp_cluster0 = 1025;
    assert(device_ioctl(0,C2PS_ACTIVATE,arg) == -EINVAL && calls == 1);
    c2ps_notify_vsync_fp = fail_vsync;
    assert(device_ioctl(0,C2PS_NOTIFY_VSYNC,0) == -EAGAIN && calls == 2);
    memset(task.task_name, 'x', sizeof(task.task_name));
    c2ps_notify_add_task_fp = check_task;
    assert(device_ioctl(0,C2PS_ADD_TASK,(unsigned long)&task) == -ENOSPC && calls == 3);
    assert(device_ioctl(0,0,0) == -ENOTTY);
    struct file owner, sibling;
    init.max_uclamp_cluster0 = 500;
    c2ps_notify_init_fp = success_init;
    c2ps_notify_uninit_fp = close_session;
    assert(device_ioctl(&owner,C2PS_ACTIVATE,arg) == 0);
    assert(activation_file == &owner);
    assert(device_release(0,&sibling) == 0 && close_calls == 0);
    assert(device_release(0,&owner) == 0 && close_calls == 1 && !activation_file);
    assert(device_release(0,&owner) == 0 && close_calls == 1);
    puts("PASS: actual C2PS ioctl copy/errors/termination and activating-file close");
}
'''
c = a.output / 'c2ps_ioctl.c'
c.write_text(prefix + uapi + slots + macros + publish + body + release + suffix)
results = []
for feature in (0, 1):
    out = a.output / ('ioctl-' + str(feature))
    cmd = [os.environ.get('CC', 'clang'), '-std=gnu11', '-O1', '-g',
           '-fsanitize=address,undefined', '-DCONFIG_MTK_C2PS=' + str(feature), str(c), '-o', str(out)]
    subprocess.run(cmd, check=True)
    subprocess.run([str(out)], check=True)
    results.append({'native_feature_in_host_model': feature, 'result': 'PASS'})
(a.output / 'RESULT.json').write_text(json.dumps({
    'scope': __doc__, 'body_sha256': hashlib.sha256(body.encode()).hexdigest(),
    'uapi_source_sha256': hashlib.sha256(h.encode()).hexdigest(),
    'cases': results, 'native_kernel_compile': False, 'native_concurrency_proof': False
}, indent=2) + '\n')
