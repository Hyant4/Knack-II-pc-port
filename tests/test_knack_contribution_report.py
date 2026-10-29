import importlib.util
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ContributionReportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = load("knack_contribution_report", ROOT / "scripts/knack_contribution_report.py")
        cls.launcher = load(
            "run_knack2_contribution_capture",
            ROOT / "scripts/run_knack2_contribution_capture.py",
        )

    def log(self, text):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        path = Path(folder.name) / "game.log"
        path.write_text(text, encoding="utf-8")
        return path

    def test_valid_explicit_state_log(self):
        path = self.log(
            "KNACK-PX: Armed relaxed=1 target=8943ac2c trigger=x\n"
            "KNACK-PX: Trigger detected frame=10 path=x\n"
            "KNACK-PX: Capture started frame=10 frames=16 target=8943ac2c\n"
            "KNACK-PX: Matching draws found frame=10 target=8943ac2c\n"
            "KNACK-PX: Queries submitted frame=10 queries=4\n"
            "KNACK-PX: Results collected frame=10 queries=4\n"
            "KNACK-PX: draw=0 mode=O ps=613b2261 inst=18 samples=50\n"
            "KNACK-PX: draw=0 mode=R ps=613b2261 inst=18 samples=80\n"
            "KNACK-PX: draw=1 mode=O ps=c99fd0c1 inst=8 samples=0\n"
            "KNACK-PX: draw=1 mode=R ps=c99fd0c1 inst=8 samples=12\n"
            "KNACK-PX: Capture completed frames=1 matching_draws=2 queries=4\n"
        )
        output = self.report.analyze(path)
        self.assertIn("after_depth=1", output)
        self.assertIn("depth_stencil_cull=1", output)

    def test_armed_only_is_error(self):
        path = self.log("KNACK-PX: Armed relaxed=1 target=8943ac2c trigger=x\n")
        with self.assertRaisesRegex(self.report.ReportError, "trigger was never detected"):
            self.report.analyze(path)

    def test_runtime_failure_is_error(self):
        path = self.log(
            "KNACK-PX: Armed relaxed=1 target=8943ac2c trigger=x\n"
            "KNACK-PX: Trigger detected frame=10 path=x\n"
            "KNACK-PX: Capture failed reason=no_matching_draws\n"
        )
        with self.assertRaisesRegex(self.report.ReportError, "no_matching_draws"):
            self.report.analyze(path)

    def test_latest_rearmed_capture_replaces_prior_failure(self):
        path = self.log(
            "KNACK-PX: Armed relaxed=1 target=any trigger=x\n"
            "KNACK-PX: Trigger detected frame=1 path=x\n"
            "KNACK-PX: Capture started frame=1 frames=1 target=any\n"
            "KNACK-PX: Capture failed reason=no_matching_draws\n"
            "KNACK-PX: Trigger detected frame=20 path=x\n"
            "KNACK-PX: Capture started frame=20 frames=1 target=any\n"
            "KNACK-PX: Results collected frame=20 queries=2\n"
            "KNACK-PX: draw=0 mode=O ps=1 inst=1 samples=4\n"
            "KNACK-PX: draw=0 mode=R ps=1 inst=1 samples=4\n"
            "KNACK-PX: Capture completed frames=1 matching_draws=1 queries=2\n"
        )
        output = self.report.analyze(path)
        self.assertIn("frame=20", output)
        self.assertNotIn("frame=1 ", output)

    def test_incomplete_results_are_error(self):
        path = self.log(
            "KNACK-PX: Armed relaxed=1 target=8943ac2c trigger=x\n"
            "KNACK-PX: Results collected frame=10 queries=2\n"
            "KNACK-PX: draw=0 mode=O ps=613b2261 inst=18 samples=50\n"
        )
        with self.assertRaisesRegex(self.report.ReportError, "expected 2 query results, got 1"):
            self.report.analyze(path)

    def test_launcher_passes_absolute_trigger(self):
        original = self.launcher.base_environment
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        capture = Path(folder.name) / "capture"
        try:
            self.launcher.base_environment = lambda *args, **kwargs: {"BASE": "1"}
            env = self.launcher.diagnostic_environment(Path(folder.name), capture_dir=capture)
        finally:
            self.launcher.base_environment = original
        self.assertEqual(env["BASE"], "1")
        self.assertEqual(env["TLG_KNACK_CONTRIB_TRIGGER"], str(capture.resolve() / "contribution.request"))
        self.assertEqual(env["TLG_FRAME_DUMP_COUNT"], "16")

    def test_probe_polling_uses_submit_boundary(self):
        source = (
            ROOT
            / "port/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp"
        ).read_text(encoding="utf-8")
        submit = source.index("void Rasterizer::OnSubmit()")
        probe = source.index("knack_contribution_probe->BeginFrame()", submit)
        tick = source.index("runtime.TickFrame()", submit)
        self.assertLess(probe, tick)


if __name__ == "__main__":
    unittest.main()
