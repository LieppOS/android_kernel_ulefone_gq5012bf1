#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright 2026 The LieppOS Contributors
"""Portable shared-state tests only; does not mock/compile Linux kernel APIs."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).absolute().parents[5]
COMPILER = '/usr/x86_64-pc-linux-gnu/gcc-bin/16/x86_64-pc-linux-gnu-gcc'


def main():
    parent = ROOT / 'out'
    made_parent = not parent.exists()
    parent.mkdir(exist_ok=True)
    try:
        with tempfile.TemporaryDirectory(prefix='c51c-state-', dir=parent) as tmp:
            for name, flags in [('native', []), ('asan-ubsan', ['-fsanitize=address,undefined']), ('tsan', ['-fsanitize=thread'])]:
                executable = Path(tmp) / name
                command = [os.environ.get('CC', COMPILER), '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
                           '-fno-omit-frame-pointer', *flags, '-I' + str(ROOT / 'drivers/leds'),
                           str(Path(__file__).with_name('state_test.c')), '-o', str(executable)]
                env = dict(os.environ, TMPDIR=tmp, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                           UBSAN_OPTIONS='halt_on_error=1', TSAN_OPTIONS='halt_on_error=1')
                built = subprocess.run(command, env=env, capture_output=True, text=True, timeout=60)
                print(name, 'compile exit', built.returncode)
                if built.returncode:
                    print(built.stderr)
                    return 1
                run = subprocess.run([str(executable)], env=env, capture_output=True, text=True, timeout=30)
                print(run.stdout, run.stderr)
                if run.returncode or 'TOTAL 12/12' not in run.stdout:
                    return 1
        # Structural guards for the actual Linux integration, not execution
        # tests of hrtimer/workqueue/regulator APIs.
        source = (ROOT / 'drivers/leds/regulator-vibrator.c').read_text()
        store = source.split('static ssize_t duration_ms_store', 1)[1].split('static DEVICE_ATTR_RW', 1)[0]
        assert store.index('led_trigger_remove(') < store.index('flush_work(') < store.index('vibr_request(')
        suspend = source.split('static int __maybe_unused vib_suspend', 1)[1].split('static int __maybe_unused vib_resume', 1)[0]
        assert suspend.index('led_trigger_remove(') < suspend.index('flush_work(') < suspend.index('reg_vibr_quiesce(')
        release = source.split('static int vibr_release', 1)[1].split('static int vib_probe', 1)[0]
        assert release.index('WRITE_ONCE(vibr->stopping, true)') < release.index('regulator_unregister_notifier(') < release.index('led_classdev_unregister(') < release.index('cancel_work_sync(') < release.index('vibr_stop_locked(')
        expiry = source.split('static enum hrtimer_restart vibr_expire', 1)[1].split('static void vibr_off_work', 1)[0]
        assert 'queue_work(' in expiry and 'mutex_lock(' not in expiry and 'regulator_' not in expiry
        print('SOURCE_ORDER 4/4 (static, not kernel runtime)')
        return 0
    finally:
        if made_parent:
            try:
                parent.rmdir()
            except OSError:
                pass


if __name__ == '__main__':
    raise SystemExit(main())
