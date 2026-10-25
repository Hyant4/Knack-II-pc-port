"""HLE-2.2D-B2-F1: passive compressed-memory diagnostic."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = Path(os.environ.get("MSYS2_ROOT") or
                 Path.home() / "AppData/Local/knack2-toolchain/msys64")

class Ngs2B2F1Tests(unittest.TestCase):
    def test_passive_memory_reconciliation(self):
        compiler = TOOLCHAIN / "clang64/bin/clang.exe"
        self.assertTrue(compiler.is_file(), f"Compiler missing: {compiler}")
        env = dict(os.environ,
                   PATH=str(compiler.parent)+os.pathsep+
                        os.environ.get("PATH",""))
        with tempfile.TemporaryDirectory(prefix="knack-b2f1-") as d:
            exe=Path(d)/"ngs2-b2f1.exe"
            cmd=[str(compiler),"-std=c11","-Wall","-Wextra","-Werror",
                 "-I"+str(ROOT/"port/src"),"-I"+str(ROOT/"port"),
                 str(ROOT/"tests/test_ngs2_b2f1_diag.c"),
                 "-pthread","-lm","-o",str(exe)]
            built=subprocess.run(cmd,env=env,capture_output=True,
                                 text=True,timeout=60)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(exe)],env=env,capture_output=True,
                               text=True,timeout=60)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
            self.assertIn(
                "ALL HLE-2.2D-B2-F1 DIAGNOSTIC TESTS PASSED",
                ran.stdout)

if __name__=="__main__":
    unittest.main()
