"""Independent Synthetic Test Suite for NGS2 ATRAC9 Trimming & Repeat Controls.

Automates execution per Tech Lead Sol directive (REVISAO_E_PACOTE_TESTES.md):
- Isolates 8 independent trimming and repeat cases in separate processes.
- Compiles against selectable runtime source via:
  1. CLI argument: --runtime-source <path>
  2. Environment variable: KNACK_RUNTIME_SOURCE
  3. Default: runtime source beside this test tree; explicit source selection is supported.
- Offers two explicit execution modes:
  1. Diagnostic Mode (default): confirms candidate RED on the exact missing control predicate,
     verifying baseline-conforming assertions while safely halting post-RED checks.
  2. Positive Contract Mode (--assert-positive-contract): verifies that on candidate/baseline
     each case fails RED with CONTROLS_CONTRACT_FAILED and the exact predicate. When Sol's
     subsequent candidate with worker/trimming/loop is applied, all 8 cases will pass GREEN.
"""

import os
try:
    from tests.native_toolchain import c_compiler
except ModuleNotFoundError:
    from native_toolchain import c_compiler
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASELINE_TLG = ROOT / "port/src/runtime_tlg.c"

# The 8 required acceptance cases and their exact missing control gate predicates
CONTROLS_CASES = (
    ("finite_trimming", "trim_pcm_exact"),
    ("cross_boundary_trimming", "cross_boundary_pcm_exact"),
    ("consecutive_finite_blocks", "block_chain_pcm_exact"),
    ("finite_repeats", "repeat_pcm_exact_without_interpass_padding"),
    ("infinite_loop", "infinite_loop_pcm_exact"),
    ("intro_then_loop", "intro_loop_pcm_exact"),
    ("pause_resume_under_repeat", "repeat_paused_resumed_exact"),
    ("boundary_and_rejection_limits", "bounds_validated_cleanly"),
    ("cancelled_snapshot_reuse", "snapshot_reuse_trim_pcm_exact"),
)


def get_runtime_source():
    # 1. Environment variable
    env_src = os.environ.get("KNACK_RUNTIME_SOURCE")
    if env_src:
        if not Path(env_src).is_file(): raise FileNotFoundError(env_src)
        return Path(env_src).resolve()

    # 3. Repository baseline
    return BASELINE_TLG.resolve()


RUNTIME_SOURCE = get_runtime_source()
EXPECT_GREEN = '--expect-red' not in sys.argv
if '--expect-red' in sys.argv: sys.argv.remove('--expect-red')


class Ngs2Atrac9ControlsContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        tc = Path(os.environ.get("MSYS2_ROOT") or Path.home() / "AppData/Local/knack2-toolchain/msys64")
        cc = c_compiler()
        if not cc.is_file():
            raise unittest.SkipTest(f"MSYS2 CLANG64 compiler not found at {cc}")
        cls.cc = cc

        cls.env = {k: v for k, v in os.environ.items() if not k.startswith(("TLG_", "SDL_"))}
        cls.env["PATH"] = str(cc.parent) + os.pathsep + cls.env.get("PATH", "")

        cls.tmpdir = tempfile.TemporaryDirectory(prefix="knack-controls-")
        cls.exe = Path(cls.tmpdir.name) / "controls.exe"

        src_arg = str(RUNTIME_SOURCE).replace("\\", "/")
        cmd = [
            str(cls.cc),
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wframe-larger-than=65536",
            "-I" + str(ROOT / "port/src"),
            "-I" + str(ROOT / "port"),
            f"-DRUNTIME_TLG_SOURCE=\"{src_arg}\"",
            str(ROOT / "tests/test_ngs2_atrac9_controls.c"),
            "-pthread",
            "-lm",
            "-o",
            str(cls.exe),
        ]
        build = subprocess.run(cmd, env=cls.env, capture_output=True, text=True, timeout=60)
        if build.returncode != 0:
            cls.tmpdir.cleanup()
            raise RuntimeError(f"Build failed for {src_arg}:\nSTDOUT:\n{build.stdout}\nSTDERR:\n{build.stderr}")

    @classmethod
    def tearDownClass(cls):
        cls.tmpdir.cleanup()

    @unittest.skipIf(EXPECT_GREEN, "Use --expect-red for the previous candidate")
    def test_diagnostic_mode_confirms_missing_controls_red(self):
        """Diagnostic mode runs each case, confirms baseline/candidate conformance, and proves exact RED gate."""
        for case, expected_predicate in CONTROLS_CASES:
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
                    f"Diagnostic mode for {case} exited with {run.returncode}:\n{output}",
                )
                self.assertIn(
                    f"CONTROLS_RED_CONFIRMED case={case}",
                    output,
                    f"Expected CONTROLS_RED_CONFIRMED for {case}:\n{output}",
                )
                self.assertIn(
                    f"predicate={expected_predicate}",
                    output,
                    f"Expected failure predicate '{expected_predicate}' for {case}:\n{output}",
                )

    @unittest.skipIf(EXPECT_GREEN, "Use --expect-red for the previous candidate")
    def test_positive_contracts_fail_red_on_missing_controls(self):
        """Positive contract mode (--assert-positive-contract) must fail RED on current source with exact predicate."""
        for case, expected_predicate in CONTROLS_CASES:
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
                    f"Positive contract for {case} was expected to FAIL RED, but exited 0!\n{output}",
                )
                self.assertIn(
                    f"CONTROLS_CONTRACT_FAILED case={case}",
                    output,
                    f"Expected CONTROLS_CONTRACT_FAILED for {case}:\n{output}",
                )
                self.assertIn(
                    expected_predicate,
                    output,
                    f"Expected failing predicate '{expected_predicate}' in error output:\n{output}",
                )

    @unittest.skipUnless(EXPECT_GREEN, "Positive candidate mode")
    def test_positive_pcm_contracts_green(self):
        for case, _ in CONTROLS_CASES:
            with self.subTest(case=case):
                run = subprocess.run([str(self.exe),case,'--assert-positive-contract'],
                                     env=self.env,capture_output=True,text=True,timeout=15)
                self.assertEqual(run.returncode,0,run.stdout+run.stderr)
                self.assertIn('PASS_CONTROLS_CASE '+case,run.stdout)

if __name__ == "__main__":
    # Custom CLI argument parsing for --runtime-source
    args = []
    i = 1
    while i < len(sys.argv):
        if sys.argv[i] == "--runtime-source" and i + 1 < len(sys.argv):
            RUNTIME_SOURCE = Path(sys.argv[i + 1]).resolve()
            i += 2
        else:
            args.append(sys.argv[i])
            i += 1
    sys.argv = [sys.argv[0]] + args
    unittest.main()
