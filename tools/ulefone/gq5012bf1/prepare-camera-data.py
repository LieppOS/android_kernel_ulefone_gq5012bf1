#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Prepare private stock-derived sensor headers without modifying the references.

The historical generator flattened PDAF coordinate-pair arrays. Add explicit
pair braces required by the warning-as-error build; never change any values.
No stock data is embedded in this source file or intended for publication.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re

NAMES = ('imx989', 'imx989cts', 's5kgm2sp', 's5kgn1sp', 'ov32b40',
         's5kjn1', 's5kjn1cts', 's5kjn1main2', 'ov64b')


def normalize(text):
    def pairs(match):
        values = [x.strip() for x in match.group(2).split(',') if x.strip()]
        if not values or len(values) % 2:
            raise ValueError('PDAF array must contain complete coordinate pairs')
        for value in values:
            int(value, 0)  # Do not reinterpret expressions or unknown layouts.
        return match.group(1) + '{' + ', '.join(
            '{' + ', '.join(values[i:i + 2]) + '}'
            for i in range(0, len(values), 2)) + '}'
    result = re.sub(r'(\.(?:i4Crop|i4PosL|i4PosR)\s*=\s*)\{([^{}]+)\}', pairs, text)
    if re.sub(r'[{}\s]', '', result) != re.sub(r'[{}\s]', '', text):
        raise ValueError('Normalization changed data or non-brace tokens')
    return result


def prepare(source, output):
    if source.resolve() == output.resolve():
        raise ValueError('Source and output directories must differ')
    prepared = {}
    for name in NAMES:
        filename = name + '_stock_data.h'
        original = (source / filename).read_bytes()
        data = normalize(original.decode()).encode()
        prepared[filename] = (original, data)
        dest = output / filename
        if dest.exists() and dest.read_bytes() != data:
            raise ValueError('Existing prepared header differs: ' + filename)
    output.mkdir(parents=True, exist_ok=True)
    receipt = {}
    for filename, (original, data) in prepared.items():
        dest = output / filename
        if not dest.exists():
            with dest.open('xb') as f:
                f.write(data)
        receipt[filename] = {'source_sha256': hashlib.sha256(original).hexdigest(),
                             'prepared_sha256': hashlib.sha256(data).hexdigest(),
                             'brace_only_change': original != data}
    (output / 'input-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    receipt = prepare(args.source, args.output)
    print('Prepared', len(receipt), 'private camera headers; register/mode values unchanged')
