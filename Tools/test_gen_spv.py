#!/usr/bin/env python3
"""Regression tests for the Vulkan SPIR-V header generator."""
import pathlib
import subprocess
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "Tools" / "gen_spv.py"
HEADER = ROOT / "Src" / "XGui" / "Graphics" / "XGpuRenderDriver_vulkan_shaders.h"


class GenSpvTests(unittest.TestCase):
    def test_generated_header_is_utf8(self):
        subprocess.run([sys.executable, str(GENERATOR)], cwd=ROOT, check=True)
        HEADER.read_text(encoding="utf-8")


if __name__ == "__main__":
    unittest.main()
