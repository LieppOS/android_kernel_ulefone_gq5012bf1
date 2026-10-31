#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Compile actual DSI DFPS/VM emitter bodies against recording host shims.

No device access, Linux execution, GCE execution, timing or panel acceptance is
simulated. Tests cover route selection, payloads, and emitted register operations.
Kernel functions/constants are extracted each run, not maintained as test copies.
Generated files belong only in the caller's --work-dir outside kernel source.
"""
import argparse
import importlib.util
import os
from pathlib import Path
import re
import resource
import subprocess
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).absolute().parent
KERNEL = HERE.parents[4]
DSI = KERNEL / 'drivers/gpu/drm/mediatek/mediatek_v2/mtk_dsi.c'
PANEL_EXT = DSI.with_name('mtk_panel_ext.h')


def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--cc', default=os.environ.get('HOSTCC', 'cc'))
    args = parser.parse_args()
    work = args.work_dir.absolute()
    if work == KERNEL or KERNEL in work.parents:
        parser.error('--work-dir must be outside kernel source')
    work.mkdir(parents=True, exist_ok=True)
    # Reuse the existing functional test extraction helpers, not a new framework.
    spec = importlib.util.spec_from_file_location('tkip_extract', HERE.parent / 'tkip_mic/run-tests.py')
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    source = DSI.read_text()
    defines = ['DSI_START', 'VM_CMD_START', 'DSI_INTSTA', 'VM_CMD_DONE_INT_EN',
               'DSI_CON_CTRL', 'DSI_DUAL_EN', 'DSI_MODE_CTRL', 'MIX_MODE',
               'DSI_TXRX_CTRL', 'DIS_EOT', 'DSI_CMDQ_SIZE', 'CMDQ_SIZE',
               'CMDQ_SIZE_SEL', 'VM_CMD_EN', 'TS_VFP_EN', 'VM_LONG_PACKET',
               'CONFIG', 'DATA_ID', 'DATA_0', 'DATA_1', 'BTA', 'HSTX',
               'LONG_PACKET', 'SHORT_PACKET', 'MTK_DSI_HOST_IS_READ']
    constants = [helper.extract_define(source, name) for name in defines]
    constants += [helper.extract_define(PANEL_EXT.read_text(), name)
                  for name in ['MAX_DYN_CMD_NUM', 'MAX_TX_CMD_NUM_PACK']]
    constants.append(helper.extract_block(PANEL_EXT.read_text(), r'^enum MTK_PANEL_OUTPUT_PORT_MODE\b'))
    constants.append(helper.extract_define((KERNEL / 'include/drm/drm_mipi_dsi.h').read_text(), 'MIPI_DSI_MODE_VIDEO'))
    (work / 'dsi-constants.inc').write_text('\n'.join(constants) + '\n')
    functions = ['mtk_dsi_is_cmd_mode', 'build_vm_cmdq', 'mtk_dsi_vm_cmdq',
                 'mtk_dsi_cmdq_pack_gce', 'mipi_dsi_dcs_write_gce',
                 'mtk_dsi_send_switch_cmd_nothing']
    bodies = [helper.extract_block(source, r'^(?:static )?(?:void|bool) ' + name + r'\([^;{]*\)\s*\{')
              for name in functions]
    (work / 'dsi-functions.inc').write_text('\n\n'.join(bodies) + '\n')
    for mode, extra in [('native', []), ('asan-ubsan', ['-fsanitize=address,undefined', '-fno-sanitize-recover=all'])]:
        binary = work / ('test-dfps-' + mode)
        command = [args.cc, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-parameter', '-Wno-sign-compare', '-fno-omit-frame-pointer',
                   *extra, '-I', str(work), '-I', str(KERNEL / 'include/video'),
                   str(HERE / 'test_dfps.c'), '-o', str(binary)]
        subprocess.run(command, check=True)
        print(mode, flush=True)
        subprocess.run([str(binary)], check=True, cwd=work)
    return 0


if __name__ == '__main__':
    sys.exit(main())
