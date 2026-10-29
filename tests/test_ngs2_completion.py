"""Independent Synthetic Test Suite for NGS2 ATRAC9 Stream Completion and Lifecycle.

Automates execution per Tech Lead Sol directive (DECISAO_EOF_E_TESTES_CANDIDATO.md):
- Isolates 7 independent lifecycle and stream completion cases in separate processes.
- Offers two explicit execution modes:
  1. Baseline Diagnostic Mode: confirms baseline RED on the exact missing contract predicate,
     verifying baseline-conforming assertions while safely halting post-RED checks.
  2. Positive Contract Mode (--assert-positive-contract): verifies that on baseline each
     case fails RED with COMPLETION_CONTRACT_FAILED and the exact predicate. When Sol's
     external candidate is applied, all 7 cases will exit 0 and pass GREEN.
"""

import os
try:
    from tests.native_toolchain import c_compiler
except ModuleNotFoundError:
    from native_toolchain import c_compiler
from pathlib import Path
import subprocess
import tempfile
import unittest
import sys

EXPECT_GREEN = "--expect-red" not in sys.argv
if "--expect-green" in sys.argv:
    sys.argv.remove("--expect-green")
if "--expect-red" in sys.argv:
    sys.argv.remove("--expect-red")

ROOT = Path(__file__).resolve().parents[1]

COMPLETION_CASES = (
    ("initial_play_no_fifo", "(flags & SCE_NGS2_STATE_ACTIVE) != 0"),
    ("inter_segment_wait", "(flags & SCE_NGS2_STATE_ACTIVE) != 0"),
    ("finite_stream_eof_and_drain", "(flags_during & SCE_NGS2_STATE_ACTIVE) != 0"),
    ("pause_resume_with_pending_eof", "(flags_paused & SCE_NGS2_STATE_ACTIVE) != 0"),
    ("eos_during_copy_barrier", "(flags_copy & SCE_NGS2_STATE_ACTIVE) != 0"),
    ("cancellation_during_decode_barrier", "va->atrac9->generation > gen_a_initial"),
    ("stop_action2_and_reuse", "(flags_reuse & SCE_NGS2_STATE_ACTIVE) != 0"),
)


class Ngs2CompletionContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        tc = Path(os.environ.get("MSYS2_ROOT") or Path.home() / "AppData/Local/knack2-toolchain/msys64")
        cc = c_compiler()
        if not cc.is_file():
            raise unittest.SkipTest(f"MSYS2 CLANG64 compiler not found at {cc}")
        cls.cc = cc

        cls.env = {k: v for k, v in os.environ.items() if not k.startswith(("TLG_", "SDL_"))}
        cls.env["PATH"] = str(cc.parent) + os.pathsep + cls.env.get("PATH", "")

        cls.tmpdir = tempfile.TemporaryDirectory(prefix="knack-completion-")
        cls.exe = Path(cls.tmpdir.name) / "completion.exe"

        cmd = [
            str(cls.cc),
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wframe-larger-than=65536",
            "-I" + str(ROOT / "port/src"),
            "-I" + str(ROOT / "port"),
            str(ROOT / "tests/test_ngs2_completion.c"),
            "-pthread",
            "-lm",
            "-o",
            str(cls.exe),
        ]
        build = subprocess.run(cmd, env=cls.env, capture_output=True, text=True, timeout=60)
        if build.returncode != 0:
            cls.tmpdir.cleanup()
            raise RuntimeError(f"Build failed:\nSTDOUT:\n{build.stdout}\nSTDERR:\n{build.stderr}")

    @classmethod
    def tearDownClass(cls):
        cls.tmpdir.cleanup()

    @unittest.skipIf(EXPECT_GREEN, "Baseline RED mode")
    def test_baseline_diagnostic_mode(self):
        """Baseline diagnostic mode runs each case, confirms baseline assertions, and verifies exact RED gate."""
        for case, expected_predicate in COMPLETION_CASES:
            with self.subTest(case=case):
                run = subprocess.run(
                    [str(self.exe), case],
                    env=self.env,
                    capture_output=True,
                    text=True,
                    timeout=15,
                )
                output = run.stdout + run.stderr
                self.assertEqual(
                    run.returncode,
                    0,
                    f"Baseline diagnostic mode for {case} exited with {run.returncode}:\n{output}",
                )
                self.assertIn(
                    f"BASELINE_RED_CONFIRMED case={case}",
                    output,
                    f"Expected baseline RED confirmation for {case}:\n{output}",
                )
                self.assertIn(
                    f"predicate={expected_predicate}",
                    output,
                    f"Expected failure predicate '{expected_predicate}' for {case}:\n{output}",
                )

    @unittest.skipIf(EXPECT_GREEN, "Baseline RED mode")
    def test_positive_contracts_fail_red_on_baseline(self):
        """Positive contract mode (--assert-positive-contract) must fail RED on baseline with exact predicate."""
        for case, expected_predicate in COMPLETION_CASES:
            with self.subTest(case=case):
                run = subprocess.run(
                    [str(self.exe), case, "--assert-positive-contract"],
                    env=self.env,
                    capture_output=True,
                    text=True,
                    timeout=15,
                )
                output = run.stdout + run.stderr
                self.assertNotEqual(
                    run.returncode,
                    0,
                    f"Positive contract for {case} was expected to FAIL RED on baseline, but exited 0!\n{output}",
                )
                self.assertIn(
                    f"COMPLETION_CONTRACT_FAILED case={case}",
                    output,
                    f"Expected COMPLETION_CONTRACT_FAILED for {case}:\n{output}",
                )
                self.assertIn(
                    expected_predicate,
                    output,
                    f"Expected failing predicate '{expected_predicate}' in error output:\n{output}",
                )

    @unittest.skipUnless(EXPECT_GREEN, "Use --expect-green for a fixed candidate")
    def test_positive_contracts_green(self):
        for case, _ in COMPLETION_CASES:
            with self.subTest(case=case):
                run = subprocess.run([str(self.exe), case, "--assert-positive-contract"],
                                     env=self.env, capture_output=True, text=True, timeout=15)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                self.assertIn(f"PASS_COMPLETION_CASE {case}", run.stdout)

if __name__ == "__main__":
    unittest.main()
