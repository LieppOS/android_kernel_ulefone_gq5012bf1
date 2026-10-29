#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""C56-H4: reject false firmware-recovery success after a failed RAM download.
The sealed H3 generator remains unchanged and reproducible. Only private H4
outputs are written; no firmware payload or recovered implementation is stored
here. This source correction is not hardware validation.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys
sys.dont_write_bytecode = True
P = Path(__file__).with_name('focaltech_c56_h3.py')
spec = importlib.util.spec_from_file_location('c56_h3_source', P)
h3 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(h3)
OUT = Path.home() / 'LieppOS/build/c56/h4/private-source' / h3.REL
OLD_WORKER_SHA = '5569fcfa6c40e24fadea7597477309f0aa9035884164fd39a38adc7355a75916'


def patch_recovery_worker(src):
    a, _, z = h3.function_span(src, 'fts_fwrecover_work')
    old = src[a:z]
    assert hashlib.sha256(old.encode()).hexdigest() == OLD_WORKER_SHA
    new = old.replace('u8 boot_state = 0;', 'u8 boot_state = 0;\n\tint ret;', 1)
    needle = '\tfts_read_reg(FTS_REG_BOOT_STATE, &boot_state);'
    assert new.count(needle) == 1
    new = new.replace(needle, '''\tret = fts_read_reg(FTS_REG_BOOT_STATE, &boot_state);
\tif (ret) {
\t\tupg->ts_data->fw_is_running = false;
\t\tFTS_ERROR("C56-H4 boot-state read failed:%d", ret);
\t\treturn;
\t}''')
    needle = '\tif ((boot_state == upg->setting_nf->upgsts_boot)\n\t    && (fts_check_bootid() == 0)) {'
    assert new.count(needle) == 1
    new = new.replace(needle, '''\tif (boot_state == upg->setting_nf->upgsts_boot) {
\t\tret = fts_check_bootid();
\t\tif (ret) {
\t\t\tupg->ts_data->fw_is_running = false;
\t\t\tFTS_ERROR("C56-H4 recovery boot ID failed:%d", ret);
\t\t\treturn;
\t\t}''')
    needle = '\t\tfts_fw_resume(false, FW_AUTO);'
    assert new.count(needle) == 1
    new = new.replace(needle, '''\t\t/* fts_fw_resume() is synchronous: fts_fw_write_start() updates
\t\t * fw_is_running before returning, so this check cannot observe a
\t\t * later asynchronous transition.
\t\t */
\t\tret = fts_fw_resume(false, FW_AUTO);
\t\tif (ret || !upg->ts_data->fw_is_running) {
\t\t\tupg->ts_data->fw_is_running = false;
\t\t\tFTS_ERROR("C56-H4 firmware recovery failed:%d", ret < 0 ? ret : -EIO);
\t\t\treturn;
\t\t}''')
    # Preserve the original logical source location after the replaced body.
    # A #line applies to the next line; src[z:] starts with the old closing
    # brace's newline, so assign that empty line the old brace's number.
    logical = 1
    for line in src[:z].splitlines(keepends=True):
        directive = re.match(r'\s*#line\s+(\d+)', line)
        if directive:
            logical = int(directive[1])
        elif line.endswith('\n'):
            logical += 1
    reset = '\n#line ' + str(logical) + ' "' + h3.REL + 'focaltech_flash.c"\n'
    return src[:a] + new + reset + src[z:]


def generate():
    original, result = h3.generate()
    result['focaltech_flash.c'] = patch_recovery_worker(result['focaltech_flash.c'])
    return original, result


def main():
    original, result = generate()
    OUT.mkdir(parents=True, exist_ok=True)
    receipt = {}
    for name, text in result.items():
        (OUT / name).write_text(text)
        receipt[name] = {
            'original_sha256': hashlib.sha256(original[name].encode()).hexdigest(),
            'modified_sha256': hashlib.sha256(text.encode()).hexdigest(),
            'changed': original[name] != text,
        }
    (OUT.parents[4] / 'source-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('Private H4 source:', OUT)


if __name__ == '__main__':
    main()
