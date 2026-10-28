#!/usr/bin/env python3
"""Run actual FBT idle-accounting C; hotplug/counter services are modeled."""
import argparse
import os
from pathlib import Path
import re
import subprocess
root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=Path)
a = p.parse_args()
a.output.mkdir(exist_ok=False)
t = (root/'public/drivers/misc/mediatek/performance/fpsgo_v3/fbt/src/fbt_cpu_ctrl.c').read_text()
m = re.search(r'^static int update_cpu_loading_locked\(void\)\n\{', t, re.M)
assert m
end, depth = m.end(), 1
while depth:
    depth += (t[end] == '{') - (t[end] == '}')
    end += 1
prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
typedef uint64_t u64;
#define DEBUG_LOG 0
#define for_each_possible_cpu(i) for((i)=0;(i)<2;(i)++)
#define __cpu_ctrl_systrace(...) ((void)0)
struct cpu_info {u64 time;};
static struct cpu_info cw[2],ci[2],pw[2],pi[2];
static struct cpu_info *cur_wall_time=cw,*cur_idle_time=ci,*prev_wall_time=pw,*prev_idle_time=pi;
static bool online[2]={true,true};
static u64 walls[2],idles[2];
static bool cpu_active(int i) {return online[i];}
static u64 get_cpu_idle_time(int i,u64 *wall,int iowait) {*wall=walls[i];return idles[i];}
static u64 mul_u64_u64_div_u64(u64 a,u64 b,u64 c) {return (unsigned __int128)a*b/c;}
static int cfp_cur_loading;
'''
suffix = r'''
int main(void) {
 walls[0]=walls[1]=100;idles[0]=idles[1]=20;
 update_cpu_loading_locked();assert(!cfp_cur_loading);
 walls[0]=walls[1]=200;idles[0]=idles[1]=40;
 update_cpu_loading_locked();assert(cfp_cur_loading==80);
 online[1]=false;walls[0]=300;idles[0]=60;
 update_cpu_loading_locked();assert(cfp_cur_loading==80 && !cw[1].time && !ci[1].time);
 online[1]=true;walls[0]=400;idles[0]=80;walls[1]=5;idles[1]=1;
 update_cpu_loading_locked();assert(cfp_cur_loading==80 && cw[1].time==5);
 idles[0]=UINT64_MAX;walls[1]=105;idles[1]=51;
 update_cpu_loading_locked();assert(cfp_cur_loading==50 && !cw[0].time && !ci[0].time);
 online[0]=false;walls[1]=1;idles[1]=0;
 update_cpu_loading_locked();assert(!cfp_cur_loading); /* reset */
 walls[1]=101;idles[1]=200;
 update_cpu_loading_locked();assert(!cfp_cur_loading); /* impossible idle delta */
 walls[1]=UINT64_MAX-1;idles[1]=201;
 update_cpu_loading_locked();assert(cfp_cur_loading==99); /* no 100*busy overflow */
 puts("PASS actual FBT idle sampling: offline/online rebaseline, unsupported accounting, counter reset, invalid deltas and overflow-safe percentage");
}
'''
c=a.output/'idle-hotplug.c'
c.write_text(prefix+t[m.start():end]+suffix)
exe=a.output/'idle-hotplug'
subprocess.run([os.environ.get('CC','clang'),'-std=gnu11','-O1','-g',
    '-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
