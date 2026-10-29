#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""H8 private generation: checked no-flash PM restore and fail-closed activity.
Never writes H7/H4/C29 inputs, builds, operates on the phone or stores firmware.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('c56_h7_source', Path(__file__).with_name('focaltech_c56_h7.py'))
h7 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(h7)
span = h7.h4.h3.function_span
REL = h7.REL
W = Path.home() / 'LieppOS/build/c56/h8'


def once(src, old, new):
    assert src.count(old) == 1, (old, src.count(old))
    return src.replace(old, new, 1)


def replace_fn(src, name, replacement, filename):
    a, _, z = span(src, name)
    logical = 1
    for line in src[:z].splitlines(keepends=True):
        directive = re.match(r'\s*#line\s+(\d+)', line)
        if directive:
            logical = int(directive[1])
        elif line.endswith('\n'):
            logical += 1
    return src[:a] + replacement + '\n#line ' + str(logical) + ' "' + REL + filename + '"\n' + src[z:]


def modify_fn(src, name, transform, filename):
    a, _, z = span(src, name)
    return replace_fn(src, name, transform(src[a:z]), filename)


def worker_lock(body):
    brace = body.index('{')
    inner = body[brace + 1:-1]
    masked = h7.h4.h3.mask_c(inner)
    for m in reversed(list(re.finditer(r'\breturn\s*;', masked))):
        inner = inner[:m.start()] + 'goto c56_pm_out;' + inner[m.end():]
    return body[:brace + 1] + '''
    mutex_lock(&fts_c56_life.pm_lock);
    if (fts_c56_stopping() || READ_ONCE(fts_data->c56_pm_blocked))
        goto c56_pm_out;
    {
''' + inner + '''
    }
c56_pm_out:
    mutex_unlock(&fts_c56_life.pm_lock);
}
'''


def generate():
    _, baseline = h7.generate()
    frozen = Path.home() / 'LieppOS/build/c56/h7/private-source' / REL
    for name, text in baseline.items():
        assert (frozen / name).read_text() == text, ('H7 drift', name)
    out = dict(baseline)
    name = 'focaltech_core.h'
    out[name] = once(out[name], '#endif\n};\n\n\nenum _FTS_BUS_TYPE',
                     '#endif\n    /* H8-private appended state; no earlier field offsets change. */\n'
                     '    bool c56_pm_blocked;\n};\n\n\nenum _FTS_BUS_TYPE')
    name = 'focaltech_core.c'
    src = out[name]
    src = replace_fn(src, 'fts_ts_suspend', '#include "focaltech_c56_pm.h"', name)
    src = replace_fn(src, 'fts_ts_resume', '/* H8 resume is defined with suspend in focaltech_c56_pm.h. */', name)
    src = modify_fn(src, 'fts_wait_tp_to_valid', lambda s: once(s,
        'if ((idh == chip_idh) || (fts_check_cid(ts_data, idh) == 0)) {',
        'if (ret >= 0 && ((idh == chip_idh) || (fts_check_cid(ts_data, idh) == 0))) {'), name)
    src = modify_fn(src, 'fts_irq_enable', lambda s: once(s, 'if (!fts_c56_stopping() &&',
        'if (!fts_c56_stopping() && !READ_ONCE(fts_data->c56_pm_blocked) &&'), name)
    src = modify_fn(src, 'fts_irq_handler', lambda s: once(s,
        'if (fts_c56_stopping()) return IRQ_HANDLED;',
        'if (fts_c56_stopping() || READ_ONCE(fts_data->c56_pm_blocked)) return IRQ_HANDLED;'), name)
    def aod(s):
        s = once(s, '        fts_ts_suspend(fts_data ? fts_data->dev : NULL);',
                 '        _c56_result = fts_ts_suspend(fts_data ? fts_data->dev : NULL);')
        s = once(s, '        fts_ts_resume(fts_data ? fts_data->dev : NULL);',
                 '        _c56_result = fts_ts_resume(fts_data ? fts_data->dev : NULL);')
        return once(s, '{ _c56_result = (0); goto _c56_out; }', 'goto _c56_out;')
    src = modify_fn(src, 'yft_fts_ts_aod_contorl', aod, name)
    out[name] = src
    name = 'focaltech_flash.c'
    src = replace_fn(out[name], 'fts_enter_normal_fw', '#include "focaltech_c56_resume_fw.h"', name)
    src = modify_fn(src, 'fts_fw_recovery', lambda s: once(s,
        '\tFTS_INFO("check if boot recovery");',
        '\tif (READ_ONCE(fts_data->c56_pm_blocked)) { _c56_result = -EHOSTDOWN; goto _c56_out; }\n'
        '\tFTS_INFO("check if boot recovery");'), name)
    for fn in ['fts_fwrecover_work', 'fts_fwload_work']:
        src = modify_fn(src, fn, worker_lock, name)
    def boot_worker(s):
        # Acquisition must finish even if display sleep wins the race, so a
        # later explicit normal resume has its embedded firmware available.
        key = '\tif (upg->ts_data->fw_loading) {'
        assert s.count(key) == 1
        a = s.index(key)
        head, tail = s[:a], s[a:]
        tail = once(tail, '\t\treturn;', '\t\tgoto c56_pm_out;')
        return head + ('\tmutex_lock(&fts_c56_life.pm_lock);\n'
                       '\tif (fts_c56_stopping() || READ_ONCE(upg->ts_data->c56_pm_blocked))\n'
                       '\t\tgoto c56_pm_out;\n') + tail[:-1] + (
                       '\nc56_pm_out:\n\tmutex_unlock(&fts_c56_life.pm_lock);\n}')
    src = modify_fn(src, 'fts_fwupg_work', boot_worker, name)
    out[name] = src
    name = 'focaltech_gesture.c'
    src = replace_fn(out[name], 'fts_gesture_suspend', '#include "focaltech_c56_pm_gesture.h"', name)
    out[name] = replace_fn(src, 'fts_gesture_resume', '/* H8 checked gesture pair is defined above. */', name)
    name = 'focaltech_esdcheck.c'
    src = modify_fn(out[name], 'esdcheck_func', lambda s: once(s,
        'if (ts_data->esd_support && fts_esdcheck_data.mode)',
        'if (ts_data->esd_support && fts_esdcheck_data.mode && !READ_ONCE(ts_data->c56_pm_blocked))'), name)
    out[name] = modify_fn(src, 'fts_esdcheck_switch', lambda s: once(s,
        'if (ts_data->esd_support) {',
        'if (ts_data->esd_support && (!enable || !READ_ONCE(ts_data->c56_pm_blocked))) {'), name)
    return baseline, out


def main():
    baseline, result = generate()
    dest = W / 'private-source' / REL
    dest.mkdir(parents=True, exist_ok=True)
    receipt = {}
    for name, text in result.items():
        (dest / name).write_text(text)
        receipt[name] = {'h7_sha256': hashlib.sha256(baseline[name].encode()).hexdigest(),
                         'h8_sha256': hashlib.sha256(text.encode()).hexdigest(),
                         'changed': baseline[name] != text}
    assert {n for n, v in receipt.items() if v['changed']} == {
        'focaltech_core.c', 'focaltech_core.h', 'focaltech_flash.c',
        'focaltech_gesture.c', 'focaltech_esdcheck.c'}
    assert baseline['focaltech_spi.c'] == result['focaltech_spi.c']
    (W / 'source-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('Private H8 PM source generated; H7 transport and all frozen inputs unchanged')


if __name__ == '__main__':
    main()
