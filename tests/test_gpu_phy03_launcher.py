"""Launcher contract checks. No runtime, game, GPU or save is opened."""

import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock


SPEC = importlib.util.spec_from_file_location("gpu_phy03_launcher", Path(__file__).resolve().parents[1] / "scripts/run_knack2_gpu_phy03.py")
launcher = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(launcher)


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / "synthetic-dump"
        self.game.mkdir()
        self.probe = self.root / "tlg-probe.exe"
        self.probe.write_bytes(b"synthetic fixture, never executed")

    def paths(self, out=None, capture=None):
        out = out or self.root / "fresh"
        return launcher.validate_paths(out, capture or out / "control", self.game, self.probe)

    def test_validation_has_no_writes(self):
        before = set(self.root.rglob("*"))
        out, capture, _, _ = self.paths()
        self.assertFalse(out.exists())
        self.assertFalse(capture.exists())
        self.assertEqual(before, set(self.root.rglob("*")))

    def test_existing_output_rejected_even_if_empty(self):
        out = self.root / "exists"
        out.mkdir()
        with self.assertRaisesRegex(ValueError, "new directory"):
            self.paths(out)

    def test_capture_outside_output_rejected(self):
        with self.assertRaisesRegex(ValueError, "inside"):
            self.paths(capture=self.root / "elsewhere")

    def test_dump_overlap_rejected(self):
        with self.assertRaisesRegex(ValueError, "overlaps"):
            self.paths(out=self.game / "child")

    def test_normal_output_overlap_rejected(self):
        with self.assertRaisesRegex(ValueError, "overlaps"):
            self.paths(out=launcher.ROOT / "out/gpu-phy03-never-create")

    def test_environment_retains_runner_filter_and_adds_only_three_keys(self):
        base = {"PATH": "synthetic", "TLG_UPSCALER": "off", "TLG_CONFIG": "isolated.ini"}
        env = launcher.diagnostic_environment(base, 16, self.root / "control")
        self.assertEqual(set(env) - set(base), {"TLG_GPU_PHY03_TRIGGER", "TLG_GPU_PHY03_DIR", "TLG_GPU_PHY03_FRAMES"})
        self.assertEqual(env["TLG_GPU_PHY03_FRAMES"], "16")
        self.assertEqual(env["TLG_UPSCALER"], "off")
        self.assertNotIn("TLG_GPU_PHY03_TRIGGER", base)
        self.assertNotIn("TLG_KNACK_SMALL_SYNC", env)
        self.assertFalse((self.root / "control").exists())

    def test_final_frame_mode_disables_lineage_and_legacy_dump_triggers(self):
        base = {"PATH": "synthetic", "TLG_UPSCALER": "off", "TLG_GPU_PHY03_TRIGGER": "old",
                "TLG_GPU_PHY03_DIR": "old", "TLG_GPU_PHY03_FRAMES": "2",
                "TLG_FRAME_DUMP_TRIGGER": "old", "TLG_FRAME_DUMP_COUNT": "2"}
        env = launcher.sequence_environment(base, 16, self.root / "control")
        self.assertFalse(any(k.startswith("TLG_GPU_PHY03_") for k in env))
        self.assertNotIn("TLG_FRAME_DUMP_TRIGGER", env)
        self.assertEqual(env["TLG_FRAME_SEQUENCE_COUNT"], "16")
        self.assertEqual(env["TLG_FRAME_SEQUENCE_TRIGGER"], str(self.root / "control/frame_sequence.request"))
        self.assertEqual(env["TLG_FRAME_SEQUENCE_DIR"], str(self.root / "control/frame_sequence"))
        self.assertEqual(env["TLG_UPSCALER"], "off")
        self.assertEqual(base["TLG_GPU_PHY03_FRAMES"], "2")
        self.assertFalse((self.root / "control").exists())

    def test_stage_observations_are_explicit_and_use_independent_output(self):
        env = launcher.sequence_environment({}, 16, self.root / "control", observe_stages=True)
        self.assertEqual(env["TLG_GPU_PHY03_OBSERVE"], "1")
        self.assertEqual(env["TLG_GPU_PHY03_FRAMES"], env["TLG_FRAME_SEQUENCE_COUNT"])
        self.assertNotEqual(env["TLG_GPU_PHY03_DIR"], env["TLG_FRAME_SEQUENCE_DIR"])
        self.assertTrue(env["TLG_GPU_PHY03_DIR"].endswith("stage_observations"))

    def test_trace_requires_observation_mode_and_is_explicit(self):
        with self.assertRaisesRegex(ValueError, "requires stage"):
            launcher.sequence_environment({}, 16, self.root / "control", relic_trace=True)
        self.assertNotIn("TLG_RELIC_TRACE", launcher.sequence_environment({"TLG_RELIC_TRACE": "1"}, 16, self.root / "control"))
        env = launcher.sequence_environment({}, 16, self.root / "control", observe_stages=True, relic_trace=True)
        self.assertEqual(env["TLG_RELIC_TRACE"], "1")

    def test_pm4_trace_requires_relic_trace_and_drops_inherited_flag(self):
        with self.assertRaisesRegex(ValueError, "requires relic"):
            launcher.sequence_environment({}, 16, self.root / "control", observe_stages=True, pm4_trace=True)
        env = launcher.sequence_environment({"TLG_RELIC_PM4_TRACE": "1"}, 16, self.root / "control")
        self.assertNotIn("TLG_RELIC_PM4_TRACE", env)
        env = launcher.sequence_environment({}, 16, self.root / "control", observe_stages=True,
                                            relic_trace=True, pm4_trace=True)
        self.assertEqual(env["TLG_RELIC_PM4_TRACE"], "1")

    def test_submission_trace_is_explicit_and_requires_pm4(self):
        with self.assertRaisesRegex(ValueError, "requires PM4"):
            launcher.sequence_environment({}, 16, self.root / "control", observe_stages=True,
                                          relic_trace=True, submit_trace=True)
        env = launcher.sequence_environment({"TLG_RELIC_SUBMIT_TRACE": "1"}, 16, self.root / "control")
        self.assertNotIn("TLG_RELIC_SUBMIT_TRACE", env)
        env = launcher.sequence_environment({}, 16, self.root / "control", observe_stages=True,
                                            relic_trace=True, pm4_trace=True, submit_trace=True)
        self.assertEqual(env["TLG_RELIC_SUBMIT_TRACE"], "1")

    def test_producer_trace_is_explicit_and_requires_pm4(self):
        with self.assertRaisesRegex(ValueError, "requires PM4"):
            launcher.sequence_environment({}, 16, self.root / "control", observe_stages=True,
                                          relic_trace=True, producer_trace=True)
        env = launcher.sequence_environment({"TLG_RELIC_PRODUCER_TRACE": "1"}, 16, self.root / "control")
        self.assertNotIn("TLG_RELIC_PRODUCER_TRACE", env)
        env = launcher.sequence_environment({}, 16, self.root / "control", observe_stages=True,
                                            relic_trace=True, pm4_trace=True, producer_trace=True)
        self.assertEqual(env["TLG_RELIC_PRODUCER_TRACE"], "1")
        self.assertNotIn("TLG_RELIC_SUBMIT_TRACE", env)

    def test_list_trace_requires_producer_and_drops_inherited_flag(self):
        with self.assertRaisesRegex(ValueError, "requires producer"):
            launcher.sequence_environment({}, 16, self.root / "control", list_trace=True)
        self.assertNotIn("TLG_RELIC_LIST_TRACE", launcher.sequence_environment(
            {"TLG_RELIC_LIST_TRACE": "1"}, 16, self.root / "control"))
        env = launcher.sequence_environment({}, 16, self.root / "control", observe_stages=True,
                                            relic_trace=True, pm4_trace=True, producer_trace=True, list_trace=True)
        self.assertEqual(env["TLG_RELIC_LIST_TRACE"], "1")

    def test_trigger_consumer_can_read_and_remove_immediately_on_publication(self):
        capture = self.root / "control"
        capture.mkdir()
        real_link = launcher.os.link
        observed = []

        def consume_immediately(source, target):
            real_link(source, target)
            observed.append((target.name, target.read_bytes()))
            target.unlink()  # On Windows this fails if the writer still has a handle open.

        with mock.patch.object(launcher.os, "link", side_effect=consume_immediately):
            targets = launcher.publish_capture_triggers(capture, observe_stages=True)
        self.assertEqual(observed, [("gpu_phy03.request", b"capture\n"),
                                    ("frame_sequence.request", b"capture\n")])
        self.assertEqual(len(targets), 2)
        self.assertEqual(list(capture.iterdir()), [])

    def test_trigger_rejects_existing_capture_without_modifying_evidence(self):
        for name in ("frame_sequence", "stage_observations", "gpu_phy03", "frame_sequence.request"):
            with self.subTest(name=name):
                capture = self.root / name
                capture.mkdir()
                previous = capture / name
                previous.write_bytes(b"preserved")
                with self.assertRaisesRegex(ValueError, "fresh session"):
                    launcher.publish_capture_triggers(capture, observe_stages=True)
                self.assertEqual(previous.read_bytes(), b"preserved")
                self.assertEqual(list(capture.iterdir()), [previous])

    def test_trigger_never_overwrites_a_concurrent_publication(self):
        capture = self.root / "control"
        capture.mkdir()
        real_link = launcher.os.link

        def race(source, target):
            target.write_bytes(b"other publisher")
            real_link(source, target)

        with mock.patch.object(launcher.os, "link", side_effect=race):
            with self.assertRaises(FileExistsError):
                launcher.publish_capture_triggers(capture)
        target = capture / "frame_sequence.request"
        self.assertEqual(target.read_bytes(), b"other publisher")
        self.assertEqual(list(capture.iterdir()), [target])

    def test_trigger_partial_failure_retains_published_marker_and_cleans_temporary_files(self):
        capture = self.root / "control"
        capture.mkdir()
        real_link = launcher.os.link

        def fail_second(source, target):
            if target.name == "frame_sequence.request":
                raise PermissionError("synthetic second publication failure")
            real_link(source, target)

        with mock.patch.object(launcher.os, "link", side_effect=fail_second):
            with self.assertRaises(PermissionError):
                launcher.publish_capture_triggers(capture, observe_stages=True)
        first = capture / "gpu_phy03.request"
        self.assertEqual(first.read_bytes(), b"capture\n")
        self.assertEqual(list(capture.iterdir()), [first])


if __name__ == "__main__":
    unittest.main()
