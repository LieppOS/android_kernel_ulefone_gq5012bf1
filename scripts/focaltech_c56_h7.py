#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""H7: permit stock-sized framed PRAM writes without relaxing read/direct limits.
Generate only private H7 source. Never run the H4 generator's writing main(),
modify C29/H4/rollback artifacts, operate on the device, or embed firmware here.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
P = Path(__file__).with_name('focaltech_c56_h4.py')
spec = importlib.util.spec_from_file_location('c56_h4_source', P)
h4 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(h4)
REL = h4.h3.REL
W = Path.home() / 'LieppOS/build/c56/h7'
OUT = W / 'private-source' / REL
H4_SPI_SHA = 'b7c4b1601122a696a4ea50dbb342f50542416d007c07eb36a5b92dbd2b3dcf73'


def once(src, old, new):
    assert src.count(old) == 1, (old, src.count(old))
    return src.replace(old, new, 1)


def patch_spi(src):
    assert hashlib.sha256(src.encode()).hexdigest() == H4_SPI_SHA
    src = once(src, '#include "focaltech_core.h"\n',
               '#include "focaltech_core.h"\n#include "focaltech_c56_spi.h"\n'
               '#line 32 "' + REL + 'focaltech_spi.c"\n')
    src = once(src, '-> 4096 max frame', '-> 4096 cache cutoff')
    src = once(src, '-> 3520 B bus bufs', '-> GFP_KERNEL flags, NOT buffer length')
    src = once(src, ' * NOT reconstructed here: nothing. This file is complete for the transport.',
               ' * C56-H7 corrects cache/PRAM bounds; whole-driver parity remains partial.')
    src = once(src, '#define SPI_BUF_LENGTH              3520',
               '#define SPI_BUF_LENGTH              FTS_MAX_BUS_BUF')
    src = once(src, 'u8 *rx_buf, u32 len)\n{',
               'u8 *rx_buf, u32 len, int allow_bulk)\n{')
    src = once(src, 'if (!spi || !tx_buf || !rx_buf || !len || (len > SPI_FRAME_MAX)) {',
               'if (!spi || !fts_c56_spi_frame_allowed(tx_buf, rx_buf, len, allow_bulk)) {')
    old = 'fts_spi_transfer(txbuf, rxbuf, txlen);'
    assert src.count(old) == 2
    src = src.replace(old, 'fts_spi_transfer(txbuf, rxbuf, txlen, 0);')
    src = once(src, 'fts_spi_transfer(txbuf, rxbuf, txlen_need);',
               'fts_spi_transfer(txbuf, rxbuf, txlen_need, 0);')
    a, _, z = h4.h3.function_span(src, 'fts_write')
    body = once(src[a:z], 'fts_spi_transfer(txbuf, rxbuf, txlen, 0);',
                'fts_spi_transfer(txbuf, rxbuf, txlen, 1);')
    # Bound the input before any allocation or copy. Reject u32 length wrap,
    # unrepresentable frames and larger-than-stock PRAM packets explicitly.
    body = once(body, 'if (!writebuf || !writelen) {',
                'if (!writebuf || !writelen || writelen > FTS_C56_SPI_PRAM_PAYLOAD_MAX + 1U) {')
    return src[:a] + body + src[z:]


def generate():
    _, baseline = h4.generate()  # pure, no H4 output writes
    assert re.search(r'#define\s+FTS_MAX_BUS_BUF\s+4096\b', baseline['focaltech_core.h'])
    frozen = Path.home() / 'LieppOS/build/c56/h4/private-source' / REL
    for name, text in baseline.items():
        assert (frozen / name).read_text() == text, ('H4 source drift', name)
    result = dict(baseline)
    result['focaltech_spi.c'] = patch_spi(baseline['focaltech_spi.c'])
    return baseline, result


def main():
    baseline, result = generate()
    OUT.mkdir(parents=True, exist_ok=True)
    receipt = {}
    for name, text in result.items():
        (OUT / name).write_text(text)
        receipt[name] = {
            'h4_sha256': hashlib.sha256(baseline[name].encode()).hexdigest(),
            'h7_sha256': hashlib.sha256(text.encode()).hexdigest(),
            'changed': baseline[name] != text,
        }
    assert [n for n, r in receipt.items() if r['changed']] == ['focaltech_spi.c']
    (W / 'source-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('Private H7 source:', OUT, '; only focaltech_spi.c changed vs frozen H4')


if __name__ == '__main__':
    main()
