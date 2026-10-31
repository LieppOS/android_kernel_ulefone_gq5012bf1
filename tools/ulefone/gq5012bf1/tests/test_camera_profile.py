# SPDX-License-Identifier: GPL-2.0-only
"""Camera build preparation tests; synthetic C syntax checks, no device access."""
from pathlib import Path
import os
import runpy
import subprocess
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1]
ROOT = TOOLS.parents[2]
WRAPPER = TOOLS / "build-camera.sh"


class CameraProfileTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name) / "baseline.config"
        self.base.write_text('CONFIG_KSU=y\nCONFIG_KSU_SUSFS=y\nCONFIG_CUSTOM_KERNEL_IMGSENSOR="donor"\nCONFIG_CFI_CLANG=y\n')
        self.out = Path(self.tmp.name) / "out"

    def prepare(self, **changes):
        env = dict(os.environ, KERNEL_SRC=str(ROOT), BASE_CONFIG=str(self.base),
                   OUT=str(self.out), PREPARE_ONLY="1")
        env.update(changes)
        return subprocess.run(["bash", str(WRAPPER)], env=env, capture_output=True, text=True)

    def test_changes_only_sensor_selection_and_can_resume(self):
        self.assertEqual(self.prepare().returncode, 0)
        expected = next(x for x in (ROOT / "configs/gq5012bf1-camera.fragment").read_text().splitlines()
                        if x.startswith("CONFIG_CUSTOM_KERNEL_IMGSENSOR="))
        before = self.base.read_text().splitlines()
        after = (self.out / ".config").read_text().splitlines()
        self.assertEqual([x for x in after if not x.startswith("CONFIG_CUSTOM_KERNEL_IMGSENSOR=")],
                         [x for x in before if not x.startswith("CONFIG_CUSTOM_KERNEL_IMGSENSOR=")])
        self.assertIn(expected, after)
        self.assertEqual(self.prepare().returncode, 0)

    def test_rejects_and_preserves_stale_output(self):
        self.out.mkdir()
        stale = self.base.read_bytes()
        (self.out / ".config").write_bytes(stale)
        self.assertNotEqual(self.prepare().returncode, 0)
        self.assertEqual((self.out / ".config").read_bytes(), stale)

    def test_rejects_missing_or_duplicate_base_selection(self):
        for content in ('CONFIG_KSU=y\n', 'CONFIG_CUSTOM_KERNEL_IMGSENSOR="a"\nCONFIG_CUSTOM_KERNEL_IMGSENSOR="b"\n'):
            with self.subTest(content=content):
                self.base.write_text(content)
                self.assertNotEqual(self.prepare().returncode, 0)
                self.assertFalse((self.out / ".config").exists())

    def test_board_defconfig_uses_same_profile(self):
        def selection(p):
            return [x for x in p.read_text().splitlines() if x.startswith("CONFIG_CUSTOM_KERNEL_IMGSENSOR=")]
        self.assertEqual(selection(ROOT / "configs/gq5012bf1-camera.fragment"),
                         selection(ROOT / "arch/arm64/configs/gq5012bf1_defconfig"))


class CameraDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.helpers = runpy.run_path(str(TOOLS / "prepare-camera-data.py"))

    def test_coordinate_pairs_are_braced_without_changing_values(self):
        normalize = self.helpers["normalize"]
        before = '.i4Crop = {0, 384, 2048, 1536}, .i4PosL = {9, 8}, .i4PosR = {8, 8}'
        after = normalize(before)
        self.assertEqual(after, '.i4Crop = {{0, 384}, {2048, 1536}}, .i4PosL = {{9, 8}}, .i4PosR = {{8, 8}}')
        self.assertEqual(normalize(after), after)
        with self.assertRaises(ValueError):
            normalize('.i4Crop = {1, 2, 3}')

    def test_normalized_synthetic_initializer_passes_strict_compiler(self):
        source = 'struct pd { int i4Crop[2][2]; }; static struct pd x = { .i4Crop = {1, 2, 3, 4} };'
        command = ['cc', '-Werror', '-Wmissing-braces', '-x', 'c', '-fsyntax-only', '-']
        self.assertNotEqual(subprocess.run(command, input=source, text=True, capture_output=True).returncode, 0)
        fixed = self.helpers['normalize'](source)
        result = subprocess.run(command, input=fixed, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_preparation_preserves_references_and_rejects_changed_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'source'
            output = Path(tmp) / 'output'
            source.mkdir()
            for name in self.helpers['NAMES']:
                (source / (name + '_stock_data.h')).write_text('.i4Crop = {1, 2}')
            before = {p.name: p.read_bytes() for p in source.iterdir()}
            self.helpers['prepare'](source, output)
            self.helpers['prepare'](source, output)
            self.assertEqual(before, {p.name: p.read_bytes() for p in source.iterdir()})
            (output / 'imx989_stock_data.h').write_text('unexpected output')
            with self.assertRaises(ValueError):
                self.helpers['prepare'](source, output)
            self.assertEqual((output / 'imx989_stock_data.h').read_text(), 'unexpected output')
            with self.assertRaises(ValueError):
                self.helpers['prepare'](source, source)


if __name__ == "__main__":
    unittest.main()
