#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Read-only offline ELF/CRC/KCFI qualification. Runtime compatibility is not implied."""
import argparse
import hashlib
import json
import mmap
from pathlib import Path
import re
import struct


class Elf:
    def __init__(self, path):
        self.file = path.open("rb")
        self.data = mmap.mmap(self.file.fileno(), 0, access=mmap.ACCESS_READ)
        assert self.data[:6] == b"\x7fELF\x02\x01", "expected little-endian ELF64"
        assert struct.unpack_from("<H", self.data, 18)[0] == 183, "not AArch64"
        offset = struct.unpack_from("<Q", self.data, 40)[0]
        step, count, names = struct.unpack_from("<HHH", self.data, 58)
        self.sections = [struct.unpack_from("<IIQQQQIIQQ", self.data, offset + i * step)
                         for i in range(count)]
        strings = self.contents(names)
        self.indices = {self.string(strings, s[0]): i for i, s in enumerate(self.sections)}
        index = self.indices[".symtab"]
        strings = self.contents(self.sections[index][6])
        self.symbols = {}
        s = self.sections[index]
        for offset in range(s[4], s[4] + s[5], s[9]):
            name, info, _, section, value, size = struct.unpack_from("<IBBHQQ", self.data, offset)
            self.symbols[self.string(strings, name)] = (info, section, value, size)

    @staticmethod
    def string(data, offset):
        return data[offset:data.index(b"\0", offset)].decode()

    def contents(self, index):
        s = self.sections[index]
        return self.data[s[4]:s[4] + s[5]]

    def named(self, name):
        return self.contents(self.indices[name])

    def prefix(self, name):
        info, index, value, size = self.symbols[name]
        assert info & 15 == 2 and size, "not a function: " + name
        section = self.sections[index]
        relative = value - section[3]
        assert relative >= 4, "no KCFI prefix: " + name
        return self.data[section[4] + relative - 4:section[4] + relative].hex()

    def modinfo(self):
        return dict(s.decode().split("=", 1) for s in self.named(".modinfo").split(b"\0") if b"=" in s)

    def versions(self):
        raw = self.named("__versions")
        assert len(raw) % 64 == 0
        return {name.split(b"\0", 1)[0].decode(): crc
                for crc, name in struct.iter_unpack("<Q56s", raw)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--baseline-module", required=True, type=Path)
    parser.add_argument("--reference-out", required=True, type=Path)
    parser.add_argument("--crosscheck-out", required=True, type=Path)
    args = parser.parse_args()
    module, baseline = Elf(args.candidate), Elf(args.baseline_module)
    info = module.modinfo()
    assert info["vermagic"] == baseline.modinfo()["vermagic"]
    assert info["license"] == "GPL" and not info.get("depends")
    assert not args.candidate.read_bytes().endswith(b"~Module signature appended~\n")
    assert "cleanup_module" not in module.symbols, "unexpected unload path"
    assert not any(n.startswith("__ksymtab_") for n in module.symbols), "unexpected exports"
    imports = {n for n, (i, s, _, _) in module.symbols.items() if n and s == 0 and i >> 4 == 1}
    allowed = {
        "__stack_chk_fail", "_printk", "alt_cb_patch_nops", "bus_find_device", "capable",
        "device_match_name", "i2c_bus_type", "i2c_transfer", "i2c_verify_client", "mutex_lock",
        "mutex_unlock", "of_device_is_compatible", "param_get_bool", "param_set_bool",
        "pm_runtime_get_if_active", "put_device", "scnprintf", "strcmp",
    }
    assert imports <= allowed, "unreviewed imports: " + str(imports - allowed)
    assert {"i2c_transfer", "pm_runtime_get_if_active", "capable", "bus_find_device"} <= imports
    versions = module.versions()
    assert imports <= versions.keys() and set(versions) == imports | {"module_layout"}
    for out in (args.reference_out, args.crosscheck_out):
        symbols = {p[1]: int(p[0], 16) for line in (out / "Module.symvers").read_text().splitlines()
                   if (p := line.split())}
        assert all(symbols.get(n) == crc for n, crc in versions.items()), "CRC mismatch: " + str(out)
        config = (out / ".config").read_text().splitlines()
        assert "CONFIG_CFI_CLANG=y" in config and "CONFIG_MODVERSIONS=y" in config
        # GKI protects imports using the UNPROTECTED allowlist, not the exports list.
        header = (out / "include/generated/gki_module_unprotected.h").read_text()
        zero = re.search(r"#define\s+NR_UNPROTECTED_SYMBOLS\s+0\b", header)
        names = set(re.findall(r'"([A-Za-z0-9_]+)"', header))
        assert zero or names, "unrecognized GKI allowlist header"
        assert zero or set(versions) <= names, "GKI unsigned-import restriction"
    kernel = Elf(args.reference_out / "vmlinux")
    other = Elf(args.crosscheck_out / "vmlinux")
    cfis = {}
    for local, remote in (("sample_set", "param_set_bool"), ("enable_set", "param_set_bool"),
                          ("status_get", "param_get_bool")):
        actual = module.prefix(local)
        assert actual == kernel.prefix(remote) == other.prefix(remote), "callback KCFI mismatch"
        cfis[local] = actual
    assert module.prefix("init_module") == baseline.prefix("init_module")
    assert module.symbols["__kcfi_typeid_device_match_name"][2] == int.from_bytes(
        bytes.fromhex(kernel.prefix("device_match_name")), "little")
    print(json.dumps({
        "result": "ABI_VERIFIED_OFFLINE", "runtime": "NOT_LOADED; NOT_HARDWARE_VERIFIED",
        "candidate_sha256": hashlib.sha256(args.candidate.read_bytes()).hexdigest(),
        "vermagic": info["vermagic"], "imports": sorted(imports), "CRC_count": len(versions),
        "CRC_match_two_reference_outputs": True, "callback_KCFI_prefixes": cfis,
        "init_KCFI_matches_baseline": True, "device_match_callback_KCFI_matches_kernel": True,
        "GKI_unsigned_import_gate_reference_check": "PASS; live protection state untested",
        "unsigned": True, "exports": 0, "unload_callback": False,
    }, indent=2))


if __name__ == "__main__":
    main()
