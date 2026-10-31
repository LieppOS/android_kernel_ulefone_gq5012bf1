#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Offline build in a fresh private snapshot; never invoke adb or change reference outputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("kernel-source", "reference-out", "toolchain-dir", "out"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    source, reference, toolchain, out = (
        p.resolve() for p in
        (args.kernel_source, args.reference_out, args.toolchain_dir, args.out)
    )
    if out.exists():
        parser.error("--out must be a new task-owned path (existing outputs are never replaced)")
    if out.is_relative_to(source) or out.is_relative_to(reference):
        parser.error("--out must be outside the reference source and output trees")
    here = Path(__file__).resolve().parent
    checks = (".config", "Module.symvers", "include/config/kernel.release",
              "include/generated/autoconf.h", "include/generated/utsrelease.h")
    before = {name: digest(reference / name) for name in checks}
    config = (reference / ".config").read_text()
    for flag in ("CONFIG_CFI_CLANG=y", "CONFIG_MODVERSIONS=y", "CONFIG_MODULES=y"):
        if flag not in config.splitlines():
            parser.error("required protection/configuration missing: " + flag)
    compiler = subprocess.check_output([str(toolchain / "bin/clang"), "--version"], text=True).splitlines()[0]
    expected = re.search(r'^CONFIG_CC_VERSION_TEXT="(.*)"$', config, re.M).group(1)
    if compiler != expected:
        parser.error("compiler differs from the reference config")
    release = (reference / "include/config/kernel.release").read_text().strip()
    out.mkdir(parents=True)
    kernel_out = out / "kernel-out"
    kernel_out.mkdir()
    # Generated headers/host tools only, NOT a duplicate kernel source tree.
    for name in ("include", "scripts", "arch/arm64/include/generated"):
        shutil.copytree(reference / name, kernel_out / name, symlinks=True)
    # Do not allow Kbuild to write through copied reference-output symlinks.
    for root, directories, files in os.walk(kernel_out):
        for name in directories + files:
            if (Path(root) / name).is_symlink():
                raise RuntimeError("reference generated snapshot contains a symlink: " + str(Path(root) / name))
    for name in (".config", "Module.symvers"):
        shutil.copy2(reference / name, kernel_out / name)
    module = out / "module"
    module.mkdir()
    for name in ("Kbuild", "ois_diag.c", "protocol.h"):
        shutil.copy2(here / name, module / name)
    env = os.environ.copy()
    env["PATH"] = str(toolchain / "bin") + os.pathsep + env["PATH"]
    env.update(KBUILD_BUILD_USER="lieppos", KBUILD_BUILD_HOST="lieppos-kernel",
               KBUILD_BUILD_TIMESTAMP="Sun Oct 25 16:47:11 UTC 2026",
               KBUILD_BUILD_VERSION="1", SOURCE_DATE_EPOCH="1792946831")
    cmd = ["make", "-C", str(source), "O=" + str(kernel_out), "M=" + str(module),
           "ARCH=arm64", "LLVM=1", "LLVM_IAS=1", "HOSTCC=gcc",
           "DEVICE_MODULES_PATH=" + str(source), "MTK_PLATFORM=mt6878",
           "KERNELRELEASE=" + release, "-j2", "modules"]
    try:
        with (out / "build.log").open("w") as log:
            subprocess.run(cmd, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    finally:
        after = {name: digest(reference / name) for name in checks}
        if after != before:
            raise RuntimeError("reference inputs changed during the build; discard qualification")
    if digest(kernel_out / ".config") != before[".config"]:
        raise RuntimeError("Kbuild changed the selected configuration")
    if (kernel_out / "include/config/kernel.release").read_text().strip() != release:
        raise RuntimeError("private output kernel release drifted")
    receipt = {
        "qualification": "BUILD_VERIFIED_ONLY; OFFLINE_ABI_REVIEW_REQUIRED; NOT_LOADED",
        "command": cmd, "kernel_source_commit": subprocess.check_output(
            ["git", "--no-optional-locks", "-C", str(source), "rev-parse", "HEAD"], text=True).strip(),
        "reference_inputs": before, "reference_unchanged": True,
        "compiler": compiler, "compiler_sha256": digest(toolchain / "bin/clang"),
        "source_sha256": {name: digest(here / name) for name in ("Kbuild", "ois_diag.c", "protocol.h")},
        "kernel_release": release,
        "candidate_sha256": digest(module / "ois_diag.ko"),
        "unsigned": True, "phone_access": False,
    }
    (out / "BUILD.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
