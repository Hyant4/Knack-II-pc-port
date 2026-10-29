"""Synthetic capture fixtures only. Does not launch the runtime or touch game data."""

import copy
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib


SPEC = importlib.util.spec_from_file_location("gpu_phy03_analyze", Path(__file__).resolve().parents[1] / "scripts/gpu_phy03_analyze.py")
analyzer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(analyzer)


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.write("session.json", {"schema": 1, "status": "Completed", "requested": 2, "started": 2})
        for index in range(2):
            attachments = [{"slot": i, "image_uid": i + 1, "guest_address": 0x1000 + i * 16,
                            "format": "R8G8B8A8Unorm", "width": 2, "height": 2, "samples": 1}
                           for i in range(5)]
            snapshots = []
            for stage, uid, address, format_name in (
                ("gbuffer", 3, 0x1020, "R8G8B8A8Unorm"),
                ("composition", 10, 0x2000, "B10G11R11UfloatPack32"),
                ("display", 20 + index, 0x3000 + index * 16, "A2B10G10R10UnormPack32"),
            ):
                data = bytes(range(index, index + 16))
                name = f"{stage}_{index:03}.raw"
                (self.directory / name).write_bytes(data)
                snapshots.append({"stage": stage, "file": name, "format": format_name,
                                  "image_uid": uid, "guest_address": address, "alias_generation": 0,
                                  "scheduler_tick": 100 + index * 10, "width": 2, "height": 2,
                                  "bpp": 4, "bytes": 16, "samples": 1, "mip": 0, "layer": 0,
                                  "hash_algorithm": "fnv1a64", "content_hash": analyzer.fnv1a64(data)})
            self.write(f"frame_{index:03}.json", {
                "schema": 1, "frame_index": index, "present_index": 401 + index,
                # Submission counters deliberately differ from present counters and repeat.
                "submit_sequence": 27, "display_uid": 20 + index, "display_address": 0x3000 + index * 16,
                "producer_vs": "6e383eb8", "producer_ps": "b1f064c3", "first_consumer_ps": "836b20e6",
                "composition_cs": "4105cfbc", "attachments": attachments, "snapshots": snapshots})

    def write(self, name, value):
        (self.directory / name).write_text(json.dumps(value), encoding="utf-8")

    def change(self, name, mutate):
        data = json.loads((self.directory / name).read_text())
        mutate(data)
        self.write(name, data)

    def rejected(self, reason):
        with self.assertRaisesRegex(analyzer.CaptureError, reason):
            analyzer.validate(self.directory)

    def test_present_alignment_ignores_submission_counter(self):
        report = analyzer.validate(self.directory)
        self.assertEqual(report["frames"], 2)
        self.assertEqual([r["present_index"] for r in report["results"]], [401, 402])
        self.assertTrue(all(r["classification"] == "inconclusive" for r in report["results"]))
        self.assertFalse(report["visibility_automatic"])

    def test_sixteen_frames(self):
        self.write("session.json", {"schema": 1, "status": "Completed", "requested": 16, "started": 16})
        template = json.loads((self.directory / "frame_001.json").read_text())
        for index in range(2, 16):
            frame = copy.deepcopy(template)
            frame.update(frame_index=index, present_index=401 + index)
            for snapshot in frame["snapshots"]:
                source = self.directory / snapshot["file"]
                snapshot["file"] = f"{snapshot['stage']}_{index:03}.raw"
                (self.directory / snapshot["file"]).write_bytes(source.read_bytes())
            self.write(f"frame_{index:03}.json", frame)
        self.assertEqual(analyzer.validate(self.directory)["frames"], 16)

    def test_truncation(self):
        (self.directory / "gbuffer_000.raw").write_bytes(b"short")
        self.rejected("missing_or_truncated_data")

    def test_same_size_corruption(self):
        (self.directory / "display_001.raw").write_bytes(bytes(16))
        self.rejected("content_hash_mismatch")

    def test_adjacent_frame_swap(self):
        a, b = (self.directory / f"display_{i:03}.raw" for i in range(2))
        first, second = a.read_bytes(), b.read_bytes()
        a.write_bytes(second)
        b.write_bytes(first)
        self.rejected("content_hash_mismatch")

    def test_missing_and_extra_frames(self):
        path = self.directory / "frame_001.json"
        data = path.read_bytes()
        path.unlink()
        self.rejected("manifest_frame_mismatch")
        path.write_bytes(data)
        (self.directory / "frame_002.json").write_bytes(data)
        self.rejected("manifest_frame_mismatch")

    def test_incomplete_session(self):
        for status in ("Capturing", "Failed"):
            with self.subTest(status=status):
                self.change("session.json", lambda x: x.update(status=status))
                self.rejected("incomplete_session")

    def test_missing_session(self):
        (self.directory / "session.json").unlink()
        self.rejected("unreadable_manifest")

    def test_duplicate_and_skipped_present(self):
        for value in (401, 403):
            with self.subTest(value=value):
                self.change("frame_001.json", lambda x: x.update(present_index=value))
                self.rejected("nonconsecutive_presents")

    def test_display_identity_mismatch(self):
        self.change("frame_001.json", lambda x: x.update(display_uid=20))
        self.rejected("display_identity_mismatch")

    def test_attachment_identity_mismatch(self):
        self.change("frame_000.json", lambda x: x["attachments"][2].update(image_uid=99))
        self.rejected("gbuffer_attachment_mismatch")

    def test_stage_order_and_missing_stage(self):
        original = json.loads((self.directory / "frame_000.json").read_text())
        for snapshots in (original["snapshots"][::-1], original["snapshots"][:2]):
            self.write("frame_000.json", {**original, "snapshots": snapshots})
            self.rejected("snapshot_stage_mismatch")

    def test_bad_snapshot_contracts(self):
        original = json.loads((self.directory / "frame_000.json").read_text())
        for field, value, reason in (
            ("bytes", 17, "invalid_snapshot_size"), ("samples", 4, "unsupported_subresource"),
            ("mip", 1, "unsupported_subresource"), ("file", "../other.raw", "snapshot_filename_mismatch"),
            ("format", "R32Sfloat", "unsupported_format"), ("image_uid", 0, "invalid_image_uid"),
            ("width", 0, "invalid_dimensions"), ("hash_algorithm", "none", "content_hash_mismatch"),
        ):
            with self.subTest(field=field):
                mutated = copy.deepcopy(original)
                mutated["snapshots"][0][field] = value
                self.write("frame_000.json", mutated)
                self.rejected(reason)

    def test_rejects_unreferenced_data(self):
        (self.directory / "display_016.raw").write_bytes(b"extra")
        self.rejected("unexpected_raw_files")

    def test_labels_must_cover_exact_capture(self):
        with self.assertRaisesRegex(analyzer.CaptureError, "labels_frame_mismatch"):
            analyzer.validate(self.directory, {"0": {}})

    def test_classification_matrix(self):
        cases = (("absent", "absent", "absent", "geometry_or_visibility_tests"),
                 ("present", "absent", "absent", "composition_or_sampled_inputs"),
                 ("present", "present", "absent", "after_main_composition_before_display"),
                 ("present", "present", "present", "visible_in_all_captured_stages"),
                 ("absent", "present", "present", "inconclusive_conflicting_observations"),
                 ("unknown", "absent", "absent", "inconclusive"))
        for g, c, d, expected in cases:
            with self.subTest(g=g, c=c, d=d):
                self.assertEqual(analyzer.classify(dict(zip(analyzer.FORMATS, (g, c, d)))), expected)

    def test_invalid_labels(self):
        with self.assertRaisesRegex(analyzer.CaptureError, "invalid_visibility_label"):
            analyzer.classify({"gbuffer": "black"})

    def test_fnv_known_vectors(self):
        self.assertEqual(analyzer.fnv1a64(b""), "cbf29ce484222325")
        self.assertEqual(analyzer.fnv1a64(b"a"), "af63dc4c8601ec8c")
        self.assertEqual(analyzer.fnv1a64(b"foobar"), "85944171f73967e8")

    def test_pixel_format_decoding(self):
        self.assertEqual(list(analyzer.rgb_pixels(bytes([255, 0, 0, 255]), "R8G8B8A8Unorm")), [b"\xff\0\0"])
        self.assertEqual(list(analyzer.rgb_pixels(bytes([255, 0, 0, 255]), "B8G8R8A8Srgb")), [b"\0\0\xff"])
        red = struct.pack("<I", 1023)
        self.assertEqual(list(analyzer.rgb_pixels(red, "A2B10G10R10UnormPack32")), [b"\xff\0\0"])
        self.assertEqual(list(analyzer.rgb_pixels(red, "A2R10G10B10UnormPack32")), [b"\0\0\xff"])
        hdr_red = struct.pack("<I", 15 << 6)  # R=1.0, G=B=0
        self.assertEqual(list(analyzer.rgb_pixels(hdr_red, "B10G11R11UfloatPack32")), [bytes([188, 0, 0])])

    def test_png_integrity(self):
        previews = self.directory / "previews"
        analyzer.validate(self.directory, previews=previews)
        image = (previews / "gbuffer_000.png").read_bytes()
        self.assertEqual(image[:8], b"\x89PNG\r\n\x1a\n")
        position = 8
        chunks = {}
        while position < len(image):
            size = struct.unpack_from(">I", image, position)[0]
            kind = image[position + 4:position + 8]
            payload = image[position + 8:position + 8 + size]
            crc = struct.unpack_from(">I", image, position + 8 + size)[0]
            self.assertEqual(crc, zlib.crc32(kind + payload))
            chunks[kind] = payload
            position += size + 12
        self.assertEqual(struct.unpack(">IIBBBBB", chunks[b"IHDR"])[:2], (2, 2))
        self.assertEqual(len(zlib.decompress(chunks[b"IDAT"])), 14)


class TemporalCaptureTests(unittest.TestCase):
    def setUp(self):
        fixture = CaptureTests()
        fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        self.directory = fixture.directory
        self.write, self.change, self.rejected = fixture.write, fixture.change, fixture.rejected
        self.write("session.json", {"schema": 2, "contract": analyzer.CONTRACT,
                                   "status": "Completed", "requested": 2, "started": 2})
        for index in range(2):
            frame = json.loads((self.directory / f"frame_{index:03}.json").read_text())
            frame.update(schema=2, contract=analyzer.CONTRACT, content_preservation="not_asserted")
            events = []
            for j, snapshot in enumerate(frame["snapshots"]):
                physical_format = [37, 122, 64][j]
                resource = {"uid": snapshot["image_uid"], "image": hex(snapshot["image_uid"] + 100),
                            "alias": 0, "address": snapshot["guest_address"], "bytes": 16,
                            "width": 2, "height": 2, "format": physical_format}
                snapshot.update(identity=resource, event_sequence=index * 20 + j * 2 + 2,
                                submit_sequence=27 + j)
                events.append({"event_sequence": index * 20 + j * 2 + 1, "submit_sequence": 27 + j,
                               "epoch_index": index, "tick": 100 + index,
                               "phase": ["gbuffer_producer", "dispatch_storage", "draw_color"][j],
                               "shader": ["b1f064c3", "4105cfbc", "12345678"][j],
                               "slot": 2 if j == 0 else 0, "op": "LOAD",
                               "kind": "gbuffer_producer" if j == 0 else "sampled_input",
                               "before": copy.deepcopy(resource), "after": copy.deepcopy(resource),
                               "inputs": [] if j == 0 else [copy.deepcopy(frame["snapshots"][j-1]["identity"])],
                               "after_composition": j == 2})
            frame["resource_events"] = events
            self.write(f"frame_{index:03}.json", frame)

    def test_ordered_resource_versions_validate_without_claiming_visibility(self):
        report = analyzer.validate(self.directory)
        self.assertEqual(report["schema"], 2)
        self.assertEqual(report["content_preservation"], "not_asserted")
        self.assertFalse(report["visibility_automatic"])
        self.assertEqual(report["results"][0]["classification"], "inconclusive")

    def test_load_and_clear_are_events_with_no_pixel_preservation_claim(self):
        for operation in ("LOAD", "CLEAR"):
            with self.subTest(operation=operation):
                frame = json.loads((self.directory / "frame_000.json").read_text())
                # Restore a clean frame if this is the second subcase.
                frame["resource_events"] = frame["resource_events"][:2] + [frame["resource_events"][-1]]
                before = copy.deepcopy(frame["snapshots"][1]["identity"])
                after = copy.deepcopy(before); after["alias"] = 1
                mutation = {"event_sequence": 5, "submit_sequence": 29, "epoch_index": 0, "tick": 100,
                            "phase": "draw_color", "shader": "15126eb7", "slot": 0,
                            "op": operation, "kind": "destination", "before": before, "after": after,
                            "inputs": [], "after_composition": True}
                frame["resource_events"][-1].update(event_sequence=7, inputs=[after])
                frame["resource_events"].insert(2, mutation)
                frame["snapshots"][2]["event_sequence"] = 8
                self.write("frame_000.json", frame)
                self.assertEqual(analyzer.validate(self.directory)["frames"], 2)

    def test_duplicate_sequence_and_cross_frame_reordering_are_rejected(self):
        self.change("frame_001.json", lambda f: f["resource_events"][0].update(event_sequence=1))
        self.rejected("cross_frame_event_order")

    def test_duplicate_event_is_rejected(self):
        self.change("frame_000.json", lambda f: f["snapshots"][0].update(event_sequence=1))
        self.rejected("duplicate_event_sequence")

    def test_future_or_fabricated_input_is_rejected(self):
        self.change("frame_000.json", lambda f: f["resource_events"][2]["inputs"][0].update(alias=99))
        self.rejected("unproven_sampled_input")

    def test_display_needs_a_path_from_composition(self):
        self.change("frame_000.json", lambda f: f["resource_events"][2].update(inputs=[f["snapshots"][0]["identity"]]))
        self.rejected("display_without_composition_path")

    def test_backing_swap_is_not_treated_as_a_new_valid_image(self):
        def mutate(frame):
            target = frame["resource_events"][1]
            target["after"] = copy.deepcopy(frame["snapshots"][0]["identity"])
            target["after"]["image"] = "0x999"
        self.change("frame_000.json", mutate)
        self.rejected("conflicting_resource_alias")

    def test_overlapping_alias_is_rejected(self):
        def mutate(frame):
            frame["resource_events"][2]["after"]["address"] = 0x1028
        self.change("frame_000.json", mutate)
        self.rejected("conflicting_resource_alias")

    def test_other_epoch_cannot_claim_the_event(self):
        self.change("frame_000.json", lambda f: f["resource_events"][1].update(epoch_index=1))
        self.rejected("cross_epoch_event")

    def test_missing_identity_or_contract_is_rejected(self):
        self.change("frame_000.json", lambda f: f["snapshots"][1].pop("identity"))
        self.rejected("invalid_resource_identity")

    def test_incorrect_snapshot_identity_is_rejected(self):
        self.change("frame_000.json", lambda f: f["snapshots"][1]["identity"].update(uid=999))
        self.rejected("snapshot_resource_identity_mismatch")

    def test_physical_format_must_match_decoded_pixel_format(self):
        self.change("frame_000.json", lambda f: f["snapshots"][1]["identity"].update(format=37))
        self.rejected("snapshot_physical_format_mismatch")

    def test_event_budget_is_bounded(self):
        self.change("frame_000.json", lambda f: f.update(resource_events=f["resource_events"] * 1366))
        self.rejected("invalid_resource_events")


class Pm4AnalyzerTests(unittest.TestCase):
    def test_index_offset_is_not_index_count_in_draw_index_offset2(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            record = {"order": 0, "kind": 1, "opcode": 0x35, "packet_words": 5,
                      "command": 0x4000, "buffer_base": 0x4000, "buffer_words": 5,
                      "remaining_words": 5, "submission": 17, "before_indices": 77,
                      "before_instances": 2, "after_indices": 60, "after_instances": 2,
                      "stages": 0, "primitive": 0, "color_control": 0, "color_mask": 0,
                      "skipped": False, "skip_words": 0, "programs": [0]*6,
                      # header, max_size, index_offset, index_count, draw_initiator
                      "raw_prefix": [0xC0033500, 180, 4500, 60, 0]}
            counts = [0]*256
            counts[0x35] = 1
            trace = {"schema": 1, "contract": "pm4_execution_metadata_v1", "frame_index": 0,
                     "present_index": 3, "association": "prepare_interval_only",
                     "coverage": "selected_executed_graphics_packets_and_all_type3_opcode_counts",
                     "stream_completeness": "not_proven",
                     "raw_sampling": "bounded_prefix_after_register_apply_not_atomic",
                     "condition_source": "existing_skip_decision_no_second_condition_read",
                     "status": "complete", "dropped": 0, "opcode_counts": counts, "records": [record]}
            path = directory / "pm4_trace_000.json"
            path.write_text(json.dumps(trace))
            frame = {"frame_index": 0, "present_index": 3, "pm4_trace": path.name}
            result = analyzer.validate_pm4_trace(directory, frame)
            self.assertEqual(result["sampled_register_comparisons"], 1)
            self.assertEqual(result["sampled_register_mismatches"], [])
            record["after_indices"] = 61
            path.write_text(json.dumps(trace))
            mismatch = analyzer.validate_pm4_trace(directory, frame)["sampled_register_mismatches"]
            self.assertEqual(mismatch, [{"order": 0, "opcode": 0x35, "field": "after_indices",
                                         "applied": 61, "sampled": 60}])


if __name__ == "__main__":
    unittest.main()
