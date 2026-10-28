#!/usr/bin/env python3
"""Build the committed dev20 overlay in the explicitly authorized Codex sandbox.

Engineering only. No Image/vmlinux executable/boot artifact, device access, or
owner-worktree writes. Use --resume for the existing authenticated sandbox.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tarfile

SOURCE = Path(__file__).resolve().parents[2]
KERNEL = Path('/home/<user>/kernel-work/noOEM09/dk12/src')
PIN = 'f3f4cb91171896b13d40ad40290d641a0c350832'
BASE = Path('/mnt/<reference-ssd>/lieppos-builds/c20-dev20/out')
AUTHORIZED = Path('/mnt/<reference-ssd>/lieppos-builds/codex-fpsgo-dev20-continuation')
TC = Path('/home/<user>/kernel-work/platform-batch-04-cleancheck/ws/prebuilts/clang/host/linux-x86/clang-r487747c/bin')
PATCHES = ['native-owner-overlay.patch', 'native-control-lease.patch',
           'scheduler-uclamp-lease.patch', 'native-adpf-contract.patch',
           'native-cpuqos-task-lease.patch', 'native-adpf-ratio-lease.patch',
           'native-adpf-session-lease.patch']
SELECTED = {'scheduler', 'cpufreq_sugov_ext', 'mtk_core_ctl', 'mtk_perf_ioctl',
            'mtk_perf_ioctl_magt', 'mtk_ioctl_powerhal', 'powerhal_cpu_ctrl',
            'mtk_fpsgo', 'fpsgo', 'mtk_c2ps', 'c2ps_perf_ioctl', 'cpuqos_v3'}
BASE_HASHES = {
    '.config': '343f058c395fe15b4862ab2c2d8b81e8ecba8a7edeaacaf392985025aa20385d',
    'Module.symvers': 'eb1007d74c2b7480731dcebc3f2811d9536d66fd89c48ea14c1807cae67e1fd7',
    'include/generated/utsrelease.h': '370291ac07e143f3c7d54fdb8cd4ed82a0a1dc456b930dc9c7c7dd937bb42826',
    'include/generated/compile.h': '27e37a70f1e1ae06c8913327c3a1492c6fb4ac57cd741a04a23580a0c8c04a9a',
}
ENV = dict(os.environ, GIT_CONFIG_GLOBAL='/dev/null', GIT_OPTIONAL_LOCKS='0')

def digest(p):
    h = hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda: f.read(1024 * 1024), b''):
            h.update(b)
    return h.hexdigest()

def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], env=ENV, text=True).strip()

def archive(root, revision, dest):
    dest.mkdir(parents=True, exist_ok=True)
    proc = subprocess.Popen(['git', '-C', str(root), 'archive', revision], env=ENV, stdout=subprocess.PIPE)
    try:
        with tarfile.open(fileobj=proc.stdout, mode='r|') as stream:
            for member in stream:
                stream.extract(member, dest, filter='data')
    finally:
        proc.stdout.close()
    if proc.wait():
        raise RuntimeError('git archive failed')

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, default=AUTHORIZED)
    ap.add_argument('--resume', action='store_true')
    a = ap.parse_args()
    out = a.output.absolute()
    if out != AUTHORIZED or out.resolve() != AUTHORIZED:
        ap.error('only the specifically user-authorized distinct Codex output is allowed')
    for root in [SOURCE, KERNEL]:
        meta = root / '.git'
        assert meta.is_dir() and meta.resolve() == meta
        assert not (meta / 'commondir').exists() and not (meta / 'objects/info/alternates').exists()
    assert not git(SOURCE, 'status', '--porcelain'), 'commit source before building'
    revision = git(SOURCE, 'rev-parse', 'HEAD')
    assert git(KERNEL, 'rev-parse', PIN+'^{commit}') == PIN
    before = {n: digest(BASE/n) for n in BASE_HASHES}
    assert before == BASE_HASHES
    usage = subprocess.check_output(['btrfs', 'filesystem', 'usage', '-b', str(SOURCE)], text=True)
    m = re.search(r'Metadata,DUP: Size:(\d+), Used:(\d+)', usage)
    metadata = int(m[1])-int(m[2])
    stats = subprocess.check_output(['btrfs', 'device', 'stats', str(SOURCE)], text=True)
    assert all(int(line.split()[-1]) == 0 for line in stats.splitlines())
    # No Btrfs-heavy build: all source snapshots/objects are on ext4. Reserve
    # 128 MiB local lightweight receipts above the absolute metadata floor;
    # 48 GiB projected external peak plus the 25 GiB free-space floor.
    assert metadata >= 4*2**30+128*2**20
    assert shutil.disk_usage(SOURCE).free >= 25*2**30+128*2**20
    assert shutil.disk_usage(out.parent).free >= 73*2**30
    assert subprocess.check_output(['findmnt','-T',str(out.parent),'-n','-o','FSTYPE'],text=True).strip() == 'ext4'
    tree, kout, group = out/'kernel', out/'out', out/'group'
    cohort = tree/'drivers/misc/mediatek/performance/lieppos14'
    if a.resume:
        assert json.loads((out/'PREPARE.json').read_text())['kernel_commit'] == PIN
        # Existing private native edits must be exactly the reviewed overlays.
        # No automatic reset/rebase, no touching the owner's active worktree.
        for patch in PATCHES:
            subprocess.run(['git','-C',str(tree),'apply','--reverse','--check',
                            str(SOURCE/'integration/target-c'/patch)],env=ENV,check=True)
    else:
        out.mkdir(exist_ok=False)
        archive(KERNEL, PIN, tree)
        archive(SOURCE, revision, cohort)
        for patch in PATCHES:
            subprocess.run(['git','-C',str(tree),'apply',str(cohort/'integration/target-c'/patch)],env=ENV,check=True)
        kout.mkdir()
        for name in ['.config','Module.symvers','include','scripts','arch/arm64/include']:
            dest = kout/name
            dest.parent.mkdir(parents=True,exist_ok=True)
            if (BASE/name).is_dir():
                shutil.copytree(BASE/name,dest,symlinks=True)
            else:
                shutil.copy2(BASE/name,dest)
        (kout/'Makefile').write_text('include '+str(tree/'Makefile')+'\n')
        group.mkdir()
        (group/'performance').symlink_to(cohort, target_is_directory=True)
        (group/'native').symlink_to(tree/'drivers/misc/mediatek/sched', target_is_directory=True)
    cpuqos = tree/'drivers/misc/mediatek/cache-auditor/cpuqos_v3'
    if not (group/'cpuqos').is_symlink():
        assert not (group/'cpuqos').exists()
        (group/'cpuqos').symlink_to(cpuqos, target_is_directory=True)
    assert (group/'cpuqos').resolve() == cpuqos
    (group/'Kbuild').write_text('DEVICE_MODULES_PATH := $(srctree)\nexport DEVICE_MODULES_PATH\nobj-m += performance/ native/ cpuqos/\n')
    archive(SOURCE, revision, cohort)
    assert (group/'performance').resolve() == cohort and (group/'native').resolve() == tree/'drivers/misc/mediatek/sched'
    (kout/'.config').write_text((BASE/'.config').read_text()+'\n'+(SOURCE/'integration/target-c/performance.config').read_text())
    make = ['make','-C',str(tree),'O='+str(kout),'ARCH=arm64','LLVM='+str(TC)+'/',
            'LLVM_IAS=1','LOCALVERSION=+','DEVICE_MODULES_PATH='+str(tree),'-j8','KBUILD_MODPOST_WARN=']
    receipt = dict(kernel_commit=PIN, source_commit=revision, baseline_inputs=before,
                   compiler_sha256=digest(TC/'clang'), linker_sha256=digest(TC/'ld.lld'),
                   patches={p:digest(SOURCE/'integration/target-c'/p) for p in PATCHES},
                   metadata_headroom_bytes=metadata, commands=[], acceptance='HOLD',
                   hardware_validation=0, runtime_validation=False, deployable=False)
    def run(cmd, label, cwd=out):
        with (out/(label+'.log')).open('w') as log:
            proc = subprocess.run(cmd, cwd=cwd, env=ENV, stdout=log, stderr=subprocess.STDOUT)
        receipt['commands'].append(dict(label=label, argv=cmd, rc=proc.returncode))
        (out/'BUILD.json').write_text(json.dumps(receipt,indent=2)+'\n')
        if proc.returncode:
            raise SystemExit('build failed: '+label+'; see '+str(out/(label+'.log')))
    for target in ['olddefconfig','modules_prepare','vmlinux_o']:
        run(make+[target], 'recipe-'+target)
    parse = lambda p: dict(re.findall(r'^(CONFIG_\w+)=(.*)$',p.read_text(),re.M))
    base_config, config = parse(BASE/'.config'), parse(kout/'.config')
    delta = {k:[base_config.get(k),config.get(k)] for k in base_config.keys()|config.keys() if base_config.get(k)!=config.get(k)}
    assert set(delta) == {'CONFIG_LIEPPOS_PERFORMANCE','CONFIG_LIEPPOS_PERFORMANCE_TARGET_C','CONFIG_LIEPPOS_C2PS','CONFIG_MTK_PERF_CTRL_LEASE'}
    assert config['CONFIG_NONLINEAR_FREQ_CTL'] == 'y'
    # Kbuild's native genksyms .cmd files are the ONLY CRC source. The linked
    # core is a genuine relocatable build, never a minimal fake vmlinux object.
    link = (kout/'.vmlinux.o.cmd').read_text().split('--start-group',1)[1].split('--end-group',1)[0]
    archives = ['vmlinux.a']+shlex.split(link)
    (kout/'.vmlinux.objs').write_text(''.join(subprocess.check_output([str(TC/'llvm-ar'),'t',p],cwd=kout,text=True) for p in archives))
    run(['scripts/mod/modpost','-m','-E','-o','vmlinux-strict.symvers','vmlinux.o'],'recipe-core-modpost',kout)
    base_rows = (BASE/'Module.symvers').read_text().splitlines()
    rows = [l for l in base_rows if l.split()[2]!='vmlinux' and Path(l.split()[2]).name not in SELECTED]
    (kout/'Module.symvers').write_text((kout/'vmlinux-strict.symvers').read_text()+'\n'.join(rows)+'\n')
    receipt['provider_input_sha256'] = digest(kout/'Module.symvers')
    run(make+['M='+str(group),'modules'],'recipe-modules')
    modules = [Path(l) for l in (group/'modules.order').read_text().splitlines()]
    (group/'objects.order').write_text(''.join(str(p.with_suffix('.o'))+'\n' for p in modules))
    # GNU make on this host accidentally adds -n to its ordinary modpost rule
    # from MAKEFLAGS assignment text. Require an explicit full strict rerun:
    # no -n (ignore missing files), -w, KBUILD_MODPOST_WARN or fake inputs.
    run(['scripts/mod/modpost','-m','-E','-e','-i','Module.symvers','-o',str(group/'strict.symvers'),'-T',str(group/'objects.order')],'recipe-strict-modpost',kout)
    assert (group/'strict.symvers').read_bytes() == (group/'Module.symvers').read_bytes()
    exported = (group/'strict.symvers').read_text().splitlines()
    assert len({l.split()[1] for l in exported}) == len(exported)
    symbols = {l.split()[1]:l.split()[0] for l in (kout/'Module.symvers').read_text().splitlines()+exported}
    info=[]
    for p in modules:
        entry={k:subprocess.check_output(['modinfo','-F',k,str(p)],text=True).strip() for k in ['name','vermagic','depends']}
        assert entry['vermagic'].startswith('6.1.115-mt6878-gq5012bf1-dev20+ ')
        versions=subprocess.check_output(['modprobe','--dump-modversions',str(p)],text=True).splitlines()
        assert all(symbols.get(l.split()[1])==l.split()[0] for l in versions)
        info.append(dict(entry,path=str(p),sha256=digest(p),versioned_imports=len(versions)))
    assert before == {n:digest(BASE/n) for n in before}
    assert not (kout/'vmlinux').exists() and not (kout/'arch/arm64/boot/Image').exists()
    receipt.update(effective_config_sha256=digest(kout/'.config'),config_delta=delta,
                   generated_autoconf_sha256=digest(kout/'include/generated/autoconf.h'),
                   kernel_core_symvers_sha256=digest(kout/'vmlinux-strict.symvers'),
                   source_group_symvers_sha256=digest(group/'strict.symvers'),
                   versioned_imports=sum(m['versioned_imports'] for m in info),modules=info,
                   baseline_inputs_unchanged=True, strict_modpost=True)
    (out/'BUILD.json').write_text(json.dumps(receipt,indent=2)+'\n')
    (out/'PREPARE.json').write_text(json.dumps(dict(kernel_commit=PIN,source_commit=revision,baseline_inputs=before),indent=2)+'\n')
    print(json.dumps(dict(source_commit=revision,modules=len(info),strict_modpost=True,receipt=str(out/'BUILD.json'),acceptance='HOLD')))

if __name__ == '__main__':
    main()
