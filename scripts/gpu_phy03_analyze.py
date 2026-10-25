"""Validate GPU-PHY-03 or final-game-frame captures. Hashes do not prove relic visibility.

Optional labels are explicit human observations, keyed by frame index, with gbuffer,
composition and display values of present/absent/unknown. No pixel color is treated
as proof that a particular mesh exists. Previews use the native stored formats.
"""

import argparse
import json
import math
import re
from pathlib import Path
import struct
import sys
import zlib


FORMATS = {
    "gbuffer": {"R8G8B8A8Unorm"},
    "composition": {"B10G11R11UfloatPack32"},
    "display": {"R8G8B8A8Unorm", "R8G8B8A8Srgb", "B8G8R8A8Unorm", "B8G8R8A8Srgb",
                "A2B10G10R10UnormPack32", "A2R10G10B10UnormPack32"},
}


class CaptureError(ValueError):
    pass


def require(condition, reason):
    if not condition:
        raise CaptureError(reason)


def fnv1a64(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{value:016x}"


def integer(value, low, high):
    return type(value) is int and low <= value <= high


def classify(observation):
    g, c, d = (observation.get(stage, "unknown") for stage in FORMATS)
    require(all(x in {"present", "absent", "unknown"} for x in (g, c, d)), "invalid_visibility_label")
    if "unknown" in (g, c, d):
        return "inconclusive"
    if g == c == d == "absent":
        return "geometry_or_visibility_tests"
    if g == "present" and c == d == "absent":
        return "composition_or_sampled_inputs"
    if g == c == "present" and d == "absent":
        return "after_main_composition_before_display"
    if g == c == d == "present":
        return "visible_in_all_captured_stages"
    return "inconclusive_conflicting_observations"


def read_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise CaptureError(f"unreadable_manifest:{path.name}") from error


CONTRACT = "ordered_resource_versions_v2"
PHYSICAL_FORMATS = {"R8G8B8A8Unorm": 37, "R8G8B8A8Srgb": 43,
                    "B8G8R8A8Unorm": 44, "B8G8R8A8Srgb": 50,
                    "A2R10G10B10UnormPack32": 58, "A2B10G10R10UnormPack32": 64,
                    "B10G11R11UfloatPack32": 122}


def identity(value):
    require(isinstance(value, dict), "invalid_resource_identity")
    for name in ("uid", "address", "bytes", "width", "height", "format"):
        require(integer(value.get(name), 1, 2**64 - 1), "invalid_resource_identity:" + name)
    require(integer(value.get("alias"), 0, 2**64 - 1), "invalid_resource_identity:alias")
    require(isinstance(value.get("image"), str) and
            re.fullmatch(r"0x[0-9a-fA-F]+", value["image"]) and int(value["image"], 16),
            "invalid_resource_identity:image")
    require(value["address"] + value["bytes"] <= 2**64 - 1, "overflow_resource_identity")
    return value


def version(value):
    return tuple(value[k] for k in ("uid", "image", "alias", "address", "bytes", "width", "height", "format"))


def same_storage(a, b):
    return all(a[k] == b[k] for k in ("uid", "image", "address", "bytes", "width", "height", "format"))


def validate_temporal(frame, previous_event):
    require(frame.get("contract") == CONTRACT and frame.get("content_preservation") == "not_asserted",
            "invalid_temporal_contract")
    events = frame.get("resource_events")
    require(isinstance(events, list) and 1 <= len(events) <= 4096, "invalid_resource_events")
    timeline = []
    for snapshot in frame["snapshots"]:
        image = identity(snapshot.get("identity"))
        require(all(image[k] == snapshot[v] for k, v in (
            ("uid", "image_uid"), ("address", "guest_address"), ("alias", "alias_generation"),
            ("width", "width"), ("height", "height"))), "snapshot_resource_identity_mismatch")
        require(image["bytes"] >= snapshot["bytes"], "snapshot_resource_size_mismatch")
        require(image["format"] == PHYSICAL_FORMATS[snapshot["format"]], "snapshot_physical_format_mismatch")
        require(integer(snapshot.get("submit_sequence"), 0, 2**64 - 1), "invalid_snapshot_submit")
        timeline.append((snapshot.get("event_sequence"), "snapshot", snapshot))
    for event in events:
        require(isinstance(event, dict) and event.get("epoch_index") == frame["frame_index"], "cross_epoch_event")
        require(event.get("phase") in {"gbuffer_producer", "draw_color", "draw_storage", "dispatch_storage"},
                "invalid_event_phase")
        require(event.get("kind") in {"gbuffer_producer", "sampled_input", "destination"}, "invalid_event_origin")
        require(event.get("op") in {"LOAD", "CLEAR", "none"}, "invalid_event_operation")
        require(type(event.get("after_composition")) is bool, "invalid_event_composition_flag")
        require(integer(event.get("slot"), 0, 2**32 - 1) and
                integer(event.get("submit_sequence"), 0, 2**64 - 1) and
                integer(event.get("tick"), 0, 2**64 - 1), "invalid_event_metadata")
        require(isinstance(event.get("shader"), str) and
                re.fullmatch(r"[0-9a-fA-F]{8,16}", event["shader"]), "invalid_event_shader")
        identity(event.get("before")); identity(event.get("after"))
        require(isinstance(event.get("inputs"), list) and len(event["inputs"]) <= 128, "invalid_event_inputs")
        for item in event["inputs"]:
            identity(item)
        timeline.append((event.get("event_sequence"), "write", event))
    require(all(integer(seq, 1, 2**64 - 1) for seq, _, _ in timeline), "invalid_event_sequence")
    require(len({seq for seq, _, _ in timeline}) == len(timeline), "duplicate_event_sequence")
    timeline.sort(key=lambda row: row[0])
    require(previous_event is None or timeline[0][0] > previous_event, "cross_frame_event_order")
    seen, latest = {}, {}
    stages = []
    root_uid = frame["snapshots"][0]["image_uid"]
    composition_uid = frame["snapshots"][1]["image_uid"]
    anchors = {a["image_uid"]: a for a in frame["attachments"]}
    composition_producer = None
    for seq, kind, item in timeline:
        if kind == "snapshot":
            require(version(item["identity"]) in seen, "snapshot_without_observed_producer")
            stages.append(item["stage"])
            require(stages == list(FORMATS)[:len(stages)], "snapshot_event_order")
            if item["stage"] == "composition":
                require(composition_producer == version(item["identity"]), "composition_producer_mismatch")
            if item["stage"] == "display":
                require(seen[version(item["identity"])], "display_without_composition_path")
            continue
        before, after = item["before"], item["after"]
        key = (after["uid"], after["image"])
        for old_key, old in latest.items():
            overlap = after["address"] < old["address"] + old["bytes"] and old["address"] < after["address"] + after["bytes"]
            require(old_key == key or not (overlap or old["image"] == after["image"] or old["uid"] == after["uid"]),
                    "conflicting_resource_alias")
        if key in latest:
            require(same_storage(latest[key], after) and version(before) in seen, "unproven_resource_transition")
        inputs = item["inputs"]
        require(all(version(v) in seen for v in inputs), "unproven_sampled_input")
        from_composition = any(seen[version(v)] for v in inputs)
        if item["kind"] == "gbuffer_producer":
            require(not stages and item["phase"] == "gbuffer_producer" and item["shader"] == "b1f064c3",
                    "late_or_invalid_gbuffer_producer")
            anchor = anchors.get(after["uid"])
            require(anchor is not None and all(after[k] == anchor[v] for k, v in (
                ("address", "guest_address"), ("width", "width"), ("height", "height"))), "root_attachment_mismatch")
        elif item["kind"] == "destination":
            supported_write = (item["phase"] == "draw_color" and item["op"] in {"LOAD", "CLEAR"}) or (
                item["phase"] in {"draw_storage", "dispatch_storage"} and item["op"] == "none")
            require(supported_write and
                    version(before) in seen and same_storage(before, after), "unproven_destination_association")
        else:
            require(bool(inputs), "missing_sampled_input")
        if version(before) in seen:
            from_composition |= seen[version(before)]
        if item["phase"] == "dispatch_storage" and item["shader"] == "4105cfbc":
            require(stages == ["gbuffer"] and any(v["uid"] == root_uid for v in inputs) and
                    after["uid"] == composition_uid, "invalid_composition_input")
            from_composition = True
            composition_producer = version(after)
        require(item["after_composition"] == ("composition" in stages), "event_composition_order_mismatch")
        seen[version(after)] = from_composition
        latest[key] = after
    require(stages == list(FORMATS), "missing_temporal_snapshot")
    return timeline[-1][0]


def validate_final_frames(directory, session, labels=None, previews=None):
    """Validate final game frames without claiming intermediate-stage correspondence."""
    require(session.get("schema") == 1, "invalid_session_schema")
    require(session.get("status") == "Completed", f"incomplete_session:{session.get('reason', '')}")
    require(session.get("boundary") == "post_processed_game_image_before_window_blit", "invalid_capture_boundary")
    count = session.get("requested")
    require(integer(count, 1, 16) and all(session.get(k) == count for k in
            ("staged", "saved", "presented", "published")), "invalid_frame_count")
    require(labels is None or isinstance(labels, dict) and set(labels) == {str(i) for i in range(count)},
            "labels_frame_mismatch")
    require({p.name for p in directory.glob("frame_*.json")} == {f"frame_{i:03}.json" for i in range(count)},
            "manifest_frame_mismatch")
    require({p.name for p in directory.glob("*.raw")} == {f"output_{i:03}.raw" for i in range(count)},
            "raw_frame_mismatch")
    frames = []
    previous_present = previous_tick = None
    for index in range(count):
        frame = read_json(directory / f"frame_{index:03}.json")
        require(isinstance(frame, dict) and frame.get("schema") == 1 and frame.get("index") == index,
                "invalid_frame_schema")
        require(frame.get("file") == f"output_{index:03}.raw", "invalid_raw_path")
        require(frame.get("format") in FORMATS["display"], "unsupported_output_format")
        require(integer(frame.get("width"), 1, 32768) and integer(frame.get("height"), 1, 32768),
                "invalid_output_extent")
        size = frame["width"] * frame["height"] * 4
        require(size <= 128 * 1024 * 1024 and frame.get("bytes") == size, "invalid_output_size")
        require(type(frame.get("reused")) is bool, "invalid_reuse_flag")
        present, tick = frame.get("present_index"), frame.get("tick")
        require(integer(present, 1, 2**64 - 1) and
                (previous_present is None or present == previous_present + 1), "nonconsecutive_presents")
        require(integer(tick, 1, 2**64 - 1) and (previous_tick is None or tick >= previous_tick), "invalid_copy_tick")
        previous_present, previous_tick = present, tick
        raw = directory / frame["file"]
        require(raw.stat().st_size == size, "raw_size_mismatch")
        require(fnv1a64(raw.read_bytes()) == frame.get("fnv1a64"), "raw_hash_mismatch")
        observation = labels[str(index)] if labels is not None else {"display": "unknown"}
        require(isinstance(observation, dict) and observation.get("display") in {"present", "absent", "unknown"},
                "invalid_visibility_label")
        frames.append({**frame, "display_visibility": observation["display"]})
    if previews is not None:
        for frame in frames:
            write_preview(Path(previews) / f"output_{frame['index']:03}.png",
                          (directory / frame["file"]).read_bytes(), frame)
    return {"integrity": "passed", "contract": "final_game_frames_v1", "frames": count,
            "boundary": session["boundary"], "visibility_automatic": False,
            "classification": "final_frames_only_no_stage_localization", "results": frames}


def validate_front_census(census):
    require(isinstance(census, dict) and census.get("contract") == "direct_draw_front_census_v1" and
            census.get("coverage") == "Rasterizer_Draw_only" and
            census.get("association") == "prepare_interval_only" and
            census.get("program_identity") == "register_addresses_not_code_identity",
            "invalid_front_census_contract")
    rows, dropped = census.get("rows"), census.get("dropped")
    require(isinstance(rows, list) and len(rows) <= 256 and integer(dropped, 0, 2**32-1) and
            census.get("status") == ("truncated" if dropped else "complete"), "invalid_front_census_budget")
    totals = {str(i): 0 for i in range(1, 7)}
    identities = set()
    for row in rows:
        require(isinstance(row, dict) and integer(row.get("outcome"), 1, 6) and
                type(row.get("indexed")) is bool and type(row.get("pipelined")) is bool,
                "invalid_front_census_row")
        programs = row.get("programs")
        require(isinstance(programs, list) and len(programs) == 6 and
                all(integer(x, 0, 2**64-1) for x in programs), "invalid_front_census_programs")
        for key in ("stages", "primitive", "color_control", "color_mask"):
            require(integer(row.get(key), 0, 2**32-1), "invalid_front_census_u32")
        for key in ("vs_hash", "ps_hash", "instance_sum", "index_sum", "raw_zero_instances"):
            require(integer(row.get(key), 0, 2**64-1), "invalid_front_census_u64")
        require(integer(row.get("calls"), 1, 2**64-1) and row["raw_zero_instances"] <= row["calls"],
                "invalid_front_census_calls")
        require(row["outcome"] in (4, 5) or (row["vs_hash"] == row["ps_hash"] == 0),
                "invalid_front_census_hash_claim")
        identity = json.dumps({k: v for k, v in row.items() if k not in
                               ("calls", "instance_sum", "index_sum", "raw_zero_instances")}, sort_keys=True)
        require(identity not in identities, "duplicate_front_census_row")
        identities.add(identity)
        totals[str(row["outcome"])] += row["calls"]
    return {"integrity": "passed", "status": census["status"], "dropped": dropped,
            "outcome_calls": totals, "rows": rows,
            "classification": "cpu_entry_census_not_guest_packet_proof",
            "stage_a_b_intervals": "not_proven_aligned_when_pipelined"}


def validate_draw_trace(directory, frame):
    index = frame["frame_index"]
    require(frame.get("draw_trace") == f"draw_trace_{index:03}.json", "invalid_draw_trace_path")
    trace = read_json(directory / frame["draw_trace"])
    require(isinstance(trace, dict) and trace.get("schema") == 1 and
            trace.get("contract") == "draw_emission_metadata_v1" and trace.get("frame_index") == index and
            trace.get("present_index") == frame["present_index"], "draw_trace_identity_mismatch")
    require(trace.get("scope") == "shader_pair_candidates" and
            trace.get("coverage") == "DrawRecord_and_DrawIndirectRecord" and
            trace.get("before_record_coverage") == "not_observed" and
            trace.get("buffer_contents") == "not_collected", "invalid_draw_trace_claim")
    records, dropped = trace.get("records"), trace.get("dropped")
    require(isinstance(records, list) and len(records) <= 512 and integer(dropped, 0, 2**32-1) and
            trace.get("status") == ("truncated" if dropped else "complete"), "invalid_draw_trace_budget")
    kinds = {i: 0 for i in range(1, 7)}
    instances = 0
    for order, record in enumerate(records):
        require(isinstance(record, dict) and record.get("order") == order and
                integer(record.get("kind"), 1, 6) and type(record.get("indexed")) is bool, "invalid_draw_trace_record")
        for key in ("count", "instances", "first_instance", "vertex_offset_bits", "index_offset", "mrt_mask",
                    "depth_control", "stencil_control", "polygon_control", "clipper_control", "color_mask"):
            require(integer(record.get(key), 0, 2**32-1), "invalid_draw_trace_u32")
        for key in ("tick", "pipeline", "indirect_args_buffer", "indirect_count_buffer", "indirect_offset", "max_count", "stride"):
            require(integer(record.get(key), 0, 2**64-1), "invalid_draw_trace_u64")
        for key, length in (("viewport_float_bits", 6), ("screen_scissor_words", 2)):
            values = record.get(key)
            require(isinstance(values, list) and len(values) == length and
                    all(integer(x, 0, 2**32-1) for x in values), "invalid_draw_trace_state")
        buffers = record.get("buffers")
        require(isinstance(buffers, list) and len(buffers) == 2, "invalid_draw_trace_buffers")
        for slot, buffer in zip((0, 2), buffers):
            require(buffer.get("slot") == slot and type(buffer.get("recorded")) is bool and
                    all(integer(buffer.get(key), 0, 2**64-1) for key in ("buffer", "offset", "range", "push_adjust")),
                    "invalid_draw_trace_buffer")
        kinds[record["kind"]] += 1
        if record["kind"] == 3:
            instances += record["instances"]
    front = {"integrity": "not_collected"}
    if "front_census" in trace:
        try:
            front = validate_front_census(trace["front_census"])
        except CaptureError as error:
            front = {"integrity": "failed", "error": str(error)}
    return {"integrity": "passed", "status": trace["status"], "dropped": dropped,
            "direct_entries": kinds[1], "direct_binding_rejections": kinds[2], "direct_emissions": kinds[3],
            "indirect_emissions": kinds[4], "indirect_entries": kinds[5], "indirect_binding_rejections": kinds[6],
            "recorded_direct_instance_sum": instances, "indirect_instance_counts": "unknown",
            "front_census": front,
            "classification": "emission_metadata_only_no_root_cause"}


def validate_submission_samples(data):
    require(isinstance(data, dict) and data.get("contract") == "command_submission_samples_v1" and
            data.get("hash_algorithm") == "xxh3_64" and data.get("sample_limit_words") == 262144 and
            data.get("atomicity") == "not_proven" and
            data.get("association") == "submission_id_across_prepare_intervals", "invalid_submission_sample_contract")
    records, dropped = data.get("records"), data.get("dropped")
    require(isinstance(records, list) and len(records) <= 128 and integer(dropped, 0, 2**32-1) and
            data.get("status") == ("truncated" if dropped else "complete"), "invalid_submission_sample_budget")
    for order, row in enumerate(records):
        require(isinstance(row, dict) and row.get("order") == order and integer(row.get("kind"), 1, 2) and
                integer(row.get("submission"), 0, 2**64-1), "invalid_submission_sample_record")
        require(integer(row.get("copy_mode"), 0, 2) and
                row["copy_mode"] in ((1, 2) if row["kind"] == 1 else (0,)), "invalid_submission_copy_mode")
        samples = row.get("samples")
        require(isinstance(samples, list) and len(samples) == 4, "invalid_submission_samples")
        for i, sample in enumerate(samples):
            require(isinstance(sample, dict) and integer(sample.get("status"), 0, 2) and
                    integer(sample.get("address"), 0, 2**64-1) and integer(sample.get("words"), 0, 2**64-1),
                    "invalid_submission_sample_descriptor")
            digest = sample.get("hash")
            require(isinstance(digest, str) and len(digest) == 16 and
                    all(x in "0123456789abcdef" for x in digest), "invalid_submission_sample_hash")
            status = sample["status"]
            if status == 0:
                require(sample["address"] == sample["words"] == 0 and digest == "0"*16,
                        "invalid_unsampled_submission_descriptor")
            elif status == 1:
                require(sample["words"] <= 262144 and (sample["words"] == 0 or sample["address"] != 0),
                        "invalid_measured_submission_descriptor")
            else:
                require(sample["words"] > 262144 and digest == "0"*16, "invalid_oversized_submission_descriptor")
            unused = row["kind"] == 2 and i >= 2
            require(status == 0 if unused else status in (1, 2), "missing_submission_sample")
    return {"integrity": "passed", "status": data["status"], "dropped": dropped, "records": records,
            "classification": "point_samples_not_atomic_or_complete_lifetime_proof"}


def compare_submission_samples(traces):
    """Join by the existing submission ID across windows, never by image/file index."""
    groups, failed, truncated = {}, 0, 0
    for trace in traces:
        data = trace.get("submission_samples", {})
        if data.get("integrity") == "failed":
            failed += 1
        if data.get("integrity") != "passed":
            continue
        truncated += int(data["dropped"] != 0)
        for record in data["records"]:
            row = {**record, "observation_index": trace["observation_index"], "present_index": trace["present_index"]}
            groups.setdefault(record["submission"], {1: [], 2: []})[record["kind"]].append(row)

    def compare(left, right, same_descriptor):
        if left["status"] != 1 or right["status"] != 1:
            return "not_sampled_within_budget"
        if left["words"] != right["words"] or (same_descriptor and left["address"] != right["address"]):
            return "sample_descriptor_mismatch"
        return "sample_hash_equal" if left["hash"] == right["hash"] else "sample_hash_changed"

    results = []
    for seq, group in sorted(groups.items()):
        row = {"submission": seq, "submit_records": len(group[1]), "parser_records": len(group[2])}
        if seq == 2**64-1 or len(group[1]) > 1 or len(group[2]) > 1:
            row["association"] = "ambiguous_submission_identity"
        elif not group[1] or not group[2]:
            row["association"] = "missing_submission_or_parser_sample"
        else:
            submit, parser = group[1][0], group[2][0]
            row.update(association="matched_submission_id", copy_mode=submit["copy_mode"],
                       submit_observation=submit["observation_index"], parser_observation=parser["observation_index"],
                       parser_present=parser["present_index"])
            row["buffers"] = [{"buffer": name,
                               "input_to_ready": compare(submit["samples"][i], submit["samples"][i+2], submit["copy_mode"] == 1),
                               "ready_to_parser": compare(submit["samples"][i+2], parser["samples"][i], True)}
                              for i, name in enumerate(("dcb", "ccb"))]
        results.append(row)
    return {"classification": "point_samples_not_atomic_or_complete_lifetime_proof", "results": results,
            "metadata_failures": failed, "truncated_windows": truncated,
            "coverage": "active_capture_only_pending_submissions_may_have_no_entry_sample"}


def validate_render_list_samples(data):
    require(data.get("contract") == "gnm_index_offset_producers_v2" and
            data.get("list_contract") == "knack_render_list_point_samples_v1" and
            data.get("list_atomicity") == "not_proven" and data.get("list_count_limit") == 4096 and
            data.get("list_byte_budget") == 1048576 and
            integer(data.get("list_scan_attempts"), 0, 32) and
            integer(data.get("list_reserved_bytes"), 0, 1048576), "invalid_list_contract")
    statuses, scans, counts = {}, {}, []
    for writer in data["records"]:
        q = writer.get("list")
        require(isinstance(q, dict) and integer(q.get("status"), 0, 6) and
                integer(q.get("scan_status"), 0, 8), "invalid_list_status")
        for k in ("owner_frame", "builder_frame", "list", "entries", "sort_keys", "cursor"):
            require(integer(q.get(k), 0, 2**64-1), "invalid_list_pointer")
        for k in ("count", "capacity", "batch_count", "scanned", "null_records", "null_materials",
                  "readable_materials", "unreadable_materials"):
            require(integer(q.get(k), 0, 2**32-1), "invalid_list_count")
        status, scan = q["status"], q["scan_status"]
        statuses[str(status)] = statuses.get(str(status), 0) + 1
        scans[str(scan)] = scans.get(str(scan), 0) + 1
        require(q["scanned"] == q["null_records"] + q["null_materials"] +
                q["readable_materials"] + q["unreadable_materials"], "invalid_list_scan_sum")
        if status != 1:
            require(scan == 0 and q["scanned"] == 0, "invalid_list_failed_header_scan")
            continue
        require(writer["host_unwind"] == 1 and q["owner_frame"] and q["builder_frame"] and
                q["list"] and q["list"] % 8 == 0 and 0 <= q["count"] <= q["capacity"] <= 0x7fffffff and
                (not q["count"] or q["entries"]) and 1 <= q["batch_count"] <= 64 and
                q["cursor"] >= 0x54 and (q["cursor"] - 0x54) % 0x58 == 0,
                "invalid_list_header")
        base, frames = writer["caller_allocation_base"], writer["frames"]
        returns = (0xbf5834, 0xbe5989, 0xc348c5, 0x9bc831)
        require(writer["caller"] - base == 0xbb05f1 and len(frames) >= 4 and
                all(frames[i]["caller_allocation_base"] == base and
                    frames[i]["caller"] - base == pc for i, pc in enumerate(returns)) and
                frames[2]["frame"] == q["builder_frame"] and frames[3]["frame"] == q["owner_frame"],
                "invalid_list_source_chain")
        counts.append(q["count"])
        if scan in (1, 6, 7, 8):
            require(q["scanned"] == q["count"] <= 4096, "invalid_list_scan_extent")
            if scan == 1:
                require(q["unreadable_materials"] == 0, "invalid_list_complete_scan")
            if scan == 6:
                require(q["unreadable_materials"] > 0, "invalid_list_partial_scan")
        else:
            require(q["scanned"] == 0, "invalid_list_skipped_scan")
            require(scan != 4 or q["count"] > 4096, "invalid_list_limit_claim")
    return {"integrity": "passed", "header_statuses": statuses, "scan_statuses": scans,
            "sampled_headers": len(counts), "min_count": min(counts) if counts else None,
            "max_count": max(counts) if counts else None, "atomicity": "not_proven"}


def validate_producer_samples(data):
    require(isinstance(data, dict) and data.get("contract") in
            ("gnm_index_offset_producers_v1", "gnm_index_offset_producers_v2"), "invalid_producer_claim")
    modern = data["contract"] == "gnm_index_offset_producers_v2"
    expected = {"coverage": "successful_sceGnmDrawIndexOffset_only",
                "association": "collector_serial_address_packet_candidate_only",
                "stack": "windows_metadata_to_import_caller_then_guest_frame_links" if modern else
                "bounded_frame_pointer_links_not_complete_unwind"}
    require(all(data.get(k) == v for k, v in expected.items()),
            "invalid_producer_claim")
    records, dropped = data.get("records"), data.get("dropped")
    require(isinstance(records, list) and len(records) <= 4096 and integer(dropped, 0, 2**32-1) and
            data.get("status") == ("truncated" if dropped else "complete"), "invalid_producer_budget")
    serials = set()
    for order, r in enumerate(records):
        require(isinstance(r, dict) and r.get("order") == order, "invalid_producer_order")
        for key in ("serial", "command", "thread"):
            require(integer(r.get(key), 0 if key == "thread" else 1, 2**64-1),
                    "invalid_producer_u64")
        require(r["serial"] not in serials, "duplicate_producer_serial")
        serials.add(r["serial"])
        packet, frames = r.get("packet"), r.get("frames")
        require(isinstance(packet, list) and len(packet) == 5 and
                all(integer(x, 0, 2**32-1) for x in packet) and
                (packet[0] & 0xfffffffe) == 0xc0033500 and packet[1] == packet[3],
                "invalid_producer_packet")
        require(integer(r.get("stop"), 1, 7 if modern else 5) and isinstance(frames, list) and len(frames) <= 8,
                "invalid_producer_stack")
        if modern:
            for key in ("caller", "caller_allocation_base", "guest_frame", "guest_stack"):
                require(integer(r.get(key), 0, 2**64-1), "invalid_producer_host_context")
            require(integer(r.get("host_unwind"), 1, 6) and integer(r.get("host_steps"), 0, 4),
                    "invalid_producer_host_unwind")
            require(r["caller_allocation_base"] <= r["caller"], "invalid_producer_caller_allocation")
            if r["host_unwind"] == 1:
                require(r["caller_allocation_base"] > 0 and r["guest_stack"] > 0 and
                        r["host_steps"] > 0 and r["stop"] not in (4, 6), "invalid_producer_host_match")
                require(not frames or (isinstance(frames[0], dict) and
                        frames[0].get("frame") == r["guest_frame"]), "invalid_producer_guest_start")
            else:
                require(not frames and r["guest_frame"] == r["guest_stack"] == 0 and
                        r["stop"] == (4 if r["host_unwind"] == 5 else 6), "invalid_producer_failed_unwind")
        previous = 0
        for link in frames:
            require(isinstance(link, dict) and integer(link.get("frame"), 1, 2**64-1) and
                    integer(link.get("caller"), 1, 2**64-1) and link["frame"] % 8 == 0 and
                    integer(link.get("caller_allocation_base"), 0, link["caller"]) and
                    link["frame"] > previous and link["frame"] - frames[0]["frame"] <= 1024*1024,
                    "invalid_producer_frame")
            previous = link["frame"]
            if modern:
                require(link["caller_allocation_base"] > 0 and link["frame"] >= r["guest_stack"] and
                        link["frame"] - r["guest_stack"] <= 1024*1024, "invalid_producer_guest_frame")
        require(r["stop"] != 5 or len(frames) == 8, "invalid_producer_stack_limit")
        require(r["stop"] != 4 or not frames, "invalid_producer_unsupported_stack")
    lists = {"integrity": "not_collected"}
    if "list_contract" in data:
        try:
            lists = validate_render_list_samples(data)
        except CaptureError as error:
            lists = {"integrity": "failed", "reason": str(error)}
    return {"integrity": "passed", "status": data["status"], "dropped": dropped, "records": records,
            "list_samples": lists,
            "stack_contract": data["stack"],
            "stack_quality": "host_match_checked_guest_chain_bounded" if modern else "legacy_host_start_unverified",
            "host_matches": sum(r["host_unwind"] == 1 for r in records) if modern else None,
            "host_failures": sum(r["host_unwind"] != 1 for r in records) if modern else None}


def compare_producer_samples(traces):
    """Candidate links only: observed HLE writers do not cover every way to rewrite a DCB."""
    collected = [t for t in traces if t.get("producer_samples", {}).get("integrity") != "not_collected"
                 and "producer_samples" in t]
    if not collected:
        return {"integrity": "not_collected"}
    if any(t["producer_samples"].get("integrity") != "passed" for t in collected):
        return {"integrity": "failed", "reason": "invalid_producer_input"}
    events, seen = [], set()
    dropped = sum(t["producer_samples"]["dropped"] for t in collected)
    for t in collected:
        for kind, records in (("writer", t["producer_samples"]["records"]),
                              ("consumer", t.get("producer_consumers", []))):
            for r in records:
                serial = r["serial"]
                if serial in seen:
                    return {"integrity": "failed", "reason": "duplicate_collector_serial"}
                seen.add(serial)
                events.append((serial, kind, {**r, "observation_index": t["observation_index"],
                                              "present_index": t["present_index"]}))
    latest, rows, used = {}, [], {}
    for _, kind, r in sorted(events, key=lambda item: item[0]):
        if kind == "writer":
            latest[r["command"]] = r
            continue
        writer = latest.get(r["command"])
        status = "missing_writer" if writer is None else (
            "candidate_match" if writer["packet"] == r["packet"] else "latest_writer_packet_mismatch")
        row = {"consumer": r, "status": status, "writer": writer}
        if status == "candidate_match":
            used[writer["serial"]] = used.get(writer["serial"], 0) + 1
            row["observed_uses"] = used[writer["serial"]]
        rows.append(row)
    return {"integrity": "passed", "classification": "candidate_links_not_buffer_generation_proof",
            "writer_dropped": dropped, "coverage_limited": bool(dropped) or len(collected) != len(traces) or
            any(t.get("dropped", 0) for t in collected) or any(r["status"] == "missing_writer" for r in rows),
            "writers": sum(kind == "writer" for _, kind, _ in events), "consumers": len(rows),
            "candidate_matches": sum(r["status"] == "candidate_match" for r in rows),
            "missing_writers": sum(r["status"] == "missing_writer" for r in rows),
            "packet_mismatches": sum(r["status"] == "latest_writer_packet_mismatch" for r in rows),
            "rows": rows}


def validate_pm4_trace(directory, frame):
    index = frame["frame_index"]
    require(frame.get("pm4_trace") == f"pm4_trace_{index:03}.json", "invalid_pm4_trace_path")
    trace = read_json(Path(directory) / frame["pm4_trace"])
    require(isinstance(trace, dict) and trace.get("schema") == 1 and
            trace.get("contract") == "pm4_execution_metadata_v1" and
            trace.get("frame_index") == index and trace.get("present_index") == frame["present_index"],
            "pm4_trace_identity_mismatch")
    expected = {"association": "prepare_interval_only",
                "coverage": "selected_executed_graphics_packets_and_all_type3_opcode_counts",
                "stream_completeness": "not_proven", "raw_sampling": "bounded_prefix_after_register_apply_not_atomic",
                "condition_source": "existing_skip_decision_no_second_condition_read"}
    require(all(trace.get(k) == v for k, v in expected.items()), "invalid_pm4_trace_claim")
    counts, records, dropped = trace.get("opcode_counts"), trace.get("records"), trace.get("dropped")
    require(isinstance(counts, list) and len(counts) == 256 and
            all(integer(x, 0, 2**64-1) for x in counts), "invalid_pm4_opcode_counts")
    require(isinstance(records, list) and len(records) <= 4096 and integer(dropped, 0, 2**32-1) and
            trace.get("status") == ("truncated" if dropped else "complete"), "invalid_pm4_trace_budget")
    kinds = {str(i): 0 for i in range(1, 5)}
    observed = [0] * 256
    comparisons, mismatches, decisions, edges = 0, [], [], []
    for order, record in enumerate(records):
        require(isinstance(record, dict) and record.get("order") == order and
                integer(record.get("kind"), 1, 4) and integer(record.get("opcode"), 0, 255) and
                type(record.get("skipped")) is bool, "invalid_pm4_trace_record")
        for key in ("packet_words", "skip_words", "before_indices", "before_instances", "after_indices",
                    "after_instances", "stages", "primitive", "color_control", "color_mask"):
            require(integer(record.get(key), 0, 2**32-1), "invalid_pm4_trace_u32")
        for key in ("command", "buffer_base", "buffer_words", "remaining_words", "submission"):
            require(integer(record.get(key), 0, 2**64-1), "invalid_pm4_trace_u64")
        programs, raw = record.get("programs"), record.get("raw_prefix")
        require(isinstance(programs, list) and len(programs) == 6 and
                all(integer(x, 0, 2**64-1) for x in programs), "invalid_pm4_trace_programs")
        require(isinstance(raw, list) and len(raw) <= 8 and all(integer(x, 0, 2**32-1) for x in raw),
                "invalid_pm4_trace_prefix")
        kind, opcode = record["kind"], record["opcode"]
        kinds[str(kind)] += 1
        if kind in (1, 2):
            require(1 <= record["packet_words"] <= 16384 and
                    len(raw) == min(8, record["packet_words"], record["remaining_words"]),
                    "invalid_pm4_trace_prefix_extent")
        else:
            require(record["packet_words"] == 0 and not raw and opcode == 0,
                    "invalid_pm4_trace_boundary")
        if kind != 2:
            require(not record["skipped"] and record["skip_words"] == 0, "invalid_pm4_skip_claim")
        if kind == 1:
            observed[opcode] += 1
            # These comparisons use a sampled prefix, not the decoder's atomic input.
            field, slot = {0x2F: ("after_instances", 1), 0x27: ("after_indices", 4),
                           0x35: ("after_indices", 3), 0x2D: ("after_indices", 1)}.get(opcode, (None, None))
            if field is not None and len(raw) > slot:
                comparisons += 1
                if record[field] != raw[slot]:
                    mismatches.append({"order": order, "opcode": opcode, "field": field,
                                       "applied": record[field], "sampled": raw[slot]})
            if opcode == 0x3F and len(raw) >= 4:
                edges.append({"order": order, "parent_buffer": record["buffer_base"],
                              "child_buffer": ((raw[2] & 0xFFFF) << 32) | raw[1],
                              "child_words": raw[3] & 0xFFFFF})
        elif kind == 2:
            require(opcode == 0x22, "invalid_pm4_condition_opcode")
            decisions.append({"order": order, "command": record["command"],
                              "skipped": record["skipped"], "skip_words": record["skip_words"]})
    require(all(observed[i] <= counts[i] for i in range(256)), "pm4_opcode_count_underflow")
    submission = {"integrity": "not_collected"}
    if "submission_samples" in trace:
        try:
            submission = validate_submission_samples(trace["submission_samples"])
        except CaptureError as error:
            submission = {"integrity": "failed", "reason": str(error)}
    producer, consumers = {"integrity": "not_collected"}, []
    if "producer_samples" in trace:
        try:
            producer = validate_producer_samples(trace["producer_samples"])
            serials = {r["serial"] for r in producer["records"]}
            for r in records:
                serial = r.get("producer_serial")
                require(integer(serial, 1, 2**64-1) and serial not in serials, "invalid_pm4_producer_serial")
                serials.add(serial)
                if r["kind"] == 1 and r["opcode"] == 0x35:
                    consumers.append({"serial": serial, "command": r["command"], "packet": r["raw_prefix"][:5],
                                      "submission": r["submission"], "instances": r["after_instances"],
                                      "programs": r["programs"], "primitive": r["primitive"],
                                      "color_mask": r["color_mask"]})
        except CaptureError as error:
            producer, consumers = {"integrity": "failed", "reason": str(error)}, []
    return {"integrity": "passed", "status": trace["status"], "dropped": dropped, "kind_counts": kinds,
            "opcode_counts": counts, "sampled_register_comparisons": comparisons,
            "sampled_register_mismatches": mismatches, "condition_decisions": decisions,
            "indirect_buffer_edges": edges, "stream_completeness": "not_proven", "submission_samples": submission,
            "producer_samples": producer, "producer_consumers": consumers,
            "classification": "executed_packet_metadata_not_guest_generation_proof"}


def validate_stage_observations(directory, final_directory, final_result, previews=None):
    """Pair by the confirmed presentation of each actual prepared Frame, not file index."""
    directory, final_directory = Path(directory).resolve(), Path(final_directory).resolve()
    session, final = read_json(directory / "session.json"), read_json(final_directory / "session.json")
    require(isinstance(session, dict) and session.get("schema") == 3 and
            session.get("contract") == "stage_observations_v1", "invalid_observation_contract")
    require(session.get("status") == "Completed", f"incomplete_observations:{session.get('reason', '')}")
    capture_id = session.get("capture_id")
    require(isinstance(capture_id, str) and re.fullmatch(r"[0-9a-f]{32}", capture_id) and
            capture_id == final.get("capture_id"), "observation_session_mismatch")
    count = session.get("requested")
    require(integer(count, 1, 16) and session.get("started") == count, "invalid_observation_count")
    require({p.name for p in directory.glob("frame_*.json")} == {f"frame_{i:03}.json" for i in range(count)},
            "observation_manifest_mismatch")
    expected_raw, observations, previous_present, previous_event = set(), [], None, 0
    traces, pm4_traces = [], []
    for index in range(count):
        frame = read_json(directory / f"frame_{index:03}.json")
        require(frame.get("schema") == 3 and frame.get("contract") == "stage_observations_v1" and
                frame.get("frame_index") == index, "invalid_observation_frame")
        require(frame.get("association") == "prepare_interval_only" and
                frame.get("content_path") == "not_proven", "invalid_observation_claim")
        present = frame.get("present_index")
        require(integer(present, 1, 2**64-1) and (previous_present is None or present == previous_present + 1),
                "nonconsecutive_observation_presents")
        previous_present = present
        status, snapshots = frame.get("gbuffer_status"), frame.get("snapshots")
        require(status in {"observed", "missing", "ambiguous"} and isinstance(snapshots, list), "invalid_gbuffer_status")
        stages = [s.get("stage") for s in snapshots]
        require(stages in ([['display']] if status == 'missing' else
                          [['gbuffer', 'display']] if status == 'observed' else
                          [['display'], ['gbuffer', 'display']]),
                "observation_stages_mismatch")
        for snapshot in snapshots:
            stage = snapshot["stage"]
            require(snapshot.get("file") == f"{stage}_{index:03}.raw", "invalid_observation_path")
            expected_raw.add(snapshot["file"])
            require(snapshot.get("format") in FORMATS[stage] and
                    integer(snapshot.get("width"), 1, 32768) and integer(snapshot.get("height"), 1, 32768),
                    "invalid_observation_format_or_extent")
            size = snapshot["width"] * snapshot["height"] * 4
            require(size <= 128*1024*1024 and snapshot.get("bytes") == size, "invalid_observation_bytes")
            identity = snapshot.get("identity")
            require(isinstance(identity, dict) and identity.get("width") == snapshot["width"] and
                    identity.get("height") == snapshot["height"] and
                    identity.get("format") == PHYSICAL_FORMATS[snapshot["format"]] and
                    integer(identity.get("uid"), 1, 2**64-1) and isinstance(identity.get("image"), str) and
                    re.fullmatch(r"0x[0-9a-fA-F]+", identity["image"]) and int(identity["image"], 16) > 0 and
                    integer(identity.get("alias"), 0, 2**64-1) and
                    integer(identity.get("bytes"), size, 2**64-1) and
                    integer(identity.get("address"), 1, 2**64-1) and
                    identity["address"] + identity["bytes"] <= 2**64-1, "invalid_observation_identity")
            event = snapshot.get("event_sequence")
            require(integer(event, previous_event + 1, 2**64-1) and
                    integer(snapshot.get("scheduler_tick"), 1, 2**64-1) and
                    integer(snapshot.get("submit_sequence"), 0, 2**64-1), "invalid_observation_order")
            previous_event = event
            data_path = directory / snapshot["file"]
            require(data_path.stat().st_size == size and
                    fnv1a64(data_path.read_bytes()) == snapshot.get("content_hash"), "observation_integrity_failed")
            if stage == "gbuffer":
                attachments = frame.get("attachments")
                require(isinstance(attachments, list) and len(attachments) == 5, "invalid_observation_attachments")
                root = next((a for a in attachments if a.get("slot") == 2), {})
                require(root.get("image_uid") == identity["uid"] and root.get("guest_address") == identity["address"],
                        "observation_root_mismatch")
        observations.append(frame)
        if "draw_trace" in frame:
            try:
                summary = validate_draw_trace(directory, frame)
            except (CaptureError, OSError, TypeError, KeyError, AttributeError) as error:
                summary = {"integrity": "failed", "reason": str(error), "classification": "inconclusive"}
            traces.append({"observation_index": index, "present_index": present, **summary})
        if "pm4_trace" in frame:
            try:
                summary = validate_pm4_trace(directory, frame)
            except (CaptureError, OSError, TypeError, KeyError, AttributeError) as error:
                summary = {"integrity": "failed", "reason": str(error), "classification": "inconclusive"}
            pm4_traces.append({"observation_index": index, "present_index": present, **summary})
    require({p.name for p in directory.glob("*.raw")} == expected_raw, "observation_raw_mismatch")
    final_by_present = {r["present_index"]: r["index"] for r in final_result["results"]}
    pairs = [{"present_index":f["present_index"], "final_index":final_by_present[f["present_index"]],
              "observation_index":f["frame_index"], "gbuffer_status":f["gbuffer_status"],
              "association":"prepare_interval_only", "content_path":"not_proven"}
             for f in observations if f["present_index"] in final_by_present]
    if previews is not None:
        for frame in observations:
            for snapshot in frame["snapshots"]:
                write_preview(Path(previews) / Path(snapshot["file"]).with_suffix(".png"),
                              (directory / snapshot["file"]).read_bytes(), snapshot)
    return {"integrity":"passed", "frames":count, "pairs":pairs, "draw_traces":traces, "pm4_traces":pm4_traces,
            "submission_comparison":compare_submission_samples(pm4_traces),
            "producer_comparison":compare_producer_samples(pm4_traces),
            "unmatched_final_frames":sorted(set(final_by_present.values()) - {p["final_index"] for p in pairs}),
            "classification":"temporal_observations_no_proven_pixel_path", "visibility_automatic":False}


def validate(directory, labels=None, previews=None):
    directory = Path(directory).resolve()
    session = read_json(directory / "session.json")
    if isinstance(session, dict) and session.get("contract") == "final_game_frames_v1":
        return validate_final_frames(directory, session, labels, previews)
    require(isinstance(session, dict) and session.get("schema") in (1, 2), "invalid_session_schema")
    schema = session["schema"]
    require(schema != 2 or session.get("contract") == CONTRACT, "invalid_session_contract")
    require(session.get("status") == "Completed", f"incomplete_session:{session.get('reason', '')}")
    count = session.get("requested")
    require(integer(count, 1, 16) and session.get("started") == count, "invalid_frame_count")
    require(isinstance(labels, (dict, type(None))), "invalid_labels")
    require(labels is None or set(labels) == {str(i) for i in range(count)}, "labels_frame_mismatch")
    expected_manifests = {f"frame_{i:03}.json" for i in range(count)}
    require({p.name for p in directory.glob("frame_*.json")} == expected_manifests, "manifest_frame_mismatch")
    expected_raw = set()
    result = []
    previous_present = None
    previous_event = None
    for index in range(count):
        frame = read_json(directory / f"frame_{index:03}.json")
        require(isinstance(frame, dict) and frame.get("schema") == schema, "invalid_frame_schema")
        require(frame.get("frame_index") == index, "frame_index_mismatch")
        present = frame.get("present_index")
        require(integer(present, 1, 2**64 - 1), "invalid_present_index")
        require(previous_present is None or present == previous_present + 1, "nonconsecutive_presents")
        previous_present = present
        require(integer(frame.get("submit_sequence"), 0, 2**64 - 1), "invalid_submit_sequence")
        require([frame.get(k) for k in ("producer_vs", "producer_ps", "first_consumer_ps", "composition_cs")]
                == ["6e383eb8", "b1f064c3", "836b20e6", "4105cfbc"], "unexpected_passes")
        attachments = frame.get("attachments")
        require(isinstance(attachments, list) and len(attachments) == 5, "missing_attachments")
        require([a.get("slot") for a in attachments] == list(range(5)), "attachment_slot_mismatch")
        snapshots = frame.get("snapshots")
        require(isinstance(snapshots, list) and [s.get("stage") for s in snapshots] == list(FORMATS),
                "snapshot_stage_mismatch")
        for snapshot in snapshots:
            stage = snapshot["stage"]
            require(snapshot.get("format") in FORMATS[stage], "unsupported_format")
            w, h = snapshot.get("width"), snapshot.get("height")
            require(integer(w, 1, 16384) and integer(h, 1, 16384), "invalid_dimensions")
            require(snapshot.get("bpp") == 4 and snapshot.get("bytes") == w * h * 4
                    and w * h * 4 <= 128 * 1024 * 1024, "invalid_snapshot_size")
            require((snapshot.get("samples"), snapshot.get("mip"), snapshot.get("layer")) == (1, 0, 0),
                    "unsupported_subresource")
            for field in ("image_uid", "guest_address", "scheduler_tick"):
                require(integer(snapshot.get(field), 1, 2**64 - 1), f"invalid_{field}")
            require(integer(snapshot.get("alias_generation"), 0, 2**64 - 1), "invalid_alias_generation")
            name = f"{stage}_{index:03}.raw"
            require(snapshot.get("file") == name, "snapshot_filename_mismatch")
            expected_raw.add(name)
            path = directory / name
            require(not path.is_symlink() and path.is_file() and path.stat().st_size == w * h * 4,
                    f"missing_or_truncated_data:{name}")
            data = path.read_bytes()
            require(snapshot.get("hash_algorithm") == "fnv1a64"
                    and snapshot.get("content_hash") == fnv1a64(data), f"content_hash_mismatch:{name}")
            if stage == "gbuffer":
                attachment = attachments[2]
                require(all(attachment.get(k) == snapshot.get(k) for k in
                            ("image_uid", "guest_address", "format", "width", "height", "samples")),
                        "gbuffer_attachment_mismatch")
            if stage == "display":
                require(frame.get("display_uid") == snapshot["image_uid"]
                        and frame.get("display_address") == snapshot["guest_address"], "display_identity_mismatch")
            if previews is not None:
                write_preview(Path(previews) / f"{stage}_{index:03}.png", data, snapshot)
        if schema == 2:
            previous_event = validate_temporal(frame, previous_event)
        observation = labels.get(str(index), {}) if labels is not None else {}
        require(isinstance(observation, dict) and not set(observation).difference(FORMATS), "invalid_stage_labels")
        result.append({"frame_index": index, "present_index": present,
                       "observation_source": "human_labels" if labels is not None else "none",
                       "classification": classify(observation)})
    require({p.name for p in directory.glob("*.raw")} == expected_raw, "unexpected_raw_files")
    return {"integrity": "passed", "schema": schema, "frames": count,
            "content_preservation": "not_asserted", "visibility_automatic": False, "results": result}


def unsigned_float(bits, mantissa):
    exponent, fraction = bits >> mantissa, bits & ((1 << mantissa) - 1)
    if exponent == 31:
        return float("inf") if fraction == 0 else 0.0
    if exponent == 0:
        return math.ldexp(fraction / (1 << mantissa), -14)
    return math.ldexp(1 + fraction / (1 << mantissa), exponent - 15)


def rgb_pixels(data, format_name):
    for (word,) in struct.iter_unpack("<I", data):
        if format_name == "B10G11R11UfloatPack32":
            values = (unsigned_float(word & 2047, 6), unsigned_float((word >> 11) & 2047, 6),
                      unsigned_float(word >> 22, 5))
            # Preview only: simple Reinhard tone map followed by sRGB encoding.
            rgb = []
            for value in values:
                value = 1.0 if math.isinf(value) else max(0.0, value) / (1 + max(0.0, value))
                rgb.append(round(255 * (12.92 * value if value <= 0.0031308 else 1.055 * value**(1 / 2.4) - 0.055)))
            yield bytes(rgb)
        elif format_name.startswith("A2"):
            values = [round(255 * ((word >> shift) & 1023) / 1023) for shift in (0, 10, 20)]
            yield bytes(values[::-1] if format_name.startswith("A2R") else values)
        else:
            values = [word & 255, (word >> 8) & 255, (word >> 16) & 255]
            yield bytes(values[::-1] if format_name.startswith("B8") else values)


def write_preview(path, data, snapshot):
    w, h = snapshot["width"], snapshot["height"]
    pixels = b"".join(rgb_pixels(data, snapshot["format"]))
    scanlines = b"".join(b"\0" + pixels[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))

    content = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
               + chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--labels", type=Path, help="JSON of explicit human observations, keyed by frame index")
    parser.add_argument("--previews", type=Path, help="write PNG previews; HDR conversion is for inspection only")
    parser.add_argument("--stage-observations", type=Path, help="optional independent stage observations, paired by presentation")
    parser.add_argument("--stage-previews", type=Path)
    args = parser.parse_args(argv)
    try:
        labels = read_json(args.labels) if args.labels is not None else None
        result = validate(args.directory, labels, args.previews)
        if args.stage_observations is not None:
            try:
                result["stage_observations"] = validate_stage_observations(
                    args.stage_observations, args.directory, result, args.stage_previews)
            except (CaptureError, OSError, TypeError, KeyError, AttributeError) as error:
                result["stage_observations"] = {"integrity":"failed", "reason":str(error), "classification":"inconclusive"}
    except (CaptureError, OSError, TypeError, KeyError, AttributeError) as error:
        print(json.dumps({"integrity": "failed", "reason": str(error), "classification": "inconclusive"}))
        return 2
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
