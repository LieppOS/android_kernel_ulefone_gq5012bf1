#!/usr/bin/env python3
"""Import the verified MediaTek vendor module source closures into the LieppOS
kernel repository.

This is a source-snapshot import.  It copies only tracked upstream source,
preserves symlinks, and records a per-file SHA-256 manifest.  Build artifacts,
firmware blobs, device-unique calibration data, private keys and files with
explicit no-redistribution notices are never copied into the repository.

The external source trees are treated as read-only inputs.  Git queries use
GIT_OPTIONAL_LOCKS=0 and repository-relative pathspecs only.

Usage:
  import-vendor-modules.py --source-root <dir containing gpu/ connectivity/
                           mtkcam/ met_drv_v3/> [--quarantine <dir>] [--dry-run]

The source root may also be given as LIEPPOS_VENDOR_SOURCE_ROOT.  After the
copy, every symlink inside the imported tree must resolve unless it is listed
in KNOWN_UNRESOLVED_LINKS with a recorded reason; otherwise the import fails.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

KERNEL_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
GIT_ENV = dict(os.environ, GIT_OPTIONAL_LOCKS="0")

COMPONENTS = {
    "gpu": {
        "repo": "gpu",
        "dirs": [""],
        "source_commit": "c7cb71b6a3a36c095f55e306d05e840bf81c1eb0",
        "upstream": "NothingOSS/android_kernel_modules_nothing_mt6878",
        "upstream_commit": "580d08eac018657a993e56b1806407e41a3b9545",
        "upstream_extra": "Mali r44p1 page-migration/pm_driver graft c3d070e6155c9f373224fa2b17027d07583c37bc",
        "license": "GPL-2.0 WITH Linux-syscall-note / GPL-2.0",
        "build_modules": [
            "mali_kbase_mt6878_r44.ko",
            "mali_mgm_mt6878_r44.ko",
            "mali_prot_alloc_mt6878_r44.ko",
            "mali_dmabuf_test_mt6878_r44.ko",
        ],
        "nested": [],
    },
    "connectivity": {
        "repo": "connectivity",
        "dirs": [
            "conninfra",
            "connfem",
            "wlan/adaptor",
            "wlan/core/gen4m",
            "bt/mt66xx/6878",
            "bt/mt66xx/btif",
            # C49 closure: targets of the bt/mt66xx/6878 relative symlinks and
            # the shared BT headers used by btif/Kbuild.
            "bt/mt66xx/include",
            "bt/mt66xx/btmtk_buffer_mode.c",
            "bt/mt66xx/btmtk_chip_reset.c",
            "bt/mt66xx/btmtk_fw_log.c",
            "bt/mt66xx/btmtk_main.c",
            "bt/mt66xx/btmtk_woble.c",
            "gps/data_link",
            # C49 closure: targets of gps/data_link/plat/*/fw_log{,_gps.c}.
            "gps/fw_log",
            "gps/fw_log_gps.c",
            "gps/gps_pwr",
            "gps/gps_scp",
            "fmradio",
            "common",
        ],
        "source_commit": "1a9a0c3231c7859eb8595a8236aa40214a7076c7",
        "upstream": "NothingOSS MediaTek vendor kernel_modules/connectivity",
        "upstream_commit": "7de5e1c",
        "upstream_extra": "MT6878 vendor subset recorded by the source-owning PROVENANCE.md",
        "license": "GPL-2.0 / BSD-2-Clause (WLAN core); one Inprocomm TKIP file blocked",
        "build_modules": [
            "conninfra.ko",
            "connfem.ko",
            "wmt_chrdev_wifi_connac2.ko",
            "bt_drv_6878.ko",
            "gps_drv_dl_v051.ko",
            "gps_pwr.ko",
            "gps_scp.ko",
            "fmradio_drv_connac2x.ko",
        ],
        "nested": [],
    },
    "mtkcam": {
        "repo": "mtkcam",
        "dirs": [
            "camsys",
            "ccusys",
            "cam_cal",
            "imgsensor/src-v4l2",
            "mtk-dpe",
            "imgsys",
            "mtk-hcp",
            "mtk-aie",
            "include",
            # C49 closure: headers included by camsys, imgsensor and imgsys.
            "imgsensor/inc",
            "mtk-ipesys-me",
        ],
        "source_commit": "7286106471dba28f8a4224e70a45be3c8d5505be",
        "upstream": "NothingOSS mtkcam",
        "upstream_commit": "5f75a3b",
        "upstream_extra": "baseline import 43d9270; nested imgsensor-v4l2 d44461a5f43f1db7e8664179cb3248ebf84ce9d3",
        "license": "GPL-2.0 / GPL-2.0-only; two camsys files carry no per-file SPDX",
        "build_modules": [
            "mtk-cam-isp7sp.ko",
            "camera_eeprom_v4l2.ko",
            "camera_dpe_isp7sp.ko",
            "mtk_ccuv.ko",
            "ccd_rpmsg.ko",
            "mtk_ccd_remoteproc.ko",
            "imgsensor.ko",
            "imgsensor-glue.ko",
            "mtk_imgsys_isp7sp.ko",
            "mtk-hcp.ko",
        ],
        "nested": [
            ("imgsensor/src-v4l2", "d44461a5f43f1db7e8664179cb3248ebf84ce9d3"),
        ],
    },
    "met": {
        "repo": "met_drv_v3",
        "dirs": [""],
        "source_commit": "c9b8a7d47a28d6e8362e667b629739adf79090e8",
        "upstream": "MediaTek MET v3 source drop (no vendor Git metadata)",
        "upstream_commit": "c9b8a7d47a28d6e8362e667b629739adf79090e8",
        "upstream_extra": "source-owning pin recorded by the local MET provenance note",
        "license": "GPL-2.0",
        "build_modules": [
            "met.ko",
            "met_backlight_api.ko",
            "met_emi_api.ko",
            "met_gpu_adv_api.ko",
            "met_gpu_api.ko",
            "met_gpueb_api.ko",
            "met_ipi_api.ko",
            "met_mcupm_api.ko",
            "met_scmi_api.ko",
            "met_sspm_api.ko",
            "met_vcore_api.ko",
        ],
        "nested": [],
    },
}

BUILD_BASENAMES = {
    "Kbuild", "Makefile", "README", "Android.mk", "Android.mk_disable",
    "BUILD.bazel", "Kconfig", "Kconfig.targetc", ".gitignore", ".checkpatch.conf",
}
BUILD_EXTENSIONS = {
    ".mk", ".bp", ".rc", ".cfg", ".sh", ".md", ".txt", ".json", ".xml",
    ".bazel", ".conf", ".rules", ".pl", ".none", ".openwrt", ".ce", ".lnt",
    ".x86", ".ddk", ".defconfig",
}
BLOCKED_EXTENSIONS = {".bin", ".dat", ".o", ".ko", ".mod", ".cmd", ".order", ".symvers", ".new"}
# Developer-machine paths; /mnt/sdcard is an Android runtime path, not a leak.
LOCAL_PATH_RE = re.compile(r"/home/[A-Za-z0-9_.-]+/|/mnt/(?!sdcard/)[A-Za-z0-9_.-]+/")

# Upstream relative symlinks that intentionally stay unresolved in the import.
# Each must be justified; any other dangling link aborts the import.
KNOWN_UNRESOLVED_LINKS = {
    ("connectivity", "gps/data_link/plat/v060/gps_mcudl"): "OPTIONAL_COMPONENT",
    ("connectivity", "gps/data_link/plat/v061/gps_mcudl"): "OPTIONAL_COMPONENT",
    ("connectivity", "gps/data_link/plat/v061_mt6653/gps_mcudl"): "OPTIONAL_COMPONENT",
    ("connectivity", "wlan/core/gen4m/ext"): "OPTIONAL_COMPONENT",
}
# LieppOS-authored files that replace upstream files excluded from the import.
# They live in the kernel repository, are never copied from the source root,
# and are recorded with their hash. The upstream counterpart must be excluded
# for the stated reason, otherwise the import fails.
LIEPPOS_AUTHORED = {
    ("connectivity", "wlan/core/gen4m/mgmt/tkip_mic.c"): {
        "replaces_exclusion": "restrictive",
        "license": "BSD-2-Clause",
        "origin": "INDEPENDENT",
        "description": ("Independent TKIP Michael MIC receive verification "
                        "(IEEE Std 802.11-2020 12.5.2.3) implementing the "
                        "tkipMicDecapsulate*() interface of include/mgmt/privacy.h; "
                        "C50, see docs C50-WIFI-SOURCE-IMPLEMENTATION"),
    },
}

UNRESOLVED_REASONS = {
    "gps_mcudl": ("gps/gps_mcudl is only used by GPS data_link v060/v061 "
                  "(not MT6878, which builds v051) and embeds MCU firmware "
                  "(plat/v060/fw/inc/mcu_rom_code.h); not imported"),
    "ext": ("wlan/core/gen4m_ext is a separate vendor extension project that "
            "is absent from the source repository; Kbuild.main only uses it "
            "when CONFIG_MTK_TC10_FEATURE=y (CONFIG_MTK_WIFI_EXT), which "
            "MT6878 does not set"),
}
RESTRICTIVE_RE = re.compile(
    r"strictly prohibited|confidential trade secret|unpublished work|"
    r"no redistribution|not redistribut|confidential and proprietary|"
    r"authorised by a licensing agreement|authorized by a licensing agreement",
    re.I,
)


def git_files(repo: str) -> list[str]:
    out = subprocess.check_output(["git", "-C", repo, "ls-files", "-z"], env=GIT_ENV).decode()
    return [f for f in out.split("\0") if f]


def read_bytes(path: str) -> bytes:
    with open(path, "rb") as fh:
        return fh.read()


def classify(path: str) -> str:
    """Return licensed/buildfile/symlink/binary/restrictive/unlicensed/localpath/missing."""
    if os.path.islink(path):
        return "symlink"
    if not os.path.exists(path):
        return "missing"
    data = read_bytes(path)
    if b"\0" in data[:8192]:
        return "binary"
    text = data[:200000].decode("utf-8", "ignore")
    if LOCAL_PATH_RE.search(text):
        return "localpath"
    if RESTRICTIVE_RE.search(text):
        return "restrictive"
    if re.search(r"SPDX-License-Identifier:", text):
        return "licensed"
    low = text.lower()
    for marker in (
        "gnu general public license",
        "gpl-2.0",
        "gpl v2",
        "redistribution and use in source and binary forms",
        "apache license",
        "may be distributed under the terms of the bsd license",
    ):
        if marker in low:
            return "licensed"
    base = os.path.basename(path)
    ext = os.path.splitext(path)[1].lower()
    if base in BUILD_BASENAMES or base.startswith(("Kbuild", "Makefile", "Kconfig")):
        return "buildfile"
    if ext in BUILD_EXTENSIONS:
        return "buildfile"
    if "/configs/" in path and (base == "defconfig" or ext == ""):
        return "buildfile"
    return "unlicensed"


def used_paths(source_root: str, dirs: list[str]) -> set[str]:
    """Return source paths referenced by prior build command records, if present."""
    used: set[str] = set()
    for d in dirs:
        root = os.path.join(source_root, d)
        if not os.path.isdir(root):
            continue
        for dirpath, _dirnames, filenames in os.walk(root):
            for name in filenames:
                if not (name.endswith(".o.cmd") or name.endswith(".ko.cmd")):
                    continue
                try:
                    text = open(os.path.join(dirpath, name), errors="ignore").read()
                except OSError:
                    continue
                for match in re.findall(r"(\S+\.(?:c|h|S|s))", text):
                    p = match
                    if p.startswith(source_root + "/"):
                        rel = os.path.relpath(p, source_root)
                    elif p.startswith("../"):
                        rel = p[3:]
                    else:
                        continue
                    used.add(rel.replace("\\", "/"))
    return used


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--source-root", default=os.environ.get("LIEPPOS_VENDOR_SOURCE_ROOT"))
    ap.add_argument("--kernel", default=KERNEL_ROOT)
    ap.add_argument("--quarantine", default=None,
                    help="optional local directory for blocked files (never inside the repository)")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    if not args.source_root:
        print("error: --source-root or LIEPPOS_VENDOR_SOURCE_ROOT is required", file=sys.stderr)
        return 2

    source_root = os.path.abspath(args.source_root)
    kernel = os.path.abspath(args.kernel)
    dest_rel = "vendor/mediatek/kernel_modules"
    dest_root = os.path.join(kernel, dest_rel)
    quarantine = os.path.abspath(args.quarantine) if args.quarantine else None
    if quarantine and (quarantine + "/").startswith(kernel + "/"):
        print("error: quarantine must be outside the kernel repository", file=sys.stderr)
        return 2

    if not os.path.isdir(os.path.join(kernel, ".git")):
        print(f"error: kernel repository not found: {kernel}", file=sys.stderr)
        return 2
    overlay = {f"{dest_rel}/{c}/{p}" for c, p in LIEPPOS_AUTHORED}
    dirty = [line for line in subprocess.run(
        ["git", "-C", kernel, "status", "--porcelain", "--", dest_rel],
        capture_output=True, text=True).stdout.splitlines() if line[3:] not in overlay]
    if dirty:
        print(f"error: {dest_rel} has uncommitted changes", file=sys.stderr)
        return 2

    used = {
        "connectivity": used_paths(
            source_root,
            [os.path.join(COMPONENTS["connectivity"]["repo"], d) for d in COMPONENTS["connectivity"]["dirs"]],
        ),
        "mtkcam": used_paths(
            source_root,
            [os.path.join(COMPONENTS["mtkcam"]["repo"], d) for d in COMPONENTS["mtkcam"]["dirs"]],
        ),
    }

    report = {
        "schema": 1,
        "import_method": "tracked source snapshot; no upstream Git history is claimed",
        "source_root_label": "authorized external development workspace (path omitted)",
        "components": {},
        "totals": {"imported": 0, "excluded": 0, "bytes": 0},
    }

    for name, spec in COMPONENTS.items():
        repo = os.path.join(source_root, spec["repo"])
        if not os.path.isdir(repo):
            print(f"error: missing source repository {spec['repo']}", file=sys.stderr)
            return 2
        files = git_files(repo)
        for nested, _sha in spec["nested"]:
            nested_repo = os.path.join(repo, nested)
            files += [f"{nested}/{f}" for f in git_files(nested_repo)]

        selected: list[str] = []
        for rel in files:
            if spec["dirs"] == [""] or any(rel == d or rel.startswith(d + "/") for d in spec["dirs"] if d):
                selected.append(rel)

        entries = []
        excluded = []
        for rel in sorted(selected):
            src = os.path.join(repo, rel)
            ext = os.path.splitext(rel)[1].lower()
            if ext in BLOCKED_EXTENSIONS or rel.endswith(".o.new"):
                excluded.append({"path": rel, "reason": "build artifact or firmware blob extension"})
                continue
            cat = classify(src)
            if cat == "missing":
                excluded.append({"path": rel, "reason": "missing from working tree"})
                continue
            if cat in {"binary", "restrictive", "localpath"}:
                excluded.append({"path": rel, "reason": cat})
                continue
            if cat == "unlicensed":
                # Keep only files proven to be compiled by the MT6878 build; the
                # rest are unused chip-family or generated register headers.
                key = f"{spec['repo']}/{rel}" if spec["repo"] in ("connectivity", "mtkcam") else rel
                if key in used.get(name, set()):
                    cat = "unlicensed-used"
                else:
                    excluded.append({"path": rel, "reason": "no recognized license; not used by MT6878 build"})
                    continue
            if os.path.splitext(rel)[1].lower() in BLOCKED_EXTENSIONS and cat != "buildfile":
                excluded.append({"path": rel, "reason": "build artifact or firmware blob extension"})
                continue

            dst = os.path.join(dest_root, name, rel)
            entry = {"path": rel, "kind": cat}
            if os.path.islink(src):
                entry["link"] = os.readlink(src)
            else:
                data = read_bytes(src)
                entry["sha256"] = hashlib.sha256(data).hexdigest()
                entry["size"] = len(data)
            entries.append(entry)

        # Quarantine the blocked files for local audit; never place them in the repo.
        for item in excluded:
            if item["reason"] not in {"restrictive", "binary", "unlicensed"}:
                continue
            src = os.path.join(repo, item["path"])
            if quarantine and os.path.isfile(src) and not os.path.islink(src):
                q = os.path.join(quarantine, name, item["path"])
                os.makedirs(os.path.dirname(q), exist_ok=True)
                if not args.dry_run:
                    shutil.copy2(src, q)

        if not args.dry_run:
            for entry in entries:
                src = os.path.join(repo, entry["path"])
                dst = os.path.join(dest_root, name, entry["path"])
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                if os.path.lexists(dst):
                    os.remove(dst)
                if "link" in entry:
                    os.symlink(entry["link"], dst)
                else:
                    shutil.copy2(src, dst)

        comp = {
            "source_repository": spec["repo"],
            "source_commit": spec["source_commit"],
            "upstream": spec["upstream"],
            "upstream_commit": spec["upstream_commit"],
            "upstream_extra": spec["upstream_extra"],
            "license": spec["license"],
            "build_modules": spec["build_modules"],
            "selected_paths": spec["dirs"],
            "imported_files": len(entries),
            "imported_bytes": sum(e.get("size", 0) for e in entries),
            "excluded_files": len(excluded),
            "exclusions": excluded,
            "files": entries,
        }
        report["components"][name] = comp
        report["totals"]["imported"] += len(entries)
        report["totals"]["excluded"] += len(excluded)
        report["totals"]["bytes"] += comp["imported_bytes"]
        print(f"{name:14} imported={len(entries):5} excluded={len(excluded):4} bytes={comp['imported_bytes']/1024/1024:.1f} MiB")

    if not args.dry_run:
        unexpected = []
        unresolved = []
        for name, comp in report["components"].items():
            for entry in comp["files"]:
                if "link" not in entry:
                    continue
                dst = os.path.join(dest_root, name, entry["path"])
                if os.path.exists(dst):
                    continue
                cls = KNOWN_UNRESOLVED_LINKS.get((name, entry["path"]))
                if cls is None:
                    unexpected.append(f"{name}/{entry['path']} -> {entry['link']}")
                    continue
                reason = UNRESOLVED_REASONS[os.path.basename(entry["path"])]
                unresolved.append({"component": name, "path": entry["path"], "link": entry["link"],
                                   "class": cls, "reason": reason})
        if unexpected:
            print("error: unexpected dangling symlinks:\n  " + "\n  ".join(unexpected), file=sys.stderr)
            return 3
        report["unresolved_symlinks"] = unresolved
        authored = []
        for (name, rel), meta in sorted(LIEPPOS_AUTHORED.items()):
            reasons = {e["path"]: e["reason"] for e in report["components"][name]["exclusions"]}
            if reasons.get(rel) != meta["replaces_exclusion"]:
                print(f"error: {name}/{rel}: upstream file is not excluded as "
                      f"{meta['replaces_exclusion']}", file=sys.stderr)
                return 3
            dst = os.path.join(dest_root, name, rel)
            if not os.path.isfile(dst) or os.path.islink(dst):
                print(f"error: LieppOS-authored file missing: {name}/{rel}", file=sys.stderr)
                return 3
            data = read_bytes(dst)
            authored.append(dict(meta, component=name, path=rel,
                                 sha256=hashlib.sha256(data).hexdigest(), size=len(data)))
        report["lieppos_authored"] = authored
        os.makedirs(dest_root, exist_ok=True)
        with open(os.path.join(dest_root, "SOURCE-MANIFEST.json"), "w") as fh:
            json.dump(report, fh, indent=1, sort_keys=True)
            fh.write("\n")
        print(f"wrote {os.path.join(dest_root, 'SOURCE-MANIFEST.json')}")
    print(f"totals imported={report['totals']['imported']} excluded={report['totals']['excluded']} bytes={report['totals']['bytes']/1024/1024:.1f} MiB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
