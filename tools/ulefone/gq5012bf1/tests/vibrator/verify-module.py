#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright 2026 The LieppOS Contributors
"""Read-only, scoped ELF64 AArch64 vibrator module comparison; no objcopy.

Usage: python3 -B verify-module.py BASELINE.ko CANDIDATE.ko VMLINUX.symvers
Checks import CRCs and six existing callback KCFI preamble words, not hardware,
module loading, new callback execution or whole-kernel ABI equivalence.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys


class Elf:
    def __init__(self, path):
        self.data = path.read_bytes()
        header = struct.unpack_from('<16sHHIQQQIHHHHHH', self.data)
        if header[0][:6] != b'\x7fELF\x02\x01' or header[1] != 1 or header[2] != 183:
            raise ValueError('Expected little-endian ELF64 AArch64 module')
        self.sections = [struct.unpack_from('<IIQQQQIIQQ', self.data, header[6] + i * header[11]) for i in range(header[12])]
        names = self.section(self.sections[header[13]])
        self.named = {names[s[0]:].split(b'\0')[0].decode(): s for s in self.sections}

    def section(self, descriptor):
        return self.data[descriptor[4]:descriptor[4] + descriptor[5]]

    def versions(self):
        data = self.section(self.named['__versions'])
        if len(data) % 64:
            raise ValueError('Invalid arm64 modversion record size')
        return {data[i + 8:i + 64].split(b'\0')[0].decode(): int.from_bytes(data[i:i + 8], 'little') for i in range(0, len(data), 64)}

    def symbols(self):
        table = self.named['.symtab']
        names = self.section(self.sections[table[6]])
        data = self.section(table)
        for i in range(0, len(data), table[9]):
            name, info, other, section, value, size = struct.unpack_from('<IBBHQQ', data, i)
            yield names[name:].split(b'\0')[0].decode(), info, section, value

    def callback_tag(self, callback):
        for name, info, section, value in self.symbols():
            if name == callback:
                if info & 15 != 2 or value < 4:
                    raise ValueError('Callback lacks expected KCFI preamble')
                data = self.section(self.sections[section])
                return int.from_bytes(data[value - 4:value], 'little')
        raise ValueError('Missing callback ' + callback)

    def info(self):
        return dict(x.decode().split('=', 1) for x in self.section(self.named['.modinfo']).split(b'\0') if b'=' in x)


def main(paths):
    # Do not resolve/traverse unapproved protected input, including raw .. paths.
    if any('androido_dalykai' in path.parts for path in paths):
        raise ValueError('Forbidden input path')
    before = [hashlib.sha256(p.read_bytes()).hexdigest() for p in paths]
    old, new = Elf(paths[0]), Elf(paths[1])
    a, b = old.versions(), new.versions()
    core = {l.split()[1]: int(l.split()[0], 16) for l in paths[2].read_text().splitlines() if l.strip()}
    callbacks = ['vib_probe', 'vib_remove', 'vib_shutdown', 'vib_suspend', 'regulator_vibrator_set', 'regulator_oc_event']
    tags = {name: {'old': hex(old.callback_tag(name)), 'new': hex(new.callback_tag(name))} for name in callbacks}
    exports = [name for name, _, _, _ in new.symbols() if name.startswith('__ksymtab_')]
    undefined = {name for name, _, section, _ in new.symbols() if section == 0 and name}
    old_info, new_info = old.info(), new.info()
    result = dict(baseline_sha256=before[0], candidate_sha256=before[1], core_symvers_sha256=before[2],
                  matching_module_identity=all(old_info.get(n) == new_info.get(n) for n in ['name', 'vermagic', 'depends', 'license']),
                  vermagic=new_info['vermagic'], baseline_imports=len(a), candidate_imports=len(b),
                  common_imports=len(a.keys() & b.keys()),
                  common_crc_mismatch=[n for n in a.keys() & b.keys() if a[n] != b[n]],
                  core_crc_mismatch=[n for n, v in b.items() if core.get(n) != v],
                  unversioned_undefined=sorted(undefined - b.keys()),
                  callback_kcfi_preambles=tags, matching_callback_tags=all(x['old'] == x['new'] for x in tags.values()),
                  candidate_exports=exports, scope='identity/import CRC/six existing callback tags only; NOT runtime or whole ABI proof')
    result['inputs_unchanged'] = before == [hashlib.sha256(p.read_bytes()).hexdigest() for p in paths]
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result['matching_module_identity'] and result['matching_callback_tags'] and result['inputs_unchanged'] and not any(result[x] for x in ['common_crc_mismatch', 'core_crc_mismatch', 'unversioned_undefined', 'candidate_exports']) else 1


if __name__ == '__main__':
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    raise SystemExit(main([Path(x).absolute() for x in sys.argv[1:]]))
