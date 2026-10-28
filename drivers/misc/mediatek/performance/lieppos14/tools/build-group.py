#!/usr/bin/env python3
"""Build this committed performance group using an authenticated prepared target.
Consumes kernel/workspace facilities; does not create or modify kernel recipes.
"""
import argparse, hashlib, json, os, subprocess, sys, tarfile, time
from pathlib import Path

def digest(p):
    h = hashlib.sha256()
    with Path(p).open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()

def git_head(root):
    git = root / '.git'
    if not git.is_dir() or (git / 'commondir').exists() or (git / 'objects/info/alternates').exists():
        raise SystemExit(f'require standalone authenticated repository, not linked metadata: {root}')
    env = dict(os.environ, GIT_CONFIG_GLOBAL='/dev/null', GIT_OPTIONAL_LOCKS='0')
    return subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], env=env, text=True).strip()

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--kernel', type=Path, required=True)
    ap.add_argument('--prepared', type=Path, required=True)
    ap.add_argument('--toolchain', type=Path, required=True, help='clang bin directory')
    ap.add_argument('--output', type=Path, required=True, help='must not exist')
    ap.add_argument('--gate', type=Path, required=True, help='existing infrastructure storage gate')
    ap.add_argument('--jobs', type=int, default=8)
    a = ap.parse_args()
    root = Path(__file__).resolve().parents[1]
    for p in (root, a.kernel, a.prepared, a.toolchain, a.output, a.gate):
        if str(p.absolute()).startswith('/home/<user>/<protected-tree>/'):
            raise SystemExit('protected input/output forbidden')
    source_commit, kernel_commit = git_head(root), git_head(a.kernel)
    env = dict(os.environ, GIT_CONFIG_GLOBAL='/dev/null', GIT_OPTIONAL_LOCKS='0')
    subprocess.run(['git', '-C', str(root), 'diff', '--exit-code', 'HEAD', '--'], env=env, check=True)
    gate = subprocess.run([str(a.gate)], capture_output=True, text=True)
    if gate.returncode or not gate.stdout.strip().endswith('PASS'):
        raise SystemExit('resource gate failed: ' + gate.stdout + gate.stderr)
    out = a.output.absolute()
    out.mkdir(parents=True, exist_ok=False)
    kout, tree = out / 'kout', out / 'tree'
    kout.mkdir(); tree.mkdir()
    # Generated module-build inputs only; no vmlinux/GKI clone duplication.
    inputs = ['Makefile', '.config', 'Module.symvers', 'include', 'scripts', 'arch/arm64/include']
    before = {n: digest(a.prepared / n) for n in ('.config', 'Module.symvers', 'include/generated/utsrelease.h')}
    for n in inputs:
        dest = kout / n
        dest.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['cp', '-a', '--reflink=always', str(a.prepared / n), str(dest)], check=True)
    archive = out / 'source.tar'
    with archive.open('wb') as f:
        subprocess.run(['git', '-C', str(root), 'archive', source_commit], env=env, stdout=f, check=True)
    subprocess.run(['tar', '-C', str(tree), '-xf', str(archive)], check=True)
    flags = f'-faddrsig -ffile-prefix-map={tree}=/usr/src/lieppos-performance -fdebug-prefix-map={out}=/usr/src/lieppos-build'
    args = ['make', '-C', str(a.kernel.absolute()), f'O={kout}', f'M={tree}', 'ARCH=arm64',
            f'LLVM={a.toolchain.absolute()}/', 'LLVM_IAS=1', f'-j{max(1, min(a.jobs, 16))}',
            'CONFIG_LIEPPOS_PERFORMANCE=m', 'CONFIG_MTK_FPSGO_V3=m',
            'CONFIG_MTK_PERFORMANCE_MODULE=m', 'KBUILD_MODPOST_WARN=', 'KBUILD_EXTRA_SYMBOLS=',
            f'KCFLAGS={flags}', 'KBUILD_BUILD_USER=lieppos', 'KBUILD_BUILD_HOST=offline', 'modules']
    start = time.time()
    with (out / 'build.log').open('w') as log:
        result = subprocess.run(['nice', '-n', '15', 'ionice', '-c', '3'] + args, env=env, stdout=log, stderr=subprocess.STDOUT)
    after = {n: digest(a.prepared / n) for n in before}
    report = {'schema': 'lieppos.performance-build/1', 'source_commit': source_commit,
              'kernel_commit': kernel_commit, 'source_archive_sha256': digest(archive),
              'config_sha256': before['.config'], 'kernel_symvers_sha256': before['Module.symvers'],
              'prepared_inputs_unchanged': before == after, 'storage_gate': gate.stdout.strip(),
              'compiler_sha256': digest(a.toolchain / 'clang'), 'linker_sha256': digest(a.toolchain / 'ld.lld'),
              'release': (kout / 'include/config/kernel.release').read_text().strip(),
              'argv': args, 'rc': result.returncode, 'seconds': time.time() - start,
              'acceptance': 'HOLD', 'deployable': False,
              'modules': {str(p.relative_to(tree)): digest(p) for p in tree.rglob('*.ko')} if result.returncode == 0 else {}}
    (out / 'BUILD.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'rc': result.returncode, 'modules': len(report['modules']), 'release': report['release'],
                      'receipt': str(out / 'BUILD.json'), 'inputs_unchanged': before == after}))
    return result.returncode or (0 if before == after else 1)

if __name__ == '__main__':
    sys.exit(main())
