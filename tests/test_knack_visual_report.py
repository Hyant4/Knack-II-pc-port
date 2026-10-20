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


class VisualReportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = load("knack_visual_report", ROOT / "scripts/knack_visual_report.py")
        cls.launcher = load(
            "run_knack2_visual_capture", ROOT / "scripts/run_knack2_visual_capture.py"
        )

    def log(self, text):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        path = Path(folder.name) / "game.log"
        path.write_text(text, encoding="utf-8")
        return path

    def test_reports_only_visible_draws(self):
        path = self.log(
            "KNACK-VIS: Armed prim=4 rts=5 roi=0,0,10,10 first=0 max=2 trigger=x\n"
            "KNACK-VIS: Results collected frame=8 queries=2\n"
            "KNACK-VIS: draw=0 pipe=11 vs=22 gs=0 ps=33 prim=4 rts=5 idx=6 inst=1 samples=0\n"
            "KNACK-VIS: draw=1 pipe=44 vs=55 gs=0 ps=66 prim=4 rts=5 idx=9 inst=2 samples=17\n"
            "KNACK-VIS: Capture completed frames=1 matching_draws=2 visible_draws=1 queries=2\n"
        )
        output = self.report.analyze(path)
        self.assertIn("queried=2 visible=1", output)
        self.assertIn("draw=1 pipe=44", output)
        self.assertNotIn("draw=0 pipe=11", output)

    def test_empty_capture_is_error(self):
        path = self.log("KNACK-VIS: Armed prim=4 rts=5 roi=0,0,10,10 first=0 max=2 trigger=x\n")
        with self.assertRaisesRegex(self.report.ReportError, "no KNACK-VIS result frames"):
            self.report.analyze(path)

    def test_latest_rearmed_capture_replaces_prior_failure(self):
        path = self.log(
            "KNACK-VIS: Armed prim=4 rts=5 roi=0,0,10,10 first=0 max=2 trigger=x\n"
            "KNACK-VIS: Capture started frame=1 frames=1 prim=4 rts=5 roi=0,0,10,10\n"
            "KNACK-VIS: Capture failed reason=no_matching_draws\n"
            "KNACK-VIS: Trigger detected frame=8 path=x\n"
            "KNACK-VIS: Capture started frame=8 frames=1 prim=4 rts=5 roi=0,0,10,10\n"
            "KNACK-VIS: Results collected frame=8 queries=1\n"
            "KNACK-VIS: draw=1 pipe=44 vs=55 gs=0 ps=66 prim=4 rts=5 idx=9 inst=2 samples=17\n"
            "KNACK-VIS: Capture completed frames=1 matching_draws=1 visible_draws=1 queries=1\n"
        )
        self.assertIn("draw=1 pipe=44", self.report.analyze(path))

    def test_launcher_sets_visual_and_frame_record_triggers(self):
        original = self.launcher.base_environment
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        capture = Path(folder.name) / "capture"
        self.launcher.options = type(
            "Options",
            (),
            {
                "roi": "10,20,30,40",
                "primitive": 4,
                "render_targets": 5,
                "frames": 1,
                "first_draw": 0,
                "max_draws": 1024,
                "pad_replay": None,
                "small_sync": False,
            },
        )()
        try:
            self.launcher.base_environment = lambda *args, **kwargs: {}
            env = self.launcher.diagnostic_environment(Path(folder.name), capture_dir=capture)
        finally:
            self.launcher.base_environment = original
        self.assertEqual(env["TLG_KNACK_VIS_ROI"], "10,20,30,40")
        self.assertEqual(env["TLG_KNACK_VIS_TRIGGER"], str(capture.resolve() / "visual.request"))
        self.assertEqual(
            env["TLG_FRAME_RECORD_TRIGGER"], str(capture.resolve() / "frame_record.request")
        )

    def test_sync_ab_is_targeted_and_opt_in(self):
        source = (
            ROOT / "port/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn('std::getenv("TLG_KNACK_SMALL_SYNC")', source)
        self.assertIn("KnackSmallGeometryVertexShader = 0x6e383eb8", source)
        self.assertIn("knack_small_sync_pending.exchange(false", source)
        self.assertNotIn('TLG_FULL_BARRIERS"]', source)

    def test_launcher_enables_sync_only_when_requested(self):
        original = self.launcher.base_environment
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        capture = Path(folder.name) / "capture"
        self.launcher.options = type(
            "Options",
            (),
            {
                "roi": "10,20,30,40",
                "primitive": 4,
                "render_targets": 5,
                "frames": 1,
                "first_draw": 0,
                "max_draws": 1024,
                "pad_replay": None,
                "small_sync": True,
            },
        )()
        try:
            self.launcher.base_environment = lambda *args, **kwargs: {}
            env = self.launcher.diagnostic_environment(Path(folder.name), capture_dir=capture)
        finally:
            self.launcher.base_environment = original
        self.assertEqual(env["TLG_KNACK_SMALL_SYNC"], "1")


if __name__ == "__main__":
    unittest.main()
