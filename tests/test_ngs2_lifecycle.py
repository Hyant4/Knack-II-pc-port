"""Independent synthetic analysis & contract tests for NGS2 Voice Lifecycle & VoiceGetState (Revision 3).

Follows Tech Lead Sol directives (DECISAO_ADENDO_E_PROXIMA_TAREFA.md):
- Separates verified PASS cases, positive RED gates, and BLOCKED contract demonstrations.
- Acknowledges proven instruction masks vs consumer interpretation names.
- SubMixer lifecycle explicitly documented as UNCOVERED (0x10 query on voice_a only).
- Post-RED drain transition assertions acknowledged as UNTESTED/BLOCKED.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

# 1. Baseline conforming cases: Sizes, idempotence, isolation, reconfig
BASELINE_PASS_CASES = (
    'buffer_size_validation',
    'query_idempotence',
    'voice_isolation',
    'reconfig_generation_cancellation',
)

# 2. Positive contract gates proven RED on baseline with exact failure predicates
POSITIVE_CONTRACT_RED_CASES = (
    ('streaming_empty_active', '(flags & SCE_NGS2_STATE_ACTIVE) != 0'),
    ('inflight_decode_labeled_injection', '(flags & SCE_NGS2_STATE_ACTIVE) != 0'),
    ('pause_active_state', '(flags & SCE_NGS2_STATE_ACTIVE) != 0'),
    ('drain_transition_and_completion', '(flags1 & SCE_NGS2_STATE_ACTIVE) != 0'),
)

# 3. Blocked contract demonstrations: sensitive to engine state but expectations are blocked
BLOCKED_CONTRACT_CASES = (
    ('consumer_gain_vector_impact', 'effective_gain > 0.0f',
     'BLOCKED: effective_gain > 0.0f not universal in PLAY (attack envelope/mute can yield 0.0f); fixture pending'),
    ('position_progress_and_pause_freeze', 'pos_a1 > pos_a0',
     'BLOCKED: pause without render does not test engine progress freeze; units/width/monotonicity unproven'),
)

class Ngs2LifecycleContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        tc = Path(os.environ.get('MSYS2_ROOT') or Path.home() / 'AppData/Local/knack2-toolchain/msys64')
        cc = tc / 'clang64/bin/clang.exe'
        if not cc.is_file():
            raise unittest.SkipTest(f"Compiler not found at {cc}")
        cls.cc = cc

        cls.env = {k: v for k, v in os.environ.items() if not k.startswith(('TLG_', 'SDL_'))}
        cls.env['PATH'] = str(cc.parent) + os.pathsep + cls.env.get('PATH', '')

        cls.tmpdir = tempfile.TemporaryDirectory(prefix='knack-life3-')
        cls.exe = Path(cls.tmpdir.name) / 'lifecycle.exe'

        cmd = [
            str(cls.cc), '-std=c11', '-Wall', '-Wextra', '-Werror',
            '-Wframe-larger-than=65536',
            '-I' + str(ROOT / 'port/src'),
            '-I' + str(ROOT / 'port'),
            str(ROOT / 'tests/test_ngs2_lifecycle.c'),
            '-pthread', '-lm', '-o', str(cls.exe)
        ]
        build = subprocess.run(cmd, env=cls.env, capture_output=True, text=True, timeout=60)
        if build.returncode != 0:
            cls.tmpdir.cleanup()
            raise RuntimeError(f"Build failed: {build.stdout}\n{build.stderr}")

    @classmethod
    def tearDownClass(cls):
        cls.tmpdir.cleanup()

    def test_baseline_conformance_cases(self):
        """Cases that already conform on baseline: sizes, idempotence, isolation, reconfig."""
        for case in BASELINE_PASS_CASES:
            with self.subTest(case=case):
                run = subprocess.run([str(self.exe), case], env=self.env, capture_output=True, text=True, timeout=10)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                self.assertIn(f'PASS_LIFECYCLE_CASE {case}', run.stdout)

    def test_baseline_observations_for_pending_contracts(self):
        """Baseline observation mode returns exit 0 and logs current baseline values."""
        all_pending = [c for c, _ in POSITIVE_CONTRACT_RED_CASES] + [c for c, _, _ in BLOCKED_CONTRACT_CASES]
        for case in all_pending:
            with self.subTest(case=case):
                run = subprocess.run([str(self.exe), case], env=self.env, capture_output=True, text=True, timeout=10)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                self.assertIn(f'PASS_LIFECYCLE_CASE {case}', run.stdout)
                self.assertIn('BASELINE OBSERVATION:', run.stdout)

    def test_positive_contracts_green(self):
        """Implemented lifecycle contracts must satisfy all assertions."""
        for case, _ in POSITIVE_CONTRACT_RED_CASES:
            with self.subTest(case=case):
                run = subprocess.run([str(self.exe), case, '--assert-positive-contract'],
                                     env=self.env, capture_output=True, text=True, timeout=10)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                self.assertIn(f'PASS_LIFECYCLE_CASE {case}', run.stdout)

    def test_blocked_contract_demonstrations_fail_red(self):
        """Assert blocked contract demonstrations prove sensitivity failure on baseline."""
        for case, predicate, reason in BLOCKED_CONTRACT_CASES:
            with self.subTest(case=case, reason=reason):
                run = subprocess.run([str(self.exe), case, '--assert-positive-contract'],
                                     env=self.env, capture_output=True, text=True, timeout=10)
                output = run.stdout + run.stderr
                self.assertNotEqual(run.returncode, 0,
                                    f"Blocked contract demonstration for {case} was expected to fail on baseline, but passed!")
                self.assertIn(f'LIFECYCLE_CONTRACT_FAILED case={case}', output)
                self.assertIn(predicate, output)

if __name__ == '__main__':
    unittest.main()
