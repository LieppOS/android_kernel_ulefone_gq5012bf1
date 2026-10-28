#!/usr/bin/env python3
"""Build the imported MediaTek vendor module groups against a prepared target.

Run after build-kernel.sh and build-modules.sh.  The prepared kernel output
($OUT) supplies .config, generated headers and Module.symvers; every vendor
module is built with M=vendor/mediatek/kernel_modules/<group> so its objects
land in $OUT, never in the source tree.  Provider order and
KBUILD_EXTRA_SYMBOLS follow the C29 reference external-module recipe.

Environment:
  OUT            prepared kernel output (required)
  KERNEL_SRC     kernel source (default: repository root)
  TOOLCHAIN_DIR  clang root containing bin/clang (or put clang on PATH)
  WORK_DIR       logs/, symvers/ and vendor-build-receipt.json (default: dirname $OUT)
  JOBS           parallel jobs (default: nproc)
  ONLY           optional comma-separated build names to run
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "..", ".."))
KERNEL = os.path.abspath(os.environ.get("KERNEL_SRC", REPO))
OUT = os.environ.get("OUT")
if not OUT:
    sys.exit("error: set OUT to the prepared kernel output directory")
OUT = os.path.abspath(OUT)
WORK = os.path.abspath(os.environ.get("WORK_DIR", os.path.dirname(OUT)))
LOGS = os.path.join(WORK, "logs")
SYMVERS = os.path.join(WORK, "symvers")
JOBS = os.environ.get("JOBS", str(os.cpu_count() or 4))
TOOLCHAIN_DIR = os.environ.get("TOOLCHAIN_DIR", "")
TOOL = os.path.join(TOOLCHAIN_DIR, "bin") if TOOLCHAIN_DIR else ""
ONLY = {x for x in os.environ.get("ONLY", "").split(",") if x}
VENDOR = "vendor/mediatek/kernel_modules"

ENV = dict(os.environ)
if TOOL:
    ENV["PATH"] = TOOL + ":" + ENV.get("PATH", "")
# Same deterministic stamps as build-kernel.sh / build-modules.sh.
ENV.setdefault("KBUILD_BUILD_USER", "lieppos")
ENV.setdefault("KBUILD_BUILD_HOST", "lieppos-kernel")
ENV.setdefault("KBUILD_BUILD_TIMESTAMP", "Sun Oct 25 16:47:11 UTC 2026")
ENV.setdefault("KBUILD_BUILD_VERSION", "1")
ENV.setdefault("SOURCE_DATE_EPOCH", "1792946831")

CONN_VARS = {"CONFIG_MTK_COMBO": "y", "CONFIG_MTK_COMBO_CHIP_CONSYS_6878": "y"}
CONN_BASE = ["connectivity/conninfra", "connectivity/connfem",
             "connectivity/wlan/adaptor/build/connac2x"]
CAM = "mtkcam"
CAM_BASE = [f"{CAM}/cam_cal/src_v4l2/custom", f"{CAM}/mtk-dpe"]
MET_APIS = ["met_backlight_api", "met_emi_api", "met_gpu_adv_api", "met_gpu_api",
            "met_gpueb_api", "met_ipi_api", "met_mcupm_api", "met_scmi_api",
            "met_sspm_api", "met_vcore_api"]

# (name, M relative to VENDOR, providers for KBUILD_EXTRA_SYMBOLS, extra make vars)
BUILDS: list[tuple[str, str, list[str], dict[str, str]]] = [
    ("conninfra", "connectivity/conninfra", [], CONN_VARS),
    ("connfem", "connectivity/connfem", [], CONN_VARS),
    ("wlan-adaptor-connac2x", "connectivity/wlan/adaptor/build/connac2x", CONN_BASE[:2], CONN_VARS),
    # gen4m builds with the LieppOS-authored mgmt/tkip_mic.c (C50); the vendor
    # file with a no-redistribution notice stays excluded from the import.
    # DRIVER_BUILD_DATE matches KBUILD_BUILD_TIMESTAMP, as in the C29 recipe.
    ("wlan-gen4m", "connectivity/wlan/core/gen4m", CONN_BASE,
     dict(CONN_VARS, DRIVER_BUILD_DATE="20261025164711",
          MODULE_NAME="wlan_drv_gen4m_6878", SEGMENT="SP")),
    ("bt-6878", "connectivity/bt/mt66xx/6878", CONN_BASE, CONN_VARS),
    ("gps-dl-v051", "connectivity/gps/data_link/plat/v051", CONN_BASE, CONN_VARS),
    ("gps-pwr", "connectivity/gps/gps_pwr", CONN_BASE, CONN_VARS),
    ("gps-scp", "connectivity/gps/gps_scp", CONN_BASE, CONN_VARS),
    ("fmradio-connac2x", "connectivity/fmradio/Build/connac2x", CONN_BASE, CONN_VARS),
    ("met", "met", [], {}),
    *[("met-" + api.replace("_", "-"), f"met/met_api/{api}", ["met"], {}) for api in MET_APIS],
    ("cam-cal-v4l2", f"{CAM}/cam_cal/src_v4l2/custom", [], {}),
    ("mtk-dpe", f"{CAM}/mtk-dpe", CAM_BASE[:1], {}),
    ("ccusys", f"{CAM}/ccusys", CAM_BASE, {}),
    ("camsys-rpmsg", f"{CAM}/camsys/rpmsg", CAM_BASE + [f"{CAM}/ccusys"], {}),
    ("camsys-remoteproc", f"{CAM}/camsys/remoteproc",
     CAM_BASE + [f"{CAM}/ccusys", f"{CAM}/camsys/rpmsg"], {}),
    # imgsensor/src-v4l2/Kbuild assumes the Android layout in which vendor/ is a
    # sibling of DEVICE_MODULES_PATH ($(DEVICE_MODULES_PATH)/../vendor/...).  In
    # the combined LieppOS tree vendor/ lives inside the kernel root, so the
    # command line overrides MTK_ISP_DRIVER_PATH instead of editing the source.
    ("imgsensor", f"{CAM}/imgsensor/src-v4l2",
     CAM_BASE + [f"{CAM}/ccusys", f"{CAM}/camsys/rpmsg", f"{CAM}/camsys/remoteproc"],
     {"MTK_ISP_DRIVER_PATH": os.path.join(KERNEL, VENDOR, CAM)}),
    ("camsys", f"{CAM}/camsys",
     CAM_BASE + [f"{CAM}/ccusys", f"{CAM}/camsys/rpmsg", f"{CAM}/camsys/remoteproc",
                 f"{CAM}/imgsensor/src-v4l2"], {}),
    ("mtk-hcp", f"{CAM}/mtk-hcp", [], {}),
    ("imgsys", f"{CAM}/imgsys", [f"{CAM}/mtk-hcp"], {}),
    ("mtk-aie", f"{CAM}/mtk-aie", [], {}),
]


def out_path(rel: str) -> str:
    return os.path.join(OUT, VENDOR, rel)


def kernel_symbols() -> set[str]:
    syms = set()
    for line in open(os.path.join(OUT, "Module.symvers"), errors="ignore"):
        parts = line.rstrip("\n").split("\t")
        if len(parts) >= 4:
            syms.add(parts[1])
    return syms


def make_symvers(name: str, providers: list[str], ksyms: set[str]) -> str | None:
    """Concatenate provider Module.symvers, dropping kernel and duplicate symbols."""
    seen: set[str] = set()
    lines = []
    for p in providers:
        f = os.path.join(out_path(p), "Module.symvers")
        if not os.path.isfile(f):
            raise SystemExit(f"error: provider {p} has no Module.symvers; build it first")
        for line in open(f, errors="ignore"):
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 4 or parts[1] in ksyms or parts[1] in seen:
                continue
            seen.add(parts[1])
            lines.append(line)
    if not lines:
        return None
    dst = os.path.join(SYMVERS, name + ".symvers")
    with open(dst, "w") as fh:
        fh.writelines(lines)
    return dst


def modpost_kbuild_shim(rel: str) -> bool:
    """Linux 6.1 scripts/Makefile.modpost includes
    $(or $(wildcard $(src)/Kbuild), $(src)/Makefile) relative to the objtree.
    For an M= directory inside srctree the wildcard misses, so modpost either
    aborts on an absent Makefile (fmradio/Build/connac2x) or includes the
    Android-build Makefile instead of Kbuild (wlan/core/gen4m, whose Makefile
    needs Makefile.include).  Kbuild takes precedence in kbuild, so link the
    source Kbuild into the output directory whenever it exists (build output
    only, never the source tree)."""
    src_dir = os.path.join(KERNEL, VENDOR, rel)
    if not os.path.exists(os.path.join(src_dir, "Kbuild")):
        return False
    od = out_path(rel)
    os.makedirs(od, exist_ok=True)
    link = os.path.join(od, "Kbuild")
    target = os.path.join(src_dir, "Kbuild")
    if os.path.islink(link) and os.readlink(link) == target:
        return True
    if os.path.lexists(link):
        os.remove(link)
    os.symlink(target, link)
    return True


def build(name: str, rel: str, providers: list[str], extra_vars: dict[str, str], ksyms: set[str]) -> dict:
    log = os.path.join(LOGS, name + ".log")
    cmd = ["make", "-C", KERNEL, "O=" + OUT, "ARCH=arm64", "LLVM=1", "LLVM_IAS=1", "HOSTCC=gcc",
           "DEVICE_MODULES_PATH=" + KERNEL, "TOP=" + KERNEL, "MTK_PLATFORM=mt6878",
           "-j" + JOBS, "M=" + os.path.join(VENDOR, rel)]
    sv = make_symvers(name, providers, ksyms) if providers else None
    if sv:
        cmd.append("KBUILD_EXTRA_SYMBOLS=" + sv)
    cmd += [f"{k}={v}" for k, v in extra_vars.items()]
    cmd.append("modules")
    shim = modpost_kbuild_shim(rel)
    print("BUILD", name, flush=True)
    with open(log, "w") as fh:
        rc = subprocess.run(cmd, stdout=fh, stderr=subprocess.STDOUT, env=ENV).returncode
    text = open(log, errors="ignore").read()
    undefined = sorted(set(re.findall(r'"(\S+)" \[[^\]]+\] undefined!', text)))
    result = {"name": name, "m": os.path.join(VENDOR, rel), "rc": rc,
              "log": os.path.relpath(log, WORK), "extra_symbols": providers,
              "make_vars": extra_vars, "modpost_kbuild_shim": shim, "undefined_symbols": undefined}
    if rc != 0:
        result["tail"] = "\n".join(text.splitlines()[-20:])
    print(("PASS " if rc == 0 else f"FAIL rc={rc} ") + name + (f" undefined={len(undefined)}" if undefined else ""),
          flush=True)
    return result


def modinfo(path: str, field: str) -> str:
    return subprocess.run(["modinfo", "-F", field, path], capture_output=True, text=True).stdout.strip()


def hash_outputs(rel: str) -> list[dict]:
    found = []
    for root, _dirs, files in os.walk(out_path(rel)):
        for f in files:
            if not f.endswith(".ko"):
                continue
            p = os.path.join(root, f)
            data = open(p, "rb").read()
            found.append({"path": os.path.relpath(p, OUT), "sha256": hashlib.sha256(data).hexdigest(),
                          "size": len(data), "vermagic": modinfo(p, "vermagic"),
                          "depends": modinfo(p, "depends")})
    return sorted(found, key=lambda x: x["path"])


def main() -> int:
    for f in (".config", "Module.symvers", "include/config/kernel.release", "include/generated/autoconf.h"):
        if not os.path.isfile(os.path.join(OUT, f)):
            print(f"error: missing {OUT}/{f}; run build-kernel.sh and build-modules.sh first", file=sys.stderr)
            return 2
    os.makedirs(LOGS, exist_ok=True)
    os.makedirs(SYMVERS, exist_ok=True)
    ksyms = kernel_symbols()
    results = []
    for name, rel, providers, extra_vars in BUILDS:
        if ONLY and name not in ONLY:
            continue
        results.append(build(name, rel, providers, extra_vars, ksyms))

    if not ONLY or "mali-r44p1" in ONLY:
        # The imported recipe enforces the target's generated config and symvers.
        gpu_log = os.path.join(LOGS, "mali-r44p1.log")
        env = dict(ENV, KERNEL_SRC=KERNEL, OUT_DIR=OUT, JOBS=JOBS)
        env["TOOLCHAIN_BIN"] = TOOL or os.path.dirname(
            subprocess.run(["sh", "-c", "command -v clang"], capture_output=True, text=True, env=ENV).stdout.strip())
        print("BUILD mali-r44p1", flush=True)
        with open(gpu_log, "w") as fh:
            rc = subprocess.run(["bash", os.path.join(KERNEL, VENDOR, "gpu/build-mali-r44.sh")],
                                stdout=fh, stderr=subprocess.STDOUT, env=env).returncode
        text = open(gpu_log, errors="ignore").read()
        undefined = sorted(set(re.findall(r'"(\S+)" \[[^\]]+\] undefined!', text)))
        r = {"name": "mali-r44p1", "m": os.path.join(VENDOR, "gpu"), "rc": rc,
             "log": os.path.relpath(gpu_log, WORK), "extra_symbols": [], "make_vars": {},
             "modpost_kbuild_shim": False, "undefined_symbols": undefined}
        if rc != 0:
            r["tail"] = "\n".join(text.splitlines()[-30:])
        print(("PASS " if rc == 0 else f"FAIL rc={rc} ") + "mali-r44p1", flush=True)
        results.append(r)

    clang = subprocess.run(["clang", "--version"], capture_output=True, text=True, env=ENV).stdout.splitlines()
    receipt = {
        "kernel_source_commit": subprocess.run(["git", "-C", KERNEL, "rev-parse", "HEAD"],
                                               capture_output=True, text=True).stdout.strip(),
        "kernel_release": open(os.path.join(OUT, "include/config/kernel.release")).read().strip(),
        "toolchain": clang[0] if clang else "",
        "target_module_symvers_sha256": hashlib.sha256(
            open(os.path.join(OUT, "Module.symvers"), "rb").read()).hexdigest(),
        "builds": results,
        "outputs": {g: hash_outputs(g) for g in ("connectivity", "met", "mtkcam", "gpu")},
    }
    rpath = os.path.join(WORK, "vendor-build-receipt.json")
    with open(rpath, "w") as fh:
        json.dump(receipt, fh, indent=1, sort_keys=True)
        fh.write("\n")
    failed = [r for r in results if r["rc"] != 0 or r["undefined_symbols"]]
    print(f"builds={len(results)} failed={len(failed)} receipt={rpath}")
    for r in failed:
        print("FAILED", r["name"], r["rc"], r["undefined_symbols"][:5])
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
