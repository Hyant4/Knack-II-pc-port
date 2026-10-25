"""Exercise production manifest/presentation state in a CPU-only C++ harness.

The production State prefix and Present function are compiled verbatim. Vulkan image
IDs are inert integers; Stage/dispatch/readback code is excluded. This verifies callback
ordering, provenance, budgeting and completion, not Vulkan behavior. No runtime is launched.
"""

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = Path(os.environ.get("MSYS2_ROOT") or Path(os.environ.get("LOCALAPPDATA", "")) / "knack2-toolchain/msys64") / "clang64"
COMPILER = TOOLCHAIN / "bin/clang++.exe"


@unittest.skipUnless(COMPILER.is_file(), "existing Windows Clang toolchain unavailable")
class NativeStateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="gpu-phy03-native-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        text = (ROOT / "port/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp").read_text()
        prefix = text[text.index("namespace GpuPhy03 {"):text.index("    bool Stage(const Instance&")]
        header = (ROOT / "port/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.h").read_text(encoding="utf-8")
        types = header[header.index("struct RelicCommandSample {"):header.index("class GraphicsPipeline;")]
        prefix = prefix.replace("namespace GpuPhy03 {", "namespace GpuPhy03 {\n" + types, 1)
        start_sb = text.index("Identity Identify(const VideoCore::Image& image) {")
        end_sb = text.index("State& Get()", start_sb)
        stable = text[start_sb:end_sb]
        start_prepare = text.index("void Prepare(const Instance& instance,")
        start = text.index("void Present(const void* frame, bool success, bool reused,")
        end = text.index("} // namespace GpuPhy03", start)
        present = text[start:text.index("bool TraceEnabled()", start)]
        prepare = text[start_prepare:start]
        source = r'''
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>
#include <fmt/format.h>
using u8 = unsigned char; using u32 = unsigned int; using u64 = unsigned long long;
namespace vk {
using Image = u64;
enum class Format : u32 { eUndefined = 0, eR8G8B8A8Unorm = 37, eB8G8R8A8Unorm = 44 };
inline std::string to_string(Format f) { return f == Format::eB8G8R8A8Unorm ? "B8G8R8A8Unorm" : "R8G8B8A8Unorm"; }
}
using VkImage = vk::Image;
struct Instance {}; struct Scheduler {}; struct Runtime {};
namespace VideoCore {
struct ImageId {
    u32 index{};
    constexpr ImageId(u32 value = 0) : index(value) {}
    auto operator<=>(const ImageId&) const = default;
};
struct Extent2D { u32 width{}, height{}; };
struct ImageCI { Extent2D extent; vk::Format format{vk::Format::eR8G8B8A8Unorm}; };
struct BackingSubImage { ImageCI image_ci; };
struct BackingImage { BackingSubImage image; };
struct ImageInfo { u64 guest_address{}, guest_size{}; };
struct Image {
    u64 image_uid{};
    vk::Image image{};
    u64 alias_generation{};
    ImageInfo info;
    BackingImage backing_storage;
    BackingImage* backing = &backing_storage;
    vk::Image GetImage() const { return image; }
};
struct TextureCache {
    std::map<ImageId, Image> images;
    Image& GetImage(ImageId id) { return images[id]; }
};
}
'''
        stage_spy = r'''
    Snapshot fake_stage;
    bool Stage(const Instance&, Scheduler&, Runtime&, VideoCore::Image& image,
               const std::shared_ptr<Epoch>& e, const char*) {
        const auto& ci = image.backing->image.image_ci;
        BookSnapshot(e, fake_stage, {image.image_uid, image.GetImage(), image.alias_generation,
            image.info.guest_address, image.info.guest_size, ci.extent.width, ci.extent.height, u32(ci.format)});
        return true;
    }
'''
        source += prefix + stage_spy + "};\n" + stable + "State& Get() { static State state; return state; }\n}\n" + prepare + present + "}\n"
        source += r'''
int main(int argc, char** argv) {
    using namespace GpuPhy03;
    if (argc != 3) return 10;
    const std::filesystem::path base = argv[1];
    const auto output = base / "capture";
    const auto trigger = base / "request";
    const std::string mode = argv[2];
    const u32 frame_count = mode == "complete-16" ? 16 : 2;
    std::ofstream(trigger) << "capture";
    _putenv_s("TLG_GPU_PHY03_TRIGGER", trigger.string().c_str());
    _putenv_s("TLG_GPU_PHY03_DIR", output.string().c_str());
    _putenv_s("TLG_GPU_PHY03_FRAMES", frame_count == 16 ? "16" : "2");
    if (mode.starts_with("observe-")) {
        if (mode.starts_with("observe-trace")) _putenv_s("TLG_RELIC_TRACE", "1");
        if (mode.starts_with("observe-trace-pm4")) _putenv_s("TLG_RELIC_PM4_TRACE", "1");
        if (mode.starts_with("observe-trace-pm4-submit")) _putenv_s("TLG_RELIC_SUBMIT_TRACE", "1");
        if (mode.starts_with("observe-trace-pm4-producer")) _putenv_s("TLG_RELIC_PRODUCER_TRACE", "1");
        if (mode.starts_with("observe-trace-pm4-producer-list")) _putenv_s("TLG_RELIC_LIST_TRACE", "1");
        _putenv_s("TLG_GPU_PHY03_OBSERVE", "1");
        _putenv_s("TLG_GPU_PHY03_FRAMES", "16");
        _putenv_s("TLG_FRAME_SEQUENCE_ID", "1234567890abcdef1234567890abcdef");
    }
    auto& s = Get();
    s.Poll();
    if (!s.active || std::filesystem::exists(trigger)) return 11;
    if (mode.starts_with("observe-")) {
        int tokens[16]{};
        for (u32 i = 0; i < 16; ++i) {
            auto e = std::make_shared<Epoch>(); e->index = i;
            const bool missing = mode == "observe-missing";
            if (!missing) {
                e->gbuffer = true; e->observation_ambiguous = mode == "observe-ambiguous";
                for (u32 j = 0; j < 5; ++j) {
                    if (j) e->attachments += ',';
                    e->attachments += fmt::format("{{\"slot\":{},\"image_uid\":{},\"guest_address\":{}}}", j, j+1, 0x1000+j*16);
                }
                s.current = e; s.epochs.push_back(e);
            }
            unsigned char bytes[16]{}; u64 hash = 14695981039346656037ull;
            for (u32 j = 0; j < 16; ++j) { bytes[j] = i+j; hash ^= bytes[j]; hash *= 1099511628211ull; }
            Snapshot snapshot; snapshot.stage = "gbuffer"; snapshot.file = fmt::format("gbuffer_{:03}.raw", i);
            snapshot.format = "R8G8B8A8Unorm"; snapshot.width = 2; snapshot.height = 2;
            snapshot.bytes = 16; snapshot.tick = 7+i; snapshot.hash = fmt::format("{:016x}", hash);
            if (!missing) {
                std::ofstream file(output / snapshot.file, std::ios::binary); file.write((const char*)bytes, 16);
                s.BookSnapshot(e, snapshot, {3, 103, 7, 0x1020, 16, 2, 2, 37});
            }
            snapshot.stage = "display"; snapshot.file = fmt::format("display_{:03}.raw", i);
            { std::ofstream file(output / snapshot.file, std::ios::binary); file.write((const char*)bytes, 16); }
            s.fake_stage = snapshot;
            VideoCore::Image image; image.image_uid = 20+i; image.image = 120+i;
            image.alias_generation = 70+i; image.info.guest_address = 0x3000+i*16; image.info.guest_size = 16;
            image.backing_storage.image.image_ci.extent = {2, 2};
            // A deliberately unrelated/stale lineage cannot block observation mode.
            Identity stale{image.image_uid, image.image, 1, image.info.guest_address, 16, 2, 2, 37};
            s.lineage[stale.Key()] = {stale, {}};
            Instance instance; Scheduler scheduler; Runtime runtime;
            if (mode.starts_with("observe-trace")) {
                DrawTraceRecord r; r.count = 12+i; r.instances = 3; r.first_instance = 2;
                r.indexed = true; r.tick = 7+i; r.pipeline = 0xabcdef;
                r.buffers[0] = {true, 512, 64, 192, 12};
                for (u32 kind : {1u, 3u, 5u, 4u}) { r.kind = kind; s.RecordDrawTrace(r); }
                FrontTraceKey key; key.outcome = 1; key.programs[1] = 0x123400;
                s.trace_window->AddFront(key, 12+i, 3, false);
                s.trace_window->AddFront(key, 8, 1, true);
                if (mode == "observe-trace-front-overflow") {
                    for (u32 j = 1; j <= DrawTraceWindow::FrontCapacity; ++j) {
                        auto other = key; other.programs[0] = j;
                        s.trace_window->AddFront(other, 1, 1, false);
                    }
                    s.trace_window->AddFront(key, 4, 2, false); // existing key still works when full
                }
                if (mode == "observe-trace-overflow")
                    for (u32 j = 0; j < DrawTraceWindow::Capacity; ++j) s.RecordDrawTrace(r);
                if (s.trace_window->pm4) {
                    Pm4TraceRecord p; p.kind = 1; p.opcode = 0x2f; p.packet_words = 2;
                    p.raw_words = 2; p.raw = {0xc0002f00, 7}; p.remaining_words = 16;
                    p.buffer_words = 16; p.buffer_base = 0x4000; p.command = 0x4000;
                    p.before_instances = 1; p.after_instances = 7;
                    if (s.producer_enabled) {
                        RelicProducerSample writer; writer.command = 0x5000; writer.thread = 7;
                        writer.packet = {0xc0033500, 60, 4500, 60, 0}; writer.stop = 1;
                        writer.frame_count = 1; writer.frames[0] = {0x8000, 0xbb05f1};
                        writer.frames[0].caller_allocation_base = 0x1000;
                        writer.caller = 0xbb05f1; writer.caller_allocation_base = 0x1000;
                        writer.guest_frame = 0x8000; writer.guest_stack = 0x7f00;
                        writer.host_unwind = 1; writer.host_steps = 2;
                        if (s.list_enabled) {
                            writer.caller=0x1000+0xbb05f1; writer.frame_count=4;
                            const u64 returns[]{0xbf5834,0xbe5989,0xc348c5,0x9bc831};
                            for (u32 k=0;k<4;++k) writer.frames[k]={0x8000+k*0x100,0x1000+returns[k],0x1000};
                            auto& q = writer.list; q.status=1; q.scan_status=1;
                            q.owner_frame=0x8300; q.builder_frame=0x8200; q.list=0x6000;
                            q.entries=0x7000; q.count=12; q.capacity=16; q.batch_count=7;
                            q.cursor=0x54+7*0x58; q.scanned=12; q.null_records=1;
                            q.null_materials=2; q.readable_materials=9;
                            s.trace_window->pm4->ReserveListScan(q);
                        }
                        writer.serial = ++s.producer_sequence;
                        s.trace_window->pm4->AddProducer(writer);
                        if (mode == "observe-trace-pm4-producer-overflow") {
                            for (u32 j = 0; j < Pm4TraceWindow::ProducerCapacity; ++j) {
                                writer.serial = ++s.producer_sequence;
                                s.trace_window->pm4->AddProducer(writer);
                            }
                        }
                        p.opcode = 0x35; p.packet_words = 5; p.raw_words = 5;
                        p.raw = {0xc0033500, 60, 4500, 60, 0}; p.after_indices = 60;
                        p.command = 0x5000; p.buffer_base = 0x5000;
                        p.producer_serial = ++s.producer_sequence;
                    }
                    s.trace_window->pm4->Add(p, true);
                    if (mode == "observe-trace-pm4-overflow")
                        for (u32 j = 0; j < Pm4TraceWindow::Capacity; ++j) s.trace_window->pm4->Add(p, true);
                    p.opcode = 0x10; s.trace_window->pm4->Add(p, false); // counted, not retained
                    if (s.submission_enabled) {
                        RelicSubmissionSample sub; sub.kind = 1; sub.copy_mode = 1; sub.submission = 100+i;
                        sub.samples[0] = {0x4000, 4, 0x1234, 1}; sub.samples[1] = {0, 0, 0x5678, 1};
                        sub.samples[2] = sub.samples[0]; sub.samples[3] = sub.samples[1];
                        s.trace_window->pm4->AddSubmission(sub);
                        sub.kind = 2; sub.copy_mode = 0; sub.samples[2] = {}; sub.samples[3] = {};
                        s.trace_window->pm4->AddSubmission(sub);
                        if (mode == "observe-trace-pm4-submit-overflow")
                            for (u32 j = 0; j < Pm4TraceWindow::SubmissionCapacity; ++j)
                                s.trace_window->pm4->AddSubmission(sub);
                    }
                }
            }
            Prepare(instance, scheduler, runtime, &image, &tokens[i]);
            if (!s.error.empty() || s.current || !s.prepared.contains(&tokens[i])) return 66;
            Present(&tokens[i], mode != "observe-failed-present", false);
            if (mode == "observe-failed-present") {
                for (auto& snap : s.epochs.back()->snapshots) snap.saved = true;
                s.Publish(s.epochs.back());
                return !s.completed && s.error == "prepared_frame_not_presented" &&
                    !std::filesystem::exists(output / "frame_000.json") ? 0 : 67;
            }
        }
        if (s.completed) return 68;
        for (auto it = s.epochs.rbegin(); it != s.epochs.rend(); ++it) {
            for (auto& snapshot : (*it)->snapshots) snapshot.saved = true;
            s.Publish(*it);
        }
        return s.completed && s.error.empty() && s.prepared.empty() ? 0 : 69;
    }
    int frame_storage[16]{};
    for (u32 i = 0; i < frame_count; ++i) {
        auto e = std::make_shared<Epoch>();
        e->index = i; e->prepared = true; e->gbuffer = true; e->composition = false;
        e->display_uid = 20 + i; e->display_address = 0x3000 + i * 16; e->submit_sequence = 27;
        for (u32 j = 0; j < 5; ++j) {
            if (j) e->attachments += ',';
            e->attachments += fmt::format("{{\"slot\":{},\"image_uid\":{},\"guest_address\":{},"
                "\"format\":\"R8G8B8A8Unorm\",\"width\":2,\"height\":2,\"samples\":1}}", j, j+1, 0x1000+j*16);
        }
        const char* stages[] = {"gbuffer", "composition", "display"};
        const char* formats[] = {"R8G8B8A8Unorm", "B10G11R11UfloatPack32", "A2B10G10R10UnormPack32"};
        const u64 ids[] = {3, 10, e->display_uid};
        const u64 addresses[] = {0x1020, 0x2000, e->display_address};
        s.current = e; s.submit_sequence = 27;
        for (u32 j = 0; j < 3; ++j) {
            Snapshot snapshot;
            snapshot.stage = stages[j]; snapshot.file = fmt::format("{}_{:03}.raw", stages[j], i);
            snapshot.format = formats[j]; snapshot.uid = ids[j]; snapshot.address = addresses[j];
            snapshot.alias = i + 1;
            snapshot.width = 2; snapshot.height = 2; snapshot.bytes = 16; snapshot.bpp = 4; snapshot.tick = 100 + i;
            unsigned char bytes[16]; u64 hash = 14695981039346656037ull;
            for (u32 k = 0; k < 16; ++k) { bytes[k] = k + i; hash ^= bytes[k]; hash *= 1099511628211ull; }
            std::ofstream file(output / snapshot.file, std::ios::binary);
            file.write(reinterpret_cast<const char*>(bytes), 16);
            snapshot.hash = fmt::format("{:016x}", hash);
            const u32 vk_formats[] = {37, 122, 64};
            Identity identity{ids[j], ids[j] + 100, i + 1, addresses[j], 16, 2, 2, vk_formats[j]};
            const auto previous = s.lineage.find(identity.Key());
            if (previous != s.lineage.end()) s.NotePreparation(previous->second.version, identity, true);
            if (j == 0) {
                s.input_versions.clear(); s.input_from_composition = false;
                s.Write(identity, e, true, "gbuffer_producer", 0xb1f064c3, 2, "CLEAR", 100+i);
            } else {
                const auto& prior = e->snapshots.back();
                s.input_versions = {s.lineage.at({prior.uid, prior.uid + 100}).version};
                s.input_from_composition = j == 2;
                s.Write(identity, e, true, j == 1 ? "dispatch_storage" : "draw_color",
                        j == 1 ? 0x4105cfbc : 0x12345678, j == 1 ? ~0u : 0, "LOAD", 100+i);
            }
            s.BookSnapshot(e, snapshot, identity);
            if (j == 1) {
                e->composition = true;
                Identity updated = identity; updated.alias += 100;
                s.NotePreparation(identity, updated, true);
                s.input_versions.clear(); s.input_from_composition = false;
                s.Write(updated, {}, true, "draw_color", 0x15126eb7, 0, i % 2 ? "CLEAR" : "LOAD", 100+i);
            }
        }
        s.epochs.push_back(e); s.prepared[&frame_storage[i]] = e;
    }
    s.Session();
    if (mode != "complete-16" && mode != "complete" && mode != "failed" && mode != "gap" && !mode.starts_with("prepare-")) {
        s.lineage.clear(); s.write_preparations.clear(); s.input_versions.clear();
        s.input_from_composition = false;
    }
    if (mode.starts_with("prepare-")) {
        auto e = s.epochs[1];
        e->prepared = false;
        s.fake_stage = e->snapshots.back(); e->snapshots.pop_back();
        s.prepared.clear();
        s.epochs[0]->presented = true;
        VideoCore::Image image;
        image.image_uid = 21; image.image = 121; image.alias_generation = 2;
        image.info.guest_address = 0x3010; image.info.guest_size = 16;
        image.backing_storage.image.image_ci.extent = {2, 2};
        image.backing_storage.image.image_ci.format = static_cast<vk::Format>(64);
        if (mode == "prepare-token-reuse") s.prepared[&frame_storage[1]] = s.epochs[0];
        if (mode == "prepare-backing") ++image.image;
        if (mode == "prepare-wrong-frame") s.current = s.epochs[0];
        if (mode == "prepare-no-composition") s.lineage.at({21, 121}).from_composition = false;
        Instance instance; Scheduler scheduler; Runtime runtime;
        Prepare(instance, scheduler, runtime, &image, &frame_storage[1]);
        if (mode == "prepare-token-reuse")
            return s.error == "frame_token_already_prepared" && !e->prepared ? 0 : 61;
        if (mode == "prepare-valid")
            return s.error.empty() && !s.current && e->prepared &&
                s.prepared.at(&frame_storage[1]) == e && e->snapshots.size() == 3 ? 0 : 59;
        return !s.error.empty() && !s.active && !e->prepared && s.prepared.empty() ? 0 : 60;
    }
    if (mode == "complete-16") {
        for (u32 i = 0; i < frame_count; ++i) {
            for (auto& snapshot : s.epochs[i]->snapshots) snapshot.saved = true;
            Present(&frame_storage[i], true, false);
            if (s.completed != (i + 1 == frame_count)) return 51;
        }
        return s.completed && !s.active && s.error.empty() ? 0 : 52;
    }
    if (mode == "access") {
        return ShaderAccess(false, false) == Access::ReadOnly &&
            ShaderAccess(true, false) == Access::MayWrite &&
            ShaderAccess(false, true) == Access::Atomic &&
            ShaderAccess(true, true) == Access::Atomic ? 0 : 30;
    }
    if (mode == "event-budget") {
        s.current = s.epochs[1];
        Identity image{9, 42, 7, 0x7000, 256, 16, 4, 1};
        s.input_versions.assign(128, image);
        for (u32 i = 0; i < 2000 && s.active; ++i)
            s.Write(image, s.current, true, "dispatch_storage", 0x12345678);
        return s.error == "association_metadata_budget_exceeded" && !s.active &&
            s.event_bytes <= s.AssociationBudget && !s.completed ? 0 : 63;
    }
    if (mode == "budget-boundary") {
        if (!s.Reserve(s.StagingBudget) || s.outstanding != s.StagingBudget) return 42;
        if (s.Reserve(~0ull) || s.outstanding != s.StagingBudget || s.active) return 43;
        s.Release(s.StagingBudget);
        return s.outstanding == 0 && s.error == "staging_budget_exceeded" ? 0 : 44;
    }
    if (mode.starts_with("budget")) {
        constexpr u64 bytes = 1920ull * 1080 * 4;
        u32 pending = 0;
        for (u32 i = 0; i < 16 * 3; ++i) {
            const auto before = s.outstanding;
            if (!s.Reserve(bytes)) {
                if (mode != "budget-delayed" || i != 16 || s.outstanding != before ||
                    s.error != "staging_budget_exceeded" || s.active || s.completed) return 31;
                while (pending) { s.Release(bytes); --pending; }
                for (auto& e : s.epochs) {
                    e->presented = true;
                    for (auto& snapshot : e->snapshots) snapshot.saved = true;
                    s.Publish(e); // late callbacks must not publish after failure
                }
                return s.outstanding == 0 && !s.completed &&
                    !std::filesystem::exists(output / "frame_000.json") ? 0 : 32;
            }
            ++pending;
            if (mode == "budget-streaming" && pending == 12) {
                while (pending) { s.Release(bytes); --pending; }
            }
        }
        while (pending) { s.Release(bytes); --pending; }
        return mode == "budget-streaming" && s.outstanding == 0 && s.error.empty() ? 0 : 33;
    }
    if (mode.starts_with("temporal-")) {
        s.current = s.epochs[1];
        Identity before{9, 42, 7, 0x7000, 256, 16, 4, 1};
        s.Write(before, s.current, true, "dispatch_storage", 0x4105cfbc);
        Identity after = before; ++after.alias;
        bool safe = true;
        if (mode == "temporal-backing") ++after.image;
        if (mode == "temporal-alias") { ++after.uid; ++after.image; }
        if (mode == "temporal-unobserved") ++before.alias;
        if (mode == "temporal-unsafe") safe = false;
        if (mode != "temporal-no-proof") s.NotePreparation(before, after, safe);
        if (!s.error.empty()) return !s.active ? 0 : 56;
        s.input_versions.clear(); s.input_from_composition = false;
        s.Write(after, {}, true, mode == "temporal-storage" ? "dispatch_storage" : "draw_color", 0x15126eb7, 0,
                mode == "temporal-storage" ? "none" : mode == "temporal-clear" ? "CLEAR" : "LOAD", 42);
        if (mode == "temporal-no-proof") return s.error == "ambiguous_lineage_write" ? 0 : 57;
        return s.error.empty() && s.Read(after) == s.current &&
            s.lineage.at(after.Key()).from_composition &&
            s.current->events.back().find("\"kind\":\"destination\"") != std::string::npos ? 0 : 58;
    }
    if (mode.starts_with("read-diagnostic-")) {
        s.current = s.epochs[1];
        Identity tracked{9, 42, 7, 0x7000, 256, 16, 4, 1};
        s.root = tracked;
        s.Write(tracked, s.current);
        Identity incoming = tracked;
        bool full_image = true;
        if (mode == "read-diagnostic-generation" || mode == "read-diagnostic-combined" ||
            mode == "read-diagnostic-valid") {
            Identity prepared = tracked; ++prepared.alias;
            s.NotePreparation(tracked, prepared, true);
            incoming.alias += mode == "read-diagnostic-valid" ? 1 : 2;
        }
        if (mode == "read-diagnostic-partial" || mode == "read-diagnostic-combined") full_image = false;
        if (mode == "read-diagnostic-missing" || mode == "read-diagnostic-combined")
            s.lineage.at(tracked.Key()).epoch.reset();
        s.resources.push_back({0, Access::ReadOnly, incoming, full_image, 0x836b20e6, 0, 1, 0, 1, 2, 1});
        const auto source = s.Source("synthetic_read");
        if (mode == "read-diagnostic-valid")
            return source == s.current && s.error.empty() && s.active &&
                s.lineage.at(tracked.Key()).version == tracked ? 0 : 64;
        // A repeated read and late callbacks cannot duplicate evidence or publish a failed frame.
        s.Source("synthetic_read");
        for (auto& e : s.epochs) {
            e->presented = true;
            for (auto& snapshot : e->snapshots) snapshot.saved = true;
            s.Publish(e);
        }
        return !source && s.error == "stale_or_partial_lineage_binding" && !s.active &&
            !s.completed && s.lineage.at(tracked.Key()).version == tracked &&
            !std::filesystem::exists(output / "frame_000.json") ? 0 : 65;
    }
    if (mode.starts_with("diagnostic-")) {
        s.current = s.epochs[1];
        Identity identity{9, 42, 7, 0x7000, 256, 16, 4, 1};
        s.Write(identity, s.current);
        auto source = s.current;
        bool full_image = true;
        if (mode == "diagnostic-no-source") source.reset();
        if (mode == "diagnostic-rewrite") ++identity.alias;
        if (mode == "diagnostic-alias") { ++identity.uid; ++identity.image; identity.address += 128; }
        if (mode == "diagnostic-partial") full_image = false;
        if (mode == "diagnostic-epoch") source = s.epochs[0];
        s.Write(identity, source, full_image, "synthetic_color", 0xabcdef, 3, "LOAD");
        for (auto& e : s.epochs) {
            e->presented = true;
            for (auto& snapshot : e->snapshots) snapshot.saved = true;
            s.Publish(e); // diagnostic output cannot turn a failed capture into Completed
        }
        return s.error == "ambiguous_lineage_write" && !s.active && !s.completed &&
            !s.lineage.begin()->second.epoch &&
            !std::filesystem::exists(output / "frame_000.json") ? 0 : 55;
    }
    if (mode.starts_with("lineage")) {
        s.current = s.epochs[1];
        Identity identity{9, 42, 7, 0x7000, 256, 16, 4, 1};
        s.Write(identity, s.current);
        if (!s.error.empty()) return 34;
        if (mode == "lineage-valid") return s.Read(identity) == s.current ? 0 : 35;
        if (mode == "lineage-readwrite") {
            s.resources.push_back({0, Access::MayWrite, identity, true});
            if (s.Source() || !s.error.empty()) return 45;
            s.Write(identity, s.Source());
            return s.error == "ambiguous_lineage_write" ? 0 : 46;
        }
        if (mode == "lineage-mixed") {
            Identity older{10, 84, 8, 0x9000, 256, 16, 4, 1};
            s.lineage[older.Key()] = {older, s.epochs[0]};
            s.resources.push_back({0, Access::ReadOnly, identity, true});
            s.resources.push_back({1, Access::ReadOnly, older, true});
            return !s.Source() && s.error == "cross_epoch_lineage" ? 0 : 47;
        }
        if (mode == "lineage-new-write") {
            s.Write(identity, {}); // no source, including an attachment using LOAD
            return s.error == "ambiguous_lineage_write" &&
                !s.lineage.at(identity.Key()).epoch ? 0 : 36;
        }
        if (mode == "lineage-rewrite-generation") {
            ++identity.alias;
            s.Write(identity, s.current);
            return s.error == "ambiguous_lineage_write" ? 0 : 37;
        }
        if (mode == "lineage-old-epoch") {
            s.current = s.epochs[0];
            s.Read(identity);
            return s.error == "cross_epoch_lineage" ? 0 : 38;
        }
        if (mode == "lineage-alias") {
            identity.uid = 99; identity.image = 84; identity.address += 128;
            s.Read(identity);
            return s.error == "aliased_lineage_binding" ? 0 : 39;
        }
        if (mode == "lineage-backing") identity.image = 43;
        else if (mode == "lineage-generation") ++identity.alias;
        else if (mode == "lineage-format") ++identity.format;
        else if (mode == "lineage-size") ++identity.width;
        else if (mode == "lineage-partial") {
            s.Read(identity, false);
            return s.error == "stale_or_partial_lineage_binding" ? 0 : 40;
        }
        s.Read(identity);
        return !s.error.empty() && !s.active ? 0 : 41;
    }
    if (mode == "stable-independent") {
        VideoCore::TextureCache cache;
        auto& img = cache.images[1];
        img.image_uid = 100; img.image = 200; img.alias_generation = 6;
        img.info.guest_address = 0x8000; img.info.guest_size = 256;
        img.backing_storage.image.image_ci.extent = {16, 4};
        Identity binding{100, 200, 5, 0x8000, 256, 16, 4, 37};
        s.current = s.epochs[1];
        s.root = Identity{9, 42, 7, 0x1000, 256, 16, 4, 37};
        s.Write(*s.root, s.current);
        s.resources.push_back({1, Access::ReadOnly, binding, true});
        const bool stable = StableBindings(s, cache);
        return stable && s.error.empty() && s.resources[0].binding == binding &&
            s.Read(*s.root) == s.current && s.lineage.size() == 1 ? 0 : 50;
    }
    if (mode.starts_with("stable-policy-")) {
        VideoCore::TextureCache cache;
        auto& img = cache.images[1];
        img.image_uid = 100; img.image = 200; img.alias_generation = 7;
        img.info.guest_address = 0x8000; img.info.guest_size = 256;
        img.backing_storage.image.image_ci.extent = {16, 4};
        Identity binding{100, 200, 5, 0x8000, 256, 16, 4, 37};
        s.current = s.epochs[1];
        s.root = Identity{9, 42, 4, 0x1000, 256, 16, 4, 37};
        const auto kind = mode.substr(std::string("stable-policy-").size());
        if (kind == "root" || kind == "root-partial") s.root = binding;
        if (kind == "root-overlap") s.root->address = 0x8080;
        if (kind == "root-uid") s.root->uid = binding.uid;
        if (kind == "root-handle") s.root->image = binding.image;
        if (kind == "lineage" || kind == "lineage-dead") {
            s.Write(binding, s.current);
            if (kind == "lineage-dead") s.lineage.at(binding.Key()).epoch.reset();
        }
        if (kind == "lineage-overlap") {
            Identity alias{300, 400, 9, 0x8080, 256, 16, 4, 37};
            s.Write(alias, s.current);
        }
        if (kind == "no-root") s.root.reset();
        if (kind == "unknown-root") s.root->bytes = 0;
        if (kind == "unknown-binding") { binding.bytes = 0; img.info.guest_size = 0; }
        if (kind == "overflow-binding") {
            binding.address = ~0ull - 4; img.info.guest_address = binding.address;
        }
        if (kind == "uid") ++img.image_uid;
        if (kind == "handle") ++img.image;
        if (kind == "address") img.info.guest_address = 0x1080;
        if (kind == "bytes") ++img.info.guest_size;
        if (kind == "extent") ++img.backing_storage.image.image_ci.extent.width;
        if (kind == "format") img.backing_storage.image.image_ci.format = vk::Format::eB8G8R8A8Unorm;
        const auto access = kind == "atomic" ? Access::Atomic :
                            kind == "may-write" ? Access::MayWrite : Access::ReadOnly;
        s.resources.push_back({1, access, binding, kind != "root-partial" && kind != "independent-partial",
                               0x12345678, 1, 3, 0, 1});
        s.draw_targets.push_back(Identify(img));
        const bool stable = StableBindings(s, cache, "synthetic_targets");
        if (kind == "independent-partial") {
            return stable && s.error.empty() && s.lineage.empty() &&
                s.resources[0].binding == binding ? 0 : 53;
        }
        return !stable && s.error == "binding_changed_during_resolution" && !s.active &&
            s.resources[0].binding == binding ? 0 : 54;
    }
    if (mode == "stable-bindings-ok") {
        VideoCore::TextureCache cache;
        VideoCore::Image img;
        img.image_uid = 100;
        img.image = 200;
        img.alias_generation = 5;
        img.info.guest_address = 0x8000;
        img.info.guest_size = 8294400;
        img.backing_storage.image.image_ci.extent = {1920, 1080};
        img.backing_storage.image.image_ci.format = vk::Format::eR8G8B8A8Unorm;
        cache.images[1] = img;

        Identity binding{100, 200, 5, 0x8000, 8294400, 1920, 1080, u32(vk::Format::eR8G8B8A8Unorm)};
        s.resources.push_back({1, Access::ReadOnly, binding, true});

        bool stable = StableBindings(s, cache);
        return stable && s.error.empty() ? 0 : 48;
    }
    if (mode == "stable-bindings-divergence") {
        VideoCore::TextureCache cache;
        VideoCore::Image img;
        img.image_uid = 100;
        img.image = 200;
        img.alias_generation = 5;
        img.info.guest_address = 0x8000;
        img.info.guest_size = 8294400;
        img.backing_storage.image.image_ci.extent = {1920, 1080};
        img.backing_storage.image.image_ci.format = vk::Format::eR8G8B8A8Unorm;
        cache.images[1] = img;

        Identity binding{100, 200, 5, 0x8000, 8294400, 1920, 1080, u32(vk::Format::eR8G8B8A8Unorm)};
        s.resources.push_back({1, Access::ReadOnly, binding, true});

        // Simulate alias_generation mutation
        cache.images[1].alias_generation = 6;

        bool stable = StableBindings(s, cache);
        return !stable && s.error == "binding_changed_during_resolution" && !s.active ? 0 : 49;
    }
    if (mode.starts_with("presentation-")) {
        const auto reason = mode.substr(std::string("presentation-").size());
        Present(&frame_storage[0], false, false, reason.c_str());
        return s.error == reason && !s.active && !s.completed &&
            !std::filesystem::exists(output / "frame_000.json") ? 0 : 62;
    }
    if (mode == "failed") {
        Present(&frame_storage[0], false, false);
        return s.error == "prepared_frame_not_presented" && !s.active ? 0 : 12;
    }
    Present(&frame_storage[0], true, true); // a repeated presentation cannot consume the token
    if (s.prepared.size() != 2 || s.present_sequence != 1) return 13;
    Present(&frame_storage[0], true, false);
    if (s.completed || std::filesystem::exists(output / "frame_000.json")) return 14;
    if (mode == "gap") Present(&frame_storage[0], true, true); // repeated display is not hidden
    // Simulate all fence callbacks for frame 1 arriving before frame 0.
    for (auto& snapshot : s.epochs[1]->snapshots) snapshot.saved = true;
    s.Publish(s.epochs[1]);
    if (s.completed || std::filesystem::exists(output / "frame_001.json")) return 15;
    Present(&frame_storage[1], true, false);
    if (mode == "gap") return s.error == "nonconsecutive_presents" ? 0 : 16;
    if (s.completed) return 17;
    for (auto& snapshot : s.epochs[0]->snapshots) snapshot.saved = true;
    s.Publish(s.epochs[0]);
    return s.completed && !s.active && s.error.empty() ? 0 : 18;
}
'''
        cpp = cls.directory / "state.cpp"
        cpp.write_text(source, encoding="utf-8")
        cls.executable = cls.directory / "state.exe"
        cls.environment = {k: v for k, v in os.environ.items() if not k.startswith(("TLG_", "ISS_"))}
        cls.environment["PATH"] = str(TOOLCHAIN / "bin") + os.pathsep + cls.environment.get("PATH", "")
        command = [str(COMPILER), "-std=c++23", "-DFMT_HEADER_ONLY", "-I" + str(TOOLCHAIN / "include"),
                   str(cpp), "-o", str(cls.executable)]
        result = subprocess.run(command, env=cls.environment, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise RuntimeError(result.stderr)

    def run_case(self, mode):
        directory = self.directory / mode
        directory.mkdir()
        result = subprocess.run([str(self.executable), str(directory), mode], env=self.environment,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return directory / "capture"

    def test_callbacks_after_present_and_present_after_callbacks(self):
        capture = self.run_case("complete")
        spec = importlib.util.spec_from_file_location("gpu_phy03_native_analyzer", ROOT / "scripts/gpu_phy03_analyze.py")
        analyzer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(analyzer)
        self.assertEqual(analyzer.validate(capture)["frames"], 2)

    def test_observation_prepare_uses_actual_frame_without_claiming_pixel_lineage(self):
        for mode in ("observe-complete", "observe-missing", "observe-ambiguous"):
            with self.subTest(mode=mode):
                capture = self.run_case(mode)
                session = json.loads((capture / "session.json").read_text())
                self.assertEqual(session["status"], "Completed")
                self.assertEqual(session["schema"], 3)
                self.assertEqual(session["started"], 16)
                for index in range(16):
                    frame = json.loads((capture / f"frame_{index:03}.json").read_text())
                    self.assertEqual(frame["present_index"], index+1)
                    self.assertEqual(frame["association"], "prepare_interval_only")
                    self.assertEqual(frame["content_path"], "not_proven")
                    self.assertEqual(frame["gbuffer_status"], "missing" if mode.endswith("missing") else
                                     "ambiguous" if mode.endswith("ambiguous") else "observed")
                    self.assertEqual(frame["snapshots"][-1]["identity"]["uid"], 20+index)

    def test_observation_failure_cannot_publish_from_late_callbacks(self):
        capture = self.run_case("observe-failed-present")
        self.assertEqual(json.loads((capture / "session.json").read_text())["status"], "Failed")

    def test_draw_trace_bounded_and_published_with_actual_frame(self):
        spec = importlib.util.spec_from_file_location("trace_analyzer", ROOT / "scripts/gpu_phy03_analyze.py")
        analyzer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(analyzer)
        for mode in ("observe-trace", "observe-trace-overflow"):
            with self.subTest(mode=mode):
                capture = self.run_case(mode)
                self.assertEqual(json.loads((capture / "session.json").read_text())["status"], "Completed")
                for index in range(16):
                    frame = json.loads((capture / f"frame_{index:03}.json").read_text())
                    trace = json.loads((capture / frame["draw_trace"]).read_text())
                    self.assertEqual(trace["frame_index"], index)
                    self.assertEqual(trace["present_index"], index+1)
                    self.assertEqual(trace["buffer_contents"], "not_collected")
                    self.assertEqual(len(trace["records"]), 512 if mode.endswith("overflow") else 4)
                    self.assertEqual(trace["dropped"], 4 if mode.endswith("overflow") else 0)
                    self.assertEqual(trace["records"][1]["buffers"][0]["buffer"], 512)
                    self.assertEqual(trace["records"][1]["buffers"][0]["push_adjust"], 12)
                    self.assertEqual(trace["records"][1]["count"], 12+index)
                    result = analyzer.validate_draw_trace(capture, frame)
                    self.assertEqual(result["integrity"], "passed")
                    self.assertEqual(result["direct_emissions"], 1)
                    self.assertEqual(result["indirect_instance_counts"], "unknown")
                frame = json.loads((capture / "frame_000.json").read_text())
                path = capture / frame["draw_trace"]
                trace = json.loads(path.read_text()); trace["present_index"] += 1
                path.write_text(json.dumps(trace))
                with self.assertRaisesRegex(analyzer.CaptureError, "identity_mismatch"):
                    analyzer.validate_draw_trace(capture, frame)

    def test_front_census_aggregates_bounds_and_fails_independently(self):
        spec = importlib.util.spec_from_file_location("front_analyzer", ROOT / "scripts/gpu_phy03_analyze.py")
        analyzer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(analyzer)
        for mode in ("observe-trace-front", "observe-trace-front-overflow"):
            with self.subTest(mode=mode):
                capture = self.run_case(mode)
                for index in range(16):
                    frame = json.loads((capture / f"frame_{index:03}.json").read_text())
                    trace = json.loads((capture / frame["draw_trace"]).read_text())
                    front = trace["front_census"]
                    overflow = mode.endswith("overflow")
                    self.assertEqual(front["dropped"], int(overflow))
                    self.assertEqual(len(front["rows"]), 256 if overflow else 1)
                    row = front["rows"][0]
                    self.assertEqual(row["calls"], 3 if overflow else 2)
                    self.assertEqual(row["instance_sum"], 6 if overflow else 4)
                    self.assertEqual(row["index_sum"], 24+index if overflow else 20+index)
                    self.assertEqual(row["raw_zero_instances"], 1)
                    self.assertEqual(analyzer.validate_front_census(front)["integrity"], "passed")
                path = capture / frame["draw_trace"]
                trace["front_census"]["rows"][0]["outcome"] = 0
                path.write_text(json.dumps(trace))
                result = analyzer.validate_draw_trace(capture, frame)
                self.assertEqual(result["integrity"], "passed")
                self.assertEqual(result["front_census"]["integrity"], "failed")
                del trace["front_census"]
                path.write_text(json.dumps(trace))
                self.assertEqual(analyzer.validate_draw_trace(capture, frame)["front_census"]["integrity"],
                                 "not_collected")

    def test_pm4_trace_bounded_published_and_validated_independently(self):
        spec = importlib.util.spec_from_file_location("pm4_analyzer", ROOT / "scripts/gpu_phy03_analyze.py")
        analyzer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(analyzer)
        for mode in ("observe-trace-pm4", "observe-trace-pm4-overflow"):
            with self.subTest(mode=mode):
                capture = self.run_case(mode)
                for index in range(16):
                    frame = json.loads((capture / f"frame_{index:03}.json").read_text())
                    result = analyzer.validate_pm4_trace(capture, frame)
                    overflow = mode.endswith("overflow")
                    self.assertEqual(result["integrity"], "passed")
                    self.assertEqual(result["dropped"], int(overflow))
                    self.assertEqual(result["kind_counts"]["1"], 4096 if overflow else 1)
                    self.assertEqual(result["opcode_counts"][0x10], 1)
                    self.assertEqual(result["sampled_register_mismatches"], [])
                trace_path = capture / frame["pm4_trace"]
                trace = json.loads(trace_path.read_text())
                trace["present_index"] += 1
                trace_path.write_text(json.dumps(trace))
                with self.assertRaisesRegex(analyzer.CaptureError, "identity_mismatch"):
                    analyzer.validate_pm4_trace(capture, frame)
                # A PM4 failure never changes validation of the existing draw trace.
                self.assertEqual(analyzer.validate_draw_trace(capture, frame)["integrity"], "passed")

    def test_submission_samples_bounded_published_and_independent_of_pm4(self):
        spec = importlib.util.spec_from_file_location("submission_analyzer", ROOT / "scripts/gpu_phy03_analyze.py")
        analyzer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(analyzer)
        for mode in ("observe-trace-pm4-submit", "observe-trace-pm4-submit-overflow"):
            with self.subTest(mode=mode):
                capture = self.run_case(mode)
                for index in range(16):
                    frame = json.loads((capture / f"frame_{index:03}.json").read_text())
                    result = analyzer.validate_pm4_trace(capture, frame)
                    data = result["submission_samples"]
                    self.assertEqual(result["integrity"], "passed")
                    self.assertEqual(data["integrity"], "passed")
                    self.assertEqual(data["dropped"], 2 if mode.endswith("overflow") else 0)
                    self.assertEqual(len(data["records"]), 128 if mode.endswith("overflow") else 2)
                    self.assertEqual(data["records"][0]["submission"], 100+index)
                path = capture / frame["pm4_trace"]
                trace = json.loads(path.read_text())
                trace["submission_samples"]["records"][0]["samples"][0]["status"] = 9
                path.write_text(json.dumps(trace))
                result = analyzer.validate_pm4_trace(capture, frame)
                self.assertEqual(result["integrity"], "passed")
                self.assertEqual(result["submission_samples"]["integrity"], "failed")

    def test_producer_publication_bounds_and_independent_validation(self):
        spec = importlib.util.spec_from_file_location("producer_analyzer", ROOT / "scripts/gpu_phy03_analyze.py")
        analyzer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(analyzer)
        for mode in ("observe-trace-pm4-producer", "observe-trace-pm4-producer-overflow"):
            capture = self.run_case(mode)
            previous_serial = 0
            traces = []
            for index in range(16):
                frame = json.loads((capture / f"frame_{index:03}.json").read_text())
                result = analyzer.validate_pm4_trace(capture, frame)
                data = result["producer_samples"]
                self.assertEqual(result["integrity"], "passed")
                self.assertEqual(data["integrity"], "passed", data)
                self.assertEqual(data["dropped"], int(mode.endswith("overflow")))
                self.assertEqual(len(data["records"]), 4096 if mode.endswith("overflow") else 1)
                self.assertGreater(data["records"][0]["serial"], previous_serial)
                previous_serial = data["records"][-1]["serial"]
                self.assertEqual(len(result["producer_consumers"]), 1)
                traces.append({**result, "observation_index": index, "present_index": frame["present_index"]})
            comparison = analyzer.compare_producer_samples(traces)
            self.assertEqual(comparison["candidate_matches"], 16)
            self.assertEqual(comparison["coverage_limited"], mode.endswith("overflow"))
            path = capture / frame["pm4_trace"]
            trace = json.loads(path.read_text())
            trace["producer_samples"]["records"][0]["frames"][0]["frame"] = 7
            path.write_text(json.dumps(trace))
            result = analyzer.validate_pm4_trace(capture, frame)
            self.assertEqual(result["integrity"], "passed")
            self.assertEqual(result["producer_samples"]["integrity"], "failed")

    def test_list_publication_and_failure_are_independent_of_producer_packets(self):
        spec = importlib.util.spec_from_file_location("list_analyzer", ROOT / "scripts/gpu_phy03_analyze.py")
        analyzer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(analyzer)
        capture = self.run_case("observe-trace-pm4-producer-list")
        for index in range(16):
            frame = json.loads((capture / f"frame_{index:03}.json").read_text())
            result = analyzer.validate_pm4_trace(capture, frame)
            lists = result["producer_samples"]["list_samples"]
            self.assertEqual(lists["integrity"], "passed", lists)
            self.assertEqual(lists["sampled_headers"], 1)
            self.assertEqual(lists["min_count"], 12)
            self.assertEqual(lists["scan_statuses"], {"1": 1})
        path = capture / frame["pm4_trace"]
        original = json.loads(path.read_text())
        for mutation in ("sum", "chain"):
            trace = json.loads(json.dumps(original))
            writer = trace["producer_samples"]["records"][0]
            if mutation == "sum": writer["list"]["null_records"] += 1
            else: writer["list"]["owner_frame"] += 8
            path.write_text(json.dumps(trace))
            result = analyzer.validate_pm4_trace(capture, frame)
            self.assertEqual(result["integrity"], "passed")
            self.assertEqual(result["producer_samples"]["integrity"], "passed")
            self.assertEqual(result["producer_samples"]["list_samples"]["integrity"], "failed")

    def test_observations_pair_by_present_and_fail_independently_of_final_capture(self):
        spec = importlib.util.spec_from_file_location("observation_analyzer", ROOT / "scripts/gpu_phy03_analyze.py")
        analyzer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(analyzer)
        for kind in ("valid", "missing", "ambiguous", "wrong-session", "bad-hash", "duplicate-present", "bad-path", "bad-trace", "bad-pm4"):
            with self.subTest(kind=kind):
                base = self.directory / ("pair-" + kind); base.mkdir()
                mode = "observe-trace-pm4" if kind == "bad-pm4" else "observe-trace" if kind == "bad-trace" else "observe-" + kind if kind in ("missing", "ambiguous") else "observe-complete"
                process = subprocess.run([str(self.executable), str(base), mode], env=self.environment,
                                         capture_output=True, text=True, timeout=10)
                self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
                observations = base / "capture"
                final = base / "final"; final.mkdir()
                session = {"schema": 1, "contract": "final_game_frames_v1", "requested": 16, "staged": 16,
                           "saved": 16, "presented": 16, "published": 16, "status": "Completed", "reason": "",
                           "boundary": "post_processed_game_image_before_window_blit",
                           "capture_id": "1234567890abcdef1234567890abcdef"}
                for index in range(16):
                    data = bytes(range(16)); filename = f"output_{index:03}.raw"
                    (final / filename).write_bytes(data)
                    frame = {"schema": 1, "index": index, "file": filename, "width": 2, "height": 2,
                             "format": "R8G8B8A8Unorm", "bytes": 16, "fnv1a64": analyzer.fnv1a64(data),
                             "present_index": index+3, "reused": False, "tick": index+7}
                    (final / f"frame_{index:03}.json").write_text(json.dumps(frame))
                if kind == "wrong-session": session["capture_id"] = "a" * 32
                (final / "session.json").write_text(json.dumps(session))
                path = observations / "frame_001.json"; frame = json.loads(path.read_text())
                if kind == "bad-hash": frame["snapshots"][0]["content_hash"] = "0" * 16
                if kind == "duplicate-present": frame["present_index"] -= 1
                if kind == "bad-path": frame["snapshots"][0]["file"] = "../file.raw"
                if kind == "bad-trace":
                    trace_path = observations / "draw_trace_000.json"
                    trace = json.loads(trace_path.read_text()); trace["present_index"] += 1
                    trace_path.write_text(json.dumps(trace))
                if kind == "bad-pm4":
                    trace_path = observations / "pm4_trace_000.json"
                    trace = json.loads(trace_path.read_text()); trace["present_index"] += 1
                    trace_path.write_text(json.dumps(trace))
                path.write_text(json.dumps(frame))
                final_result = analyzer.validate(final)
                if kind in ("valid", "missing", "ambiguous"):
                    previews = base / "previews"
                    result = analyzer.validate_stage_observations(observations, final, final_result, previews)
                    self.assertEqual(len(result["pairs"]), 14)
                    self.assertEqual(result["pairs"][0]["final_index"], 0)
                    self.assertEqual(result["pairs"][0]["observation_index"], 2)
                    self.assertEqual(result["unmatched_final_frames"], [14, 15])
                    self.assertEqual(result["pairs"][0]["gbuffer_status"], kind if kind != "valid" else "observed")
                    self.assertEqual(result["pairs"][0]["content_path"], "not_proven")
                    self.assertEqual(len(list(previews.glob("*.png"))), 16 if kind == "missing" else 32)
                else:
                    result = subprocess.run([str(ROOT / ".venv/Scripts/python.exe"), "-B",
                                             str(ROOT / "scripts/gpu_phy03_analyze.py"), str(final),
                                             "--stage-observations", str(observations)],
                                            capture_output=True, text=True, timeout=10)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    response = json.loads(result.stdout)
                    self.assertEqual(response["integrity"], "passed")
                    self.assertEqual(response["frames"], 16)
                    if kind == "bad-trace":
                        self.assertEqual(response["stage_observations"]["integrity"], "passed")
                        self.assertEqual(response["stage_observations"]["draw_traces"][0]["integrity"], "failed")
                    elif kind == "bad-pm4":
                        self.assertEqual(response["stage_observations"]["integrity"], "passed")
                        self.assertEqual(response["stage_observations"]["pm4_traces"][0]["integrity"], "failed")
                        self.assertEqual(response["stage_observations"]["draw_traces"][0]["integrity"], "passed")
                    else:
                        self.assertEqual(response["stage_observations"]["integrity"], "failed")

    def test_sixteen_complete_frames_validate_with_consecutive_presents(self):
        capture = self.run_case("complete-16")
        spec = importlib.util.spec_from_file_location("phy03_sixteen", ROOT / "scripts/gpu_phy03_analyze.py")
        analyzer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(analyzer)
        result = analyzer.validate(capture)
        self.assertEqual(result["frames"], 16)
        self.assertEqual(len(list(capture.glob("*.raw"))), 48)
        self.assertEqual(len(list(capture.glob("frame_*.json"))), 16)
        self.assertFalse(result["visibility_automatic"])

    def test_skipped_frame_is_failure(self):
        capture = self.run_case("failed")
        self.assertEqual(json.loads((capture / "session.json").read_text())["reason"], "prepared_frame_not_presented")

    def test_present_gap_is_failure(self):
        capture = self.run_case("gap")
        self.assertEqual(json.loads((capture / "session.json").read_text())["reason"], "nonconsecutive_presents")

    def test_shader_access_uses_write_and_atomic_metadata(self):
        self.run_case("access")

    def test_sixteen_frames_with_bounded_delayed_callbacks(self):
        self.run_case("budget-streaming")

    def test_unbounded_delayed_callbacks_fail_and_drain_safely(self):
        capture = self.run_case("budget-delayed")
        self.assertEqual(json.loads((capture / "session.json").read_text())["status"], "Failed")

    def test_exact_budget_and_overflow_safe_rejection(self):
        self.run_case("budget-boundary")

    def test_association_metadata_cannot_grow_without_bound(self):
        self.run_case("event-budget")

    def test_current_exact_lineage(self):
        self.run_case("lineage-valid")

    def test_observed_load_and_clear_keep_temporal_association(self):
        for kind in ("load", "clear", "storage"):
            with self.subTest(kind=kind):
                self.run_case("temporal-" + kind)

    def test_unobserved_generation_is_not_accepted_as_own_write(self):
        self.run_case("temporal-no-proof")

    def test_preparation_rejects_backing_alias_and_external_generation(self):
        for kind in ("backing", "alias", "unobserved", "unsafe"):
            with self.subTest(kind=kind):
                self.run_case("temporal-" + kind)

    def test_actual_prepare_binds_only_the_observed_display_producer(self):
        self.run_case("prepare-valid")

    def test_actual_prepare_rejects_wrong_epoch_backing_or_missing_composition_path(self):
        for kind in ("backing", "wrong-frame", "no-composition"):
            with self.subTest(kind=kind):
                self.run_case("prepare-" + kind)

    def test_frame_token_cannot_be_reassigned_while_prepared(self):
        self.run_case("prepare-token-reuse")

    def test_failed_present_preserves_skip_or_minimization_reason(self):
        for reason in ("presentation_suppressed_minimized", "prepared_frame_skipped_by_swapchain",
                       "prepared_frame_presentation_failed"):
            with self.subTest(reason=reason):
                self.run_case("presentation-" + reason)

    def test_new_write_does_not_inherit_load_contents(self):
        self.run_case("lineage-new-write")

    def test_writable_descriptor_does_not_prove_a_read(self):
        self.run_case("lineage-readwrite")

    def test_ambiguous_write_reports_versions_without_accepting_late_completion(self):
        for kind in ("no-source", "rewrite", "alias", "partial", "epoch"):
            with self.subTest(kind=kind):
                directory = self.directory / ("diagnostic-" + kind)
                directory.mkdir()
                result = subprocess.run([str(self.executable), str(directory), "diagnostic-" + kind],
                                        env=self.environment, capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("GPU-PHY-03: Ambiguous write", result.stdout)
                self.assertIn("phase=synthetic_color shader=00abcdef color_slot=3 attachment_op=LOAD", result.stdout)
                self.assertIn("tracked: uid=9", result.stdout)
                self.assertIn("incoming: uid=", result.stdout)
                self.assertIn("current_epoch=1", result.stdout)
                self.assertIn("Failed reason=ambiguous_lineage_write", result.stdout)
                self.assertNotIn("Completed frames=", result.stdout)

    def test_mixed_epoch_inputs_do_not_select_the_newest(self):
        self.run_case("lineage-mixed")

    def test_rejected_read_identifies_each_guard_without_relaxing_policy(self):
        for kind in ("generation", "partial", "missing", "combined", "valid"):
            with self.subTest(kind=kind):
                directory = self.directory / ("read-diagnostic-" + kind)
                directory.mkdir()
                result = subprocess.run([str(self.executable), str(directory), "read-diagnostic-" + kind],
                                        env=self.environment, capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                prefix = "GPU-PHY-03: Read rejected "
                lines = [line[len(prefix):] for line in result.stdout.splitlines() if line.startswith(prefix)]
                if kind == "valid":
                    self.assertEqual(lines, [])
                    self.assertNotIn("Failed reason=", result.stdout)
                    continue
                self.assertEqual(len(lines), 1)
                evidence = json.loads(lines[0])
                self.assertEqual(evidence["phase"], "synthetic_read")
                self.assertEqual(evidence["full_image"], int(kind not in ("partial", "combined")))
                self.assertEqual(evidence["version_accepted"], int(kind not in ("generation", "combined")))
                self.assertEqual(evidence["epoch_missing"], int(kind in ("missing", "combined")))
                self.assertEqual(evidence["tracked_epoch"], -1 if kind in ("missing", "combined") else 1)
                self.assertEqual(evidence["current_epoch"], 1)
                self.assertEqual(evidence["same_storage"], 1)
                self.assertEqual(evidence["tracked"]["alias"], 7)
                self.assertEqual(evidence["incoming"]["alias"], 9 if kind in ("generation", "combined") else 7)
                self.assertEqual(evidence["root"], evidence["tracked"])
                self.assertEqual(evidence["resource"], {"shader": "836b20e6", "base_mip": 0, "mip_count": 1,
                                                       "base_layer": 0, "layer_count": 1,
                                                       "image_levels": 2, "image_layers": 1})
                self.assertEqual(len(evidence["preparations"]), int(kind in ("generation", "combined")))
                if evidence["preparations"]:
                    self.assertEqual(evidence["preparations"][0]["after"]["alias"], 8)
                    self.assertEqual(evidence["preparations"][0]["safe"], 1)
                session = json.loads((directory / "capture/session.json").read_text())
                self.assertEqual(session["status"], "Failed")
                self.assertEqual(session["reason"], "stale_or_partial_lineage_binding")
                self.assertNotIn("Completed frames=", result.stdout)

    def test_stale_alias_epoch_backing_format_and_subresource_fail(self):
        for kind in ("old-epoch", "alias", "backing", "generation", "format", "size", "partial", "rewrite-generation"):
            with self.subTest(kind=kind):
                self.run_case("lineage-" + kind)

    def test_stable_bindings_matching_succeeds(self):
        self.run_case("stable-bindings-ok")

    def test_independent_generation_does_not_mutate_binding_or_lineage(self):
        self.run_case("stable-independent")

    def test_root_generation_is_protected_before_any_lineage_entry(self):
        for kind in ("root", "root-partial", "root-overlap", "root-uid", "root-handle"):
            with self.subTest(kind=kind):
                self.run_case("stable-policy-" + kind)

    def test_derived_and_invalidated_lineage_generation_still_fail(self):
        for kind in ("lineage", "lineage-dead", "lineage-overlap"):
            with self.subTest(kind=kind):
                self.run_case("stable-policy-" + kind)

    def test_unknown_or_overflowed_ranges_cannot_prove_independence(self):
        for kind in ("no-root", "unknown-root", "unknown-binding", "overflow-binding"):
            with self.subTest(kind=kind):
                self.run_case("stable-policy-" + kind)

    def test_structural_changes_fail_even_for_an_unrelated_resource(self):
        for kind in ("uid", "handle", "address", "bytes", "extent", "format"):
            with self.subTest(kind=kind):
                self.run_case("stable-policy-" + kind)

    def test_writable_generation_changes_remain_ambiguous(self):
        for kind in ("atomic", "may-write"):
            with self.subTest(kind=kind):
                self.run_case("stable-policy-" + kind)

    def test_independent_mip_chain_is_not_added_to_lineage(self):
        self.run_case("stable-policy-independent-partial")

    def test_stable_bindings_divergence_logs_fields_and_fails(self):
        directory = self.directory / "stable_divergence"
        directory.mkdir()
        result = subprocess.run([str(self.executable), str(directory), "stable-bindings-divergence"],
                                env=self.environment, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("GPU-PHY-03: Binding changed image_id=1 changed=[alias_generation]", result.stdout)
        self.assertIn("before: uid=100", result.stdout)
        self.assertIn("alias=5", result.stdout)
        self.assertIn("address=0x8000", result.stdout)
        self.assertIn("bytes=8294400", result.stdout)
        self.assertIn("extent=1920x1080", result.stdout)
        self.assertIn("format=R8G8B8A8Unorm(37)", result.stdout)
        self.assertIn("after:  uid=100", result.stdout)
        self.assertIn("alias=6", result.stdout)
        self.assertIn("GPU-PHY-03: Failed reason=binding_changed_during_resolution", result.stdout)


if __name__ == "__main__":
    unittest.main()
