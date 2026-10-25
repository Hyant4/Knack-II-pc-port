// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "knack_temporal.h"
#ifndef _WIN32
#include <array>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <dlfcn.h>
#endif
#include <cerrno>
#include <cmath>
#include <cstring>
#include <unordered_set>
#include <fstream>
#include <map>
#include <mutex>
#include <xxhash.h>
#include "video_core/renderer_vulkan/ui_composition.h"
#include "tlg_toggles.h"
#include "tlg_settings.h"
#include "video_core/renderer_vulkan/vk_frame_capture.h"
#include "video_core/renderer_vulkan/vk_lighting_test.h"
#include "video_core/renderer_vulkan/vk_pass_census.h"
#include "video_core/renderer_vulkan/vk_frame_record.h"
#include "common/backports.h"
#include "video_core/cache_storage.h"
#include "common/debug.h"
#include "common/hash.h"
#include "core/debug_state.h"
#include "core/emulator_settings.h"
#include "core/memory.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/amdgpu/liverpool.h"
#include "video_core/amdgpu/pm4_cmds.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/renderer_vulkan/liverpool_to_vk.h"
#include "video_core/renderer_vulkan/vk_depth_stencil_state.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "video_core/renderer_vulkan/vk_gpu_profiler.h"
#include "tlg_threads.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_hle.h"
#include "video_core/texture_cache/image_view.h"
#include "video_core/texture_cache/image.h"
#include "video_core/texture_cache/texture_cache.h"
#include <vk_mem_alloc.h>

namespace Vulkan {

// A movie replaces this attachment's background. Later subtitles, logos, fades
// and the game's own letterbox draws keep their original order and projection.
static void ClearKnackVideoBackground(vk::CommandBuffer command, const RenderState& state) {
    const vk::ClearAttachment black{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .colorAttachment = 0,
        .clearValue = vk::ClearValue{vk::ClearColorValue{std::array<float, 4>{0, 0, 0, 1}}},
    };
    const vk::ClearRect rectangle{
        .rect = {{0, 0}, {u32(state.width), u32(state.height)}},
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    command.clearAttachments(black, rectangle);
}

// Capture state and bounded diagnostic metadata remain local to this translation unit.
namespace GpuPhy03 {
namespace {
struct Snapshot {
    std::string stage, file, format, hash;
    u64 uid{}, address{}, alias{}, tick{}, bytes{};
    u32 width{}, height{}, bpp{};
    bool saved{};
    std::string identity;
    u64 event{}, submit{};
};
// is_written is merged by the shader recompiler across ImageWrite and atomic users.
// No read bit exists for a merged writable descriptor: treat it as potentially read/write.
enum class Access { ReadOnly, MayWrite, Atomic };
Access ShaderAccess(bool is_written, bool is_atomic) {
    return is_atomic ? Access::Atomic : is_written ? Access::MayWrite : Access::ReadOnly;
}
struct Identity {
    u64 uid{};
    vk::Image image{};
    u64 alias{}, address{}, bytes{};
    u32 width{}, height{}, format{};
    bool operator==(const Identity&) const = default;
    auto Key() const { return std::pair{uid, image}; }
    bool SameStorage(const Identity& other) const {
        auto copy = other; copy.alias = alias;
        return *this == copy;
    }
    std::string Json() const {
        return fmt::format("{{\"uid\":{},\"image\":\"{}\",\"alias\":{},\"address\":{},"
            "\"bytes\":{},\"width\":{},\"height\":{},\"format\":{}}}",
            uid, (void*)VkImage(image), alias, address, bytes, width, height, format);
    }
    bool KnownRange() const {
        return uid && image && address && bytes && bytes <= ~0ull - address &&
               width && height && format;
    }
    bool Overlaps(const Identity& other) const {
        if (image == other.image) return true;
        // Subtraction avoids overflow when describing an untrusted guest range.
        return address <= other.address ? other.address - address < bytes
                                        : address - other.address < other.bytes;
    }
};
// Bounded metadata only: no guest dereference, readback, or per-draw heap allocation.
struct DrawTraceRecord {
    u32 kind{}, count{}, instances{}, first_instance{}, vertex_offset{}, index_offset{}, mrt_mask{};
    bool indexed{};
    u64 tick{}, pipeline{}, indirect_args{}, indirect_count{}, indirect_offset{}, max_count{}, stride{};
    u32 depth{}, stencil{}, polygon{}, clipper{}, color_mask{};
    std::array<u32, 6> viewport{};
    std::array<u64, 2> scissor{};
    struct Buffer { bool recorded{}; u64 handle{}, offset{}, range{}, push_adjust{}; };
    std::array<Buffer, 2> buffers{};
};
// Aggregate every direct Draw entry, including calls rejected before pipeline selection.
// Program addresses are register values only, not shader identities or guest reads.
struct FrontTraceKey {
    std::array<u64, 6> programs{}; // PS, VS, GS, ES, HS, LS, including inactive slots
    u64 vs_hash{}, ps_hash{}; // known only after successful pipeline selection
    u32 outcome{}, stages{}, primitive{}, color_control{}, color_mask{};
    bool indexed{}, pipelined{};
    bool operator==(const FrontTraceKey&) const = default;
};
struct FrontTraceRow {
    FrontTraceKey key;
    u64 calls{}, instances{}, indices{}, raw_zero_instances{};
};
struct Pm4TraceRecord {
    u32 kind{}, opcode{}, packet_words{}, raw_words{}, skip_words{};
    u32 before_indices{}, before_instances{}, after_indices{}, after_instances{};
    u32 stages{}, primitive{}, color_control{}, color_mask{};
    u64 command{}, buffer_base{}, buffer_words{}, remaining_words{}, submission{};
    u64 producer_serial{}; // shared collector order; not a GPU fence or buffer generation
    bool skipped{};
    std::array<u32, 8> raw{};
    std::array<u64, 6> programs{};
};
struct Pm4TraceWindow {
    static constexpr u32 Capacity = 4096;
    std::array<Pm4TraceRecord, Capacity> records{};
    std::array<u64, 256> opcode_counts{};
    u32 used{}, dropped{};
    static constexpr u32 SubmissionCapacity = 128;
    std::array<RelicSubmissionSample, SubmissionCapacity> submissions{};
    u32 submission_used{}, submission_dropped{};
    static constexpr u32 ProducerCapacity = 4096;
    std::array<RelicProducerSample, ProducerCapacity> producers{};
    u32 producer_used{}, producer_dropped{};
    static constexpr u32 ListCountLimit = 4096, ListScanCapacity = 32;
    static constexpr u64 ListByteBudget = 1024 * 1024;
    std::array<std::array<u64, 3>, ListScanCapacity> list_keys{};
    u32 list_key_used{}; u64 list_scan_bytes{};
    u32 ReserveListScan(const RelicListSample& sample) {
        if (sample.count > ListCountLimit) return 4;
        const std::array<u64, 3> key{sample.list, sample.entries, sample.count};
        for (u32 i = 0; i < list_key_used; ++i) if (list_keys[i] == key) return 2;
        const u64 bytes = u64(sample.count) * (0x58 + sizeof(u64));
        if (list_key_used == ListScanCapacity || bytes > ListByteBudget - list_scan_bytes) return 3;
        list_keys[list_key_used++] = key; list_scan_bytes += bytes;
        return 1;
    }
    void AddProducer(const RelicProducerSample& sample) {
        if (producer_used < ProducerCapacity) producers[producer_used++] = sample;
        else if (producer_dropped != ~0u) ++producer_dropped;
    }
    void AddSubmission(const RelicSubmissionSample& sample) {
        if (submission_used < SubmissionCapacity) submissions[submission_used++] = sample;
        else if (submission_dropped != ~0u) ++submission_dropped;
    }
    void Add(const Pm4TraceRecord& record, bool retain) {
        if (record.kind == 1 && record.opcode < opcode_counts.size()) ++opcode_counts[record.opcode];
        if (!retain) return;
        if (used < Capacity) records[used++] = record;
        else if (dropped != ~0u) ++dropped;
    }
};
struct DrawTraceWindow {
    explicit DrawTraceWindow(bool pm4_enabled = false) {
        if (pm4_enabled) pm4 = std::make_unique<Pm4TraceWindow>();
    }
    std::unique_ptr<Pm4TraceWindow> pm4;
    static constexpr u32 Capacity = 512;
    std::array<DrawTraceRecord, Capacity> records{};
    u32 used{}, dropped{};
    static constexpr u32 FrontCapacity = 256;
    std::array<FrontTraceRow, FrontCapacity> front{};
    u32 front_used{}, front_dropped{};
    void AddFront(const FrontTraceKey& key, u32 count, u32 instances, bool raw_zero) {
        u32 i = 0;
        for (; i < front_used; ++i) if (front[i].key == key) break;
        if (i == front_used) {
            if (front_used == FrontCapacity) {
                if (front_dropped != ~0u) ++front_dropped;
                return;
            }
            front[front_used++].key = key;
        }
        auto& row = front[i];
        ++row.calls; row.instances += instances; row.indices += count;
        row.raw_zero_instances += raw_zero;
    }
    void Add(const DrawTraceRecord& r) {
        if (used < Capacity) records[used++] = r;
        else if (dropped != ~0u) ++dropped;
    }
};
struct Epoch {
    u32 index{};
    u64 present_index{}, display_uid{}, display_address{}, submit_sequence{};
    bool gbuffer{}, composition{}, prepared{}, presented{};
    u64 gbuffer_uid{};
    vk::Image gbuffer_image{};
    std::string attachments;
    std::vector<Snapshot> snapshots;
    std::vector<std::string> events;
    bool observation_ambiguous{};
    std::shared_ptr<DrawTraceWindow> draw_trace;
};
struct WritePreparation {
    Identity before, after;
    bool safe{};
};
struct Resource {
    VideoCore::ImageId id;
    Access access;
    Identity binding;
    bool full_image;
    u64 shader{};
    u32 base_mip{}, mip_count{}, base_layer{}, layer_count{};
    u32 image_levels{}, image_layers{};
};
struct Provenance {
    Identity version;
    std::shared_ptr<Epoch> epoch;
    bool from_composition{};
};
struct State {
    std::mutex mutex;
    std::atomic<bool> active{false};
    const bool observe = [] { const char* v = std::getenv("TLG_GPU_PHY03_OBSERVE"); return v && std::string_view(v) == "1"; }();
    const bool trace_enabled = observe && [] { const char* v = std::getenv("TLG_RELIC_TRACE"); return v && std::string_view(v) == "1"; }();
    const bool pm4_enabled = trace_enabled && [] { const char* v = std::getenv("TLG_RELIC_PM4_TRACE"); return v && std::string_view(v) == "1"; }();
    const bool submission_enabled = pm4_enabled && [] { const char* v = std::getenv("TLG_RELIC_SUBMIT_TRACE"); return v && std::string_view(v) == "1"; }();
    const bool producer_enabled = pm4_enabled && [] { const char* v = std::getenv("TLG_RELIC_PRODUCER_TRACE"); return v && std::string_view(v) == "1"; }();
    const bool list_enabled = producer_enabled && [] { const char* v = std::getenv("TLG_RELIC_LIST_TRACE"); return v && std::string_view(v) == "1"; }();
    std::shared_ptr<DrawTraceWindow> trace_window;
    const char* trigger = std::getenv("TLG_GPU_PHY03_TRIGGER");
    std::filesystem::path dir;
    std::string error;
    u32 limit = 4;
    u64 present_sequence{}, submit_sequence{}, outstanding{};
    u64 event_sequence{};
    u64 producer_sequence{}; // under mutex, spans all Prepare intervals in this capture
    static constexpr u64 AssociationBudget = 8ull * 1024 * 1024;
    u64 event_bytes{};
    bool requested{}, completed{};
    std::vector<std::shared_ptr<Epoch>> epochs;
    std::shared_ptr<Epoch> current;
    // Image identity, not a submission counter, carries the scene through composition.
    std::map<std::pair<u64, vk::Image>, Provenance> lineage;
    // Protect RT2 from its first producer, before it has a downstream lineage entry.
    std::optional<Identity> root;
    std::map<const void*, std::shared_ptr<Epoch>> prepared;
    std::vector<Resource> resources; // draw-recording thread only, under mutex
    std::vector<Identity> draw_targets;
    std::vector<WritePreparation> write_preparations;
    std::map<VideoCore::ImageId, std::pair<Identity, bool>> pending_writes;
    std::vector<Identity> input_versions;
    bool input_from_composition{};

    State() {
        if (trigger && *trigger) {
            std::printf("GPU-PHY-03: Armed trigger=%s\n", trigger);
        }
    }
    void Fail(const char* reason) {
        if (error.empty()) {
            error = reason;
            active.store(false, std::memory_order_release);
            std::printf("GPU-PHY-03: Failed reason=%s\n", reason);
            Session();
        }
    }
    void Session() {
        if (dir.empty()) return;
        std::ofstream f(dir / "session.json", std::ios::trunc);
        const char* capture_id = std::getenv("TLG_FRAME_SEQUENCE_ID");
        f << fmt::format("{{\"schema\":{},\"contract\":\"{}\",\"requested\":{},\"started\":{},"
                         "\"status\":\"{}\",\"reason\":\"{}\",\"capture_id\":\"{}\"}}\n",
                         observe ? 3 : 2, observe ? "stage_observations_v1" : "ordered_resource_versions_v2",
                         limit, epochs.size(), !error.empty() ? "Failed" : completed ? "Completed" : "Capturing", error,
                         capture_id ? capture_id : "");
        if (!f && error.empty()) {
            error = "session_write_failed";
            active.store(false, std::memory_order_release);
            std::printf("GPU-PHY-03: Failed reason=session_write_failed\n");
        }
    }
    void Poll() {
        if (requested || !trigger || !*trigger) return;
        std::error_code ec;
        if (!std::filesystem::exists(trigger, ec)) return;
        requested = true; // one request per process; rearming cannot overwrite an earlier run
        std::printf("GPU-PHY-03: Trigger detected\n");
        const char* output = std::getenv("TLG_GPU_PHY03_DIR");
        const char* count = std::getenv("TLG_GPU_PHY03_FRAMES");
        if (count) {
            char* end{};
            const unsigned long n = std::strtoul(count, &end, 10);
            if (!*count || *end || n < 1 || n > 16) { Fail("invalid_frame_count"); return; }
            limit = u32(n);
        }
        if (!output || !*output) { Fail("missing_output_directory"); return; }
        // The directory must not already exist, even if empty.
        if (!std::filesystem::create_directory(output, ec)) { Fail("output_directory_not_new"); return; }
        dir = output;
        if (!std::filesystem::remove(trigger, ec)) { Fail("trigger_remove_failed"); return; }
        if (trace_enabled) trace_window = std::make_shared<DrawTraceWindow>(pm4_enabled);
        active.store(true, std::memory_order_release);
        Session();
        std::printf("GPU-PHY-03: Capture started frames=%u staging_budget_mib=128\n", limit);
    }
    void RecordDrawTrace(const DrawTraceRecord& r) {
        if (active && trace_enabled && trace_window) trace_window->Add(r);
    }
    bool WriteDrawTrace(const std::shared_ptr<Epoch>& e) {
        if (!e->draw_trace) return true;
        std::ofstream f(dir / fmt::format("draw_trace_{:03}.json", e->index));
        f << fmt::format("{{\"schema\":1,\"contract\":\"draw_emission_metadata_v1\",\"frame_index\":{},"
            "\"present_index\":{},\"scope\":\"shader_pair_candidates\",\"coverage\":\"DrawRecord_and_DrawIndirectRecord\","
            "\"before_record_coverage\":\"not_observed\",\"buffer_contents\":\"not_collected\","
            "\"status\":\"{}\",\"dropped\":{},\"records\":[", e->index, e->present_index,
            e->draw_trace->dropped ? "truncated" : "complete", e->draw_trace->dropped);
        for (u32 i = 0; i < e->draw_trace->used; ++i) {
            const auto& r = e->draw_trace->records[i]; if (i) f << ',';
            f << fmt::format("{{\"order\":{},\"kind\":{},\"indexed\":{},\"count\":{},\"instances\":{},"
                "\"first_instance\":{},\"vertex_offset_bits\":{},\"index_offset\":{},\"mrt_mask\":{},"
                "\"tick\":{},\"pipeline\":{},\"indirect_args_buffer\":{},\"indirect_count_buffer\":{},"
                "\"indirect_offset\":{},\"max_count\":{},\"stride\":{},"
                "\"depth_control\":{},\"stencil_control\":{},\"polygon_control\":{},\"clipper_control\":{},"
                "\"color_mask\":{},\"viewport_float_bits\":[{},{},{},{},{},{}],\"screen_scissor_words\":[{},{}],\"buffers\":[",
                i, r.kind, r.indexed ? "true" : "false", r.count, r.instances, r.first_instance,
                r.vertex_offset, r.index_offset, r.mrt_mask, r.tick, r.pipeline, r.indirect_args,
                r.indirect_count, r.indirect_offset, r.max_count, r.stride,
                r.depth, r.stencil, r.polygon, r.clipper, r.color_mask,
                r.viewport[0], r.viewport[1], r.viewport[2], r.viewport[3], r.viewport[4], r.viewport[5],
                r.scissor[0], r.scissor[1]);
            for (u32 j = 0; j < 2; ++j) {
                const auto& b = r.buffers[j]; if (j) f << ',';
                f << fmt::format("{{\"slot\":{},\"recorded\":{},\"buffer\":{},\"offset\":{},\"range\":{},"
                    "\"push_adjust\":{}}}", j * 2, b.recorded ? "true" : "false", b.handle, b.offset, b.range, b.push_adjust);
            }
            f << "]}";
        }
        f << "],\"front_census\":{";
        f << fmt::format("\"contract\":\"direct_draw_front_census_v1\",\"coverage\":\"Rasterizer_Draw_only\","
            "\"association\":\"prepare_interval_only\",\"program_identity\":\"register_addresses_not_code_identity\","
            "\"status\":\"{}\",\"dropped\":{},\"rows\":[",
            e->draw_trace->front_dropped ? "truncated" : "complete", e->draw_trace->front_dropped);
        for (u32 i = 0; i < e->draw_trace->front_used; ++i) {
            if (i) f << ',';
            const auto& row = e->draw_trace->front[i]; const auto& k = row.key;
            f << fmt::format("{{\"programs\":[{},{},{},{},{},{}],\"vs_hash\":{},\"ps_hash\":{},"
                "\"outcome\":{},\"stages\":{},\"primitive\":{},\"color_control\":{},\"color_mask\":{},"
                "\"indexed\":{},\"pipelined\":{},\"calls\":{},\"instance_sum\":{},\"index_sum\":{},\"raw_zero_instances\":{}}}",
                k.programs[0], k.programs[1], k.programs[2], k.programs[3], k.programs[4], k.programs[5],
                k.vs_hash, k.ps_hash, k.outcome, k.stages, k.primitive, k.color_control, k.color_mask,
                k.indexed ? "true" : "false", k.pipelined ? "true" : "false", row.calls, row.instances,
                row.indices, row.raw_zero_instances);
        }
        f << "]}}\n"; f.close(); return bool(f);
    }
    bool WritePm4Trace(const std::shared_ptr<Epoch>& e) {
        if (!e->draw_trace || !e->draw_trace->pm4) return true;
        const auto& window = *e->draw_trace->pm4;
        std::ofstream f(dir / fmt::format("pm4_trace_{:03}.json", e->index));
        f << fmt::format("{{\"schema\":1,\"contract\":\"pm4_execution_metadata_v1\",\"frame_index\":{},"
            "\"present_index\":{},\"association\":\"prepare_interval_only\","
            "\"coverage\":\"selected_executed_graphics_packets_and_all_type3_opcode_counts\","
            "\"stream_completeness\":\"not_proven\",\"raw_sampling\":\"bounded_prefix_after_register_apply_not_atomic\","
            "\"condition_source\":\"existing_skip_decision_no_second_condition_read\","
            "\"status\":\"{}\",\"dropped\":{},\"opcode_counts\":[", e->index, e->present_index,
            window.dropped ? "truncated" : "complete", window.dropped);
        for (u32 i = 0; i < window.opcode_counts.size(); ++i) {
            if (i) f << ',';
            f << window.opcode_counts[i];
        }
        f << "],\"records\":[";
        for (u32 i = 0; i < window.used; ++i) {
            if (i) f << ',';
            const auto& r = window.records[i];
            f << fmt::format("{{\"order\":{},\"kind\":{},\"opcode\":{},\"packet_words\":{},"
                "\"command\":{},\"buffer_base\":{},\"buffer_words\":{},\"remaining_words\":{},\"submission\":{},"
                "\"before_indices\":{},\"before_instances\":{},\"after_indices\":{},\"after_instances\":{},"
                "\"stages\":{},\"primitive\":{},\"color_control\":{},\"color_mask\":{},"
                "\"skipped\":{},\"skip_words\":{},\"programs\":[{},{},{},{},{},{}],\"raw_prefix\":[",
                i, r.kind, r.opcode, r.packet_words, r.command, r.buffer_base, r.buffer_words,
                r.remaining_words, r.submission, r.before_indices, r.before_instances, r.after_indices,
                r.after_instances, r.stages, r.primitive, r.color_control, r.color_mask,
                r.skipped ? "true" : "false", r.skip_words,
                r.programs[0], r.programs[1], r.programs[2], r.programs[3], r.programs[4], r.programs[5]);
            for (u32 j = 0; j < r.raw_words; ++j) { if (j) f << ','; f << r.raw[j]; }
            f << ']';
            if (producer_enabled) f << fmt::format(",\"producer_serial\":{}", r.producer_serial);
            f << '}';
        }
        f << "]";
        if (submission_enabled) {
            f << fmt::format(",\"submission_samples\":{{\"contract\":\"command_submission_samples_v1\","
                "\"hash_algorithm\":\"xxh3_64\",\"sample_limit_words\":262144,"
                "\"atomicity\":\"not_proven\",\"association\":\"submission_id_across_prepare_intervals\","
                "\"status\":\"{}\",\"dropped\":{},\"records\":[",
                window.submission_dropped ? "truncated" : "complete", window.submission_dropped);
            for (u32 i = 0; i < window.submission_used; ++i) {
                if (i) f << ',';
                const auto& r = window.submissions[i];
                f << fmt::format("{{\"order\":{},\"submission\":{},\"kind\":{},\"copy_mode\":{},\"samples\":[",
                                 i, r.submission, r.kind, r.copy_mode);
                for (u32 j = 0; j < r.samples.size(); ++j) {
                    if (j) f << ',';
                    const auto& s = r.samples[j];
                    f << fmt::format("{{\"address\":{},\"words\":{},\"hash\":\"{:016x}\",\"status\":{}}}",
                                     s.address, s.words, s.hash, s.status);
                }
                f << "]}";
            }
            f << "]}";
        }
        if (producer_enabled) {
            f << fmt::format(",\"producer_samples\":{{\"contract\":\"gnm_index_offset_producers_v2\","
                "\"coverage\":\"successful_sceGnmDrawIndexOffset_only\","
                "\"association\":\"collector_serial_address_packet_candidate_only\","
                "\"stack\":\"windows_metadata_to_import_caller_then_guest_frame_links\","
                "\"status\":\"{}\",\"dropped\":{}",
                window.producer_dropped ? "truncated" : "complete", window.producer_dropped);
            if (list_enabled) f << fmt::format(",\"list_contract\":\"knack_render_list_point_samples_v1\","
                "\"list_atomicity\":\"not_proven\",\"list_count_limit\":4096,\"list_byte_budget\":1048576,"
                "\"list_scan_attempts\":{},\"list_reserved_bytes\":{}", window.list_key_used, window.list_scan_bytes);
            f << ",\"records\":[";
            for (u32 i = 0; i < window.producer_used; ++i) {
                if (i) f << ',';
                const auto& r = window.producers[i];
                f << fmt::format("{{\"order\":{},\"serial\":{},\"command\":{},\"thread\":{},"
                    "\"packet\":[{},{},{},{},{}],\"caller\":{},\"caller_allocation_base\":{},"
                    "\"guest_frame\":{},\"guest_stack\":{},\"host_unwind\":{},\"host_steps\":{},"
                    "\"stop\":{},\"frames\":[", i, r.serial, r.command,
                    r.thread, r.packet[0], r.packet[1], r.packet[2], r.packet[3], r.packet[4],
                    r.caller, r.caller_allocation_base, r.guest_frame, r.guest_stack,
                    r.host_unwind, r.host_steps, r.stop);
                for (u32 j = 0; j < r.frame_count; ++j) {
                    if (j) f << ',';
                    f << fmt::format("{{\"frame\":{},\"caller\":{},\"caller_allocation_base\":{}}}",
                                     r.frames[j].frame, r.frames[j].caller, r.frames[j].caller_allocation_base);
                }
                f << ']';
                if (list_enabled) {
                    const auto& q = r.list;
                    f << fmt::format(",\"list\":{{\"status\":{},\"scan_status\":{},\"owner_frame\":{},"
                        "\"builder_frame\":{},\"list\":{},\"entries\":{},\"sort_keys\":{},\"cursor\":{},"
                        "\"count\":{},\"capacity\":{},\"batch_count\":{},\"scanned\":{},"
                        "\"null_records\":{},\"null_materials\":{},\"readable_materials\":{},\"unreadable_materials\":{}}}",
                        q.status, q.scan_status, q.owner_frame, q.builder_frame, q.list, q.entries, q.sort_keys,
                        q.cursor, q.count, q.capacity, q.batch_count, q.scanned, q.null_records,
                        q.null_materials, q.readable_materials, q.unreadable_materials);
                }
                f << '}';
            }
            f << "]}";
        }
        f << "}\n"; f.close(); return bool(f);
    }
    void PublishObservation(const std::shared_ptr<Epoch>& e) {
        if (!error.empty() || !e->presented || !e->prepared || e->snapshots.empty() ||
            !std::ranges::all_of(e->snapshots, [](const auto& item) { return item.saved; })) return;
        std::ofstream f(dir / fmt::format("frame_{:03}.json", e->index));
        f << fmt::format("{{\"schema\":3,\"contract\":\"stage_observations_v1\",\"frame_index\":{},"
            "\"present_index\":{},\"association\":\"prepare_interval_only\",\"content_path\":\"not_proven\","
            "\"gbuffer_status\":\"{}\",\"attachments\":[{}],\"snapshots\":[",
            e->index, e->present_index, e->observation_ambiguous ? "ambiguous" : e->gbuffer ? "observed" : "missing", e->attachments);
        for (size_t i = 0; i < e->snapshots.size(); ++i) {
            const auto& item = e->snapshots[i]; if (i) f << ',';
            f << fmt::format("{{\"stage\":\"{}\",\"file\":\"{}\",\"format\":\"{}\",\"identity\":{},"
                "\"width\":{},\"height\":{},\"bytes\":{},\"content_hash\":\"{}\",\"scheduler_tick\":{},"
                "\"event_sequence\":{},\"submit_sequence\":{}}}", item.stage, item.file, item.format,
                item.identity, item.width, item.height, item.bytes, item.hash, item.tick, item.event, item.submit);
        }
        f << "]";
        if (e->draw_trace) f << fmt::format(",\"draw_trace\":\"draw_trace_{:03}.json\"", e->index);
        if (e->draw_trace && e->draw_trace->pm4)
            f << fmt::format(",\"pm4_trace\":\"pm4_trace_{:03}.json\"", e->index);
        f << "}\n"; f.close();
        if (!f) { Fail("observation_manifest_write_failed"); return; }
        if (!WriteDrawTrace(e)) std::printf("RELIC-TRACE: write failed frame=%u; observations retained\n", e->index);
        if (!WritePm4Trace(e)) std::printf("RELIC-PM4: write failed frame=%u; observations retained\n", e->index);
        completed = epochs.size() == limit && std::ranges::all_of(epochs, [](const auto& item) {
            return item->prepared && item->presented && !item->snapshots.empty() &&
                std::ranges::all_of(item->snapshots, [](const auto& snapshot) { return snapshot.saved; });
        });
        if (completed) active.store(false, std::memory_order_release);
        Session();
    }
    void Publish(const std::shared_ptr<Epoch>& e) {
        if (observe) { PublishObservation(e); return; }
        if (!error.empty()) return; // late callbacks cannot complete a failed capture
        if (!e->presented || e->snapshots.size() != 3 ||
            !std::ranges::all_of(e->snapshots, [](const auto& s) { return s.saved; })) return;
        const auto path = dir / fmt::format("frame_{:03}.json", e->index);
        std::ofstream f(path, std::ios::trunc);
        f << fmt::format("{{\"schema\":2,\"contract\":\"ordered_resource_versions_v2\","
                         "\"content_preservation\":\"not_asserted\",\"frame_index\":{},\"present_index\":{},"
                         "\"submit_sequence\":{},\"display_uid\":{},\"display_address\":{},"
                         "\"producer_vs\":\"6e383eb8\",\"producer_ps\":\"b1f064c3\","
                         "\"first_consumer_ps\":\"836b20e6\",\"composition_cs\":\"4105cfbc\","
                         "\"attachments\":[{}],\"snapshots\":[",
                         e->index, e->present_index, e->submit_sequence, e->display_uid,
                         e->display_address, e->attachments);
        for (size_t i = 0; i < e->snapshots.size(); ++i) {
            const auto& s = e->snapshots[i];
            if (i) f << ',';
            f << fmt::format("{{\"stage\":\"{}\",\"file\":\"{}\",\"format\":\"{}\","
                             "\"image_uid\":{},\"guest_address\":{},\"alias_generation\":{},"
                             "\"scheduler_tick\":{},\"width\":{},\"height\":{},\"bpp\":{},"
                             "\"bytes\":{},\"samples\":1,\"mip\":0,\"layer\":0,"
                             "\"hash_algorithm\":\"fnv1a64\",\"content_hash\":\"{}\","
                             "\"identity\":{},\"event_sequence\":{},\"submit_sequence\":{}}}",
                             s.stage, s.file, s.format, s.uid, s.address, s.alias, s.tick,
                             s.width, s.height, s.bpp, s.bytes, s.hash, s.identity, s.event, s.submit);
        }
        f << "],\"resource_events\":[";
        for (size_t i = 0; i < e->events.size(); ++i) {
            if (i) f << ',';
            f << e->events[i];
        }
        f << "]}\n";
        f.close();
        if (!f) { Fail("frame_manifest_write_failed"); return; }
        std::printf("GPU-PHY-03: Frame %u saved present_index=%llu\n", e->index,
                    static_cast<unsigned long long>(e->present_index));
        completed = epochs.size() == limit && std::ranges::all_of(epochs, [](const auto& item) {
            return item->presented && item->snapshots.size() == 3 &&
                std::ranges::all_of(item->snapshots, [](const auto& s) { return s.saved; });
        });
        if (completed && error.empty()) {
            active.store(false, std::memory_order_release);
            std::printf("GPU-PHY-03: Completed frames=%u\n", limit);
        }
        Session();
    }
    static constexpr u64 StagingBudget = 128ull * 1024 * 1024;
    bool Reserve(u64 size) {
        if (!error.empty()) return false;
        if (!size || size > StagingBudget || outstanding > StagingBudget - size) {
            Fail("staging_budget_exceeded");
            return false;
        }
        outstanding += size;
        return true;
    }
    void Release(u64 size) {
        if (size > outstanding) { Fail("staging_accounting_underflow"); return; }
        outstanding -= size;
    }
    void BookSnapshot(const std::shared_ptr<Epoch>& e, Snapshot snapshot, const Identity& identity) {
        snapshot.identity = identity.Json();
        snapshot.event = ++event_sequence; snapshot.submit = submit_sequence;
        e->snapshots.push_back(std::move(snapshot));
    }
    bool PreparedAdvance(const Identity& before, const Identity& after) const {
        if (before == after) return true;
        Identity cursor = before;
        for (size_t step = 0; step < write_preparations.size(); ++step) {
            bool found = false;
            for (const auto& p : write_preparations) {
                if (p.safe && p.before == cursor && p.before.SameStorage(p.after) && p.after != cursor) {
                    cursor = p.after; found = true; break;
                }
            }
            if (cursor == after) return true;
            if (!found) break;
        }
        return false;
    }
    void NotePreparation(const Identity& before, const Identity& after, bool safe) {
        if (write_preparations.size() >= 128) { Fail("write_preparation_budget_exceeded"); return; }
        for (const auto& [key, proof] : lineage) {
            if (!proof.epoch || !proof.version.Overlaps(before)) continue;
            if (proof.version.Key() != before.Key() || !before.SameStorage(after) ||
                !PreparedAdvance(proof.version, before) || !safe) {
                std::printf("GPU-PHY-03: Unproven preparation safe=%u\n  tracked=%s\n  before=%s\n  after=%s\n",
                            unsigned(safe), proof.version.Json().c_str(), before.Json().c_str(), after.Json().c_str());
                Fail("unproven_image_write_preparation"); return;
            }
        }
        write_preparations.push_back({before, after, safe && before.KnownRange() &&
                                     after.KnownRange() && before.SameStorage(after)});
    }
    bool Independent(const Identity& binding) const {
        // An unknown root/range cannot prove independence. Retain invalidated lineage entries
        // too: their memory must not silently become a source again under a different version.
        if (!root || !root->KnownRange() || !binding.KnownRange()) return false;
        const auto conflicts = [&](const Identity& tracked) {
            return !tracked.KnownRange() || tracked.uid == binding.uid ||
                   tracked.Overlaps(binding);
        };
        if (conflicts(*root)) return false;
        for (const auto& [key, proof] : lineage) {
            if (conflicts(proof.version)) return false;
        }
        return true;
    }
    std::shared_ptr<Epoch> Read(const Identity& binding, bool full_image = true,
                              const char* phase = "unknown", const Resource* resource = nullptr) {
        const auto found = lineage.find(binding.Key());
        if (found != lineage.end()) {
            const auto& proof = found->second;
            if (!full_image || !PreparedAdvance(proof.version, binding) || !proof.epoch) {
                // Failure-only evidence. Do not rebase versions or change the rejection policy.
                if (error.empty()) {
                    std::string preparations;
                    for (const auto& p : write_preparations) {
                        if (!preparations.empty()) preparations += ',';
                        preparations += fmt::format("{{\"before\":{},\"after\":{},\"safe\":{}}}",
                            p.before.Json(), p.after.Json(), unsigned(p.safe));
                    }
                    const auto context = resource ? fmt::format(
                        "{{\"shader\":\"{:08x}\",\"base_mip\":{},\"mip_count\":{},"
                        "\"base_layer\":{},\"layer_count\":{},\"image_levels\":{},\"image_layers\":{}}}",
                        resource->shader, resource->base_mip, resource->mip_count,
                        resource->base_layer, resource->layer_count,
                        resource->image_levels, resource->image_layers) : "null";
                    const auto diagnostic = fmt::format(
                        "{{\"phase\":\"{}\",\"full_image\":{},\"version_accepted\":{},"
                        "\"epoch_missing\":{},\"tracked_epoch\":{},\"current_epoch\":{},"
                        "\"same_storage\":{},\"from_composition\":{},\"event_sequence\":{},"
                        "\"submit_sequence\":{},\"tracked\":{},\"incoming\":{},\"root\":{},"
                        "\"resource\":{},\"preparations\":[{}]}}",
                        phase, unsigned(full_image), unsigned(PreparedAdvance(proof.version, binding)),
                        unsigned(!proof.epoch), proof.epoch ? int(proof.epoch->index) : -1,
                        current ? int(current->index) : -1, unsigned(proof.version.SameStorage(binding)),
                        unsigned(proof.from_composition), event_sequence, submit_sequence,
                        proof.version.Json(), binding.Json(), root ? root->Json() : "null",
                        context, preparations);
                    std::printf("GPU-PHY-03: Read rejected %s\n", diagnostic.c_str());
                }
                Fail("stale_or_partial_lineage_binding");
                return {};
            }
            if (!current || proof.epoch != current) {
                Fail("cross_epoch_lineage");
                return {};
            }
            return proof.epoch;
        }
        for (const auto& [key, proof] : lineage) {
            if (proof.version.Overlaps(binding)) {
                Fail("aliased_lineage_binding");
                return {};
            }
        }
        return {};
    }
    std::shared_ptr<Epoch> Source(const char* phase = "unknown") {
        input_versions.clear(); input_from_composition = false;
        std::shared_ptr<Epoch> source;
        for (const auto& r : resources) {
            if (r.access != Access::ReadOnly) continue;
            const auto input = Read(r.binding, r.full_image, phase, &r);
            if (!error.empty()) return {};
            if (input && source && input != source) { Fail("mixed_epoch_inputs"); return {}; }
            if (input) {
                source = input;
                if (input_versions.size() == 128) { Fail("association_input_budget_exceeded"); return {}; }
                const auto& proof = lineage.at(r.binding.Key());
                input_versions.push_back(proof.version);
                input_from_composition |= proof.from_composition;
            }
        }
        return source;
    }
    void Write(const Identity& binding, const std::shared_ptr<Epoch>& source, bool full_image = true,
               const char* phase = "unknown", u64 shader = 0, u32 slot = ~0u,
               const char* attachment_op = "none", u64 tick = 0, bool safe_storage = true) {
        if (!error.empty()) return;
        std::shared_ptr<Epoch> origin = source;
        const auto previous = lineage.find(binding.Key());
        const bool attachment = std::string_view(phase) == "draw_color" &&
            (std::string_view(attachment_op) == "LOAD" || std::string_view(attachment_op) == "CLEAR");
        const bool storage_write = std::string_view(phase) == "draw_storage" ||
                                   std::string_view(phase) == "dispatch_storage";
        const bool destination = previous != lineage.end() && previous->second.epoch == current &&
            current && (attachment || storage_write) && full_image && safe_storage &&
            PreparedAdvance(previous->second.version, binding);
        if (!origin && destination) origin = current;
        bool invalidated = false, touched = false;
        Identity prior = binding;
        bool prior_from_composition = false;
        for (const auto& [key, proof] : lineage) {
            if (!proof.version.Overlaps(binding)) continue;
            touched = true;
            if (key == binding.Key()) prior = proof.version;
            const bool new_epoch = proof.epoch && proof.epoch != origin && proof.epoch->prepared &&
                source == current && (std::string_view(phase) == "gbuffer_producer" ||
                    (!input_versions.empty() && (std::string_view(phase) == "draw_color" ||
                     std::string_view(phase) == "draw_storage" || std::string_view(phase) == "dispatch_storage")));
            const bool retired = proof.epoch && proof.epoch->prepared && proof.epoch != current && !origin;
            const bool conflict = proof.epoch && (!origin ||
                (proof.epoch != origin && !new_epoch) || key != binding.Key() ||
                !PreparedAdvance(proof.version, binding)) && !retired;
            invalidated |= conflict;
            if (proof.epoch == current && key == binding.Key())
                prior_from_composition |= proof.from_composition;
            if (conflict || !full_image || (origin && origin != current)) {
                const auto& old = proof.version;
                std::printf("GPU-PHY-03: Lineage overlap phase=%s shader=%08llx color_slot=%u\n"
                    "  tracked: uid=%llu image=%p alias=%llu address=0x%llx bytes=%llu extent=%ux%u format=%u epoch=%lld\n",
                    phase, static_cast<unsigned long long>(shader), slot,
                    static_cast<unsigned long long>(old.uid), (void*)VkImage(old.image),
                    static_cast<unsigned long long>(old.alias), static_cast<unsigned long long>(old.address),
                    static_cast<unsigned long long>(old.bytes), old.width, old.height, old.format,
                    proof.epoch ? static_cast<long long>(proof.epoch->index) : -1ll);
            }
        }
        if (invalidated || (!full_image && (origin || touched)) || (origin && origin != current) ||
            (origin && (!safe_storage || !binding.KnownRange()))) {
            for (auto& [key, proof] : lineage) {
                if (proof.version.Overlaps(binding)) proof.epoch.reset();
            }
            std::printf("GPU-PHY-03: Ambiguous write phase=%s shader=%08llx color_slot=%u attachment_op=%s\n"
                "  incoming: uid=%llu image=%p alias=%llu address=0x%llx bytes=%llu extent=%ux%u format=%u\n"
                "  origin: source_epoch=%lld current_epoch=%lld full_image=%u touched=%u invalidated=%u\n",
                phase, static_cast<unsigned long long>(shader), slot, attachment_op,
                static_cast<unsigned long long>(binding.uid), (void*)VkImage(binding.image),
                static_cast<unsigned long long>(binding.alias), static_cast<unsigned long long>(binding.address),
                static_cast<unsigned long long>(binding.bytes), binding.width, binding.height, binding.format,
                source ? static_cast<long long>(source->index) : -1ll,
                current ? static_cast<long long>(current->index) : -1ll,
                unsigned(full_image), unsigned(touched), unsigned(invalidated));
            Fail("ambiguous_lineage_write"); return;
        }
        if (!origin) {
            // A prepared frame already owns its ordered staging copies and presenter token.
            // A subsequent unassociated write cannot reuse that old frame's association.
            for (auto it = lineage.begin(); it != lineage.end();) {
                if (it->second.version.Overlaps(binding)) it = lineage.erase(it);
                else ++it;
            }
            return;
        }
        if (origin->events.size() == 4096) { Fail("association_event_budget_exceeded"); return; }
        std::string inputs;
        if (source && std::string_view(phase) != "gbuffer_producer") {
            for (const auto& input : input_versions) {
                if (!inputs.empty()) inputs += ',';
                inputs += input.Json();
            }
        }
        const char* kind = std::string_view(phase) == "gbuffer_producer" ? "gbuffer_producer" :
            !inputs.empty() ? "sampled_input" : destination ? "destination" : "explicit_source";
        const bool from_composition = prior_from_composition || input_from_composition ||
            (std::string_view(phase) == "dispatch_storage" && shader == 0x4105cfbc);
        auto event = fmt::format("{{\"event_sequence\":{},\"submit_sequence\":{},"
            "\"epoch_index\":{},\"tick\":{},\"phase\":\"{}\",\"shader\":\"{:08x}\","
            "\"slot\":{},\"op\":\"{}\",\"kind\":\"{}\",\"before\":{},\"after\":{},"
            "\"inputs\":[{}],\"after_composition\":{}}}", ++event_sequence, submit_sequence,
            origin->index, tick, phase, shader, slot, attachment_op, kind, prior.Json(), binding.Json(),
            inputs, origin->composition ? "true" : "false");
        if (event.size() > AssociationBudget - event_bytes) {
            Fail("association_metadata_budget_exceeded"); return;
        }
        event_bytes += event.size();
        origin->events.push_back(std::move(event));
        // This tag records an observed sequence of resource uses, never conservation of texels.
        lineage[binding.Key()] = {binding, origin, from_composition};
    }
    bool Stage(const Instance& instance, Scheduler& scheduler, Runtime& runtime,
               VideoCore::Image& image, const std::shared_ptr<Epoch>& e, const char* stage) {
        const auto& ci = image.backing->image.image_ci;
        u32 bpp{};
        switch (ci.format) {
        case vk::Format::eR8G8B8A8Unorm:
        case vk::Format::eR8G8B8A8Srgb:
        case vk::Format::eB8G8R8A8Unorm:
        case vk::Format::eB8G8R8A8Srgb:
        case vk::Format::eA2B10G10R10UnormPack32:
        case vk::Format::eA2R10G10B10UnormPack32:
        case vk::Format::eB10G11R11UfloatPack32: bpp = 4; break;
        default: Fail("unsupported_snapshot_format"); return false;
        }
        if (ci.imageType != vk::ImageType::e2D || ci.samples != vk::SampleCountFlagBits::e1 ||
            ci.mipLevels != 1 || ci.arrayLayers != 1 || ci.extent.depth != 1 ||
            image.aspect_mask != vk::ImageAspectFlagBits::eColor || image.scene_proxy ||
            !(ci.usage & vk::ImageUsageFlagBits::eTransferSrc) ||
            !image.backing->subresource_states.empty() || !ci.extent.width || !ci.extent.height) {
            Fail("unsupported_image_or_subresource"); return false;
        }
        const u64 size = u64(ci.extent.width) * ci.extent.height * bpp;
        if (!Reserve(size)) return false;
        // FlushBarriers may return immediately with no dependencies. EndRendering is therefore
        // unconditional and ordered before both transitions and the copy, even for no-op Transit.
        scheduler.EndRendering();
        runtime.FlushBarriers();
        const auto before = image.backing->state;
        if (before.layout == vk::ImageLayout::eUndefined) {
            Release(size); Fail("undefined_snapshot_layout"); return false;
        }
        const VkBufferCreateInfo buffer_ci{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
        const VmaAllocationCreateInfo allocation_ci{
            .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST};
        VkBuffer buffer{};
        VmaAllocation allocation{};
        VmaAllocationInfo info{};
        if (vmaCreateBuffer(instance.GetAllocator(), &buffer_ci, &allocation_ci, &buffer,
                            &allocation, &info) != VK_SUCCESS) {
            Release(size); Fail("staging_allocation_failed"); return false;
        }
        if (!info.pMappedData) {
            vmaDestroyBuffer(instance.GetAllocator(), buffer, allocation);
            Release(size);
            Fail("staging_not_mapped"); return false;
        }
        // Called at a sampled-input transition or after a dispatch, never inside a draw.
        // Rendering already ended explicitly above. No Finish/queue wait.
        runtime.Transit(&image, vk::ImageLayout::eTransferSrcOptimal,
                        vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead);
        runtime.FlushBarriers();
        const auto handle = image.GetImage();
        const vk::BufferImageCopy region{
            .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1}, .imageExtent = ci.extent};
        scheduler.Record([handle, buffer, region](vk::CommandBuffer cmd) {
            cmd.copyImageToBuffer(handle, vk::ImageLayout::eTransferSrcOptimal, buffer, region);
            const vk::BufferMemoryBarrier2 host{
                .srcStageMask = vk::PipelineStageFlagBits2::eCopy,
                .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eHost,
                .dstAccessMask = vk::AccessFlagBits2::eHostRead,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = buffer, .offset = 0, .size = VK_WHOLE_SIZE};
            cmd.pipelineBarrier2({.bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &host});
        });
        runtime.Transit(&image, before.layout, before.pl_stage, before.access_mask);
        runtime.FlushBarriers();
        const size_t slot = e->snapshots.size();
        const auto file = fmt::format("{}_{:03}.raw", stage, e->index);
        BookSnapshot(e, {stage, file, vk::to_string(ci.format), {}, image.image_uid,
            image.info.guest_address, image.alias_generation, scheduler.CurrentTick(), size,
            ci.extent.width, ci.extent.height, bpp, false}, {image.image_uid, image.GetImage(),
            image.alias_generation, image.info.guest_address, image.info.guest_size,
            ci.extent.width, ci.extent.height, u32(ci.format)});
        std::printf("GPU-PHY-03: Frame %u staged stage=%s bytes=%llu\n", e->index, stage,
                    static_cast<unsigned long long>(size));
        scheduler.DeferPriorityOperation([this, e, slot, allocator = instance.GetAllocator(),
                                         buffer, allocation, info, size, path = dir / file] {
            bool ok = vmaInvalidateAllocation(allocator, allocation, 0, VK_WHOLE_SIZE) == VK_SUCCESS;
            u64 hash = 14695981039346656037ull;
            if (ok) {
                const auto* bytes = static_cast<const u8*>(info.pMappedData);
                for (u64 i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
                std::ofstream output(path, std::ios::binary);
                output.write(reinterpret_cast<const char*>(bytes), size);
                output.close();
                ok = bool(output);
            }
            vmaDestroyBuffer(allocator, buffer, allocation);
            std::scoped_lock lock{mutex};
            Release(size);
            if (!ok) { Fail("snapshot_invalidate_or_write_failed"); return; }
            e->snapshots[slot].hash = fmt::format("{:016x}", hash);
            e->snapshots[slot].saved = true;
            Publish(e);
        });
        return true;
    }
};
Identity Identify(const VideoCore::Image& image) {
    const auto& ci = image.backing->image.image_ci;
    return {image.image_uid, image.GetImage(), image.alias_generation, image.info.guest_address,
            image.info.guest_size, ci.extent.width, ci.extent.height, u32(ci.format)};
}
bool StableBindings(State& s, VideoCore::TextureCache& cache,
                    const char* phase = "binding_resolution") {
    for (const auto& resource : s.resources) {
        const auto current = Identify(cache.GetImage(resource.id));
        if (!(resource.binding == current)) {
            auto without_generation_change = current;
            without_generation_change.alias = resource.binding.alias;
            const bool independent_generation = resource.access == Access::ReadOnly &&
                without_generation_change == resource.binding &&
                s.Independent(resource.binding) && s.Independent(current);
            if (resource.access == Access::ReadOnly &&
                s.PreparedAdvance(resource.binding, current)) continue;
            std::string changed;
            const auto add_changed = [&](const char* name) {
                if (!changed.empty()) {
                    changed += ',';
                }
                changed += name;
            };
            if (resource.binding.uid != current.uid) add_changed("image_uid");
            if (resource.binding.image != current.image) add_changed("vk_image");
            if (resource.binding.alias != current.alias) add_changed("alias_generation");
            if (resource.binding.address != current.address) add_changed("guest_address");
            if (resource.binding.bytes != current.bytes) add_changed("guest_size");
            if (resource.binding.width != current.width || resource.binding.height != current.height) add_changed("extent");
            if (resource.binding.format != current.format) add_changed("format");

            std::printf("GPU-PHY-03: Binding changed image_id=%u changed=[%s]\n"
                        "  before: uid=%llu image=%p alias=%llu address=0x%llx bytes=%llu extent=%ux%u format=%s(%u)\n"
                        "  after:  uid=%llu image=%p alias=%llu address=0x%llx bytes=%llu extent=%ux%u format=%s(%u)\n",
                        resource.id.index, changed.c_str(),
                        static_cast<unsigned long long>(resource.binding.uid),
                        (void*)VkImage(resource.binding.image),
                        static_cast<unsigned long long>(resource.binding.alias),
                        static_cast<unsigned long long>(resource.binding.address),
                        static_cast<unsigned long long>(resource.binding.bytes),
                        resource.binding.width, resource.binding.height,
                        vk::to_string(static_cast<vk::Format>(resource.binding.format)).c_str(),
                        resource.binding.format,
                        static_cast<unsigned long long>(current.uid),
                        (void*)VkImage(current.image),
                        static_cast<unsigned long long>(current.alias),
                        static_cast<unsigned long long>(current.address),
                        static_cast<unsigned long long>(current.bytes),
                        current.width, current.height,
                        vk::to_string(static_cast<vk::Format>(current.format)).c_str(),
                        current.format);
            std::printf("  context: phase=%s shader=%08llx access=%s subresource=m%u+%u/l%u+%u "
                        "independent_before=%u independent_after=%u decision=%s\n",
                        phase, static_cast<unsigned long long>(resource.shader),
                        resource.access == Access::ReadOnly ? "read_only" :
                        resource.access == Access::Atomic ? "atomic" : "may_write",
                        resource.base_mip, resource.mip_count,
                        resource.base_layer, resource.layer_count,
                        unsigned(s.Independent(resource.binding)), unsigned(s.Independent(current)),
                        independent_generation ? "independent_generation" : "fail");
            for (size_t i = 0; i < s.draw_targets.size(); ++i) {
                const auto& target = s.draw_targets[i];
                std::printf("  target: slot=%u uid=%llu alias=%llu address=0x%llx bytes=%llu\n",
                            unsigned(i), static_cast<unsigned long long>(target.uid),
                            static_cast<unsigned long long>(target.alias),
                            static_cast<unsigned long long>(target.address),
                            static_cast<unsigned long long>(target.bytes));
            }
            // Preparing an unrelated read/target (e.g. a depth mip chain) can publish a new
            // generation. Do not rebase its snapshot or manufacture any lineage for it.
            // Root/derived overlaps and all structural changes still fail conservatively.
            if (independent_generation) continue;
            s.Fail("binding_changed_during_resolution");
            return false;
        }
    }
    return true;
}
State& Get() {
    // Deferred scheduler callbacks can outlive other static objects during shutdown.
    // Keep only this small metadata owner alive for the process; staging frees at its tick.
    static State* const state = new State;
    return *state;
}
} // namespace

bool Enabled() { const auto& s = Get(); return s.trigger && *s.trigger; }
bool Recording() { return Get().active.load(std::memory_order_acquire); }
bool NeedsBoundary() {
    auto& s = Get(); std::scoped_lock lock{s.mutex};
    std::error_code ec;
    return s.active || (!s.requested && s.trigger && *s.trigger &&
                         std::filesystem::exists(s.trigger, ec));
}

void BeginBind() {
    auto& s = Get(); std::scoped_lock lock{s.mutex};
    s.resources.clear(); s.draw_targets.clear();
    s.write_preparations.clear(); s.pending_writes.clear();
    s.input_versions.clear(); s.input_from_composition = false;
}
void BeginImageWrite(VideoCore::TextureCache& cache, VideoCore::ImageId id) {
    auto& s = Get(); std::scoped_lock lock{s.mutex};
    if (!s.active || s.observe) return;
    const auto& image = cache.GetImage(id);
    if (s.pending_writes.contains(id)) { s.Fail("duplicate_write_preparation"); return; }
    s.pending_writes[id] = {Identify(image), False(image.flags & VideoCore::ImageFlagBits::Aliased)};
}
void EndImageWrite(VideoCore::TextureCache& cache, VideoCore::ImageId id) {
    auto& s = Get(); std::scoped_lock lock{s.mutex};
    if (!s.active || s.observe) return;
    const auto pending = s.pending_writes.find(id);
    if (pending == s.pending_writes.end()) { s.Fail("missing_write_preparation"); return; }
    const auto& image = cache.GetImage(id);
    s.NotePreparation(pending->second.first, Identify(image), pending->second.second &&
                      False(image.flags & VideoCore::ImageFlagBits::Aliased));
    s.pending_writes.erase(pending);
}
void BufferWrites(const Pipeline* pipeline) {
    auto& s = Get(); std::scoped_lock lock{s.mutex};
    if (!s.active || s.observe || s.lineage.empty()) return;
    for (const auto* stage : pipeline->GetStages()) {
        if (!stage) continue;
        for (const auto& resource : stage->buffers) {
            if (resource.IsSpecial() || !resource.is_written) continue;
            const auto sharp = resource.GetSharp(*stage);
            Identity write{};
            write.address = sharp.base_address;
            write.bytes = sharp.GetSize();
            for (auto& [key, proof] : s.lineage) {
                if (write.bytes && proof.version.Overlaps(write)) {
                    proof.epoch.reset();
                    s.Fail("unobserved_buffer_write_to_lineage");
                }
            }
        }
    }
}
void Sample(const Instance& instance, Scheduler& scheduler, Runtime& runtime,
            VideoCore::TextureCache& cache, VideoCore::ImageId id, Access access,
            const VideoCore::SubresourceRange& range, u64 shader) {
    auto& s = Get(); std::scoped_lock lock{s.mutex};
    if (!s.active) return;
    auto& image = cache.GetImage(id);
    const bool full_image = range == VideoCore::SubresourceRange{} &&
        image.info.resources.levels == 1 && image.info.resources.layers == 1;
    s.resources.push_back({id, access, Identify(image), full_image, shader,
        range.base.level, range.extent.levels, range.base.layer, range.extent.layers,
        image.info.resources.levels, image.info.resources.layers});
    const auto& e = s.current;
    if (!e || e->gbuffer || image.image_uid != e->gbuffer_uid || access != Access::ReadOnly) return;
    if (shader != 0x836b20e6 || range.base.level || range.base.layer ||
        range.extent.levels != 1 || range.extent.layers != 1 || image.GetImage() != e->gbuffer_image) {
        s.Fail("unexpected_gbuffer_consumer_or_backing"); return;
    }
    e->gbuffer = s.Stage(instance, scheduler, runtime, image, e, "gbuffer");
}
template <typename Targets>
void DrawTargets(VideoCore::TextureCache& cache, const GraphicsPipeline* pipeline,
             const RenderState& state,
             const Targets& targets, u64 tick) {
    auto& s = Get(); std::scoped_lock lock{s.mutex};
    if (!s.active) return;
    const auto& key = pipeline->GetGraphicsKey();
    s.draw_targets.clear();
    for (u32 i = 0; i < state.num_color_attachments; ++i) {
        s.draw_targets.push_back(targets[i].first ? Identify(cache.GetImage(targets[i].first))
                                                : Identity{});
    }
    const bool target = pipeline->GetStage(Shader::SwStage::Vertex).pgm_hash == 0x6e383eb8 &&
        key.mrt_mask == 0x1f && key.prim_type == AmdGpu::PrimitiveType::TriangleList &&
        pipeline->GetStage(Shader::SwStage::Fragment).pgm_hash == 0xb1f064c3;
    if (target && s.current && s.current->gbuffer) {
        if (s.observe) s.current->observation_ambiguous = true;
        else { s.Fail("gbuffer_producer_after_snapshot"); return; }
    }
    if (target && !s.current) {
        if (s.epochs.size() == s.limit) { s.Fail("more_scene_epochs_than_requested"); return; }
        auto e = std::make_shared<Epoch>();
        e->index = u32(s.epochs.size()); e->submit_sequence = s.submit_sequence;
        for (u32 i = 0; i < 5; ++i) {
            if (!targets[i].first) { s.Fail("missing_gbuffer_attachment"); return; }
            const auto& image = cache.GetImage(targets[i].first);
            if (state.color_attachments[i].image_layout != image.backing->state.layout ||
                targets[i].second.view_info.range != VideoCore::SubresourceRange{} ||
                image.scene_proxy || state.width != image.info.size.width ||
                state.height != image.info.size.height) { s.Fail("redirected_gbuffer_attachment"); return; }
            if (i) e->attachments += ',';
            e->attachments += fmt::format("{{\"slot\":{},\"image_uid\":{},\"guest_address\":{},"
                "\"format\":\"{}\",\"width\":{},\"height\":{},\"samples\":{}}}", i,
                image.image_uid, image.info.guest_address, vk::to_string(image.info.pixel_format),
                image.info.size.width, image.info.size.height, image.backing->num_samples);
        }
        auto& image = cache.GetImage(targets[2].first);
        e->gbuffer_uid = image.image_uid; e->gbuffer_image = image.GetImage();
        s.root = Identify(image);
        s.epochs.push_back(e); s.current = e; s.Session();
    }
    if (s.observe) {
        if (target && s.current && targets[2].first) {
            const auto& image = cache.GetImage(targets[2].first);
            if (image.image_uid != s.current->gbuffer_uid || image.GetImage() != s.current->gbuffer_image)
                s.current->observation_ambiguous = true;
        }
        return;
    }
    if (!StableBindings(s, cache, "draw_targets")) return;
    auto source = s.Source("draw_targets");
    if (!s.error.empty()) return;
    if (target) { source = s.current; s.input_versions.clear(); s.input_from_composition = false; }
    for (u32 i = 0; i < state.num_color_attachments; ++i) {
        if (!targets[i].first) continue;
        // LOAD is not evidence of a fresh origin. Every writable attachment invalidates old
        // provenance unless a current read-only input proves the source of this draw.
        s.Write(Identify(cache.GetImage(targets[i].first)), source,
                targets[i].second.view_info.range == VideoCore::SubresourceRange{},
                target ? "gbuffer_producer" : "draw_color",
                key.mrt_mask ? pipeline->GetStage(Shader::SwStage::Fragment).pgm_hash : 0,
                i, state.color_attachments[i].is_clear ? "CLEAR" : "LOAD", tick,
                False(cache.GetImage(targets[i].first).flags & VideoCore::ImageFlagBits::Aliased));
        if (!s.error.empty()) return;
    }
    for (const auto& r : s.resources) {
        if (r.access == Access::ReadOnly) continue;
        s.Write(Identify(cache.GetImage(r.id)), source, r.full_image,
                "draw_storage", r.shader, ~0u, "none", tick,
                False(cache.GetImage(r.id).flags & VideoCore::ImageFlagBits::Aliased));
        if (!s.error.empty()) return;
    }
}
void Dispatch(const Instance& instance, Scheduler& scheduler, Runtime& runtime,
              VideoCore::TextureCache& cache, u64 shader) {
    auto& s = Get(); std::scoped_lock lock{s.mutex};
    if (!s.active || s.observe) return;
    if (!StableBindings(s, cache, "dispatch")) return;
    auto source = s.Source("dispatch");
    if (!s.error.empty()) return;
    if (shader == 0x4105cfbc) {
        if (!s.current || !s.current->gbuffer || s.current->composition) {
            s.Fail("unexpected_composition_epoch"); return;
        }
        const bool reads_gbuffer = std::ranges::any_of(s.resources, [&](const auto& r) {
            const auto& image = cache.GetImage(r.id);
            return r.access == Access::ReadOnly && image.image_uid == s.current->gbuffer_uid &&
                   image.GetImage() == s.current->gbuffer_image;
        });
        if (!reads_gbuffer) { s.Fail("composition_gbuffer_identity_mismatch"); return; }
        source = s.current;
    }
    u32 compositions{};
    for (const auto& r : s.resources) {
        if (r.access == Access::ReadOnly) continue;
        auto& image = cache.GetImage(r.id);
        s.Write(Identify(image), source, r.full_image, "dispatch_storage", shader, ~0u, "none",
                scheduler.CurrentTick(), False(image.flags & VideoCore::ImageFlagBits::Aliased));
        if (!s.error.empty()) return;
        if (shader == 0x4105cfbc) {
            if (image.info.pixel_format != vk::Format::eB10G11R11UfloatPack32 || ++compositions != 1) {
                s.Fail("unexpected_composition_output"); return;
            }
            source->composition = s.Stage(instance, scheduler, runtime, image, source, "composition");
        }
    }
    if (shader == 0x4105cfbc && compositions != 1) s.Fail("missing_composition_output");
}
void OnSubmit() {
    auto& s = Get(); std::scoped_lock lock{s.mutex}; ++s.submit_sequence;
}

// Presenter calls these on its existing preparation/presentation paths. The actual successful
// swapchain present assigns present_index, after matching the exact display image at preparation.
void Prepare(const Instance& instance, Scheduler& scheduler, Runtime& runtime,
             VideoCore::Image* image, const void* frame) {
    auto& s = Get(); std::scoped_lock lock{s.mutex};
    if (s.active) {
        if (!image) { s.Fail("upscaled_display_unsupported"); return; }
        if (s.observe) {
            // A real Frame token binds this interval's observations to an actual presentation.
            // It does not prove that the display's pixels came from the observed G-buffer.
            if (s.prepared.contains(frame)) { s.Fail("frame_token_already_prepared"); return; }
            auto e = s.current;
            if (!e) {
                if (s.epochs.size() >= s.limit) { s.Fail("more_scene_epochs_than_requested"); return; }
                e = std::make_shared<Epoch>(); e->index = u32(s.epochs.size()); s.epochs.push_back(e);
            }
            e->display_uid = image->image_uid; e->display_address = image->info.guest_address;
            if (s.Stage(instance, scheduler, runtime, *image, e, "display")) {
                e->draw_trace = std::move(s.trace_window);
                if (s.trace_enabled && e->index + 1 < s.limit) s.trace_window = std::make_shared<DrawTraceWindow>(s.pm4_enabled);
                e->prepared = true; s.prepared[frame] = e; s.current.reset(); s.root.reset();
                if (e->index + 1 == s.limit) s.active.store(false, std::memory_order_release);
            }
            return;
        }
        const auto e = s.Read(Identify(*image), true, "prepare_display");
        if (!s.error.empty()) return;
        if (!e || !e->composition || e->prepared || !s.lineage.at(Identify(*image).Key()).from_composition) {
            s.Fail("display_lineage_missing_or_reused"); return;
        }
        if (s.prepared.contains(frame)) { s.Fail("frame_token_already_prepared"); return; }
        if (e->index != s.prepared.size() + std::ranges::count_if(s.epochs, [](const auto& x) { return x->presented; })) {
            s.Fail("display_epoch_out_of_order"); return;
        }
        e->display_uid = image->image_uid; e->display_address = image->info.guest_address;
        if (s.Stage(instance, scheduler, runtime, *image, e, "display")) {
            e->prepared = true; s.prepared[frame] = e;
            s.current.reset(); s.root.reset();
            if (e->index + 1 == s.limit) s.active.store(false, std::memory_order_release);
        }
    }
    // Trigger starts after this frame: a mid-G-buffer request never yields a partial first frame.
    s.Poll();
}
void Present(const void* frame, bool success, bool reused,
             const char* failure_reason = "prepared_frame_not_presented") {
    auto& s = Get(); std::scoped_lock lock{s.mutex};
    if (success) ++s.present_sequence;
    if (reused) return;
    const auto found = s.prepared.find(frame);
    if (found == s.prepared.end()) return;
    const auto e = found->second; s.prepared.erase(found);
    if (!success) { s.Fail(failure_reason); return; }
    e->presented = true; e->present_index = s.present_sequence;
    if (e->index && e->present_index != s.epochs[e->index - 1]->present_index + 1) {
        s.Fail("nonconsecutive_presents"); return;
    }
    s.Publish(e);
}
// kind: 1/2=direct entry/rejection, 3/4=direct/indirect emission, 5/6=indirect entry/rejection.
bool TraceEnabled() { const auto& s = Get(); return s.trace_enabled && s.active.load(std::memory_order_acquire); }
// Outcomes: 1=entry, 2=FilterDraw rejection, 3=no pipeline, 4=adjacency skip,
// 5=forwarded to recording/queue, 6=special operation queued instead of a normal draw.
void TraceFront(const AmdGpu::Regs& regs, u32 outcome, bool indexed, bool pipelined,
                const GraphicsPipeline* pipeline = nullptr) {
    if (!TraceEnabled()) return;
    FrontTraceKey key;
    for (u32 i = 0; i < key.programs.size(); ++i)
        key.programs[i] = regs.ProgramForStage(i)->Address<u64>();
    key.outcome = outcome; key.indexed = indexed; key.pipelined = pipelined;
    key.stages = std::bit_cast<u32>(regs.stage_enable);
    key.primitive = u32(regs.primitive_type);
    key.color_control = std::bit_cast<u32>(regs.color_control);
    key.color_mask = std::bit_cast<u32>(regs.color_target_mask);
    if (pipeline) {
        const auto& stages = pipeline->GetStages();
        if (const auto* vs = stages[u32(Shader::SwStage::Vertex)]) key.vs_hash = vs->pgm_hash;
        if (const auto* ps = stages[u32(Shader::SwStage::Fragment)]) key.ps_hash = ps->pgm_hash;
    }
    auto& s = Get(); std::scoped_lock lock{s.mutex};
    if (s.active && s.trace_enabled && s.trace_window)
        s.trace_window->AddFront(key, regs.num_indices, regs.num_instances.NumInstances(),
                                regs.num_instances.num_instances == 0);
}
void TraceDraw(const GraphicsPipeline* pipeline, const AmdGpu::Regs& regs,
               std::span<const vk::DescriptorBufferInfo> buffers, const Shader::PushData& push,
               u32 kind, bool indexed, u32 count, u32 instances, u32 first_instance,
               u32 vertex_offset, u32 index_offset, u64 tick, vk::Buffer args = {},
               u64 args_offset = 0, vk::Buffer counts = {}, u32 max_count = 0, u32 stride = 0) {
    auto& s = Get();
    if (!TraceEnabled() || !pipeline || pipeline->GetGraphicsKey().mrt_mask != 0x1f ||
        pipeline->GetStage(Shader::SwStage::Vertex).pgm_hash != 0x6e383eb8 ||
        pipeline->GetStage(Shader::SwStage::Fragment).pgm_hash != 0xb1f064c3) return;
    DrawTraceRecord r; r.kind = kind; r.indexed = indexed; r.count = count; r.instances = instances;
    r.first_instance = first_instance; r.vertex_offset = vertex_offset; r.index_offset = index_offset;
    r.mrt_mask = pipeline->GetGraphicsKey().mrt_mask; r.tick = tick;
    r.pipeline = XXH3_64bits(&pipeline->GetGraphicsKey(), sizeof(pipeline->GetGraphicsKey()));
    r.indirect_args = (u64)VkBuffer(args); r.indirect_offset = args_offset;
    r.indirect_count = (u64)VkBuffer(counts); r.max_count = max_count; r.stride = stride;
    r.depth = std::bit_cast<u32>(regs.depth_control); r.stencil = std::bit_cast<u32>(regs.stencil_control);
    r.polygon = std::bit_cast<u32>(regs.polygon_control); r.clipper = std::bit_cast<u32>(regs.clipper_control);
    r.color_mask = std::bit_cast<u32>(regs.color_target_mask);
    const auto& vp = regs.viewports[0]; r.viewport = {std::bit_cast<u32>(vp.xscale), std::bit_cast<u32>(vp.xoffset), std::bit_cast<u32>(vp.yscale),
        std::bit_cast<u32>(vp.yoffset), std::bit_cast<u32>(vp.zscale), std::bit_cast<u32>(vp.zoffset)};
    static_assert(sizeof(regs.screen_scissor) == 8);
    std::array<u32, 2> scissor; std::memcpy(scissor.data(), &regs.screen_scissor, sizeof(scissor));
    r.scissor = {scissor[0], scissor[1]};
    if (kind == 3 || kind == 4) {
        size_t binding = 0;
        for (const auto* stage : pipeline->GetStages()) {
            if (!stage) continue;
            for (u32 slot = 0; slot < stage->buffers.size(); ++slot, ++binding) {
                if (stage->sw_stage != Shader::SwStage::Vertex || (slot != 0 && slot != 2) ||
                    stage->buffers[slot].IsSpecial() || binding >= buffers.size() || binding >= push.buf_offsets.size()) continue;
                const auto& b = buffers[binding];
                r.buffers[slot / 2] = {true, (u64)VkBuffer(b.buffer), b.offset, b.range, push.buf_offsets[binding]};
            }
        }
    }
    std::scoped_lock lock{s.mutex}; s.RecordDrawTrace(r);
}
} // namespace GpuPhy03

bool Rasterizer::RelicProducerTraceActive() const {
    const auto& s = GpuPhy03::Get();
    return s.producer_enabled && s.active.load(std::memory_order_acquire);
}

#ifdef _WIN32
static u64 RelicExecutableAllocation(u64 address) {
    MEMORY_BASIC_INFORMATION region{};
    if (!address || !VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)) ||
        region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        !(region.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
        return 0;
    return reinterpret_cast<uintptr_t>(region.AllocationBase);
}

static bool ReadRelicListBytes(u64 address, void* output, size_t bytes) {
    if (!address || !bytes || address > ~0ull - bytes) return false;
    SIZE_T got{};
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), output,
                             bytes, &got) && got == bytes;
}

static void SampleRelicListHeader(RelicProducerSample& r) {
    const u64 base = r.caller_allocation_base;
    constexpr std::array<u64, 4> returns{0xbf5834, 0xbe5989, 0xc348c5, 0x9bc831};
    if (r.host_unwind != 1 || !base || r.caller - base != 0xbb05f1 || r.frame_count < returns.size()) return;
    for (u32 i = 0; i < returns.size(); ++i)
        if (r.frames[i].caller_allocation_base != base || r.frames[i].caller - base != returns[i]) return;
    auto& q = r.list;
    q.builder_frame = r.frames[2].frame; q.owner_frame = r.frames[3].frame;
    // Read-only layout guards for the supported guest image. These are not code patches.
    constexpr std::array<u8, 7> owner_store{0x4c,0x89,0x85,0x40,0xfe,0xff,0xff};
    constexpr std::array<u8, 7> builder_store{0x48,0x89,0x9d,0x68,0xfe,0xff,0xff};
    constexpr std::array<u8, 3> count_load{0x8b,0x43,0x40};
    constexpr std::array<u8, 5> material_load{0x4a,0x8b,0x44,0x36,0xcc};
    std::array<u8, 7> a{}, b{}; std::array<u8, 3> c{}; std::array<u8, 5> e{};
    if (base > ~0ull - 0xc33bc2 ||
        !ReadRelicListBytes(base + 0xc33bbb, a.data(), a.size()) || a != owner_store ||
        !ReadRelicListBytes(base + 0xbe56f5, b.data(), b.size()) || b != builder_store ||
        !ReadRelicListBytes(base + 0xbe5684, c.data(), c.size()) || c != count_load ||
        !ReadRelicListBytes(base + 0xbe579a, e.data(), e.size()) || e != material_load) {
        q.status = 2; return;
    }
    u64 builder_list{};
    if (q.owner_frame < 0x1c0 || q.builder_frame < 0x1b0 ||
        !ReadRelicListBytes(q.owner_frame - 0x1c0, &q.list, sizeof(q.list)) ||
        !ReadRelicListBytes(q.builder_frame - 0x198, &builder_list, sizeof(builder_list)) ||
        !ReadRelicListBytes(q.builder_frame - 0x164, &q.batch_count, sizeof(q.batch_count)) ||
        !ReadRelicListBytes(q.builder_frame - 0x1b0, &q.cursor, sizeof(q.cursor))) {
        q.status = 3; return;
    }
    if (q.list != builder_list) { q.status = 6; return; }
    std::array<u8, 0x48> header{};
    if (!ReadRelicListBytes(q.list, header.data(), header.size())) { q.status = 3; return; }
    std::memcpy(&q.entries, header.data(), 8); std::memcpy(&q.sort_keys, header.data() + 8, 8);
    std::memcpy(&q.count, header.data() + 0x40, 4); std::memcpy(&q.capacity, header.data() + 0x44, 4);
    if ((q.list & 7) || q.count > q.capacity || q.capacity > 0x7fffffffu ||
        (q.count && !q.entries) || !q.batch_count || q.batch_count > 64 ||
        q.cursor < 0x54 || (q.cursor - 0x54) % 0x58) { q.status = 4; return; }
    q.status = 1;
}

static void ScanRelicListMaterials(RelicListSample& q) {
    // The caller reserves a bounded payload budget before reaching this function.
    if (q.count > GpuPhy03::Pm4TraceWindow::ListCountLimit) { q.scan_status = 4; return; }
    std::vector<u8> entries(size_t(q.count) * 0x58);
    if (!entries.empty() && !ReadRelicListBytes(q.entries, entries.data(), entries.size())) {
        q.scan_status = 5; return;
    }
    for (u32 i = 0; i < q.count; ++i) {
        u64 record{}, material{};
        std::memcpy(&record, entries.data() + size_t(i) * 0x58 + 0x20, 8);
        ++q.scanned;
        if (!record) ++q.null_records;
        else if (!ReadRelicListBytes(record, &material, sizeof(material))) ++q.unreadable_materials;
        else if (!material) ++q.null_materials;
        else ++q.readable_materials;
    }
    std::array<u8, 0x48> after{};
    if (!ReadRelicListBytes(q.list, after.data(), after.size())) { q.scan_status = 8; return; }
    u64 pointer{}; u32 count{}, capacity{};
    std::memcpy(&pointer, after.data(), 8); std::memcpy(&count, after.data() + 0x40, 4);
    std::memcpy(&capacity, after.data() + 0x44, 4);
    if (pointer != q.entries || count != q.count || capacity != q.capacity) { q.scan_status = 7; return; }
    q.scan_status = q.unreadable_materials ? 6 : 1;
}

static void WalkRelicGuestFrames(RelicProducerSample& r) {
    u64 frame = r.guest_frame;
    while (frame && r.frame_count < r.frames.size()) {
        if ((frame & 7) || frame < r.guest_stack || frame - r.guest_stack > (1ull << 20) || frame > ~0ull - 16) {
            r.stop = 3; break;
        }
        u64 pair[2]{}; SIZE_T got{};
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(frame),
                               pair, sizeof(pair), &got) || got != sizeof(pair)) {
            r.stop = 2; break;
        }
        if (!pair[1]) { r.stop = 1; break; }
        const u64 allocation = RelicExecutableAllocation(pair[1]);
        if (!allocation) { r.stop = 7; break; }
        auto& link = r.frames[r.frame_count++];
        link = {frame, pair[1], allocation};
        if (!pair[0]) { frame = 0; break; }
        if (pair[0] <= frame) { r.stop = 3; break; }
        frame = pair[0];
    }
    if (!r.stop) r.stop = frame ? 5 : 1;
}
#endif

[[gnu::noinline]] void Rasterizer::TraceRelicProducer(const u32* command, u64 expected_caller) {
    if (!RelicProducerTraceActive()) return;
    RelicProducerSample r;
    r.command = reinterpret_cast<uintptr_t>(command);
    r.caller = expected_caller; // compiler-provided return PC in the ABI entry
    // The ABI writer just wrote these five words. Nothing beyond that packet is sampled.
    std::copy_n(command, r.packet.size(), r.packet.begin());
#ifdef _WIN32
    r.thread = GetCurrentThreadId();
    r.caller_allocation_base = RelicExecutableAllocation(expected_caller);
    if (!r.caller_allocation_base) {
        r.host_unwind = 6;
    } else {
        CONTEXT context{};
        RtlCaptureContext(&context);
        const u64 origin = context.Rsp;
        // Only unwind registered host frames, stopping at the exact import caller.
        // The context is a local copy. No exception handler or guest unwind is invoked.
        for (u32 i = 0; i < 4; ++i) {
            DWORD64 image_base{};
            auto* entry = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
            if (!entry) { r.host_unwind = 2; break; }
            const u64 previous_sp = context.Rsp;
            const u64 previous_pc = context.Rip;
            void* handler_data{}; DWORD64 establisher{};
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, entry, &context,
                             &handler_data, &establisher, nullptr);
            ++r.host_steps;
            if (context.Rsp <= previous_sp || context.Rsp < origin ||
                context.Rsp - origin > (1ull << 20) || !context.Rip || context.Rip == previous_pc) {
                r.host_unwind = 3; break;
            }
            if (context.Rip == expected_caller) {
                r.host_unwind = 1;
                r.guest_frame = context.Rbp; r.guest_stack = context.Rsp;
                WalkRelicGuestFrames(r);
                break;
            }
        }
        if (!r.host_unwind) r.host_unwind = 4;
    }
    if (r.host_unwind != 1) r.stop = 6;
#else
    r.host_unwind = 5; r.stop = 4;
#endif
    auto& s = GpuPhy03::Get();
    if (s.list_enabled) {
#ifdef _WIN32
        SampleRelicListHeader(r);
        if (r.list.status == 1) {
            {
                std::scoped_lock reservation{s.mutex};
                if (s.active && s.trace_window && s.trace_window->pm4)
                    r.list.scan_status = s.trace_window->pm4->ReserveListScan(r.list);
            }
            if (r.list.scan_status == 1) ScanRelicListMaterials(r.list);
        }
#else
        r.list.status = 5;
#endif
    }
    // Safe stack reads happen before taking the capture lock. No guest writes or waits.
    std::scoped_lock lock{s.mutex};
    if (s.active && s.trace_window && s.trace_window->pm4) {
        r.serial = ++s.producer_sequence;
        s.trace_window->pm4->AddProducer(r);
    }
}

bool Rasterizer::RelicPm4TraceActive() const {
    const auto& s = GpuPhy03::Get();
    return s.pm4_enabled && s.active.load(std::memory_order_acquire);
}

bool Rasterizer::RelicSubmissionTraceActive() const {
    const auto& s = GpuPhy03::Get();
    return s.submission_enabled && s.active.load(std::memory_order_acquire);
}

RelicCommandSample Rasterizer::SampleRelicCommands(std::span<const u32> commands) {
    RelicCommandSample sample;
    sample.address = reinterpret_cast<uintptr_t>(commands.data()); sample.words = commands.size();
    if (commands.size() > 262144) { sample.status = 2; return sample; }
    sample.status = 1;
    sample.hash = XXH3_64bits(commands.data(), commands.size_bytes());
    return sample;
}

void Rasterizer::RecordRelicSubmission(const RelicSubmissionSample& sample) {
    if (!RelicSubmissionTraceActive()) return;
    auto& s = GpuPhy03::Get(); std::scoped_lock lock{s.mutex};
    if (s.active && s.trace_window && s.trace_window->pm4)
        s.trace_window->pm4->AddSubmission(sample);
}

void Rasterizer::TraceRelicConsumption(u32 kind, std::span<const u32> dcb,
                                      std::span<const u32> ccb, u64 submission) {
    if (!RelicSubmissionTraceActive()) return;
    RelicSubmissionSample sample; sample.kind = kind; sample.submission = submission;
    sample.samples[0] = SampleRelicCommands(dcb); sample.samples[1] = SampleRelicCommands(ccb);
    RecordRelicSubmission(sample);
}

void Rasterizer::TraceRelicPm4(u32 kind, u32 opcode, std::span<const u32> remaining,
                              u32 packet_words, u64 buffer_base, u64 buffer_words,
                              u64 submission, u32 before_indices, u32 before_instances,
                              bool skipped, u32 skip_words) {
    if (!RelicPm4TraceActive()) return;
    const auto& regs = Regs();
    GpuPhy03::Pm4TraceRecord r;
    r.kind = kind; r.opcode = opcode; r.packet_words = packet_words;
    r.command = reinterpret_cast<uintptr_t>(remaining.data());
    r.buffer_base = buffer_base; r.buffer_words = buffer_words;
    r.remaining_words = remaining.size(); r.submission = submission;
    r.before_indices = before_indices; r.before_instances = before_instances;
    r.after_indices = regs.num_indices; r.after_instances = regs.num_instances.num_instances;
    r.skipped = skipped; r.skip_words = skip_words;
    bool retain = kind != 1 || r.before_indices != r.after_indices || r.before_instances != r.after_instances;
    using Op = AmdGpu::PM4ItOpcode;
    switch (Op(opcode)) {
    case Op::NumInstances: case Op::DrawIndex2: case Op::DrawIndexOffset2: case Op::DrawIndexAuto:
    case Op::DrawIndirect: case Op::DrawIndirectMulti: case Op::DrawIndexIndirect:
    case Op::DrawIndexIndirectMulti: case Op::DrawIndexIndirectCountMulti:
    case Op::CondExec: case Op::IndirectBuffer: case Op::ClearState:
        retain = true; break;
    default: break;
    }
    if (retain) {
        r.stages = std::bit_cast<u32>(regs.stage_enable); r.primitive = u32(regs.primitive_type);
        r.color_control = std::bit_cast<u32>(regs.color_control);
        r.color_mask = std::bit_cast<u32>(regs.color_target_mask);
        for (u32 i = 0; i < r.programs.size(); ++i) r.programs[i] = regs.ProgramForStage(i)->Address<u64>();
        if (kind == 1 || kind == 2) {
            r.raw_words = u32(std::min({remaining.size(), size_t(packet_words), r.raw.size()}));
            std::copy_n(remaining.begin(), r.raw_words, r.raw.begin());
        }
    }
    auto& s = GpuPhy03::Get(); std::scoped_lock lock{s.mutex};
    if (s.active && s.trace_window && s.trace_window->pm4) {
        if (s.producer_enabled && retain) r.producer_serial = ++s.producer_sequence;
        s.trace_window->pm4->Add(r, retain);
    }
}

namespace {

constexpr u64 KnackDetachedGeometryShader = 0x8943ac2c;
constexpr u64 KnackSmallGeometryVertexShader = 0x6e383eb8;

// Opt-in occlusion probe for Knack's detached/dynamic geometry. A trigger records a short
// window only. The original query says whether the real draw produced samples after fragment,
// depth and stencil tests. The relaxed query repeats the draw with color writes disabled and
// depth, stencil, culling and rasterizer-discard disabled; it changes no pixels and separates
// visibility tests from geometry/clip output.
class KnackContributionProbe {
public:
    struct Token {
        u32 original;
        u32 relaxed;
        bool has_relaxed;
    };

    static std::unique_ptr<KnackContributionProbe> Create(const Instance& instance) {
        const char* trigger = std::getenv("TLG_KNACK_CONTRIB_TRIGGER");
        if (!trigger || !trigger[0]) {
            return {};
        }
        return std::unique_ptr<KnackContributionProbe>(
            new KnackContributionProbe(instance, trigger));
    }

    KnackContributionProbe(const Instance& instance, const char* trigger_)
        : device{instance.GetDevice()}, trigger{trigger_},
          relaxed_supported{instance.IsDynamicColorWriteMaskSupported()} {
        const char* shader = std::getenv("TLG_KNACK_CONTRIB_GS");
        if (shader && std::strcmp(shader, "any") == 0) {
            target_shader = 0;
        } else if (shader && shader[0]) {
            target_shader = std::strtoull(shader, nullptr, 0);
        }
        if (const char* frames = std::getenv("TLG_KNACK_CONTRIB_FRAMES")) {
            capture_frames = std::clamp<u32>(std::strtoul(frames, nullptr, 10), 1, 64);
        }
        if (const char* draws = std::getenv("TLG_KNACK_CONTRIB_MAX_DRAWS")) {
            max_draws = std::clamp<u32>(std::strtoul(draws, nullptr, 10), 1,
                                        QueriesPerSlice / (relaxed_supported ? 2 : 1));
        }
        const vk::QueryPoolCreateInfo info = {
            .queryType = vk::QueryType::eOcclusion,
            .queryCount = NumSlices * QueriesPerSlice,
        };
        pool = Check<"create Knack contribution query pool">(device.createQueryPoolUnique(info));
        device.resetQueryPool(*pool, 0, NumSlices * QueriesPerSlice);
        for (auto& slice_entries : entries) {
            slice_entries.reserve(QueriesPerSlice);
        }
        std::printf("KNACK-PX: Armed relaxed=%u target=%s trigger=%s\n", u32(relaxed_supported),
                    target_shader ? fmt::format("{:08x}", target_shader).c_str() : "any", trigger);
    }

    bool Matches(u64 shader) const {
        return target_shader == 0 || shader == target_shader;
    }

    std::optional<Token> Allocate(u64 ps, u32 instances) {
        if (!recording || draw_index >= max_draws) {
            return std::nullopt;
        }
        const u32 need = relaxed_supported ? 2 : 1;
        if (used[slice] + need > QueriesPerSlice) {
            Fail("query_capacity");
            return std::nullopt;
        }
        const u32 draw = draw_index++;
        const u32 original = slice * QueriesPerSlice + used[slice]++;
        entries[slice].push_back({ps, instances, draw, false});
        u32 relaxed = 0;
        if (relaxed_supported) {
            relaxed = slice * QueriesPerSlice + used[slice]++;
            entries[slice].push_back({ps, instances, draw, true});
        }
        ++matching_draws;
        submitted_queries += need;
        if (!matching_reported) {
            matching_reported = true;
            std::printf("KNACK-PX: Matching draws found frame=%llu target=%s\n",
                        static_cast<unsigned long long>(frame),
                        target_shader ? fmt::format("{:08x}", target_shader).c_str() : "any");
        }
        return Token{original, relaxed, relaxed_supported};
    }

    vk::QueryPool Pool() const {
        return *pool;
    }

    void BeginFrame() {
        const u64 next_frame = frame + 1;
        if (recording) {
            if (used[slice]) {
                pending[slice] = true;
                frame_ids[slice] = frame;
                std::printf("KNACK-PX: Queries submitted frame=%llu queries=%u\n",
                            static_cast<unsigned long long>(frame), used[slice]);
            }
            if (remaining) {
                --remaining;
            }
            recording = remaining != 0;
            if (!recording) {
                completing = true;
            }
        }

        slice = (slice + 1) % NumSlices;
        if (pending[slice]) {
            Collect(slice);
        }
        if (used[slice]) {
            device.resetQueryPool(*pool, slice * QueriesPerSlice, used[slice]);
        }
        used[slice] = 0;
        entries[slice].clear();
        pending[slice] = false;

        CompleteIfReady();
        if (!recording && !completing) {
            CheckTrigger(next_frame);
        }
        frame = next_frame;
        draw_index = 0;
    }

private:
    struct Entry {
        u64 ps;
        u32 instances;
        u32 draw;
        bool relaxed;
    };

    KnackContributionProbe(const KnackContributionProbe&) = delete;
    KnackContributionProbe& operator=(const KnackContributionProbe&) = delete;

    void CheckTrigger(u64 next_frame) {
        std::error_code error;
        if (!std::filesystem::exists(trigger, error)) {
            if (error) {
                Fail(fmt::format("trigger_stat:{}", error.message()));
            }
            return;
        }
        std::printf("KNACK-PX: Trigger detected frame=%llu path=%s\n",
                    static_cast<unsigned long long>(next_frame), trigger);
        if (std::remove(trigger) != 0) {
            Fail(fmt::format("trigger_remove:{}", std::strerror(errno)));
            return;
        }
        remaining = capture_frames;
        recording = true;
        failed = false;
        matching_reported = false;
        matching_draws = submitted_queries = collected_queries = collected_frames = 0;
        std::printf("KNACK-PX: Capture started frame=%llu frames=%u target=%s\n",
                    static_cast<unsigned long long>(next_frame), capture_frames,
                    target_shader ? fmt::format("{:08x}", target_shader).c_str() : "any");
    }

    void Fail(const std::string& reason) {
        if (!failed) {
            failed = true;
            failure_reason = reason;
            std::printf("KNACK-PX: Capture failed reason=%s\n", reason.c_str());
        }
    }

    void CompleteIfReady() {
        if (!completing || std::ranges::any_of(pending, [](bool value) { return value; })) {
            return;
        }
        completing = false;
        if (matching_draws == 0) {
            Fail("no_matching_draws");
            return;
        }
        if (collected_queries != submitted_queries) {
            Fail(fmt::format("incomplete_results:{}/{}", collected_queries, submitted_queries));
            return;
        }
        if (failed) {
            return;
        }
        std::printf("KNACK-PX: Capture completed frames=%llu matching_draws=%llu queries=%llu\n",
                    static_cast<unsigned long long>(collected_frames),
                    static_cast<unsigned long long>(matching_draws),
                    static_cast<unsigned long long>(collected_queries));
        std::fflush(stdout);
    }

    void Collect(u32 which) {
        const u32 count = used[which];
        std::vector<u64> samples(count);
        const auto result = device.getQueryPoolResults(
            *pool, which * QueriesPerSlice, count, count * sizeof(u64), samples.data(), sizeof(u64),
            vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
        if (result != vk::Result::eSuccess) {
            Fail(fmt::format("query_results:{}", vk::to_string(result)));
            return;
        }
        collected_queries += count;
        ++collected_frames;
        std::printf("KNACK-PX: Results collected frame=%llu queries=%u\n",
                    static_cast<unsigned long long>(frame_ids[which]), count);
        for (u32 i = 0; i < count; ++i) {
            const auto& entry = entries[which][i];
            std::printf("KNACK-PX: draw=%u mode=%c ps=%08llx inst=%u samples=%llu\n", entry.draw,
                        entry.relaxed ? 'R' : 'O', static_cast<unsigned long long>(entry.ps),
                        entry.instances, static_cast<unsigned long long>(samples[i]));
        }
        std::fflush(stdout);
    }

    static constexpr u32 NumSlices = 4;
    static constexpr u32 QueriesPerSlice = 512;

    vk::Device device;
    const char* trigger;
    bool relaxed_supported = false;
    u64 target_shader = KnackDetachedGeometryShader;
    u32 capture_frames = 16;
    u32 max_draws = 256;
    vk::UniqueQueryPool pool;
    u32 slice = 0;
    u32 remaining = 0;
    u32 draw_index = 0;
    bool recording = false;
    bool completing = false;
    bool failed = false;
    bool matching_reported = false;
    u64 frame = 0;
    u64 matching_draws = 0;
    u64 submitted_queries = 0;
    u64 collected_queries = 0;
    u64 collected_frames = 0;
    std::string failure_reason;
    std::array<std::vector<Entry>, NumSlices> entries;
    std::array<u32, NumSlices> used{};
    std::array<bool, NumSlices> pending{};
    std::array<u64, NumSlices> frame_ids{};
};

std::unique_ptr<KnackContributionProbe> knack_contribution_probe;

bool KnackSmallSyncEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("TLG_KNACK_SMALL_SYNC");
        return value && value[0] == '1';
    }();
    return enabled;
}

std::atomic<bool> knack_small_sync_pending{true};

// Opt-in visual locator for deferred/G-buffer draws. It repeats matching draws inside a small
// scissor with all color, depth and stencil writes disabled, then uses an occlusion query to say
// which exact pipelines rasterized visible samples in that screen region. Original draws and all
// five G-buffer attachments stay untouched.
class KnackVisualProbe {
public:
    struct Token {
        u32 query;
    };

    static std::unique_ptr<KnackVisualProbe> Create(const Instance& instance) {
        const char* trigger = std::getenv("TLG_KNACK_VIS_TRIGGER");
        if (!trigger || !trigger[0]) {
            return {};
        }
        const char* roi_text = std::getenv("TLG_KNACK_VIS_ROI");
        s32 x = 0;
        s32 y = 0;
        u32 width = 0;
        u32 height = 0;
        if (!roi_text || std::sscanf(roi_text, "%d,%d,%u,%u", &x, &y, &width, &height) != 4 ||
            width == 0 || height == 0) {
            std::printf("KNACK-VIS: Capture failed reason=invalid_roi expected=x,y,width,height\n");
            return {};
        }
        if (!instance.IsDynamicColorWriteMaskSupported()) {
            std::printf("KNACK-VIS: Capture failed reason=dynamic_color_write_mask_unsupported\n");
            return {};
        }
        return std::unique_ptr<KnackVisualProbe>(
            new KnackVisualProbe(instance, trigger, vk::Rect2D{{x, y}, {width, height}}));
    }

    bool Matches(AmdGpu::PrimitiveType primitive, u32 render_targets) const {
        return u32(primitive) == target_primitive && render_targets == target_render_targets;
    }

    std::optional<Token> Allocate(u64 pipeline, u64 vs, u64 gs, u64 ps,
                                  AmdGpu::PrimitiveType primitive, u32 render_targets,
                                  u32 indices, u32 instances) {
        if (!recording) {
            return std::nullopt;
        }
        const u32 candidate = candidate_index++;
        if (candidate < first_draw || candidate - first_draw >= max_draws) {
            return std::nullopt;
        }
        if (used[slice] == QueriesPerSlice) {
            Fail("query_capacity");
            return std::nullopt;
        }
        const u32 query = slice * QueriesPerSlice + used[slice]++;
        entries[slice].push_back({pipeline, vs, gs, ps, u32(primitive), render_targets, indices,
                                  instances, candidate});
        ++matching_draws;
        ++submitted_queries;
        if (!matching_reported) {
            matching_reported = true;
            std::printf("KNACK-VIS: Matching draws found frame=%llu prim=%u rts=%u\n",
                        static_cast<unsigned long long>(frame), target_primitive,
                        target_render_targets);
        }
        return Token{query};
    }

    vk::QueryPool Pool() const {
        return *pool;
    }

    Scissors ClipScissors(const Scissors& original) const {
        Scissors clipped = original;
        const s64 roi_x0 = roi.offset.x;
        const s64 roi_y0 = roi.offset.y;
        const s64 roi_x1 = roi_x0 + roi.extent.width;
        const s64 roi_y1 = roi_y0 + roi.extent.height;
        for (auto& scissor : clipped) {
            const s64 x0 = std::max<s64>(scissor.offset.x, roi_x0);
            const s64 y0 = std::max<s64>(scissor.offset.y, roi_y0);
            const s64 x1 = std::min<s64>(s64(scissor.offset.x) + scissor.extent.width, roi_x1);
            const s64 y1 = std::min<s64>(s64(scissor.offset.y) + scissor.extent.height, roi_y1);
            scissor.offset = vk::Offset2D{s32(x0), s32(y0)};
            scissor.extent = vk::Extent2D{u32(std::max<s64>(x1 - x0, 0)),
                                         u32(std::max<s64>(y1 - y0, 0))};
        }
        return clipped;
    }

    void BeginFrame() {
        const u64 next_frame = frame + 1;
        if (recording) {
            if (used[slice]) {
                pending[slice] = true;
                frame_ids[slice] = frame;
                std::printf("KNACK-VIS: Queries submitted frame=%llu queries=%u\n",
                            static_cast<unsigned long long>(frame), used[slice]);
            }
            if (remaining) {
                --remaining;
            }
            recording = remaining != 0;
            if (!recording) {
                completing = true;
            }
        }

        slice = (slice + 1) % NumSlices;
        if (pending[slice]) {
            Collect(slice);
        }
        if (used[slice]) {
            device.resetQueryPool(*pool, slice * QueriesPerSlice, used[slice]);
        }
        used[slice] = 0;
        entries[slice].clear();
        pending[slice] = false;

        CompleteIfReady();
        if (!recording && !completing) {
            CheckTrigger(next_frame);
        }
        frame = next_frame;
        candidate_index = 0;
    }

private:
    struct Entry {
        u64 pipeline;
        u64 vs;
        u64 gs;
        u64 ps;
        u32 primitive;
        u32 render_targets;
        u32 indices;
        u32 instances;
        u32 draw;
    };

    KnackVisualProbe(const Instance& instance, const char* trigger_, vk::Rect2D roi_)
        : device{instance.GetDevice()}, trigger{trigger_}, roi{roi_} {
        if (const char* primitive = std::getenv("TLG_KNACK_VIS_PRIM")) {
            target_primitive = std::strtoul(primitive, nullptr, 0);
        }
        if (const char* targets = std::getenv("TLG_KNACK_VIS_RTS")) {
            target_render_targets = std::strtoul(targets, nullptr, 0);
        }
        if (const char* frames = std::getenv("TLG_KNACK_VIS_FRAMES")) {
            capture_frames = std::clamp<u32>(std::strtoul(frames, nullptr, 10), 1, 4);
        }
        if (const char* first = std::getenv("TLG_KNACK_VIS_FIRST_DRAW")) {
            first_draw = std::strtoul(first, nullptr, 10);
        }
        if (const char* draws = std::getenv("TLG_KNACK_VIS_MAX_DRAWS")) {
            max_draws = std::clamp<u32>(std::strtoul(draws, nullptr, 10), 1, QueriesPerSlice);
        }
        const vk::QueryPoolCreateInfo info = {
            .queryType = vk::QueryType::eOcclusion,
            .queryCount = NumSlices * QueriesPerSlice,
        };
        pool = Check<"create Knack visual query pool">(device.createQueryPoolUnique(info));
        device.resetQueryPool(*pool, 0, NumSlices * QueriesPerSlice);
        for (auto& slice_entries : entries) {
            slice_entries.reserve(QueriesPerSlice);
        }
        std::printf("KNACK-VIS: Armed prim=%u rts=%u roi=%d,%d,%u,%u first=%u max=%u trigger=%s\n",
                    target_primitive, target_render_targets, roi.offset.x, roi.offset.y,
                    roi.extent.width, roi.extent.height, first_draw, max_draws, trigger);
    }

    void CheckTrigger(u64 next_frame) {
        std::error_code error;
        if (!std::filesystem::exists(trigger, error)) {
            if (error) {
                Fail(fmt::format("trigger_stat:{}", error.message()));
            }
            return;
        }
        // Optional request contents replace the ROI for this capture. Plain "visual" keeps the
        // armed ROI. This permits several object probes in one gameplay session.
        if (FILE* request = std::fopen(trigger, "r")) {
            s32 x = 0;
            s32 y = 0;
            u32 width = 0;
            u32 height = 0;
            if (std::fscanf(request, "%d,%d,%u,%u", &x, &y, &width, &height) == 4 && width &&
                height) {
                roi = vk::Rect2D{{x, y}, {width, height}};
            }
            std::fclose(request);
        }
        std::printf("KNACK-VIS: Trigger detected frame=%llu path=%s\n",
                    static_cast<unsigned long long>(next_frame), trigger);
        if (std::remove(trigger) != 0) {
            Fail(fmt::format("trigger_remove:{}", std::strerror(errno)));
            return;
        }
        remaining = capture_frames;
        recording = true;
        failed = false;
        matching_reported = false;
        matching_draws = submitted_queries = collected_queries = collected_frames = visible_draws =
            0;
        std::printf("KNACK-VIS: Capture started frame=%llu frames=%u prim=%u rts=%u roi=%d,%d,%u,%u\n",
                    static_cast<unsigned long long>(next_frame), capture_frames, target_primitive,
                    target_render_targets, roi.offset.x, roi.offset.y, roi.extent.width,
                    roi.extent.height);
    }

    void Fail(const std::string& reason) {
        if (!failed) {
            failed = true;
            std::printf("KNACK-VIS: Capture failed reason=%s\n", reason.c_str());
        }
    }

    void CompleteIfReady() {
        if (!completing || std::ranges::any_of(pending, [](bool value) { return value; })) {
            return;
        }
        completing = false;
        if (matching_draws == 0) {
            Fail("no_matching_draws");
            return;
        }
        if (collected_queries != submitted_queries) {
            Fail(fmt::format("incomplete_results:{}/{}", collected_queries, submitted_queries));
            return;
        }
        if (failed) {
            return;
        }
        std::printf("KNACK-VIS: Capture completed frames=%llu matching_draws=%llu visible_draws=%llu queries=%llu\n",
                    static_cast<unsigned long long>(collected_frames),
                    static_cast<unsigned long long>(matching_draws),
                    static_cast<unsigned long long>(visible_draws),
                    static_cast<unsigned long long>(collected_queries));
        std::fflush(stdout);
    }

    void Collect(u32 which) {
        const u32 count = used[which];
        std::vector<u64> samples(count);
        const auto result = device.getQueryPoolResults(
            *pool, which * QueriesPerSlice, count, count * sizeof(u64), samples.data(), sizeof(u64),
            vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
        if (result != vk::Result::eSuccess) {
            Fail(fmt::format("query_results:{}", vk::to_string(result)));
            return;
        }
        collected_queries += count;
        ++collected_frames;
        std::printf("KNACK-VIS: Results collected frame=%llu queries=%u\n",
                    static_cast<unsigned long long>(frame_ids[which]), count);
        for (u32 i = 0; i < count; ++i) {
            const auto& entry = entries[which][i];
            visible_draws += samples[i] != 0;
            std::printf("KNACK-VIS: draw=%u pipe=%016llx vs=%08llx gs=%08llx ps=%08llx prim=%u rts=%u idx=%u inst=%u samples=%llu\n",
                        entry.draw, static_cast<unsigned long long>(entry.pipeline),
                        static_cast<unsigned long long>(entry.vs),
                        static_cast<unsigned long long>(entry.gs),
                        static_cast<unsigned long long>(entry.ps), entry.primitive,
                        entry.render_targets, entry.indices, entry.instances,
                        static_cast<unsigned long long>(samples[i]));
        }
        std::fflush(stdout);
    }

    static constexpr u32 NumSlices = 4;
    static constexpr u32 QueriesPerSlice = 2048;

    vk::Device device;
    const char* trigger;
    vk::Rect2D roi;
    u32 target_primitive = u32(AmdGpu::PrimitiveType::TriangleList);
    u32 target_render_targets = 5;
    u32 capture_frames = 1;
    u32 first_draw = 0;
    u32 max_draws = 1024;
    vk::UniqueQueryPool pool;
    u32 slice = 0;
    u32 remaining = 0;
    u32 candidate_index = 0;
    bool recording = false;
    bool completing = false;
    bool failed = false;
    bool matching_reported = false;
    u64 frame = 0;
    u64 matching_draws = 0;
    u64 submitted_queries = 0;
    u64 collected_queries = 0;
    u64 collected_frames = 0;
    u64 visible_draws = 0;
    std::array<std::vector<Entry>, NumSlices> entries;
    std::array<u32, NumSlices> used{};
    std::array<bool, NumSlices> pending{};
    std::array<u64, NumSlices> frame_ids{};
};

std::unique_ptr<KnackVisualProbe> knack_visual_probe;

} // namespace

static Shader::PushData MakeUserData(const AmdGpu::Regs& regs) {
    // TODO(roamic): Add support for multiple viewports and geometry shaders when ViewportIndex
    // is encountered and implemented in the recompiler.
    Shader::PushData push_data{};
    push_data.xoffset = regs.viewport_control.xoffset_enable ? regs.viewports[0].xoffset : 0.f;
    push_data.xscale = regs.viewport_control.xscale_enable ? regs.viewports[0].xscale : 1.f;
    push_data.yoffset = regs.viewport_control.yoffset_enable ? regs.viewports[0].yoffset : 0.f;
    push_data.yscale = regs.viewport_control.yscale_enable ? regs.viewports[0].yscale : 1.f;
    return push_data;
}

Rasterizer::Rasterizer(const Instance& instance_, Scheduler& scheduler_, Runtime& runtime_,
                       AmdGpu::Liverpool* liverpool_)
    : instance{instance_}, scheduler{scheduler_}, runtime{runtime_}, page_manager{this},
      buffer_cache{instance, scheduler, runtime, liverpool_, texture_cache, page_manager},
      texture_cache{instance, scheduler, runtime, liverpool_, buffer_cache, page_manager},
      liverpool{liverpool_}, memory{Core::Memory::Instance()},
      pipeline_cache{instance, scheduler, liverpool, buffer_cache.GetSparsePageShift()},
      host_markers_enabled{EmulatorSettings.IsVkHostMarkersEnabled()},
      guest_markers_enabled{EmulatorSettings.IsVkGuestMarkersEnabled()} {
    // Before the rasterizer is bound: Liverpool enqueues buffers only once it sees it.
    draw_prep = std::make_unique<DrawPreparation>(pipeline_cache);
    scene_targets = std::make_unique<SceneTargets>(instance, scheduler, runtime, texture_cache);
    // Object motion first: it fixes the buffer addresses the motion shader variants embed.
    object_motion = std::make_unique<ObjectMotion>(instance, scheduler);
    camera_motion = std::make_unique<CameraMotion>(instance, scheduler, texture_cache, runtime);
    camera_motion->SetObjectMotion(object_motion.get());
    upscaler = std::make_unique<TemporalUpscaler>(instance, scheduler, texture_cache, runtime,
                                                  *camera_motion, *scene_targets);
    if (!EmulatorSettings.IsNullGPU()) {
        liverpool->BindRasterizer(this);
    }
    memory->SetRasterizer(this);

    GpuProfiler::Init(instance, scheduler);
    knack_contribution_probe = KnackContributionProbe::Create(instance);
    knack_visual_probe = KnackVisualProbe::Create(instance);
    FrameRecord::Attach(instance, scheduler);
    if (DrawPipeWanted()) {
        constant_ring = std::make_unique<ConstantRing>(instance, scheduler);
        draw_pipe = std::make_unique<DrawPipe>(&RunDrawPacket, this);
        scheduler.SetOwnerHook(&SchedulerOwnerHook, this);
    }
    // tlg: this thread joins the texture binding helper before it changes image state.
    runtime.SetImageAccessHook(&JoinBindHelper, this);
    scheduler.SetSubmitCallback([this](Vulkan::SubmitInfo& info) {
        runtime.FlushBarriers();
        buffer_cache.SubmitPendingArenaBinds(info);
    });
}

Rasterizer::~Rasterizer() {
    knack_contribution_probe.reset();
    knack_visual_probe.reset();
}

inline const AmdGpu::Regs& Rasterizer::Regs() const {
    return stage_regs ? *stage_regs : liverpool->regs;
}

inline AmdGpu::CbDbExtent Rasterizer::CbExtent(u32 index) const {
    return stage_regs ? pipe_cb_extent[index] : liverpool->last_cb_extent[index];
}

inline const AmdGpu::ComputeProgram& Rasterizer::CsRegs() const {
    return stage_cs ? *stage_cs : liverpool->GetCsRegs();
}

inline AmdGpu::CbDbExtent Rasterizer::DbExtent() const {
    return stage_regs ? pipe_db_extent : liverpool->last_db_extent;
}

namespace {
/// magic_enum::enum_contains, from a table: it scans every enumerator (per texture per draw).
bool IsKnownFormat(AmdGpu::DataFormat data_fmt, AmdGpu::NumberFormat num_fmt) {
    static const auto tables = [] {
        std::pair<std::array<bool, 256>, std::array<bool, 256>> t{};
        for (const auto value : magic_enum::enum_values<AmdGpu::DataFormat>()) {
            if (static_cast<u32>(value) < 256) {
                t.first[static_cast<u32>(value)] = true;
            }
        }
        for (const auto value : magic_enum::enum_values<AmdGpu::NumberFormat>()) {
            if (static_cast<u32>(value) < 256) {
                t.second[static_cast<u32>(value)] = true;
            }
        }
        return t;
    }();
    const u32 d = static_cast<u32>(data_fmt), n = static_cast<u32>(num_fmt);
    return d < 256 && n < 256 && tables.first[d] && tables.second[n];
}
} // namespace

bool Rasterizer::FilterDraw() {
    const auto& regs = Regs();
    if (regs.color_control.mode == AmdGpu::ColorControl::OperationMode::EliminateFastClear) {
        // Clears the render target if FCE is launched before any draws
        EliminateFastClear();
        return false;
    }
    if (regs.color_control.mode == AmdGpu::ColorControl::OperationMode::FmaskDecompress) {
        // TODO: check for a valid MRT1 to promote the draw to the resolve pass.
        LOG_TRACE(Render_Vulkan, "FMask decompression pass skipped");
        ScopedMarkerInsert("FmaskDecompress");
        return false;
    }
    if (regs.color_control.mode == AmdGpu::ColorControl::OperationMode::Resolve) {
        LOG_TRACE(Render_Vulkan, "Resolve pass");
        Resolve();
        return false;
    }
    if (regs.primitive_type == AmdGpu::PrimitiveType::None) {
        LOG_TRACE(Render_Vulkan, "Primitive type 'None' skipped");
        ScopedMarkerInsert("PrimitiveTypeNone");
        return false;
    }

    const bool cb_disabled =
        regs.color_control.mode == AmdGpu::ColorControl::OperationMode::Disable;
    const auto depth_copy =
        regs.depth_render_override.force_z_dirty && regs.depth_render_override.force_z_valid &&
        regs.depth_buffer.DepthValid() && regs.depth_buffer.DepthWriteValid() &&
        regs.depth_buffer.DepthAddress() != regs.depth_buffer.DepthWriteAddress();
    const auto stencil_copy =
        regs.depth_render_override.force_stencil_dirty &&
        regs.depth_render_override.force_stencil_valid && regs.depth_buffer.StencilValid() &&
        regs.depth_buffer.StencilWriteValid() &&
        regs.depth_buffer.StencilAddress() != regs.depth_buffer.StencilWriteAddress();
    if (cb_disabled && (depth_copy || stencil_copy)) {
        // Games may disable color buffer and enable force depth/stencil dirty and valid to
        // do a copy from one depth-stencil surface to another, without a pixel shader.
        // We need to detect this case and perform the copy, otherwise it will have no effect.
        LOG_TRACE(Render_Vulkan, "Performing depth-stencil override copy");
        DepthStencilCopy(depth_copy, stencil_copy);
        return false;
    }

    return true;
}

template <typename... Parts>
VideoCore::ImageId Rasterizer::FindTargetMemoized(VideoCore::TextureCache::ImageDesc& desc,
                                                  LastTarget& last, auto&& make_desc,
                                                  const Parts&... parts) {
    static_assert((sizeof(Parts) + ...) <= 256, "target memo key too large");
    std::array<u8, 256> key{};
    u32 key_size = 0;
    const auto append = [&](const auto& part) {
        std::memcpy(key.data() + key_size, &part, sizeof(part));
        key_size += sizeof(part);
    };
    (append(parts), ...);
    const u64 generation = texture_cache.RegistryGeneration();
    const bool memo_enabled = !TlgToggle::Disabled(TlgToggle::TextureBindingMemo);
    if (memo_enabled && last.generation == generation && last.key_size == key_size &&
        std::memcmp(last.key.data(), key.data(), key_size) == 0) {
        texture_cache.MarkFound(last.image_id);
        return last.image_id;
    }
    const auto remember = [&](VideoCore::ImageId image_id) {
        last.key = key;
        last.key_size = key_size;
        last.generation = generation;
        last.image_id = image_id;
    };
    u64 hash = 0xCBF29CE484222325ull;
    for (u32 i = 0; i < key_size; i += 8) {
        u64 word;
        std::memcpy(&word, key.data() + i, 8);
        hash = (hash ^ word) * 0x100000001B3ull;
        hash ^= hash >> 29;
    }
    auto& memo = target_memo[hash % target_memo.size()];
    if (memo.generation == generation && memo.key_size == key_size &&
        std::memcmp(memo.key.data(), key.data(), key_size) == 0 && memo_enabled) {
        desc = memo.desc;
        texture_cache.MarkFound(memo.image_id);
        remember(memo.image_id);
        return memo.image_id;
    }
    make_desc();
    const VideoCore::ImageId image_id = texture_cache.FindImage(desc);
    if (generation == texture_cache.RegistryGeneration()) {
        memo.key = key;
        memo.key_size = key_size;
        memo.generation = generation;
        memo.image_id = image_id;
        memo.desc = desc;
        remember(image_id);
    } else {
        last.generation = ~0ULL;
    }
    return image_id;
}

void Rasterizer::PrepareRenderState(const GraphicsPipeline* pipeline) {
    // Prefetch render targets to handle overlaps with bound textures (e.g. mipgen)
    const auto& key = pipeline->GetGraphicsKey();
    const auto& regs = Regs();
    if (regs.color_control.degamma_enable) {
        LOG_WARNING(Render_Vulkan, "Color buffers require gamma correction");
    }

    const bool skip_cb_binding =
        regs.color_control.mode == AmdGpu::ColorControl::OperationMode::Disable;
    for (s32 cb = 0; cb < std::bit_width(key.mrt_mask); ++cb) {
        auto& [image_id, desc] = cb_descs[cb];
        const auto& col_buf = regs.color_buffers[cb];
        const u32 target_mask = regs.color_target_mask.GetMask(cb);
        if (skip_cb_binding || !col_buf || !target_mask || (key.mrt_mask & (1 << cb)) == 0) {
            image_id = {};
            continue;
        }
        const auto& hint = CbExtent(cb);
        const u32 tag = 1;
        image_id = bound_images.emplace_back(FindTargetMemoized(
            desc, last_targets[cb], [&] { std::construct_at(&desc, col_buf, hint); }, tag,
            col_buf, hint));
        auto& image = texture_cache.GetImage(image_id);
        image.binding.is_target = 1u;
    }

    if (GetEffectiveDepthStencilState(regs).needs_attachment) {
        const auto htile_address = regs.depth_htile_data_base.GetAddress();
        const auto& hint = DbExtent();
        auto& [image_id, desc] = db_desc;
        const u32 tag = 2;
        image_id = bound_images.emplace_back(FindTargetMemoized(
            desc, last_targets[AmdGpu::NUM_COLOR_BUFFERS],
            [&] {
                std::construct_at(&desc, regs.depth_buffer, regs.depth_view, regs.depth_control,
                                  htile_address, hint);
            },
            tag, regs.depth_buffer, regs.depth_view, regs.depth_control, htile_address, hint));
        auto& image = texture_cache.GetImage(image_id);
        image.binding.is_target = 1u;
    } else {
        db_desc.first = {};
    }
    // tlg: the G-buffer pass (5+ color targets) holds the scene depth, and its constants the
    // main camera (shadow passes bind the same layout with the light's camera).
    gbuffer_draw = camera_motion->Enabled() && std::popcount(key.mrt_mask) >= 5 && db_desc.first;
    if (gbuffer_draw) {
        camera_motion->OnGBufferPass(db_desc.first);
    }
    if (upscaler->Enabled() && cb_descs[0].first) {
        upscaler->OnColorTarget(cb_descs[0].first);
    }
    // tlg: scene color: a full-size RGBA16F target drawn with the scene depth.
    if (upscaler->Enabled() && db_desc.first && db_desc.first == camera_motion->Depth() &&
        cb_descs[0].first && std::popcount(key.mrt_mask) <= 2) {
        const auto& color = texture_cache.GetImage(cb_descs[0].first);
        const auto& depth = texture_cache.GetImage(db_desc.first);
        if (color.info.pixel_format == vk::Format::eR16G16B16A16Sfloat &&
            color.info.size.width == depth.info.size.width &&
            color.info.size.height == depth.info.size.height) {
            upscaler->OnSceneColor(cb_descs[0].first);
            // Blended geometry without depth writes (not full-screen passes): transparents and
            // effects start. Blended layers writing depth (decals, wet/blood ground) are
            // surfaces and stay in the history.
            if (regs.blend_control[0].enable && !regs.color_buffers[0].info.blend_bypass &&
                !regs.depth_control.depth_write_enable &&
                (regs.num_indices > 6 || regs.num_instances.NumInstances() > 1)) {
                upscaler->OnBlendedSceneDraw();
            } else if (regs.num_indices <= 6 && regs.num_instances.NumInstances() <= 1) {
                // A full-screen pass over the scene (the fog composite): the transparents are in.
                upscaler->OnSceneComposite();
            }
        }
    }
}

static std::pair<u32, u32> GetDrawOffsets(
    const AmdGpu::Regs& regs, const Shader::Info& info,
    const std::optional<Shader::Gcn::FetchShaderData>& fetch_shader) {
    u32 vertex_offset = regs.index_offset;
    u32 instance_offset = 0;
    if (fetch_shader) {
        if (vertex_offset == 0 && fetch_shader->vertex_offset_sgpr != -1) {
            vertex_offset = info.UserData()[fetch_shader->vertex_offset_sgpr];
        }
        if (fetch_shader->instance_offset_sgpr != -1) {
            instance_offset = info.UserData()[fetch_shader->instance_offset_sgpr];
        }
    }
    return {vertex_offset, instance_offset};
}

void Rasterizer::EliminateFastClear() {
    auto& col_buf = Regs().color_buffers[0];
    if (!col_buf || !col_buf.info.fast_clear) {
        return;
    }
    VideoCore::TextureCache::ImageDesc desc(col_buf, CbExtent(0));
    const auto image_id = texture_cache.FindImage(desc);
    const auto& image_view = texture_cache.FindRenderTarget(image_id, desc);
    if (!texture_cache.IsMetaCleared(col_buf.CmaskAddress(), col_buf.view.slice_start)) {
        return;
    }
    for (u32 slice = col_buf.view.slice_start; slice <= col_buf.view.slice_max; ++slice) {
        texture_cache.TouchMeta(col_buf.CmaskAddress(), slice, false);
    }
    auto& image = texture_cache.GetImage(image_id);
    const auto clear_value = LiverpoolToVK::ColorBufferClearValue(col_buf);

    ScopeMarkerBegin(fmt::format("EliminateFastClear:MRT={:#x}:M={:#x}", col_buf.Address(),
                                 col_buf.CmaskAddress()));
    runtime.ClearImage(&image, desc.view_info.range, clear_value);
    ScopeMarkerEnd();
}

// tlg: two-stage draw pipeline (vk_draw_pipe.h). Packet layout, 8-byte aligned parts:
// DrawPacket, u16 block indices, u32 block words (RegDirty::BlockWords each), then per stage a
// PacketStage with its user data and flattened user data, then (verification) the full file.
namespace {
enum class PacketKind : u32 { Draw, Task, Dispatch, Special, Indirect, IndirectDispatch };
struct TaskPacket {
    PacketKind kind;
    u32 size;
    Rasterizer::OrderedTask task;
};
struct DrawPacket {
    PacketKind kind;
    const Pipeline* pipeline;
    const PreparedDraw* prepared;
    u32 index_offset;
    u8 is_indexed;
    u8 regs_reset;
    u16 num_blocks;
    u32 num_stages;
    u32 verify;
    std::array<AmdGpu::CbDbExtent, AmdGpu::NUM_COLOR_BUFFERS> cb_extent;
    AmdGpu::CbDbExtent db_extent;
    u64 ring_end; ///< constant ring position after this packet's copies
};
struct PacketStage {
    const Shader::Info* info;
    VAddr pgm_base;
    u32 ud_size;
    u32 flat_size;
    u32 num_ring;
    u32 pad;
};
constexpr u32 AlignPacket(u32 size) {
    return (size + 7) & ~7u;
}
u32 VerifyInterval() {
    static const u32 interval = [] {
        const char* env = std::getenv("TLG_PIPE_VERIFY");
        return env ? static_cast<u32>(std::strtoul(env, nullptr, 10)) : 0u;
    }();
    return interval;
}
} // namespace

bool Rasterizer::DrawPipeWanted() {
    const char* env = std::getenv("TLG_DRAW_PIPE");
    if (env && env[0]) {
        return env[0] == '1';
    }
    // Stage B spins while draws flow. Measured ahead with 16 threads (+19%) and with 4 cores /
    // 8 threads (taskset, Steam Deck-like: +18%).
    return TlgThreads::Available() >= 8;
}

bool Rasterizer::UseDrawPipe() const {
    // tlg: off while a frame is recorded: everything then runs in stream order on this thread.
    return draw_pipe && !host_markers_enabled && !TlgToggle::Disabled(TlgToggle::DrawPipeline) &&
           !FrameRecord::Active();
}

bool Rasterizer::OnStageA() const {
#ifdef _WIN32
    // tlg (Windows): libc++ compares std::thread::id through winpthreads' pthread_equal,
    // which asks the kernel (GetThreadId) for both threads: two system calls per check, many
    // per draw. The numeric thread id comes from the TEB.
    return static_cast<u32>(gettid()) == liverpool->GetGpuCommandProcessorThreadId();
#else
    return std::this_thread::get_id() == liverpool->GetGpuCommandProcessorThread();
#endif
}

namespace {
/// Stage A: cycles waited per drain site (TLG_FRAME_STATS), keyed by function name and line.
std::unordered_map<const char*, std::unordered_map<u32, u64>> drain_sites;
} // namespace

void Rasterizer::SchedulerOwnerHook(void* context, void* caller) {
    auto* self = static_cast<Rasterizer*>(context);
    const bool stage_a = self->OnStageA();
    if (!self->draw_pipe || (stage_a && self->draw_pipe->Idle())) {
        return;
    }
    // Each caller is reported once: the site that skipped DrainDrawPipe.
    static std::mutex reported_mutex;
    static std::unordered_set<void*> reported;
    bool report;
    {
        std::scoped_lock lk{reported_mutex};
        report = reported.insert(caller).second;
    }
    if (report) {
        const char* module = "?";
        const char* name = "?";
        uintptr_t offset = reinterpret_cast<uintptr_t>(caller);
#ifndef _WIN32
        Dl_info info{};
        if (dladdr(caller, &info)) {
            module = info.dli_fname ? info.dli_fname : module;
            name = info.dli_sname ? info.dli_sname : name;
            offset -= reinterpret_cast<uintptr_t>(info.dli_fbase);
        }
#endif
        std::fprintf(stderr,
                     "tlg: scheduler used %s while draw packets are in flight, from %s+%#lx "
                     "(%s)%s\n",
                     stage_a ? "on the GPU command thread" : "by another thread", module,
                     static_cast<unsigned long>(offset), name, stage_a ? "; draining first" : "");
    }
    if (stage_a) {
        self->DrainDrawPipe(DrawPipe::ReasonRasterizer, __LINE__, __func__);
    }
}

void Rasterizer::DrainDrawPipe(u32 reason, u32 line, const char* function) {
    if (!draw_pipe || !OnStageA()) {
        return;
    }
    static const bool stats = std::getenv("TLG_FRAME_STATS") != nullptr;
    if (!stats) {
        draw_pipe->Drain(reason);
        return;
    }
    const u64 before = draw_pipe->drain_cycles;
    draw_pipe->Drain(reason);
    if (const u64 waited = draw_pipe->drain_cycles - before) {
        drain_sites[function][line] += waited;
    }
}

bool Rasterizer::IsGpuSideThread() const {
    return OnStageA() || DrawPipe::OnStageB();
}

bool Rasterizer::IsGpuSideThreadId(u32 tid) const {
    // The userfaultfd thread handles the fault while the faulting thread waits: without locks
    // only if that thread owns the caches. The GPU command thread does not while the draw
    // recording thread may be running, so its faults take the locked path of guest threads.
    if (draw_pipe) {
        return tid == draw_pipe->StageBThreadId();
    }
    return tid == liverpool->GetGpuCommandProcessorThreadId();
}

void Rasterizer::NotePendingGpuWrite(VAddr address, u64 size) {
    if (!draw_pipe || !size) {
        return;
    }
    // Draws write the same buffers over and over: an entry for the range only moves on.
    const u64 position = draw_pipe->Head();
    const VAddr end = address + size;
    for (auto& write : pending_writes) {
        if (address <= write.end && write.begin <= end) {
            write.begin = std::min(write.begin, address);
            write.end = std::max(write.end, end);
            write.position = position;
            pending_min = std::min(pending_min, address);
            pending_max = std::max(pending_max, end);
            return;
        }
    }
    pending_writes.push_back({address, end, position});
    pending_min = std::min(pending_min, address);
    pending_max = std::max(pending_max, end);
}

bool Rasterizer::PendingWriteOverlaps(VAddr address, u64 size) {
    if (pending_writes.empty() || address >= pending_max || address + size <= pending_min) {
        return false;
    }
    // Now and then drop entries whose writes the recording thread has done: the bounds shrink.
    if ((++pending_checks & 63) == 0) {
        std::erase_if(pending_writes, [&](const PendingWrite& write) {
            return draw_pipe->Reached(write.position);
        });
        pending_min = ~VAddr{0};
        pending_max = 0;
        for (const auto& write : pending_writes) {
            pending_min = std::min(pending_min, write.begin);
            pending_max = std::max(pending_max, write.end);
        }
    }
    const VAddr end = address + size;
    for (const auto& write : pending_writes) {
        if (address < write.end && write.begin < end && !draw_pipe->Reached(write.position)) {
            return true;
        }
    }
    return false;
}

void Rasterizer::CollectRingBindings(const Shader::Info& stage, const PreparedDraw* prepared,
                           boost::container::static_vector<RingBinding, Shader::NUM_BUFFERS>& out) {
    const PreparedStage* prepared_stage = nullptr;
    if (prepared && !TlgToggle::Disabled(TlgToggle::PreparedResources)) {
        for (u32 i = 0; i < prepared->num_stages; ++i) {
            const auto& candidate = prepared->stages[i];
            if (&candidate.program->info == &stage &&
                candidate.num_buffers == stage.buffers.size()) {
                prepared_stage = &candidate;
                break;
            }
        }
    }
    const u64 alignment = std::max<u64>(instance.StorageMinAlignment(),
                                        instance.UniformMinAlignment());
    const auto copy = [&](u32 index, const void* source, VAddr address, u64 size) {
        const auto offset = constant_ring->Allocate(size, alignment);
        if (!offset) {
            return;
        }
        u8* dst = constant_ring->Data(*offset);
        if (source) {
            std::memcpy(dst, source, size);
        } else {
            memory->CopySparseMemory(address, dst, size);
        }
        constant_ring->Flush(*offset, size);
        out.push_back({index, static_cast<u32>(size), *offset, address});
    };
    for (u32 index = 0; index < stage.buffers.size(); ++index) {
        const auto& desc = stage.buffers[index];
        if (desc.IsSpecial()) {
            if (desc.buffer_type == Shader::BufferType::Flatbuf &&
                !stage.flattened_ud_buf.empty()) {
                copy(index, stage.flattened_ud_buf.data(), 0,
                     stage.flattened_ud_buf.size() * sizeof(u32));
            }
            continue;
        }
        const auto vsharp = prepared_stage ? prepared_stage->buffer_sharps[index]
                                           : desc.GetSharp(stage);
        const VAddr address = vsharp.base_address;
        if (address == 0 || vsharp.GetSize() == 0) {
            continue;
        }
        const u64 size = memory->ClampRangeSize(address, vsharp.GetSize());
        if (desc.is_written) {
            NotePendingGpuWrite(address, size);
            continue;
        }
        // The stream path of BufferCache::ObtainBuffer, taken here: small, read-only, not
        // written by the GPU (now or by work still queued for the recording thread).
        if (size == 0 || size > VideoCore::BufferCache::STREAM_THRESHOLD ||
            buffer_cache.IsRegionGpuModified(address, size) || PendingWriteOverlaps(address, size)) {
            continue;
        }
        copy(index, nullptr, address, size);
    }
}

void Rasterizer::PostDraw(const Pipeline* pipeline, const PreparedDraw* used_prepared,
                          bool is_indexed, u32 index_offset, const AmdGpu::ComputeProgram* cs,
                          const IndirectDraw* indirect) {
    auto& dirty = liverpool->pipe_dirty;
    if (!pipe_synced) {
        // Stage B has not run yet: its register copy starts as this one.
        pipe_regs = liverpool->regs;
        dirty.Clear();
        pipe_synced = true;
    }
    std::array<u16, AmdGpu::RegDirty::NumBlocks> blocks;
    u32 num_blocks = 0;
#ifdef __GLIBCXX__
    for (size_t block = dirty.blocks._Find_first(); block < dirty.blocks.size();
         block = dirty.blocks._Find_next(block)) {
        blocks[num_blocks++] = static_cast<u16>(block);
    }
#else
    if (dirty.blocks.any()) {
        for (size_t block = 0; block < dirty.blocks.size(); ++block) {
            if (dirty.blocks.test(block)) {
                blocks[num_blocks++] = static_cast<u16>(block);
            }
        }
    }
#endif
    const auto stages =
        pipeline ? pipeline->GetStages() : std::span<const Shader::Info* const>{};
    // Constants: copied here into the ring, the recording thread only binds them.
    thread_local std::array<boost::container::static_vector<RingBinding, Shader::NUM_BUFFERS>,
                            Shader::MaxStageTypes>
        rings;
    u32 num_stages = 0;
    u32 size = sizeof(DrawPacket) + AlignPacket(num_blocks * sizeof(u16)) +
               num_blocks * AmdGpu::RegDirty::BlockWords * sizeof(u32);
    for (const auto* stage : stages) {
        if (stage) {
            auto& ring = rings[num_stages];
            ring.clear();
            if (constant_ring && !TlgToggle::Disabled(TlgToggle::ConstantRing)) {
                CollectRingBindings(*stage, used_prepared, ring);
            }
            ++num_stages;
            size += sizeof(PacketStage) +
                    AlignPacket(u32(stage->user_data.size() + stage->flattened_ud_buf.size()) *
                                sizeof(u32)) +
                    u32(ring.size() * sizeof(RingBinding));
        }
    }
    if (cs) {
        size += AlignPacket(sizeof(AmdGpu::ComputeProgram));
    }
    if (indirect) {
        size += AlignPacket(sizeof(IndirectDraw));
    }
    const u32 interval = VerifyInterval();
    const bool verify = interval && (draw_pipe->packets % interval) == 0;
    if (verify) {
        size += sizeof(AmdGpu::Regs);
    }

    u8* out = draw_pipe->Begin(size);
    u8* const start = out;
    auto& packet = *reinterpret_cast<DrawPacket*>(out);
    packet.kind = cs         ? (indirect ? PacketKind::IndirectDispatch : PacketKind::Dispatch)
                  : indirect ? PacketKind::Indirect
                  : pipeline ? PacketKind::Draw
                             : PacketKind::Special;
    packet.pipeline = pipeline;
    packet.prepared = used_prepared;
    packet.index_offset = index_offset;
    packet.is_indexed = is_indexed;
    packet.regs_reset = dirty.reset;
    packet.num_blocks = static_cast<u16>(num_blocks);
    packet.num_stages = num_stages;
    packet.verify = verify;
    packet.cb_extent = liverpool->last_cb_extent;
    packet.db_extent = liverpool->last_db_extent;
    packet.ring_end = constant_ring ? constant_ring->Position() : 0;
    out += sizeof(DrawPacket);
    std::memcpy(out, blocks.data(), num_blocks * sizeof(u16));
    out += AlignPacket(num_blocks * sizeof(u16));
    for (u32 i = 0; i < num_blocks; ++i) {
        std::memcpy(out,
                    liverpool->regs.reg_array.data() + blocks[i] * AmdGpu::RegDirty::BlockWords,
                    AmdGpu::RegDirty::BlockWords * sizeof(u32));
        out += AmdGpu::RegDirty::BlockWords * sizeof(u32);
    }
    u32 stage_index = 0;
    for (const auto* stage : stages) {
        if (!stage) {
            continue;
        }
        const auto& ring = rings[stage_index++];
        auto& header = *reinterpret_cast<PacketStage*>(out);
        header = {stage,
                  stage->pgm_base,
                  static_cast<u32>(stage->user_data.size()),
                  static_cast<u32>(stage->flattened_ud_buf.size()),
                  static_cast<u32>(ring.size()),
                  0};
        out += sizeof(PacketStage);
        std::memcpy(out, ring.data(), ring.size() * sizeof(RingBinding));
        out += ring.size() * sizeof(RingBinding);
        std::memcpy(out, stage->user_data.data(), stage->user_data.size_bytes());
        std::memcpy(out + stage->user_data.size_bytes(), stage->flattened_ud_buf.data(),
                    stage->flattened_ud_buf.size() * sizeof(u32));
        out += AlignPacket(u32(stage->user_data.size() + stage->flattened_ud_buf.size()) *
                           sizeof(u32));
    }
    if (cs) {
        std::memcpy(out, cs, sizeof(AmdGpu::ComputeProgram));
        out += AlignPacket(sizeof(AmdGpu::ComputeProgram));
    }
    if (indirect) {
        std::memcpy(out, indirect, sizeof(IndirectDraw));
        out += AlignPacket(sizeof(IndirectDraw));
    }
    if (verify) {
        std::memcpy(out, &liverpool->regs, sizeof(AmdGpu::Regs));
        out += sizeof(AmdGpu::Regs);
    }
    dirty.Clear();
    draw_pipe->Commit(static_cast<u32>(out - start));
    PrintPipeStats();
}

void Rasterizer::RetireSubmission() {
    if (!draw_pipe || !OnStageA()) {
        return;
    }
    while (!pipe_keepalive.empty() && draw_pipe->Reached(pipe_keepalive.front().first)) {
        pipe_keepalive.pop_front();
    }
    if (!draw_pipe->Idle()) {
        pipe_keepalive.emplace_back(draw_pipe->Head(), draw_prep->KeepCurrent());
    }
}

bool Rasterizer::RunInOrder(OrderedTask task, const void* data, u32 size, u64 toggle) {
    if (!UseDrawPipe() || TlgToggle::Disabled(toggle)) {
        DrainDrawPipe(DrawPipe::ReasonRasterizer);
        task(*this, static_cast<const u8*>(data));
        return false;
    }
    const u32 total = sizeof(TaskPacket) + AlignPacket(size);
    u8* out = draw_pipe->Begin(total);
    *reinterpret_cast<TaskPacket*>(out) = {PacketKind::Task, size, task};
    std::memcpy(out + sizeof(TaskPacket), data, size);
    draw_pipe->Commit(total);
    return true;
}

namespace {
void RunCommandTask(Rasterizer&, const u8* data) {
    auto* command = *reinterpret_cast<Common::UniqueFunction<void>* const*>(data);
    (*command)();
    delete command;
}
} // namespace

bool Rasterizer::RunCommandInOrder(Common::UniqueFunction<void>&& command) {
    if (!UseDrawPipe() || !OnStageA() || TlgToggle::Disabled(TlgToggle::PipelinedCommands)) {
        return false;
    }
    auto* heap = new Common::UniqueFunction<void>(std::move(command));
    if (!RunInOrder(&RunCommandTask, &heap, sizeof(heap), TlgToggle::PipelinedCommands)) {
        return true; // ran here after a drain (RunInOrder's fallback), and freed by the task
    }
    return true;
}

void Rasterizer::RunDrawPacket(void* context, const u8* data, u32 size) {
    auto& self = *static_cast<Rasterizer*>(context);
    if (*reinterpret_cast<const PacketKind*>(data) == PacketKind::Task) {
        const auto& task = *reinterpret_cast<const TaskPacket*>(data);
        task.task(self, data + sizeof(TaskPacket));
        return;
    }
    const auto& packet = *reinterpret_cast<const DrawPacket*>(data);
    const u8* in = data + sizeof(DrawPacket);
    const auto* blocks = reinterpret_cast<const u16*>(in);
    in += AlignPacket(packet.num_blocks * sizeof(u16));
    auto& regs = self.pipe_regs;
    if (packet.regs_reset) {
        regs.SetDefaults();
    }
    for (u32 i = 0; i < packet.num_blocks; ++i) {
        std::memcpy(regs.reg_array.data() + blocks[i] * AmdGpu::RegDirty::BlockWords, in,
                    AmdGpu::RegDirty::BlockWords * sizeof(u32));
        in += AmdGpu::RegDirty::BlockWords * sizeof(u32);
    }
    ASSERT(packet.num_stages <= Shader::Info::MaxUdSnapshots);
    for (u32 i = 0; i < packet.num_stages; ++i) {
        const auto& stage = *reinterpret_cast<const PacketStage*>(in);
        in += sizeof(PacketStage);
        self.ring_stages[i] = {stage.info, reinterpret_cast<const RingBinding*>(in),
                               stage.num_ring};
        in += stage.num_ring * sizeof(RingBinding);
        const auto* words = reinterpret_cast<const u32*>(in);
        Shader::Info::ud_snapshots[i] = {stage.info, words, stage.ud_size, words + stage.ud_size,
                                         stage.flat_size, stage.pgm_base};
        in += AlignPacket((stage.ud_size + stage.flat_size) * sizeof(u32));
    }
    Shader::Info::num_ud_snapshots = packet.num_stages;
    self.num_ring_stages = packet.num_stages;
    const AmdGpu::ComputeProgram* cs = nullptr;
    if (packet.kind == PacketKind::Dispatch || packet.kind == PacketKind::IndirectDispatch) {
        cs = reinterpret_cast<const AmdGpu::ComputeProgram*>(in);
        in += AlignPacket(sizeof(AmdGpu::ComputeProgram));
    }
    const IndirectDraw* indirect = nullptr;
    if (packet.kind == PacketKind::Indirect || packet.kind == PacketKind::IndirectDispatch) {
        indirect = reinterpret_cast<const IndirectDraw*>(in);
        in += AlignPacket(sizeof(IndirectDraw));
    }
    if (packet.verify) {
        // The resource tables the GPU thread walked must read the same now: else a write
        // still queued here (WriteData, DMA) changed them after that thread read them.
        static u32 flat_reports = 0;
        for (u32 i = 0; i < packet.num_stages && flat_reports < 32; ++i) {
            const auto& snapshot = Shader::Info::ud_snapshots[i];
            const auto& srt = snapshot.info->srt_info;
            // Stages the pipeline selection did not refresh (no user data) keep their tables.
            if (!srt.walker_func || snapshot.flat_size == 0 || snapshot.user_data_size == 0) {
                continue;
            }
            std::vector<u32> flat(snapshot.flat_size);
            std::memcpy(flat.data(), snapshot.user_data,
                        std::min(snapshot.user_data_size, snapshot.flat_size) * sizeof(u32));
            // Pointers in the tables may be stale by now: a fault only skips the check.
            sigjmp_buf recover;
            if (sigsetjmp(recover, 0)) {
                runtime_fault_recover = nullptr;
                continue;
            }
            runtime_fault_recover = &recover;
            srt.walker_func(snapshot.user_data, flat.data());
            runtime_fault_recover = nullptr;
            if (std::memcmp(flat.data(), snapshot.flat, snapshot.flat_size * sizeof(u32)) != 0) {
                u32 first = 0, count = 0;
                for (u32 w = 0; w < snapshot.flat_size; ++w) {
                    if (flat[w] != snapshot.flat[w]) {
                        first = count++ ? first : w;
                    }
                }
                std::printf("Draw pipe: resource tables of shader %016llx changed after the GPU "
                            "thread read them (%u of %u words, first %u: %#x -> %#x, user data "
                            "%u words)\n",
                            static_cast<unsigned long long>(snapshot.info->pgm_hash), count,
                            snapshot.flat_size, first, snapshot.flat[first], flat[first],
                            snapshot.user_data_size);
                ++flat_reports;
            }
        }
        const auto* live = reinterpret_cast<const AmdGpu::Regs*>(in);
        in += sizeof(AmdGpu::Regs);
        static u32 reports = 0;
        for (u32 block = 0; block < AmdGpu::RegDirty::NumBlocks && reports < 32; ++block) {
            const u32 at = block * AmdGpu::RegDirty::BlockWords;
            if (std::memcmp(regs.reg_array.data() + at, live->reg_array.data() + at,
                            AmdGpu::RegDirty::BlockWords * sizeof(u32)) != 0) {
                for (u32 w = at; w < at + AmdGpu::RegDirty::BlockWords; ++w) {
                    if (regs.reg_array[w] != live->reg_array[w]) {
                        std::printf("Draw pipe: register copy differs at word %#x (%#x vs %#x)\n",
                                    w, regs.reg_array[w], live->reg_array[w]);
                        ++reports;
                        break;
                    }
                }
            }
        }
    }
    ASSERT(static_cast<u32>(in - data) == size);
    stage_regs = &regs;
    self.pipe_cb_extent = packet.cb_extent;
    self.pipe_db_extent = packet.db_extent;
    if (packet.kind == PacketKind::Special) {
        // A draw FilterDraw handles (fast clear elimination, resolve, depth copy, skip).
        FrameCapture::Poll();
        self.scheduler.PopPendingOperations();
        self.FilterDraw();
    } else if (cs) {
        stage_cs = cs;
        const auto* pipeline = static_cast<const ComputePipeline*>(packet.pipeline);
        if (indirect) {
            self.DispatchIndirectRecord(pipeline, indirect->args, indirect->stride);
        } else {
            self.DispatchRecord(pipeline);
        }
        stage_cs = nullptr;
    } else if (indirect) {
        self.DrawIndirectRecord(static_cast<const GraphicsPipeline*>(packet.pipeline),
                                packet.is_indexed, *indirect);
    } else {
        self.DrawRecord(static_cast<const GraphicsPipeline*>(packet.pipeline), packet.prepared,
                        packet.is_indexed, packet.index_offset);
    }
    Shader::Info::num_ud_snapshots = 0;
    self.num_ring_stages = 0;
    if (self.constant_ring) {
        self.constant_ring->Stamp(packet.ring_end);
    }
}

void Rasterizer::PrintPipeStats() {
    static const bool stats = std::getenv("TLG_FRAME_STATS") != nullptr;
    if (!stats || (draw_pipe->packets & 1023) != 0) {
        return;
    }
    static auto window = std::chrono::steady_clock::now();
    static u64 last_packets = 0, last_drains = 0, last_drain_cycles = 0, last_busy = 0;
    static u64 last_tsc = __rdtsc();
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - window).count();
    if (seconds < 5.0) {
        return;
    }
    const u64 tsc = __rdtsc();
    const double cycles = double(tsc - last_tsc);
    const u64 busy = draw_pipe->busy_cycles.load(std::memory_order_relaxed);
    std::printf("Draw pipe: %.0f draws/s pipelined, %.0f drains/s that waited, stage A waited "
                "%.1f%%, stage B busy %.1f%%\n",
                (draw_pipe->packets - last_packets) / seconds,
                (draw_pipe->drains - last_drains) / seconds,
                100.0 * (draw_pipe->drain_cycles - last_drain_cycles) / cycles,
                100.0 * (busy - last_busy) / cycles);
    std::printf("  drains by reason/s:");
    for (u32 r = 0; r < DrawPipe::NumReasons; ++r) {
        if (const u64 n = draw_pipe->drains_by_reason[r]) {
            static constexpr const char* names[] = {"ConstRam", "commands", "compute",
                                                    "submission end", "draw", "rasterizer"};
            const double share = 100.0 * draw_pipe->cycles_by_reason[r] / cycles;
            draw_pipe->cycles_by_reason[r] = 0;
            if (r < 256) {
                std::printf(" %s=%.0f (%.1f%%)",
                            magic_enum::enum_name(AmdGpu::PM4ItOpcode(r)).data(), n / seconds,
                            share);
            } else {
                std::printf(" %s=%.0f (%.1f%%)", names[r - 256], n / seconds, share);
            }
        }
    }
    std::printf("\n  stage A waits over 0.5%%:");
    for (const auto& [function, lines] : drain_sites) {
        for (const auto& [line, waited] : lines) {
            if (waited * 200 > cycles) {
                std::printf(" %s:%u %.1f%%", function, line, 100.0 * waited / cycles);
            }
        }
    }
    std::printf("\n");
    drain_sites.clear();
    draw_pipe->drains_by_reason = {};
    window = now;
    last_tsc = tsc;
    last_packets = draw_pipe->packets;
    last_drains = draw_pipe->drains;
    last_drain_cycles = draw_pipe->drain_cycles;
    last_busy = busy;
}

bool Rasterizer::FilterDrawPasses() const {
    // FilterDraw's checks without its side effects: false when it would skip the draw or run
    // a pass of its own (fast clear elimination, resolve, depth/stencil copy).
    const auto& regs = Regs();
    using Mode = AmdGpu::ColorControl::OperationMode;
    const auto mode = regs.color_control.mode;
    if (mode == Mode::EliminateFastClear || mode == Mode::FmaskDecompress ||
        mode == Mode::Resolve || regs.primitive_type == AmdGpu::PrimitiveType::None) {
        return false;
    }
    const bool depth_copy =
        regs.depth_render_override.force_z_dirty && regs.depth_render_override.force_z_valid &&
        regs.depth_buffer.DepthValid() && regs.depth_buffer.DepthWriteValid() &&
        regs.depth_buffer.DepthAddress() != regs.depth_buffer.DepthWriteAddress();
    const bool stencil_copy =
        regs.depth_render_override.force_stencil_dirty &&
        regs.depth_render_override.force_stencil_valid && regs.depth_buffer.StencilValid() &&
        regs.depth_buffer.StencilWriteValid() &&
        regs.depth_buffer.StencilAddress() != regs.depth_buffer.StencilWriteAddress();
    return !(mode == Mode::Disable && (depth_copy || stencil_copy));
}

void Rasterizer::Draw(bool is_indexed, u32 index_offset, const PreparedDraw* prepared) {
    RENDERER_TRACE;
    TlgStats::draws.fetch_add(1, std::memory_order_relaxed);

    // tlg: with the draw pipeline this thread only selects the pipeline and hands the draw
    // to the recording thread (DrawRecord there); draws FilterDraw handles itself run here.
    const bool pipelined = UseDrawPipe();
    if (GpuPhy03::TraceEnabled()) GpuPhy03::TraceFront(Regs(), 1, is_indexed, pipelined);
    if (pipelined && !FilterDrawPasses() &&
        !TlgToggle::Disabled(TlgToggle::PipelinedMemoryWrites)) {
        if (GpuPhy03::TraceEnabled()) GpuPhy03::TraceFront(Regs(), 6, is_indexed, pipelined);
        PostDraw(nullptr, nullptr, false, 0);
        return;
    }
    if (!pipelined || !FilterDrawPasses()) {
        DrainDrawPipe(DrawPipe::ReasonDraw);
        FrameCapture::Poll();
        scheduler.PopPendingOperations();
        if (!FilterDraw()) {
            if (GpuPhy03::TraceEnabled()) GpuPhy03::TraceFront(Regs(), 2, is_indexed, pipelined);
            return;
        }
    }
    const GraphicsPipeline* pipeline = pipeline_cache.GetGraphicsPipeline({}, prepared);
    const PreparedDraw* used_prepared = pipeline_cache.UsedPrepared();
    if (prepared) {
        draw_prep->Count(pipeline != nullptr &&
                         prepared->state.load(std::memory_order_acquire) == PreparedDraw::Ready &&
                         prepared->reg_checksum == liverpool->gfx_reg_checksum);
    }
    if (!pipeline) {
        if (GpuPhy03::TraceEnabled()) GpuPhy03::TraceFront(Regs(), 3, is_indexed, pipelined);
        return;
    }
    // tlg: TLG_SKIP_ADJACENCY=1 drops draws with adjacency topology (geometry-shader outlines),
    // to tell whether a broken effect comes from them.
    static const bool skip_adjacency = std::getenv("TLG_SKIP_ADJACENCY") != nullptr;
    if (skip_adjacency) {
        const auto prim = pipeline->GetGraphicsKey().prim_type;
        if (prim == AmdGpu::PrimitiveType::AdjTriangleList ||
            prim == AmdGpu::PrimitiveType::AdjTriangleStrip) {
            if (GpuPhy03::TraceEnabled()) GpuPhy03::TraceFront(Regs(), 4, is_indexed, pipelined, pipeline);
            return;
        }
    }
    if (GpuPhy03::TraceEnabled()) GpuPhy03::TraceFront(Regs(), 5, is_indexed, pipelined, pipeline);
    if (pipelined && FilterDrawPasses()) {
        PostDraw(pipeline, used_prepared, is_indexed, index_offset);
        return;
    }
    DrawRecord(pipeline, used_prepared, is_indexed, index_offset);
}

void Rasterizer::DiagPassBoundary(bool force) {
    static const bool enabled = [] {
        const char* env = std::getenv("TLG_DIAG_IMAGES");
        return env && *env == '1';
    }();
    if (!enabled) {
        return;
    }
    const auto& regs = Regs();
    std::array<VAddr, 8> addresses{};
    for (u32 cb = 0; cb < addresses.size(); ++cb) {
        if (regs.color_buffers[cb]) {
            addresses[cb] = regs.color_buffers[cb].Address();
        }
    }
    if (!force && addresses == diag_addresses) {
        return;
    }
    if (FrameCapture::IsRecording() && !diag_targets.empty()) {
        char tag[16];
        std::snprintf(tag, sizeof(tag), "p%03u", diag_pass++);
        const auto targets = diag_targets;
        for (const auto image_id : targets) {
            texture_cache.DiagDumpImage(image_id, tag);
        }
    }
    diag_targets.clear();
    diag_addresses = force ? std::array<VAddr, 8>{} : addresses;
}

void Rasterizer::DrawRecord(const GraphicsPipeline* pipeline, const PreparedDraw* used_prepared,
                            bool is_indexed, u32 index_offset) {
    if (DrawPipe::OnStageB()) {
        FrameCapture::Poll();
        scheduler.PopPendingOperations();
    }
    DiagPassBoundary(false);
    const auto& regs = Regs();
    if (GpuPhy03::TraceEnabled()) GpuPhy03::TraceDraw(pipeline, regs, {}, push_data, 1, is_indexed,
        regs.num_indices, regs.num_instances.NumInstances(), 0, 0, index_offset, scheduler.CurrentTick());
    // Controlled A/B only: wait before recording this draw's Vulkan resource bindings.
    // Scheduler::Finish() submits the current command buffer; placing it after BindResources
    // would separate descriptors/vertex/index state from the draw on the new command buffer.
    // Diagnostic only: a positive result does not identify a missing upload dependency.
    const bool knack_small_draw =
        pipeline->GetStage(Shader::SwStage::Vertex).pgm_hash == KnackSmallGeometryVertexShader;
    if (knack_small_draw && KnackSmallSyncEnabled() &&
        knack_small_sync_pending.exchange(false, std::memory_order_relaxed)) [[unlikely]] {
        scheduler.Finish();
        std::printf("KNACK-SYNC: target wait completed vs=%08llx\n",
                    static_cast<unsigned long long>(KnackSmallGeometryVertexShader));
    }
    // tlg: the pass copying a finished frame to a display buffer; the previous draw's
    // target is that frame.
    const bool display_pass = regs.color_buffers[0] &&
                              FrameCapture::IsDisplayBuffer(regs.color_buffers[0].Address());
    if (camera_motion->Enabled() && display_pass) {
        scene_started = false;
        camera_motion->OnDisplayPass(cb_descs[0].first);
        if (upscaler->OnFrameStart()) {
            object_motion->InvalidateHistory();
            camera_motion->InvalidateHistory();
        }
        camera_motion->SetJitter(upscaler->Jitter());
        object_motion->OnFrameStart();
        NoteFrameStart();
    }
    bind_prepared = used_prepared;
    motion_draw = pipeline->GetGraphicsKey().motion_vectors;
    motion_geometry = 0;

    PrepareRenderState(pipeline);
    if (upscaler->Enabled() && std::popcount(pipeline->GetGraphicsKey().mrt_mask) == 1) {
        const auto& viewport = Regs().viewports[0];
        upscaler->OnDraw(pipeline->GetStage(Shader::SwStage::Vertex).pgm_hash,
                         cb_descs[0].first, db_desc.first,
                         UiComposition::NativeViewport(viewport.xscale * 2, viewport.yscale * 2));
    }
    const PreparedDraw* draw_prepared = bind_prepared;
    // tlg: vertex and index buffers are resolved while the helper binds textures (their
    // commands are recorded after BeginRendering, as before).
    draw_inputs = {pipeline, draw_prepared, index_offset, is_indexed, true, false};
    const bool bound = BindResources(pipeline);
    bind_prepared = nullptr; // indirect draws and dispatches bind without prepared sharps
    const bool inputs_resolved = draw_inputs.resolved;
    draw_inputs.pending = false;
    if (!bound) {
        if (GpuPhy03::TraceEnabled()) GpuPhy03::TraceDraw(pipeline, regs, {}, push_data, 2, is_indexed,
            regs.num_indices, regs.num_instances.NumInstances(), 0, 0, index_offset, scheduler.CurrentTick());
        return;
    }
    const auto state = BeginRendering(pipeline);
    if (GpuPhy03::Recording()) [[unlikely]] {
        GpuPhy03::DrawTargets(texture_cache, pipeline, state, cb_descs, scheduler.CurrentTick());
    }
    // tlg: before anything of the draw itself is recorded, which has to go with it.
    SubmitIfWorkPiledUp(state);
    scheduler.CountWork();

    if (!inputs_resolved) {
        ResolveVertexBuffers(pipeline, draw_prepared);
        if (is_indexed) {
            ResolveIndexBuffer(index_offset);
        }
    }
    EmitVertexBuffers();
    if (is_indexed) {
        EmitIndexBuffer();
    }

    if (needs_barrier) {
        runtime.FlushBarriers();
    }

    // tlg: screen-space (clip disabled) draws into the upscaler's output-size images.
    push_data.xscale *= target_scale[0];
    push_data.xoffset *= target_scale[0];
    push_data.yscale *= target_scale[1];
    push_data.yoffset *= target_scale[1];
    if (motion_draw && motion_geometry) {
        const auto& vs = pipeline->GetStage(Shader::SwStage::Vertex);
        const auto [base_vertex, first_instance] =
            GetDrawOffsets(regs, vs, pipeline->GetFetchShader());
        const u32 index_size =
            regs.index_buffer_type.index_type == AmdGpu::IndexType::Index16 ? 2u : 4u;
        const VAddr index_address = is_indexed
            ? regs.index_base_address.Address<VAddr>() + u64(index_offset) * index_size : 0;
        // Without a character-size skeleton the draw is gated on its bone palette changing
        // since last frame (model constants follow the camera, the palette does not). The
        // check comes first: most gated draws are static world pieces with long index lists.
        static const bool all_motion = [] {
            const char* value = std::getenv("TLG_OBJECT_MOTION_ALL");
            return value && value[0] == '1';
        }();
        bool gated = !all_motion;
        u64 palette = 0;
        for (const auto& resource : vs.buffers) {
            if (resource.IsSpecial()) continue;
            const auto buffer = resource.GetSharp(vs);
            const u32 size = buffer.GetSize();
            if (!buffer.Valid() || buffer.GetStride() != 16) continue;
            const auto role = Motion::ClassifyBuffer(size);
            if (role == Motion::BufferRole::Skeleton) {
                gated = false;
                break;
            }
            const VAddr address = buffer.base_address;
            if (role == Motion::BufferRole::SmallSkeleton &&
                memory->IsValidGpuMapping(address, 0) &&
                memory->ClampRangeSize(address, size) == size) {
                palette = XXH3_64bits_withSeed(reinterpret_cast<const void*>(address), size,
                                               palette);
            }
        }
        push_data.motion_param = 0;
        if (!gated || object_motion->Moving({.shader = vs.pgm_hash,
                                             .geometry = motion_geometry,
                                             .indices = index_address,
                                             .index_count = regs.num_indices,
                                             .instances = regs.num_instances.NumInstances(),
                                             .first_instance = first_instance},
                                            palette)) {
            Motion::VertexRange range{base_vertex, regs.num_indices};
            u64 topology = 0;
            if (is_indexed) {
                range = {};
                const u64 bytes = u64(regs.num_indices) * index_size;
                if (index_address && memory->IsValidGpuMapping(index_address, 0) &&
                    memory->ClampRangeSize(index_address, bytes) == bytes) {
                    const bool restart = regs.enable_primitive_restart != 0;
                    const u32 count = regs.num_indices;
                    const auto scanned = object_motion->IndexRange(
                        {index_address, count, index_size, s32(base_vertex), restart}, [&] {
                            const auto* data = reinterpret_cast<const void*>(index_address);
                            const auto scan_range =
                                index_size == 2
                                    ? Motion::IndexedRange(
                                          std::span(static_cast<const u16*>(data), count),
                                          s32(base_vertex), restart)
                                    : Motion::IndexedRange(
                                          std::span(static_cast<const u32*>(data), count),
                                          s32(base_vertex), restart);
                            return Motion::IndexRangeCache::Result{scan_range,
                                                                   XXH3_64bits(data, bytes)};
                        });
                    range = scanned.range;
                    topology = scanned.topology;
                }
            }
            push_data.motion_param = object_motion->PrepareDraw({
                .shader = vs.pgm_hash,
                .geometry = motion_geometry,
                .indices = index_address,
                .topology = topology,
                .index_count = regs.num_indices,
                .instances = regs.num_instances.NumInstances(),
                .first_instance = first_instance,
                .vertices = range,
            });
        }
    }
    pipeline->BindResources(set_writes, push_data);
    // tlg: jitter geometry drawn with the scene depth, not full-screen passes (a shifted
    // full-screen quad leaves an edge column unwritten).
    draw_jitter = {};
    if (upscaler->Enabled() && db_desc.first && db_desc.first == camera_motion->Depth() &&
        (regs.num_indices > 6 || regs.num_instances.NumInstances() > 1)) {
        draw_jitter = upscaler->Jitter();
    }
    UpdateDynamicState(pipeline, is_indexed);
    const bool video_background = PrepareKnackVideo(pipeline, state, false);
    MarkPass(pipeline, state);
    if (FrameRecord::IsRecording()) [[unlikely]] {
        RecordFrameInputs(pipeline, draw_prepared, &state, is_indexed, index_offset, nullptr);
    }
    scheduler.BeginRendering(state);

    const auto& vs_info = pipeline->GetStage(Shader::SwStage::Vertex);
    const auto& fetch_shader = pipeline->GetFetchShader();
    const auto [vertex_offset, instance_offset] = GetDrawOffsets(regs, vs_info, fetch_shader);

    const vk::Pipeline handle = pipeline->Handle();
    const vk::Pipeline square = SquarePass(*pipeline);
    const u32 num_indices = regs.num_indices;
    const u32 num_instances = regs.num_instances.NumInstances();
    const u32 first_vertex = vertex_offset;
    const u32 first_instance = instance_offset;
    if (GpuPhy03::TraceEnabled()) GpuPhy03::TraceDraw(pipeline, regs, buffer_infos, push_data, 3, is_indexed,
        num_indices, num_instances, first_instance, first_vertex, index_offset, scheduler.CurrentTick());
    const auto* gs_info = pipeline->GetStages()[u32(Shader::SwStage::Geometry)];
    const auto* ps_info = pipeline->GetStages()[u32(Shader::SwStage::Fragment)];
    const auto& graphics_key = pipeline->GetGraphicsKey();
    const u32 num_colors = graphics_key.num_color_attachments;
    const auto contribution = knack_contribution_probe &&
                                      knack_contribution_probe->Matches(gs_info ? gs_info->pgm_hash
                                                                                : 0)
                                  ? knack_contribution_probe->Allocate(
                                        ps_info ? ps_info->pgm_hash : 0, num_instances)
                                  : std::nullopt;
    const vk::QueryPool contribution_pool =
        contribution ? knack_contribution_probe->Pool() : vk::QueryPool{};
    const bool ps_side_effects =
        ps_info &&
        (std::ranges::any_of(ps_info->buffers, [](const auto& desc) { return desc.is_written; }) ||
         std::ranges::any_of(ps_info->images, [](const auto& desc) { return desc.is_written; }));
    const u64 pipeline_hash = XXH3_64bits(&graphics_key, sizeof(graphics_key));
    const auto visual = knack_visual_probe && !ps_side_effects &&
                                knack_visual_probe->Matches(regs.primitive_type, num_colors)
                            ? knack_visual_probe->Allocate(
                                  pipeline_hash, vs_info.pgm_hash,
                                  gs_info ? gs_info->pgm_hash : 0,
                                  ps_info ? ps_info->pgm_hash : 0, regs.primitive_type, num_colors,
                                  num_indices, num_instances)
                            : std::nullopt;
    const vk::QueryPool visual_pool = visual ? knack_visual_probe->Pool() : vk::QueryPool{};
    const auto& dynamic_state = scheduler.GetDynamicState();
    const Scissors original_scissors = dynamic_state.scissors;
    const Scissors visual_scissors =
        visual ? knack_visual_probe->ClipScissors(original_scissors) : Scissors{};
    const bool depth_write = dynamic_state.depth_write_enabled;
    const u32 stencil_front_write_mask = dynamic_state.stencil_front_write_mask;
    const u32 stencil_back_write_mask = dynamic_state.stencil_back_write_mask;
    const auto depth_stencil = GetEffectiveDepthStencilState(regs);
    const auto cull_mode = LiverpoolToVK::IsPrimitiveCulled(regs.primitive_type)
                               ? LiverpoolToVK::CullMode(regs.polygon_control.CullingMode())
                               : vk::CullModeFlagBits::eNone;
    const bool rasterizer_discard = regs.clipper_control.dx_rasterization_kill;
    const auto write_masks = graphics_key.write_masks;
    scheduler.Record([=](vk::CommandBuffer cmdbuf) {
        if (video_background) ClearKnackVideoBackground(cmdbuf, state);
        const auto draw = [&] {
            if (is_indexed) {
                cmdbuf.drawIndexed(num_indices, num_instances, 0, s32(first_vertex),
                                   first_instance);
            } else {
                cmdbuf.draw(num_indices, num_instances, first_vertex, first_instance);
            }
        };
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, handle);
        if (contribution) {
            cmdbuf.beginQuery(contribution_pool, contribution->original, {});
        }
        draw();
        if (contribution) {
            cmdbuf.endQuery(contribution_pool, contribution->original);
        }
        if (square) [[unlikely]] {
            cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, square);
            draw();
        }
        if (contribution && contribution->has_relaxed) [[unlikely]] {
            const std::array<vk::ColorComponentFlags, AmdGpu::NUM_COLOR_BUFFERS> no_writes{};
            cmdbuf.setColorWriteMaskEXT(
                0, std::span<const vk::ColorComponentFlags>{no_writes.data(), num_colors});
            cmdbuf.setDepthTestEnable(false);
            cmdbuf.setStencilTestEnable(false);
            cmdbuf.setCullMode(vk::CullModeFlagBits::eNone);
            cmdbuf.setRasterizerDiscardEnable(false);
            cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, handle);
            cmdbuf.beginQuery(contribution_pool, contribution->relaxed, {});
            draw();
            cmdbuf.endQuery(contribution_pool, contribution->relaxed);
            cmdbuf.setDepthTestEnable(depth_stencil.depth_test_enable);
            cmdbuf.setStencilTestEnable(depth_stencil.stencil_test_enable);
            cmdbuf.setCullMode(cull_mode);
            cmdbuf.setRasterizerDiscardEnable(rasterizer_discard);
            cmdbuf.setColorWriteMaskEXT(
                0, std::span<const vk::ColorComponentFlags>{write_masks.data(), num_colors});
        }
        if (visual) [[unlikely]] {
            const std::array<vk::ColorComponentFlags, AmdGpu::NUM_COLOR_BUFFERS> no_writes{};
            cmdbuf.setColorWriteMaskEXT(
                0, std::span<const vk::ColorComponentFlags>{no_writes.data(), num_colors});
            cmdbuf.setDepthWriteEnable(false);
            cmdbuf.setStencilWriteMask(vk::StencilFaceFlagBits::eFrontAndBack, 0);
            cmdbuf.setScissorWithCount(visual_scissors);
            cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, handle);
            cmdbuf.beginQuery(visual_pool, visual->query, {});
            draw();
            cmdbuf.endQuery(visual_pool, visual->query);
            cmdbuf.setScissorWithCount(original_scissors);
            cmdbuf.setStencilWriteMask(vk::StencilFaceFlagBits::eFront,
                                       stencil_front_write_mask);
            cmdbuf.setStencilWriteMask(vk::StencilFaceFlagBits::eBack,
                                       stencil_back_write_mask);
            cmdbuf.setDepthWriteEnable(depth_write);
            cmdbuf.setColorWriteMaskEXT(
                0, std::span<const vk::ColorComponentFlags>{write_masks.data(), num_colors});
        }
    });
    if (FrameCapture::Active()) {
        FrameCapture::Draw(vs_info.pgm_hash, ps_info ? ps_info->pgm_hash : 0, num_indices,
                           num_instances);
        if (const auto& bc = regs.blend_control[0]; bc.enable && regs.color_buffers[0]) {
            char note[128];
            std::snprintf(note, sizeof(note), "\n  blend ps %08x idx %u: src %u dst %u func %u z %u%u",
                          u32(ps_info ? ps_info->pgm_hash : 0), num_indices,
                          u32(bc.color_src_factor),
                          u32(bc.color_dst_factor), u32(bc.color_func),
                          u32(regs.depth_control.depth_enable),
                          u32(regs.depth_control.depth_write_enable));
            FrameCapture::Note(note);
        }
    }
    DebugState.IncDrawCall();

    ResetBindings(false);
    scheduler.KickRecording();
}

void Rasterizer::DrawIndirect(bool is_indexed, VAddr arg_address, u32 offset, u32 stride,
                              u32 max_count, VAddr count_address, u16 vertex_sgpr_offset,
                              u16 instance_sgpr_offset) {
    RENDERER_TRACE;
    // tlg: like direct draws, handed to the recording thread after the pipeline selection
    // (else the GPU thread waited here for a whole frame of queued draws).
    const bool pipelined = UseDrawPipe() && FilterDrawPasses() &&
                           !TlgToggle::Disabled(TlgToggle::PipelinedIndirectDraws);
    if (!pipelined) {
        DrainDrawPipe();
        scheduler.PopPendingOperations();
        if (!FilterDraw()) {
            return;
        }
    }

    // tlg (i3-pc 7036810): arguments that no real draw comes near make the GPU hang until the
    // driver resets the device (an instance count in the billions from unrelated float data). Every
    // slot of a multi draw is checked, not just the first, when the arguments live in plain guest
    // memory; slots the GPU wrote (compute) are not in guest memory yet and are left to the GPU.
    // TLG_INDIRECT_CHECK=0 turns the check off. The i3-pc also draws from the checked copy even for
    // GPU-written slots (particle counts a frame old): not ported, it changes what is drawn.
    static const bool check_args = [] {
        const char* value = std::getenv("TLG_INDIRECT_CHECK");
        return !value || value[0] != '0';
    }();
    if (check_args && count_address == 0 && stride >= 16 && max_count != 0 &&
        !buffer_cache.IsRegionGpuModified(arg_address + offset, u64(stride) * max_count)) {
        constexpr u32 max_elements = 0x400000;
        constexpr u32 max_instances = 0x10000;
        constexpr u32 max_slots = 256;
        for (u32 slot = 0; slot < std::min(max_count, max_slots); ++slot) {
            std::array<u32, 2> args{};
            memory->CopySparseMemory(arg_address + offset + u64(slot) * stride,
                                     reinterpret_cast<u8*>(args.data()), sizeof(args));
            if (args[0] > max_elements || args[1] > max_instances) {
                static std::atomic<u32> dropped{0};
                if (dropped.fetch_add(1, std::memory_order_relaxed) < 32) {
                    LOG_WARNING(Render,
                                "tlg: dropped indirect draw at {:#x} slot {}: count {:#x}, "
                                "instances {:#x}",
                                arg_address + offset, slot, args[0], args[1]);
                }
                return;
            }
        }
    }
    const DrawIndirectParams params = {
        .vertex_sgpr_offset = vertex_sgpr_offset,
        .instance_sgpr_offset = instance_sgpr_offset,
        .is_indirect = true, // tlg: BP07, BP11
    };
    const GraphicsPipeline* pipeline = pipeline_cache.GetGraphicsPipeline(params);
    if (!pipeline) {
        return;
    }
    const IndirectDraw indirect = {arg_address + offset, count_address, stride, max_count};
    if (pipelined) {
        PostDraw(pipeline, nullptr, is_indexed, 0, nullptr, &indirect);
        return;
    }
    DrawIndirectRecord(pipeline, is_indexed, indirect);
}

void Rasterizer::DrawIndirectRecord(const GraphicsPipeline* pipeline, bool is_indexed,
                                    const IndirectDraw& indirect) {
    if (DrawPipe::OnStageB()) {
        FrameCapture::Poll();
        scheduler.PopPendingOperations();
    }
    if (GpuPhy03::TraceEnabled()) GpuPhy03::TraceDraw(pipeline, Regs(), {}, push_data, 5, is_indexed,
        0, 0, 0, 0, 0, scheduler.CurrentTick());
    const VAddr count_address = indirect.count;
    const u32 stride = indirect.stride;
    const u32 max_count = indirect.max_count;

    // Indirect arguments may be GPU-written: leave these draws on camera fallback.
    motion_draw = false;
    motion_geometry = 0;
    PrepareRenderState(pipeline);
    if (upscaler->Enabled() && std::popcount(pipeline->GetGraphicsKey().mrt_mask) == 1) {
        const auto& viewport = Regs().viewports[0];
        upscaler->OnDraw(pipeline->GetStage(Shader::SwStage::Vertex).pgm_hash,
                         cb_descs[0].first, db_desc.first,
                         UiComposition::NativeViewport(viewport.xscale * 2, viewport.yscale * 2));
    }
    if (!BindResources(pipeline)) {
        if (GpuPhy03::TraceEnabled()) GpuPhy03::TraceDraw(pipeline, Regs(), {}, push_data, 6, is_indexed,
            0, 0, 0, 0, 0, scheduler.CurrentTick());
        return;
    }
    const auto state = BeginRendering(pipeline);
    if (GpuPhy03::Recording()) [[unlikely]] {
        GpuPhy03::DrawTargets(texture_cache, pipeline, state, cb_descs, scheduler.CurrentTick());
    }
    // tlg: before anything of the draw itself is recorded, which has to go with it.
    SubmitIfWorkPiledUp(state);
    scheduler.CountWork();

    BindVertexBuffers(pipeline);
    if (is_indexed) {
        BindIndexBuffer();
    }

    const auto [buffer, base] =
        buffer_cache.ObtainBuffer(indirect.args, stride * max_count, false);
    needs_barrier |= runtime.IsBufferAccessed(buffer, base, stride * max_count);
    // tlg: BP10 (shadPS4 c09fa46a, #5262) record the fetch so a later writer gets a barrier.
    if (Common::Backports::On(Common::Backports::Id::VertexIndexBarriers)) {
        Common::Backports::Hit(Common::Backports::Id::VertexIndexBarriers);
        bound_buffers.push_back({buffer, base, static_cast<u32>(stride * max_count), vk::AccessFlagBits2::eIndirectCommandRead});
    }

    const VideoCore::Buffer* count_buffer;
    u64 count_offset;
    if (count_address != 0) {
        std::tie(count_buffer, count_offset) = buffer_cache.ObtainBuffer(count_address, 4, false);
        needs_barrier |= runtime.IsBufferAccessed(count_buffer, count_offset, 4);
        // tlg: BP10 (shadPS4 c09fa46a, #5262) record the fetch so a later writer gets a barrier.
        if (Common::Backports::On(Common::Backports::Id::VertexIndexBarriers)) {
            Common::Backports::Hit(Common::Backports::Id::VertexIndexBarriers);
            bound_buffers.push_back({count_buffer, count_offset, static_cast<u32>(4), vk::AccessFlagBits2::eIndirectCommandRead});
        }
    }

    if (needs_barrier) {
        runtime.FlushBarriers();
    }

    // tlg: screen-space (clip disabled) draws into the upscaler's output-size images.
    push_data.xscale *= target_scale[0];
    push_data.xoffset *= target_scale[0];
    push_data.yscale *= target_scale[1];
    push_data.yoffset *= target_scale[1];
    pipeline->BindResources(set_writes, push_data);
    draw_jitter = {};
    if (upscaler->Enabled() && db_desc.first && db_desc.first == camera_motion->Depth()) {
        draw_jitter = upscaler->Jitter();
    }
    UpdateDynamicState(pipeline, is_indexed);
    const bool video_background = PrepareKnackVideo(pipeline, state, true);
    MarkPass(pipeline, state);
    if (FrameRecord::IsRecording()) [[unlikely]] {
        RecordFrameInputs(pipeline, nullptr, &state, is_indexed, 0, &indirect);
    }
    scheduler.BeginRendering(state);

    ASSERT(stride == (is_indexed ? sizeof(VkDrawIndexedIndirectCommand)
                                 : sizeof(VkDrawIndirectCommand)));
    const vk::Pipeline handle = pipeline->Handle();
    const vk::Pipeline square = SquarePass(*pipeline);
    const vk::Buffer args = buffer->Handle();
    const u64 args_offset = base;
    const vk::Buffer counts = count_address != 0 ? count_buffer->Handle() : vk::Buffer{};
    const u64 counts_offset = count_address != 0 ? count_offset : 0;
    if (GpuPhy03::TraceEnabled()) GpuPhy03::TraceDraw(pipeline, Regs(), buffer_infos, push_data, 4, is_indexed,
        0, 0, 0, 0, 0, scheduler.CurrentTick(), args, args_offset, counts, max_count, stride);
    scheduler.Record([=](vk::CommandBuffer cmdbuf) {
        if (video_background) ClearKnackVideoBackground(cmdbuf, state);
        for (const vk::Pipeline bound : {handle, square}) {
            if (!bound) {
                break;
            }
            cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, bound);
            if (is_indexed) {
                if (counts) {
                    cmdbuf.drawIndexedIndirectCount(args, args_offset, counts, counts_offset, max_count,
                                                    stride);
                } else {
                    cmdbuf.drawIndexedIndirect(args, args_offset, max_count, stride);
                }
            } else if (counts) {
                cmdbuf.drawIndirectCount(args, args_offset, counts, counts_offset, max_count, stride);
            } else {
                cmdbuf.drawIndirect(args, args_offset, max_count, stride);
            }
        }
    });
    DebugState.IncDrawCall();

    ResetBindings(false);
    scheduler.KickRecording();
}

void Rasterizer::DispatchDirect() {
    RENDERER_TRACE;
    TlgStats::dispatches.fetch_add(1, std::memory_order_relaxed);
    // tlg: like draws, handed to the draw recording thread after the pipeline selection.
    const bool pipelined = UseDrawPipe() && !TlgToggle::Disabled(TlgToggle::PipelinedDispatch);
    if (!pipelined) {
        DrainDrawPipe();
    }
    const ComputePipeline* pipeline = pipeline_cache.GetComputePipeline();
    if (!pipeline) {
        return;
    }
    if (pipelined) {
        PostDraw(pipeline, nullptr, false, 0, &CsRegs());
        return;
    }
    DispatchRecord(pipeline);
}

void Rasterizer::SubmitIfWorkPiledUp(const RenderState& next_state) {
    // tlg: commands were submitted only when a guest submission ended or something had to wait,
    // so the GPU idled while thousands of draws were recorded and then had all of them to do,
    // and whatever waited on the GPU (inFAMOUS' threads wait for readbacks hundreds of times a
    // second) waited for all of it. Once TLG_SUBMIT_DRAWS (default 1024, 0: off) draws and
    // dispatches are recorded, they are submitted where the next draw starts a render pass
    // anyway, so the GPU works on them while the rest is recorded.
    static const u32 limit = [] {
        const char* env = std::getenv("TLG_SUBMIT_DRAWS");
        return env && *env ? static_cast<u32>(std::strtoul(env, nullptr, 10)) : 1024u;
    }();
    if (limit == 0 || scheduler.WorkSinceSubmit() < limit ||
        !scheduler.WillBeginRendering(next_state)) {
        return;
    }
    // The submission waits for the recording thread, which may be waiting for a compute
    // pipeline being built (ComputeBind): the draw pipe would stall behind the build.
    if (Pipeline::deferred_binds.load(std::memory_order_relaxed) != 0) {
        return;
    }
    // The frame recorder and capture read back what each submission did: left as they were.
    if (FrameRecord::IsRecording() || FrameCapture::Active()) {
        return;
    }
    if ((++work_submits & 255) == 1) {
        std::printf("GPU: %llu submissions of piled-up draws so far (TLG_SUBMIT_DRAWS=%u)\n",
                    static_cast<unsigned long long>(work_submits), limit);
    }
    scheduler.Flush();
}

Rasterizer::ComputeBinder Rasterizer::ComputeBind(const ComputePipeline* pipeline) {
    if (pipeline->IsBuilt()) {
        return {pipeline->Handle(), nullptr};
    }
    // tlg: still being built on a compiler thread (vk_compute_pipeline.cpp): the thread that
    // records the command into Vulkan waits for it, the GPU thread goes on.
    Pipeline::deferred_binds.fetch_add(1, std::memory_order_relaxed);
    return {vk::Pipeline{}, pipeline};
}

void Rasterizer::ComputeBinder::operator()(vk::CommandBuffer cmdbuf) const {
    if (pending) [[unlikely]] {
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, pending->Handle());
        Pipeline::deferred_binds.fetch_sub(1, std::memory_order_relaxed);
        return;
    }
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, handle);
}

void Rasterizer::DispatchRecord(const ComputePipeline* pipeline) {
    if (GpuPhy03::Recording()) [[unlikely]] GpuPhy03::BufferWrites(pipeline);
    FrameCapture::Poll();
    gbuffer_draw = false;

    scheduler.PopPendingOperations();
    DiagPassBoundary(true);

    const auto& cs_program = CsRegs();

    const auto& cs = pipeline->GetStage(Shader::SwStage::Compute);
    // After the resource binding: its transfers (image uploads, downloads) mark themselves.
    const auto mark = [&] {
        if (auto* profiler = GpuProfiler::Get()) {
            profiler->Mark(cs.pgm_hash ^ 0xD15Aull, [&] {
                return fmt::format("dispatch cs {:016x} ({}x{}x{} groups)", cs.pgm_hash,
                                   cs_program.dim_x, cs_program.dim_y, cs_program.dim_z);
            });
        }
    };
    if (upscaler->Enabled()) {
        if (upscaler->KnackProfile() && cs.pgm_hash == KnackTemporal::PresentShader) {
            scene_started = false;
            camera_motion->OnDisplayPass({});
            if (upscaler->OnFrameStart()) {
                camera_motion->InvalidateHistory();
                object_motion->InvalidateHistory();
            }
            object_motion->OnFrameStart();
            NoteFrameStart();
        }
        upscaler->OnDispatch(cs.pgm_hash);
    }
    if (FrameRecord::IsRecording()) [[unlikely]] {
        RecordFrameInputs(pipeline, nullptr, nullptr, false, 0, nullptr);
    }
    if (ExecuteShaderHLE(cs, Regs(), cs_program, *this)) {
        if (FrameRecord::Open()) [[unlikely]] {
            FrameRecord::Append(" hle");
        }
        return;
    }

    if (!BindResources(pipeline)) {
        return;
    }

    if (needs_barrier) {
        runtime.FlushBarriers();
    }

    if (upscaler->ReplaceKnackDispatch(cs.pgm_hash)) return;

    scheduler.EndRendering();
    mark();
    pipeline->BindResources(set_writes, push_data);

    const auto bind = ComputeBind(pipeline);
    const u32 dim_x = cs_program.dim_x, dim_y = cs_program.dim_y, dim_z = cs_program.dim_z;
    if (FrameCapture::Active()) {
        FrameCapture::Dispatch(cs.pgm_hash, dim_x, dim_y, dim_z);
    }
    scheduler.Record([=](vk::CommandBuffer cmdbuf) {
        if (Scheduler::FullBarriers()) {
            Scheduler::RecordFullBarrier(cmdbuf);
        }
        bind(cmdbuf);
        cmdbuf.dispatch(dim_x, dim_y, dim_z);
        if (Scheduler::FullBarriers()) {
            Scheduler::RecordFullBarrier(cmdbuf);
        }
    });
    if (upscaler->KnackProfile() && cs.pgm_hash==KnackTemporal::PresentShader) {
        const bool previous_framing = knack_video_framing.active;
        if (knack_video_framing.Advance(knack_video_composed,knack_video_scene_seen)) ClearKnackVideoBars();
        if (knack_video_framing.active!=previous_framing) {
            LOG_INFO(Render_Vulkan,"Knack video: framing={}, scene_camera={}, video_composed={}",
                knack_video_framing.active?"16:9":"ultrawide",knack_video_scene_seen,knack_video_composed);
        }
        knack_video_composed = false;
        knack_video_scene_seen = false;
    }
    if (GpuPhy03::Recording()) [[unlikely]] {
        GpuPhy03::Dispatch(instance, scheduler, runtime, texture_cache, cs.pgm_hash);
    }
    DebugState.IncDispatch();
    scheduler.CountWork();

    // Verified raw fill shader: one 16-byte constant vector is stored at
    // (workgroup_x * 64 + local_x) * 16. Keep the actual buffer dispatch for aliases,
    // and retain its packed pattern for an MSAA image that cannot be uploaded.
    if (cs.pgm_hash == 0x0299e222 && cs.buffers.size() == 2 && cs.images.empty() &&
        cs_program.num_thread_x.full == 64 && cs_program.num_thread_y.full == 1 &&
        cs_program.num_thread_z.full == 1 && dim_y == 1 && dim_z == 1 &&
        !cs.buffers[0].is_written && cs.buffers[1].is_written &&
        !cs.buffers[0].IsSpecial() && !cs.buffers[1].IsSpecial()) {
        const auto source = cs.buffers[0].GetSharp(cs);
        const auto dest = cs.buffers[1].GetSharp(cs);
        if (source.base_address && dest.base_address && dim_x && source.GetSize() == 16 &&
            u64(dim_x) * 64 * 16 == dest.GetSize() &&
            memory->ClampRangeSize(dest.base_address, dest.GetSize()) == dest.GetSize() &&
            memory->ClampRangeSize(source.base_address, 16) == 16 &&
            !buffer_cache.IsRegionGpuModified(source.base_address, 16)) {
            VideoCore::BufferClears::Pattern pattern;
            std::memcpy(pattern.data(), reinterpret_cast<const void*>(source.base_address), 16);
            if (VideoCore::BufferClears::Repeats64(pattern)) {
                texture_cache.RecordBufferClear(dest.base_address, dest.GetSize(), pattern);
                if (FrameCapture::IsRecording()) {
                    LOG_INFO(Render_Vulkan, "Packed buffer clear {:#x}+{} pattern={:08x},{:08x}",
                             u64(dest.base_address), dest.GetSize(), pattern[0], pattern[1]);
                }
            }
        }
    }

    // tlg diagnostics: TLG_DIAG_IMAGES=1 writes the images a dispatch read and wrote, after
    // it, while a frame capture records (TLG_CAPTURE_TRIGGER).
    static const bool diag_images = [] {
        const char* env = std::getenv("TLG_DIAG_IMAGES");
        return env && *env == '1';
    }();
    if (diag_images && FrameCapture::IsRecording()) {
        char tag[32];
        std::snprintf(tag, sizeof(tag), "cs%08x", static_cast<u32>(cs.pgm_hash));
        const auto images = bound_images;
        for (const auto image_id : images) {
            texture_cache.DiagDumpImage(image_id, tag);
        }
    }

    ResetBindings(true);
    scheduler.KickRecording();
}

void Rasterizer::DispatchIndirect(VAddr address, u32 offset, u32 size) {
    RENDERER_TRACE;
    // tlg: handed to the recording thread like direct dispatches.
    const bool pipelined = UseDrawPipe() && !TlgToggle::Disabled(TlgToggle::PipelinedIndirectDraws);
    if (!pipelined) {
        DrainDrawPipe();
    }
    const ComputePipeline* pipeline = pipeline_cache.GetComputePipeline();
    if (!pipeline) {
        return;
    }
    if (pipelined) {
        const IndirectDraw indirect = {address + offset, 0, size, 1};
        PostDraw(pipeline, nullptr, false, 0, &CsRegs(), &indirect);
        return;
    }
    DispatchIndirectRecord(pipeline, address + offset, size);
}

void Rasterizer::DispatchIndirectRecord(const ComputePipeline* pipeline, VAddr address,
                                        u32 size) {
    scheduler.PopPendingOperations();
    const u32 offset = 0;

    if (!BindResources(pipeline)) {
        return;
    }

    const auto [buffer, base] = buffer_cache.ObtainBuffer(address + offset, size, false);
    needs_barrier |= runtime.IsBufferAccessed(buffer, base, size);
    // tlg: BP10 (shadPS4 c09fa46a, #5262) record the fetch so a later writer gets a barrier.
    if (Common::Backports::On(Common::Backports::Id::VertexIndexBarriers)) {
        Common::Backports::Hit(Common::Backports::Id::VertexIndexBarriers);
        bound_buffers.push_back({buffer, base, static_cast<u32>(size), vk::AccessFlagBits2::eIndirectCommandRead});
    }

    if (needs_barrier) {
        runtime.FlushBarriers();
    }

    if (FrameRecord::IsRecording()) [[unlikely]] {
        const IndirectDraw indirect = {address + offset, 0, size, 1};
        RecordFrameInputs(pipeline, nullptr, nullptr, false, 0, &indirect);
    }
    scheduler.EndRendering();
    pipeline->BindResources(set_writes, push_data);

    const auto bind = ComputeBind(pipeline);
    const vk::Buffer args = buffer->Handle();
    const u64 args_offset = base;
    scheduler.Record([=](vk::CommandBuffer cmdbuf) {
        if (Scheduler::FullBarriers()) {
            Scheduler::RecordFullBarrier(cmdbuf);
        }
        bind(cmdbuf);
        cmdbuf.dispatchIndirect(args, args_offset);
        if (Scheduler::FullBarriers()) {
            Scheduler::RecordFullBarrier(cmdbuf);
        }
    });
    if (GpuPhy03::Recording()) [[unlikely]] {
        GpuPhy03::Dispatch(instance, scheduler, runtime, texture_cache,
                           pipeline->GetStage(Shader::SwStage::Compute).pgm_hash);
    }
    DebugState.IncDispatch();
    scheduler.CountWork();

    ResetBindings(true);
    scheduler.KickRecording();
}

namespace {
// tlg: frame recorder helpers. Registers as 32-bit words, '.'-separated.
template <typename T>
std::string RecordWords(const T& value) {
    static_assert(sizeof(T) % 4 == 0);
    std::array<u32, sizeof(T) / 4> words;
    std::memcpy(words.data(), &value, sizeof(T));
    std::string out;
    for (size_t i = 0; i < words.size(); ++i) {
        out += fmt::format("{}{:x}", i ? "." : "", words[i]);
    }
    return out;
}
// Indexed by Shader::SwStage.
constexpr std::array<const char*, Shader::MaxStageTypes> RecordStageNames = {"ps", "hs", "ds",
                                                                            "vs", "gs", "cs"};
bool RecordVerifyDetachedUploads() {
    static const bool enabled = [] {
        const char* value = std::getenv("TLG_FRAME_RECORD_VERIFY_UPLOAD");
        return value && value[0] == '1';
    }();
    return enabled;
}
} // namespace

void Rasterizer::RecordFrameInputs(const Pipeline* pipeline, const PreparedDraw* prepared,
                                   const RenderState* state, bool is_indexed, u32 index_offset,
                                   const IndirectDraw* indirect) {
    if (!FrameRecord::Open() || DrawPipe::OnStageB()) {
        return;
    }
    const auto add = [](const std::string& text) { FrameRecord::Append(text); };
    const auto stages = pipeline->GetStages();
    const bool verify_uploads =
        RecordVerifyDetachedUploads() &&
        std::ranges::any_of(stages, [](const Shader::Info* info) {
            return info && (info->pgm_hash == KnackDetachedGeometryShader ||
                            info->pgm_hash == KnackSmallGeometryVertexShader);
        });
    // The copies end the render pass; a draw that clears its targets and continues the pass
    // in progress would clear them again when it begins a new one.
    bool gpu_ok = FrameRecord::GpuCopiesAvailable();
    if (state && scheduler.IsRenderingWith(*state)) {
        bool clears = state->depth_stencil_attachment.depth_clear ||
                      state->depth_stencil_attachment.stencil_clear;
        for (u32 i = 0; i < state->num_color_attachments; ++i) {
            clears |= state->color_attachments[i].is_clear != 0;
        }
        if (clears) {
            gpu_ok = false;
            add(" gpu=skipped_clear");
        }
    }
    // Memory the GPU wrote is read from the GPU's copy (the guest copy may be stale or
    // read-protected); anything else from guest memory, which the cache mirrors.
    const auto memory_input = [&](const std::string& key, VAddr address, u64 size,
                                  const vk::DescriptorBufferInfo* bound = nullptr,
                                  u64 bound_adjust = 0) {
        add(fmt::format(" {}={:#x}+{}:", key, address, size));
        if (address == 0 || size == 0) {
            add("null");
            return;
        }
        size = memory->ClampRangeSize(address, size);
        const bool gpu_modified = buffer_cache.IsRegionGpuModified(address, size);
        if (gpu_ok && size && size <= std::numeric_limits<u32>::max() && gpu_modified) {
            if (bound && bound->buffer) {
                FrameRecord::GpuHash("", bound->buffer, bound->offset + bound_adjust, size);
            } else {
                const auto [buffer, offset] = buffer_cache.ObtainBuffer(address, u32(size), false);
                FrameRecord::GpuHash("", buffer->Handle(), offset, size);
            }
        } else {
            add(FrameRecord::GuestHash(address, size));
        }
        // For Knack's detached/dynamic geometry, compare guest bytes with the exact Vulkan
        // descriptor already bound for this draw. Re-obtaining a small buffer would verify a
        // new stream-buffer copy instead of the one consumed by the draw.
        if (verify_uploads && gpu_ok && !gpu_modified && bound && bound->buffer && size) {
            if (size <= 1_KB) {
                add(fmt::format(" {}.cw={}", key, FrameRecord::GuestWords(address, size)));
                FrameRecord::GpuWords(fmt::format(" {}.gw=", key), bound->buffer,
                                      bound->offset + bound_adjust, size);
            } else {
                FrameRecord::GpuHash(fmt::format(" {}.gpu=", key), bound->buffer,
                                     bound->offset + bound_adjust, size);
            }
        }
    };

    const auto* saved_prepared = bind_prepared;
    bind_prepared = prepared;
    const auto& regs = Regs();
    u32 buffer_info_index = 0;
    for (const Shader::Info* info : stages) {
        if (!info) {
            continue;
        }
        const u32 s = static_cast<u32>(info->sw_stage);
        const char* name = s < RecordStageNames.size() ? RecordStageNames[s] : "xs";
        // Permutation: the pipeline key holds HashCombine(program hash, permutation index).
        const u64 key_hash =
            pipeline->IsCompute()
                ? static_cast<const ComputePipeline*>(pipeline)->GetComputeKey().value
                : static_cast<const GraphicsPipeline*>(pipeline)->GetGraphicsKey().stage_hashes[s];
        std::string perm = "?";
        for (u64 p = 0; p < 4096; ++p) {
            if (HashCombine(info->pgm_hash, p) == key_hash) {
                perm = std::to_string(p);
                break;
            }
        }
        const auto ud = info->FlatUserData();
        add(fmt::format(" {}={:016x}/p{} {}.ud={:016x}", name, info->pgm_hash, perm, name,
                        XXH3_64bits(ud.data(), ud.size() * sizeof(u32))));
        const PreparedStage* ps = FindPreparedStage(*info);
        for (u32 i = 0; i < info->buffers.size(); ++i) {
            const auto& desc = info->buffers[i];
            const vk::DescriptorBufferInfo* bound =
                buffer_info_index < buffer_infos.size() ? &buffer_infos[buffer_info_index] : nullptr;
            ++buffer_info_index;
            if (desc.IsSpecial()) {
                continue;
            }
            const auto sharp = ps ? ps->buffer_sharps[i] : desc.GetSharp(*info);
            u64 bound_adjust = 0;
            if (bound && bound->buffer && bound->buffer != buffer_cache.GetStreamBuffer().Handle() &&
                (!constant_ring || bound->buffer != constant_ring->Handle())) {
                bound_adjust = sharp.base_address % instance.StorageMinAlignment();
            }
            memory_input(fmt::format("{}.b{}{}", name, i, desc.is_written ? "w" : ""),
                         sharp.base_address, sharp.GetSize(), bound, bound_adjust);
        }
        for (u32 i = 0; i < info->images.size(); ++i) {
            const auto& desc = info->images[i];
            const auto t = ps ? ps->image_sharps[i] : desc.GetSharp(*info);
            add(fmt::format(" {}.t{}{}=", name, i, desc.is_written ? "w" : ""));
            if (t.Address() == 0) {
                add("null");
                continue;
            }
            add(fmt::format("{:#x}:{}/{}:{}x{}x{}:{}:m{}-{}", t.Address(),
                            magic_enum::enum_name(t.GetDataFmt()),
                            magic_enum::enum_name(t.GetNumberFmt()), u32(t.width) + 1,
                            u32(t.height) + 1, u32(t.depth) + 1,
                            magic_enum::enum_name(t.GetType()), u32(t.base_level),
                            u32(t.last_level)));
            if (verify_uploads && !desc.is_written) {
                const VideoCore::ImageInfo texture_info{t, desc};
                const auto image_id = texture_cache.FindImageFromRange(
                    texture_info.guest_address, texture_info.guest_size, false);
                if (image_id) {
                    const auto& image = texture_cache.GetImage(image_id);
                    add(fmt::format(":ih={:016x}:uid={}:alias={}:flags={:x}:size={}", image.hash,
                                    image.image_uid, image.alias_generation, u32(image.flags),
                                    image.info.guest_size));
                } else {
                    add(":ih=missing");
                }
            }
        }
        for (u32 i = 0; i < info->samplers.size(); ++i) {
            const auto ss = ps ? ps->sampler_sharps[i] : info->samplers[i].GetSharp(*info);
            add(fmt::format(" {}.s{}={:08x}", name, i, u32(XXH3_64bits(&ss, sizeof(ss)))));
        }
    }

    if (pipeline->IsCompute()) {
        if (!indirect) {
            const auto& cs = CsRegs();
            add(fmt::format(" groups={}x{}x{}", cs.dim_x, cs.dim_y, cs.dim_z));
        }
    } else {
        const auto* graphics = static_cast<const GraphicsPipeline*>(pipeline);
        const auto& key = graphics->GetGraphicsKey();
        add(fmt::format(" rs={} ds={}", u32(key.num_samples), u32(key.depth_samples)));
        const auto& vs = graphics->GetStage(Shader::SwStage::Vertex);
        if (const auto& fetch = graphics->GetFetchShader()) {
            for (u32 i = 0; i < fetch->attributes.size(); ++i) {
                const auto sharp = fetch->attributes[i].GetSharp(vs);
                memory_input(fmt::format("vb{}", i), sharp.base_address, sharp.GetSize());
            }
        }
        if (is_indexed) {
            const u32 index_size =
                regs.index_buffer_type.index_type == AmdGpu::IndexType::Index16 ? 2u : 4u;
            const u64 count = indirect ? regs.index_size : regs.num_indices;
            add(fmt::format(" ioff={}", index_offset));
            memory_input("ib",
                         regs.index_base_address.Address<VAddr>() + u64(index_offset) * index_size,
                         count * index_size);
        }
        add(fmt::format(" prim={}", magic_enum::enum_name(regs.primitive_type)));
        const auto& vp = regs.viewports[0];
        add(fmt::format(" vp={:g},{:g},{:g},{:g},{:g},{:g} scis={}", vp.xscale, vp.xoffset,
                        vp.yscale, vp.yoffset, vp.zscale, vp.zoffset,
                        RecordWords(regs.screen_scissor)));
        const u32 num_colors = state ? state->num_color_attachments : 0;
        for (u32 cb = 0; cb < num_colors; ++cb) {
            add(fmt::format(" rt{}=", cb));
            if (!cb_descs[cb].first) {
                add("null");
            } else {
                const auto& image = texture_cache.GetImage(cb_descs[cb].first);
                add(fmt::format("{:#x}:{}:{}x{}", image.info.guest_address,
                                vk::to_string(image.info.pixel_format), image.info.size.width,
                                image.info.size.height));
                add(fmt::format(" rt{}s={}.{}.{}.{}", cb, u32(key.color_samples[cb]),
                                image.info.num_samples, image.info.guest_samples,
                                image.backing->num_samples));
            }
            add(fmt::format(" bl{}={}", cb, RecordWords(regs.blend_control[cb])));
        }
        if (db_desc.first) {
            const auto& image = texture_cache.GetImage(db_desc.first);
            add(fmt::format(" z={:#x}:{}:{}x{} zs={}.{}.{}.{}", image.info.guest_address,
                            vk::to_string(image.info.pixel_format), image.info.size.width,
                            image.info.size.height, u32(key.depth_samples), image.info.num_samples,
                            image.info.guest_samples, image.backing->num_samples));
        }
        add(fmt::format(" cm={} csm={} cc={} dc={} st={} sref={}.{} drc={} pc={} bc={}",
                        RecordWords(regs.color_target_mask), RecordWords(regs.color_shader_mask),
                        RecordWords(regs.color_control), RecordWords(regs.depth_control),
                        RecordWords(regs.stencil_control), RecordWords(regs.stencil_ref_front),
                        RecordWords(regs.stencil_ref_back), RecordWords(regs.depth_render_control),
                        RecordWords(regs.polygon_control), RecordWords(regs.blend_constants)));
        if (state) {
            u32 clears = 0;
            for (u32 i = 0; i < state->num_color_attachments; ++i) {
                clears |= u32(state->color_attachments[i].is_clear != 0) << i;
            }
            clears |= u32(state->depth_stencil_attachment.depth_clear) << 8 |
                      u32(state->depth_stencil_attachment.stencil_clear) << 9;
            add(fmt::format(" clr={:x}", clears));
        }
    }

    if (indirect) {
        // Arguments as the guest wrote them and as the GPU reads them at this draw.
        const u64 bytes = u64(indirect->stride) * indirect->max_count;
        add(fmt::format(" max={} stride={} args.cpu={}", indirect->max_count, indirect->stride,
                        FrameRecord::GuestWords(indirect->args, bytes)));
        if (gpu_ok && bytes && bytes <= std::numeric_limits<u32>::max()) {
            const auto [buffer, offset] = buffer_cache.ObtainBuffer(indirect->args, u32(bytes), false);
            FrameRecord::GpuWords(" args=", buffer->Handle(), offset, bytes);
        }
        if (indirect->count) {
            add(fmt::format(" count.cpu={}", FrameRecord::GuestWords(indirect->count, 4)));
            if (gpu_ok) {
                const auto [buffer, offset] = buffer_cache.ObtainBuffer(indirect->count, 4, false);
                FrameRecord::GpuWords(" count=", buffer->Handle(), offset, 4);
            }
        }
    }
    bind_prepared = saved_prepared;
    if (gpu_ok) {
        FrameRecord::FlushGpuCopies();
    }
}

u64 Rasterizer::Flush() {
    DrainDrawPipe();
    const u64 current_tick = scheduler.CurrentTick();
    SubmitInfo info{};
    scheduler.Flush(info);
    return current_tick;
}

void Rasterizer::Finish() {
    DrainDrawPipe();
    scheduler.Finish();
}

// tlg (i3-pc 25f247c): a game that binds a heap whole and read-only (inFAMOUS: 0.5 and 2.5 GB
// geometry heaps, indexed by a per-instance offset, so no part can be left out) uploaded all of it
// in the frame of the first bind (a freeze of 2 to 3 seconds). The ranges are remembered per title
// (heap_binds.txt in the game cache) and, in later sessions, uploaded a slice per submission until
// the game binds them, so the first bind finds only what was written since. Uploading early is
// sound: a later write marks the pages again. On; TLG_HEAP_WARM=0 turns it off.
namespace {
constexpr u64 HugeBindSize = 256ull << 20;
constexpr u64 HugeWarmSlice = 16ull << 20;

struct HugeBind {
    VAddr base;
    u64 size;
    u64 cursor{};
    bool bound{};
};

bool HugeWarmEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("TLG_HEAP_WARM");
        return !value || value[0] != '0';
    }();
    return enabled;
}

std::filesystem::path HugeBindFile() {
    return Storage::GameCacheDir() / "heap_binds.txt";
}

std::mutex huge_binds_mutex;

std::vector<HugeBind>& HugeBinds() {
    static std::vector<HugeBind> binds = [] {
        std::vector<HugeBind> loaded;
        if (FILE* f = std::fopen(HugeBindFile().string().c_str(), "r")) {
            unsigned long long base, size;
            while (loaded.size() < 16 && std::fscanf(f, "%llx %llx", &base, &size) == 2) {
                if (size >= HugeBindSize && size <= (4ull << 30)) {
                    loaded.push_back({VAddr(base), u64(size)});
                }
            }
            std::fclose(f);
        }
        return loaded;
    }();
    return binds;
}

void NoteHugeBind(VAddr base, u64 size) {
    if (!HugeWarmEnabled()) {
        return;
    }
    std::scoped_lock lock{huge_binds_mutex};
    auto& binds = HugeBinds();
    for (auto& bind : binds) {
        if (bind.base == base && bind.size == size) {
            bind.bound = true;
            return;
        }
    }
    if (binds.size() >= 16) {
        return;
    }
    binds.push_back({base, size, 0, true});
    std::error_code ec;
    std::filesystem::create_directories(HugeBindFile().parent_path(), ec);
    if (FILE* f = std::fopen(HugeBindFile().string().c_str(), "a")) {
        std::fprintf(f, "%llx %llx\n", static_cast<unsigned long long>(base),
                     static_cast<unsigned long long>(size));
        std::fclose(f);
    }
}
} // namespace

void Rasterizer::OnSubmit() {
    DrainDrawPipe();
    FrameCapture::OnSubmit();
    if (GpuPhy03::Enabled()) [[unlikely]] GpuPhy03::OnSubmit();
    knack_small_sync_pending.store(true, std::memory_order_relaxed);
    // SubmitDone is the reliable renderer boundary even with camera motion and the upscaler off.
    // Polling here also guarantees prior query commands reached the scheduler before their slice
    // is marked pending. The normal path pays only this opt-in null check.
    if (knack_contribution_probe) {
        knack_contribution_probe->BeginFrame();
    }
    if (knack_visual_probe) {
        knack_visual_probe->BeginFrame();
    }
    if (fault_process_pending) {
        fault_process_pending = false;
        buffer_cache.ProcessFaultBuffer();
    }
    texture_cache.ProcessDownloadImages();
    texture_cache.RunGarbageCollector();
    if (HugeWarmEnabled()) {
        // One slice of each remembered heap this session has not bound yet. The draw pipe was
        // drained above, so no draw is binding buffers meanwhile.
        std::scoped_lock lock{huge_binds_mutex};
        for (auto& bind : HugeBinds()) {
            if (bind.bound) {
                continue;
            }
            const VAddr start = bind.base + bind.cursor;
            const u64 length = std::min(HugeWarmSlice, bind.size - bind.cursor);
            bind.cursor = bind.cursor + length >= bind.size ? 0 : bind.cursor + length;
            boost::container::small_vector<std::pair<VAddr, u64>, 8> parts;
            ForEachMappedRangeInRange(start, length, [&](const auto& range) {
                parts.emplace_back(range.lower(), range.upper() - range.lower());
            });
            for (const auto& [addr, part_size] : parts) {
                if (part_size > 64_KB) {
                    (void)buffer_cache.ObtainBuffer(addr, u32(part_size), false);
                }
            }
        }
    }
    runtime.TickFrame();
}

const PreparedStage* Rasterizer::FindPreparedStage(const Shader::Info& stage) const {
    // Sharps a draw-preparation worker read from the same flattened user data.
    if (!bind_prepared || TlgToggle::Disabled(TlgToggle::PreparedResources)) {
        return nullptr;
    }
    for (u32 i = 0; i < bind_prepared->num_stages; ++i) {
        const auto& candidate = bind_prepared->stages[i];
        if (&candidate.program->info == &stage &&
            candidate.num_images == stage.images.size() &&
            candidate.num_samplers == stage.samplers.size() &&
            candidate.num_buffers == stage.buffers.size()) {
            return &candidate;
        }
    }
    return nullptr;
}

bool Rasterizer::BindHelperWanted() {
    // A spinning helper pays off only with cores to spare (the recording thread spins too).
    const char* env = std::getenv("TLG_TEXTURE_HELPER");
    if (env && env[0]) {
        return env[0] == '1';
    }
    // Measured neutral on 16 threads (docs/parallel_gpu.md): opt-in.
    return false;
}

bool Rasterizer::HelperEligible(const Pipeline* pipeline) const {
    if (GpuPhy03::Recording()) [[unlikely]] return false;
    if (pipeline->IsCompute() || !bind_helper.Available() || FrameCapture::Active() ||
        TlgToggle::Disabled(TlgToggle::TextureBindHelper) ||
        TlgToggle::Disabled(TlgToggle::TextureBindingMemo) ||
        TlgToggle::Disabled(TlgToggle::UpdateImageFastPath)) {
        return false;
    }
    for (const auto* stage : pipeline->GetStages()) {
        if (!stage) {
            continue;
        }
        // Mip arrays make the descriptor layout depend on the T#s.
        for (const auto& image : stage->images) {
            if (image.mip_fallback_mode != Shader::MipStorageFallbackMode::None) {
                return false;
            }
        }
        // Storage writes invalidate cached images from the buffer side.
        for (const auto& buffer : stage->buffers) {
            if (!buffer.IsSpecial() && buffer.is_written) {
                return false;
            }
        }
    }
    return true;
}

bool Rasterizer::TexturesBindableOnHelper(const Shader::Info& stage,
                                          const PreparedStage* prepared) {
    const u64 generation = texture_cache.RegistryGeneration();
    for (u32 image_index = 0; image_index < stage.images.size(); ++image_index) {
        const auto& image_desc = stage.images[image_index];
        if (image_desc.is_written) {
            return false;
        }
        const auto tsharp =
            prepared ? prepared->image_sharps[image_index] : image_desc.GetSharp(stage);
        const auto data_fmt = tsharp.GetDataFmt();
        const auto num_fmt = tsharp.GetNumberFmt();
        if (tsharp.Address() == 0 || data_fmt == AmdGpu::DataFormat::FormatInvalid ||
            !memory->IsValidGpuMapping(tsharp.Address(), 0) ||
            !IsKnownFormat(data_fmt, num_fmt)) {
            continue; // null descriptor
        }
        const auto& entry =
            prepared ? CachedImageDescEntry(tsharp, image_desc, prepared->image_hashes[image_index])
                     : CachedImageDescEntry(tsharp, image_desc);
        if (entry.found_generation != generation) {
            return false;
        }
        VideoCore::ImageId image_id = entry.found_id;
        auto* image = &texture_cache.GetImage(image_id);
        if (const auto depth_image_id = texture_cache.GetAssociatedDepth(*image)) {
            image_id = depth_image_id;
            image = &texture_cache.GetImage(image_id);
        }
        if (image->binding.needs_rebind || !texture_cache.IsUpToDate(image_id) ||
            scene_targets->Tracks(image->image_uid) ||
            (upscaler->Enabled() && upscaler->RedirectsSampled(image_id))) {
            return false;
        }
    }
    return true;
}

void Rasterizer::RunTextureTask(void* context) {
    auto& self = *static_cast<Rasterizer*>(context);
    auto& task = self.texture_task;
    for (u32 i = 0; i < task.count; ++i) {
        auto& stage = task.stages[i];
        if (!self.TexturesBindableOnHelper(*stage.info, stage.prepared)) {
            return;
        }
        auto binding = stage.binding;
        u32 write_index = stage.write_index;
        self.BindTextures(*stage.info, stage.prepared, binding, write_index, task.barrier, true);
        task.completed = i + 1;
    }
}

void Rasterizer::JoinBindHelper(void* context) {
    if (!BindHelper::OnHelper()) {
        static_cast<Rasterizer*>(context)->bind_helper.Join();
    }
}

bool Rasterizer::BindResources(const Pipeline* pipeline) {
    if (GpuPhy03::Recording()) [[unlikely]] {
        GpuPhy03::BeginBind();
        GpuPhy03::BufferWrites(pipeline);
    }
    if (IsComputeImageCopy(pipeline) || IsComputeMetaClear(pipeline) ||
        IsComputeImageClear(pipeline)) {
        return false;
    }

    set_write_index = 0;
    set_writes.clear();
    buffer_infos.clear();
    image_infos.clear();
    // tlg: G-buffer passes sample material textures for a scene rendered below the output
    // size; the temporal upscaler restores the detail of the output's mip level (FSR guide:
    // log2(render / output)). Shadows, post-processing and UI keep the guest's bias.
    // The menu's texture LOD bias (mip_lod_bias) adds to every draw: textures without mips
    // (UI, full-screen passes) are unaffected. Steps of 1/8: dragging the slider creates few samplers.
    sampler_lod_bias = pipeline->IsCompute() ? 0.0f
                                             : std::round(TlgSettings::Get().mip_lod_bias.load() * 8.0f) / 8.0f;
    if (!pipeline->IsCompute() && !TlgToggle::Disabled(TlgToggle::SceneMipBias) &&
        std::popcount(static_cast<const GraphicsPipeline*>(pipeline)->GetGraphicsKey().mrt_mask &
                      0xff) >= 5) {
        sampler_lod_bias += upscaler->SceneMipBias();
        static float reported = 0.0f;
        if (sampler_lod_bias != reported) {
            reported = sampler_lod_bias;
            std::printf("Upscaler: scene texture LOD bias %.2f\n", sampler_lod_bias);
        }
    }

    bool uses_dma = false;

    static const bool stats = std::getenv("TLG_FRAME_STATS") != nullptr;
    if (stats && ((helper_full + helper_partial + helper_serial) & 1023) == 0) {
        static auto window = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        if (now - window >= std::chrono::seconds(5)) {
            window = now;
            const double forks = std::max<double>(1, helper_full + helper_partial);
            std::printf("Texture helper: %llu binds in parallel, %llu partly, %llu serial; "
                        "per fork %.0f cycles helper task, %.0f cycles GPU thread wait\n",
                        static_cast<unsigned long long>(helper_full),
                        static_cast<unsigned long long>(helper_partial),
                        static_cast<unsigned long long>(helper_serial),
                        bind_helper.task_cycles.exchange(0) / forks,
                        bind_helper.wait_cycles / forks);
            bind_helper.wait_cycles = 0;
            helper_full = helper_partial = helper_serial = 0;
        }
    }

    // Bind resource buffers and textures.
    Shader::Backend::Bindings binding{};
    push_data = MakeUserData(Regs());
    if (!HelperEligible(pipeline)) {
        ++helper_serial;
        for (const auto* stage : pipeline->GetStages()) {
            if (!stage) {
                continue;
            }
            set_writes.resize(set_writes.size() + stage->buffers.size() + stage->images.size() +
                              stage->samplers.size());
            stage->PushUd(binding, push_data);
            const PreparedStage* prepared = FindPreparedStage(*stage);
            BindBuffers(*stage, prepared, binding, push_data, set_write_index);
            BindTextures(*stage, prepared, binding, set_write_index, needs_barrier);
            uses_dma |= stage->uses_dma;
        }
    } else {
        // tlg: textures on the helper, buffers here. Without mip arrays every stage's
        // descriptors are [buffers][images][samplers], one each, so both sides know their
        // binding numbers and descriptor write slots up front.
        struct BufferStage {
            const Shader::Info* info;
            const PreparedStage* prepared;
            Shader::Backend::Bindings binding;
            u32 write_index;
        };
        std::array<BufferStage, Shader::MaxStageTypes> buffer_stages;
        auto& task = texture_task;
        task.count = 0;
        task.completed = 0;
        task.barrier = false;
        for (const auto* stage : pipeline->GetStages()) {
            if (!stage) {
                continue;
            }
            const u32 num_buffers = static_cast<u32>(stage->buffers.size());
            const u32 num_descriptors =
                num_buffers + static_cast<u32>(stage->images.size() + stage->samplers.size());
            set_writes.resize(set_writes.size() + num_descriptors);
            stage->PushUd(binding, push_data);
            const PreparedStage* prepared = FindPreparedStage(*stage);
            buffer_stages[task.count] = {stage, prepared, binding, set_write_index};
            auto texture_binding = binding;
            texture_binding.buffer += num_buffers;
            texture_binding.unified += num_buffers;
            task.stages[task.count] = {stage, prepared, texture_binding,
                                       set_write_index + num_buffers};
            ++task.count;
            binding.buffer += num_buffers;
            binding.unified += num_descriptors;
            set_write_index += num_descriptors;
            uses_dma |= stage->uses_dma;
        }
        bind_helper.Fork(&RunTextureTask, this);
        for (u32 i = 0; i < task.count; ++i) {
            auto& stage = buffer_stages[i];
            BindBuffers(*stage.info, stage.prepared, stage.binding, push_data, stage.write_index);
        }
        if (draw_inputs.pending && !TlgToggle::Disabled(TlgToggle::EarlyDrawInputs)) {
            ResolveVertexBuffers(draw_inputs.pipeline, draw_inputs.prepared);
            if (draw_inputs.is_indexed) {
                ResolveIndexBuffer(draw_inputs.index_offset);
            }
            draw_inputs.resolved = true;
        }
        bind_helper.Join();
        needs_barrier |= task.barrier;
        ++(task.completed == task.count ? helper_full
                                        : task.completed ? helper_partial : helper_serial);
        // Stages the helper left (an image needing work that records commands).
        for (u32 i = task.completed; i < task.count; ++i) {
            auto& stage = task.stages[i];
            u32 write_index = stage.write_index;
            BindTextures(*stage.info, stage.prepared, stage.binding, write_index, needs_barrier);
        }
    }

    if (uses_dma) {
        buffer_cache.SynchronizeDmaBuffers();
        fault_process_pending = true;
    }

    return true;
}

void Rasterizer::BindVertexBuffers(const GraphicsPipeline* pipeline, const PreparedDraw* prepared) {
    ResolveVertexBuffers(pipeline, prepared);
    EmitVertexBuffers();
}

void Rasterizer::EmitVertexBuffers() {
    // tlg: only the used entries go into the recording chunk (the static vectors hold 32 of
    // each: ~3 KiB copied per draw).
    auto& v = vertex_binds;
    const bool dynamic_input = instance.IsVertexInputDynamicState();
    const u32 num_buffers = v.num_buffers;
    scheduler.ReserveRecordData(
        v.bindings.size() * sizeof(v.bindings[0]) + v.attributes.size() * sizeof(v.attributes[0]) +
        num_buffers * (sizeof(vk::Buffer) + 3 * sizeof(vk::DeviceSize)) + 128);
    if (dynamic_input) {
        scheduler.Record([bindings = scheduler.RecordData(std::span<const vk::VertexInputBindingDescription2EXT>{v.bindings.data(), v.bindings.size()}),
                          attributes = scheduler.RecordData(std::span<const vk::VertexInputAttributeDescription2EXT>{
                              v.attributes.data(), v.attributes.size()})](
                             vk::CommandBuffer cmdbuf) {
            cmdbuf.setVertexInputEXT(bindings, attributes);
        });
    }
    if (num_buffers == 0) {
        return;
    }
    const auto buffers = scheduler.RecordData(std::span<const vk::Buffer>{v.host_buffers.data(), num_buffers});
    const auto offsets = scheduler.RecordData(std::span<const vk::DeviceSize>{v.host_offsets.data(), num_buffers});
    if (dynamic_input) {
        scheduler.Record([num_buffers, buffers, offsets](vk::CommandBuffer cmdbuf) {
            cmdbuf.bindVertexBuffers(0, num_buffers, buffers.data(), offsets.data());
        });
        return;
    }
    const auto sizes = scheduler.RecordData(std::span<const vk::DeviceSize>{v.host_sizes.data(), num_buffers});
    const auto strides = scheduler.RecordData(std::span<const vk::DeviceSize>{v.host_strides.data(), num_buffers});
    scheduler.Record([num_buffers, buffers, offsets, sizes, strides](vk::CommandBuffer cmdbuf) {
        cmdbuf.bindVertexBuffers2(0, num_buffers, buffers.data(), offsets.data(), sizes.data(),
                                  strides.data());
    });
}

void Rasterizer::ResolveVertexBuffers(const GraphicsPipeline* pipeline,
                                      const PreparedDraw* prepared) {
    const auto& regs = Regs();
    auto& v = vertex_binds;
    v.num_buffers = 0;
    v.host_buffers.clear();
    v.host_offsets.clear();
    v.host_sizes.clear();
    v.host_strides.clear();
    auto& attributes = v.attributes;
    auto& bindings = v.bindings;
    attributes.clear();
    bindings.clear();
    VertexInputs<vk::VertexInputBindingDivisorDescriptionEXT> divisors;
    VertexInputs<AmdGpu::Buffer> guest_buffers;
    // Inputs a draw-preparation worker read from the same registers and (verified) user data.
    const auto& fetch = pipeline->GetFetchShader();
    const PreparedVertexInputs* ready =
        prepared && prepared->vertex.valid && instance.IsVertexInputDynamicState() && fetch &&
                fetch->attributes.size() == prepared->vertex.count && !FrameCapture::Active() &&
                !TlgToggle::Disabled(TlgToggle::PreparedResources)
            ? &prepared->vertex
            : nullptr;
    if (ready && !fetch->MatchesBuffers(pipeline->GetStage(Shader::SwStage::Vertex),
                                        {ready->buffers, ready->count})) {
        ready = nullptr;
    }
    if (ready) {
        attributes.assign(ready->attributes, ready->attributes + ready->count);
        bindings.assign(ready->bindings, ready->bindings + ready->count);
        guest_buffers.assign(ready->buffers, ready->buffers + ready->count);
    } else {
        pipeline->GetVertexInputs(attributes, bindings, divisors, guest_buffers,
                                  regs.vgt_instance_step_rate_0, regs.vgt_instance_step_rate_1);
    }

    if (bindings.empty()) {
        // If there are no bindings, there is nothing further to do.
        return;
    }
    if (motion_draw && !guest_buffers.empty()) {
        // Include every stream: identical position buffers can be paired with different
        // skinning/instance data. Index topology and base offsets are matched separately.
        motion_geometry = ready ? ready->buffers_hash
                                : XXH3_64bits(guest_buffers.data(),
                                              guest_buffers.size() * sizeof(AmdGpu::Buffer));
    }
    // Object motion research: vertex streams of G-buffer draws.
    if (FrameCapture::Active() && gbuffer_draw) {
        for (const auto& vb : guest_buffers) {
            char note[128];
            std::snprintf(note, sizeof(note), "\n  gb vb at %#llx size %u stride %u",
                          (unsigned long long)vb.base_address, u32(vb.GetSize()),
                          u32(vb.GetStride()));
            FrameCapture::Note(note);
        }
    }

    struct BufferRange {
        VAddr base_address;
        VAddr end_address;
        const VideoCore::Buffer* buffer;
        u64 offset;

        [[nodiscard]] size_t GetSize() const {
            return end_address - base_address;
        }
    };

    // Build list of ranges covering the requested buffers
    VertexInputs<BufferRange> ranges{};
    VertexInputs<BufferRange> ranges_merged{};
    if (ready) {
        for (u32 i = 0; i < ready->num_ranges; ++i) {
            ranges_merged.emplace_back(ready->ranges[i].base, ready->ranges[i].end);
        }
    } else {
        for (const auto& buffer : guest_buffers) {
            if (buffer.base_address != 0 && buffer.GetSize() > 0) {
                ranges.emplace_back(buffer.base_address, buffer.base_address + buffer.GetSize());
            }
        }
    }

    // Merge connecting ranges together
    if (!ranges.empty()) {
        std::ranges::sort(ranges, [](const BufferRange& lhv, const BufferRange& rhv) {
            return lhv.base_address < rhv.base_address;
        });
        ranges_merged.emplace_back(ranges[0]);
        for (auto range : ranges) {
            auto& prev_range = ranges_merged.back();
            if (prev_range.end_address < range.base_address) {
                ranges_merged.emplace_back(range);
            } else {
                prev_range.end_address = std::max(prev_range.end_address, range.end_address);
            }
        }
    }

    // Map buffers for merged ranges
    for (auto& range : ranges_merged) {
        const u64 size = memory->ClampRangeSize(range.base_address, range.GetSize());
        std::tie(range.buffer, range.offset) =
            buffer_cache.ObtainBuffer(range.base_address, size, false);
        needs_barrier |= runtime.IsBufferAccessed(range.buffer, range.offset, size);
        // tlg: BP10 (shadPS4 c09fa46a, #5262) record the fetch so a later writer gets a barrier.
        if (Common::Backports::On(Common::Backports::Id::VertexIndexBarriers)) {
            Common::Backports::Hit(Common::Backports::Id::VertexIndexBarriers);
            bound_buffers.push_back({range.buffer, range.offset, static_cast<u32>(size), vk::AccessFlagBits2::eVertexAttributeRead});
        }
    }

    // Bind vertex buffers
    auto& host_buffers = v.host_buffers;
    auto& host_offsets = v.host_offsets;
    auto& host_sizes = v.host_sizes;
    auto& host_strides = v.host_strides;
    u32 stream_with_memory = 0;
    for (const auto& buffer : guest_buffers) {
        if (buffer.base_address != 0 && buffer.GetSize() > 0) {
            const auto host_buffer_info =
                ready ? ranges_merged.begin() + ready->range_index[stream_with_memory++]
                      : std::ranges::find_if(ranges_merged, [&](const BufferRange& range) {
                            return buffer.base_address >= range.base_address &&
                                   buffer.base_address < range.end_address;
                        });
            ASSERT(host_buffer_info != ranges_merged.cend());
            host_buffers.emplace_back(host_buffer_info->buffer->Handle());
            host_offsets.push_back(host_buffer_info->offset + buffer.base_address -
                                   host_buffer_info->base_address);
        } else {
            host_buffers.emplace_back(VK_NULL_HANDLE);
            host_offsets.push_back(0);
        }
        host_sizes.push_back(buffer.GetSize());
        host_strides.push_back(buffer.GetStride());
    }

    v.num_buffers = static_cast<u32>(guest_buffers.size());
}

void Rasterizer::BindIndexBuffer(u32 index_offset) {
    ResolveIndexBuffer(index_offset);
    EmitIndexBuffer();
}

void Rasterizer::EmitIndexBuffer() {
    scheduler.Record([handle = index_bind.handle, offset = index_bind.offset,
                      type = index_bind.type](vk::CommandBuffer cmdbuf) {
        cmdbuf.bindIndexBuffer(handle, offset, type);
    });
}

void Rasterizer::ResolveIndexBuffer(u32 index_offset) {
    const auto& regs = Regs();

    // Figure out index type and size.
    const bool is_index16 = regs.index_buffer_type.index_type == AmdGpu::IndexType::Index16;
    const vk::IndexType index_type = is_index16 ? vk::IndexType::eUint16 : vk::IndexType::eUint32;
    const u32 index_size = is_index16 ? sizeof(u16) : sizeof(u32);
    const VAddr index_address =
        regs.index_base_address.Address<VAddr>() + index_offset * index_size;

    // Bind index buffer.
    const u32 index_buffer_size = regs.num_indices * index_size;
    const auto [buffer, offset] =
        buffer_cache.ObtainBuffer(index_address, index_buffer_size, false);
    needs_barrier |= runtime.IsBufferAccessed(buffer, offset, index_buffer_size);
    // tlg: BP10 (shadPS4 c09fa46a, #5262) record the fetch so a later writer gets a barrier.
    if (Common::Backports::On(Common::Backports::Id::VertexIndexBarriers)) {
        Common::Backports::Hit(Common::Backports::Id::VertexIndexBarriers);
        bound_buffers.push_back({buffer, offset, static_cast<u32>(index_buffer_size), vk::AccessFlagBits2::eIndexRead});
    }
    index_bind = {buffer->Handle(), offset, index_type};
}

void Rasterizer::ResetBindings(bool is_compute) {
    for (auto& image_id : bound_images) {
        texture_cache.GetImage(image_id).binding = {};
    }
    for (const auto [buffer, offset, size, src_access] : bound_buffers) {
        const auto dst_stage = is_compute ? vk::PipelineStageFlagBits2::eComputeShader
                                          : vk::PipelineStageFlagBits2::eAllGraphics;
        runtime.AccessBuffer(buffer, offset, size, dst_stage, src_access);
    }
    bound_images.clear();
    bound_buffers.clear();
    needs_barrier = false;
}

bool Rasterizer::IsComputeMetaClear(const Pipeline* pipeline) {
    if (!pipeline->IsCompute()) {
        return false;
    }

    // Most of the time when a metadata is updated with a shader it gets cleared. It means
    // we can skip the whole dispatch and update the tracked state instead. Also, it is not
    // intended to be consumed and in such rare cases (e.g. HTile introspection, CRAA) we
    // will need its full emulation anyways.
    const auto& info = pipeline->GetStage(Shader::SwStage::Compute);

    // Assume if a shader reads metadata, it is a copy shader.
    for (const auto& desc : info.buffers) {
        const VAddr address = desc.GetSharp(info).base_address;
        if (!desc.IsSpecial() && !desc.is_written && texture_cache.IsMeta(address)) {
            return false;
        }
    }

    // Metadata surfaces are tiled and thus need address calculation to be written properly.
    // If a shader wants to encode HTILE, for example, from a depth image it will have to compute
    // proper tile address from dispatch invocation id. This address calculation contains an xor
    // operation so use it as a heuristic for metadata writes that are probably not clears.
    if (!info.has_bitwise_xor) {
        // Assume if a shader writes metadata without address calculation, it is a clear shader.
        for (const auto& desc : info.buffers) {
            const VAddr address = desc.GetSharp(info).base_address;
            if (!desc.IsSpecial() && desc.is_written && texture_cache.ClearMeta(address)) {
                // Assume all slices were updates
                LOG_TRACE(Render_Vulkan, "Metadata update skipped");
                return true;
            }
        }
    }
    return false;
}

bool Rasterizer::IsComputeImageCopy(const Pipeline* pipeline) {
    if (!pipeline->IsCompute()) {
        return false;
    }

    // Ensure shader only has 2 bound buffers
    const auto& cs_pgm = CsRegs();
    const auto& info = pipeline->GetStage(Shader::SwStage::Compute);
    if (cs_pgm.num_thread_x.full != 64 || info.buffers.size() != 2 || !info.images.empty()) {
        return false;
    }

    // Those 2 buffers must both be formatted. One must be source and another destination.
    const auto& desc0 = info.buffers[0];
    const auto& desc1 = info.buffers[1];
    if (!desc0.is_formatted || !desc1.is_formatted || desc0.is_written == desc1.is_written) {
        return false;
    }

    // Buffers must have the same size and each thread of the dispatch must copy 1 dword of data
    const AmdGpu::Buffer buf0 = desc0.GetSharp(info);
    const AmdGpu::Buffer buf1 = desc1.GetSharp(info);
    if (buf0.GetSize() != buf1.GetSize() || cs_pgm.dim_x != (buf0.GetSize() / 256)) {
        return false;
    }

    // Find images the buffer alias
    const auto image0_id = texture_cache.FindImageFromRange(buf0.base_address, buf0.GetSize());
    if (!image0_id) {
        return false;
    }
    const auto image1_id =
        texture_cache.FindImageFromRange(buf1.base_address, buf1.GetSize(), false);
    if (!image1_id) {
        return false;
    }

    // Image copy must be valid
    VideoCore::Image& image0 = texture_cache.GetImage(image0_id);
    VideoCore::Image& image1 = texture_cache.GetImage(image1_id);
    if (image0.info.guest_size != image1.info.guest_size ||
        image0.info.pitch != image1.info.pitch || image0.info.guest_size != buf0.GetSize() ||
        image0.info.num_bits != image1.info.num_bits ||
        image0.info.props.is_bc_decoded || image1.info.props.is_bc_decoded) { // tlg: TLG_BC_TINY
        return false;
    }

    // Perform image copy
    VideoCore::Image& src_image = desc0.is_written ? image1 : image0;
    VideoCore::Image& dst_image = desc0.is_written ? image0 : image1;
    runtime.CopyColorAndDepth(&src_image, &dst_image);
    return true;
}

bool Rasterizer::IsComputeImageClear(const Pipeline* pipeline) {
    if (!pipeline->IsCompute()) {
        return false;
    }

    // Ensure shader only has 2 bound buffers
    const auto& cs_pgm = CsRegs();
    const auto& info = pipeline->GetStage(Shader::SwStage::Compute);
    if (cs_pgm.num_thread_x.full != 64 || info.buffers.size() != 2 || !info.images.empty()) {
        return false;
    }

    // From those 2 buffers, first must hold the clear vector and second the image being cleared
    const auto& desc0 = info.buffers[0];
    const auto& desc1 = info.buffers[1];
    if (desc0.is_formatted || !desc1.is_formatted || desc0.is_written || !desc1.is_written) {
        return false;
    }

    // First buffer must have size of vec4 and second the size of a single layer
    const AmdGpu::Buffer buf0 = desc0.GetSharp(info);
    const AmdGpu::Buffer buf1 = desc1.GetSharp(info);
    const u32 buf1_bpp = AmdGpu::NumBitsPerBlock(buf1.GetDataFmt());
    if (buf0.GetSize() != 16 || (cs_pgm.dim_x * 128ULL * (buf1_bpp / 8)) != buf1.GetSize()) {
        return false;
    }

    // Find image the buffer alias
    const auto image1_id =
        texture_cache.FindImageFromRange(buf1.base_address, buf1.GetSize(), false);
    if (!image1_id) {
        return false;
    }

    // Image clear must be valid
    VideoCore::Image& image1 = texture_cache.GetImage(image1_id);
    if (image1.info.guest_size != buf1.GetSize() || image1.info.num_bits != buf1_bpp ||
        image1.info.props.is_depth || image1.info.props.is_bc_decoded) {
        return false;
    }

    // Perform image clear
    const float* values = reinterpret_cast<float*>(buf0.base_address);
    const vk::ClearValue clear = {
        .color = {.float32 = std::array<float, 4>{values[0], values[1], values[2], values[3]}},
    };
    const VideoCore::SubresourceRange range = {
        .base =
            {
                .level = 0,
                .layer = 0,
            },
        .extent = image1.info.resources,
    };
    runtime.ClearImage(&image1, range, clear);
    return true;
}

void Rasterizer::BindBuffers(const Shader::Info& stage, const PreparedStage* prepared,
                             Shader::Backend::Bindings& binding,
                             Shader::PushData& push_data, u32& write_index) {
    const u64 alignment = instance.StorageMinAlignment();
    for (u32 buffer_index = 0; buffer_index < stage.buffers.size(); ++buffer_index) {
        const auto& desc = stage.buffers[buffer_index];
        const RingBinding* ring = num_ring_stages ? FindRingBinding(stage, buffer_index) : nullptr;
        if (ring) {
            // Copied by the GPU command thread into the constant ring (read-only).
            // tlg: dump the ring's own copy, not the guest address. A binding whose bytes came
            // from the host keeps `address` as a label only, and it can be zero; IsValidGpuMapping
            // merely range checks against the 40 bit GPU limit, so zero passed through and the
            // dump read address zero (crash on the frame capture key).
            if (FrameCapture::Active()) {
                FrameCapture::Buffer(stage.pgm_hash, buffer_index, ring->address,
                                     constant_ring->Data(ring->offset), ring->size);
            }
            if (!desc.IsSpecial()) {
                if (upscaler->KnackProfile() && gbuffer_draw && ring->size == 432) {
                    if (camera_motion->OnKnackConstants(
                        reinterpret_cast<const float*>(constant_ring->Data(ring->offset)), ring->size))
                        knack_video_scene_seen = true;
                }
                if (ring->size == 864 && gbuffer_draw &&
                    memory->IsValidGpuMapping(ring->address, 0)) {
                    camera_motion->OnConstants(reinterpret_cast<const float*>(ring->address));
                }
                push_data.AddOffset(binding.buffer, 0);
            }
            buffer_infos.emplace_back(constant_ring->Handle(), ring->offset, ring->size);
        } else if (desc.IsSpecial()) {
            if (desc.buffer_type == Shader::BufferType::GdsBuffer) {
                const auto* gds_buf = buffer_cache.GetGdsBuffer();
                buffer_infos.emplace_back(gds_buf->Handle(), 0, gds_buf->SizeBytes());
                needs_barrier |= runtime.IsBufferAccessed(gds_buf, 0, gds_buf->SizeBytes());
            } else if (desc.buffer_type == Shader::BufferType::Flatbuf) {
                auto& vk_buffer = buffer_cache.GetStreamBuffer();
                const u32 ubo_size = stage.FlatUserData().size() * sizeof(u32);
                if (FrameCapture::Active()) {
                    FrameCapture::Buffer(stage.pgm_hash, binding.buffer, 0,
                                         stage.FlatUserData().data(), ubo_size);
                }
                const u64 offset =
                    vk_buffer.Copy(stage.FlatUserData().data(), ubo_size, alignment);
                buffer_infos.emplace_back(vk_buffer.Handle(), offset, ubo_size);
            } else if (desc.buffer_type == Shader::BufferType::ClipPlanes) {
                // Permutations compiled without enabled planes never read the buffer, so the
                // declared binding is satisfied with a null descriptor instead of a copy.
                if (Regs().clipper_control.user_clip_plane_enable == 0) {
                    buffer_infos.emplace_back(VK_NULL_HANDLE, 0, VK_WHOLE_SIZE);
                } else {
                    auto& vk_buffer = buffer_cache.GetStreamBuffer();
                    std::array<float, AmdGpu::NUM_CLIP_PLANES * 4> planes{};
                    for (u32 i = 0; i < AmdGpu::NUM_CLIP_PLANES; ++i) {
                        const auto& plane = Regs().clip_user_data[i];
                        planes[i * 4 + 0] = std::bit_cast<float>(plane.data_x);
                        planes[i * 4 + 1] = std::bit_cast<float>(plane.data_y);
                        planes[i * 4 + 2] = std::bit_cast<float>(plane.data_z);
                        planes[i * 4 + 3] = std::bit_cast<float>(plane.data_w);
                    }
                    const u32 ubo_size = static_cast<u32>(sizeof(planes));
                    const u64 offset = vk_buffer.Copy(planes.data(), ubo_size, alignment);
                    buffer_infos.emplace_back(vk_buffer.Handle(), offset, ubo_size);
                }
            } else if (desc.buffer_type == Shader::BufferType::BdaPagetable) {
                const auto* bda_buffer = buffer_cache.GetBdaPageTableBuffer();
                buffer_infos.emplace_back(bda_buffer->Handle(), 0, bda_buffer->SizeBytes());
            } else if (desc.buffer_type == Shader::BufferType::FaultBuffer) {
                const auto* fault_buffer = buffer_cache.GetFaultBuffer();
                buffer_infos.emplace_back(fault_buffer->Handle(), 0, fault_buffer->SizeBytes());
            } else if (desc.buffer_type == Shader::BufferType::SharedMemory) {
                auto& lds_buffer = buffer_cache.GetStreamBuffer();
                const auto& cs_program = CsRegs();
                const auto lds_size = cs_program.SharedMemSize() * cs_program.NumWorkgroups();
                const auto [data, offset] = lds_buffer.Map(lds_size, alignment);
                std::memset(data, 0, lds_size);
                lds_buffer.Commit();
                buffer_infos.emplace_back(lds_buffer.Handle(), offset, lds_size);
            } else {
                UNREACHABLE_MSG("Unexpected buffer type {}", u32(desc.buffer_type));
            }
        } else {
            const auto vsharp =
                prepared ? prepared->buffer_sharps[buffer_index] : desc.GetSharp(stage);
            if (upscaler->KnackProfile() && vsharp.GetSize() == 432 && gbuffer_draw &&
                memory->IsValidGpuMapping(vsharp.base_address, 0)) {
                if (camera_motion->OnKnackConstants(reinterpret_cast<const float*>(vsharp.base_address), 432))
                    knack_video_scene_seen = true;
            }
            if (vsharp.GetSize() == 864 && gbuffer_draw &&
                memory->IsValidGpuMapping(vsharp.base_address, 0)) {
                camera_motion->OnConstants(reinterpret_cast<const float*>(vsharp.base_address));
            }
            // Object motion research: the vertex shader buffers of G-buffer draws.
            if (FrameCapture::Active() && gbuffer_draw && stage.sw_stage == Shader::SwStage::Vertex &&
                vsharp.base_address != 0 && memory->IsValidGpuMapping(vsharp.base_address, 0)) {
                const auto* f = reinterpret_cast<const float*>(vsharp.base_address);
                char note[192];
                std::snprintf(note, sizeof(note),
                              "\n  gb vs %08x slot %u at %#llx size %u stride %u: %g %g %g %g",
                              u32(stage.pgm_hash), binding.buffer,
                              (unsigned long long)vsharp.base_address, u32(vsharp.GetSize()),
                              u32(vsharp.GetStride()), f[0], f[1], f[2], f[3]);
                FrameCapture::Note(note);
            }
            if (FrameCapture::Active() && vsharp.base_address != 0 && vsharp.GetSize() != 0 &&
                memory->IsValidGpuMapping(vsharp.base_address, 0)) {
                FrameCapture::Buffer(stage.pgm_hash, binding.buffer, vsharp.base_address,
                                     reinterpret_cast<const void*>(vsharp.base_address),
                                     memory->ClampRangeSize(vsharp.base_address, vsharp.GetSize()));
            }
            if (vsharp.base_address == 0 || vsharp.GetSize() == 0) {
                buffer_infos.emplace_back(VK_NULL_HANDLE, 0, VK_WHOLE_SIZE);
            } else {
                const u64 size = memory->ClampRangeSize(vsharp.base_address, vsharp.GetSize());
                if (size != vsharp.GetSize()) {
                    LOG_ERROR(Render, "Clamped size from {} to {} for stage {:#x}",
                              vsharp.GetSize(), size, stage.pgm_hash);
                }
                if (!desc.is_written && size >= HugeBindSize) {
                    NoteHugeBind(vsharp.base_address, size);
                }
                const auto [buffer, offset] = buffer_cache.ObtainBuffer(
                    vsharp.base_address, size, desc.is_written, desc.is_formatted);
                const u64 offset_aligned = Common::AlignDown(offset, alignment);
                const u64 adjust = offset - offset_aligned;
                if (adjust % 4 != 0) {
                    LOG_WARNING(Render_Vulkan, "Buffer binding in shader {:#x} isn't dword aligned",
                                stage.pgm_hash);
                }
                push_data.AddOffset(binding.buffer, adjust);
                buffer_infos.emplace_back(buffer->Handle(), offset_aligned, size + adjust);
                bound_buffers.emplace_back(
                    buffer, offset, size,
                    vk::AccessFlagBits2::eShaderRead |
                        (desc.is_written ? vk::AccessFlagBits2::eShaderWrite
                                         : vk::AccessFlagBits2::eNone));
                if (desc.is_written) {
                    // Raw storage-buffer writes can also make an aliased cached image stale.
                    texture_cache.InvalidateMemoryFromGPU(vsharp.base_address, size);
                }
                needs_barrier |= runtime.IsBufferAccessed(buffer, offset, size, desc.is_written);
            }
        }

        auto& set_write = set_writes[write_index++];
        set_write.dstSet = VK_NULL_HANDLE;
        set_write.dstBinding = binding.unified++;
        set_write.dstArrayElement = 0;
        set_write.descriptorCount = 1;
        set_write.descriptorType = vk::DescriptorType::eStorageBuffer;
        set_write.pBufferInfo = &buffer_infos.back();
        ++binding.buffer;
    }
}

Rasterizer::ImageDescCacheEntry& Rasterizer::CachedImageDescEntry(const AmdGpu::Image& sharp,
                                                                   const Shader::ImageResource& res,
                                                                   u64 hash) {
    std::array<u64, 4> key;
    std::memcpy(key.data(), &sharp, sizeof(key));
    // tlg (gow3_pc #348): needs_native too, since FindImage resolves partial height views only for it.
    const u32 flags = u32(res.is_written) | u32(res.is_depth) << 1 | u32(res.is_array) << 2 |
                      u32(res.needs_native) << 3;
    // Two-way set associative: a frame binds a few thousand distinct T#s.
    auto* set = &image_desc_cache[(hash % (image_desc_cache.size() / 2)) * 2];
    const auto matches = [&](const ImageDescCacheEntry& e) {
        return e.flags == flags && e.sharp == key;
    };
    const bool disabled = TlgToggle::Disabled(TlgToggle::ImageDescCache);
    ImageDescCacheEntry* slot = nullptr;
    if (!disabled && matches(set[0])) {
        slot = &set[0];
    } else if (!disabled && matches(set[1])) {
        slot = &set[1];
    } else {
        // Replace the least recently used way that no binding of this call points at.
        slot = set[0].last_use <= set[1].last_use ? &set[0] : &set[1];
        if (slot->pinned == bind_epoch) {
            slot = slot == &set[0] ? &set[1] : &set[0];
        }
        if (slot->pinned == bind_epoch) {
            slot = &image_desc_overflow.emplace_back();
        }
    }
    slot->last_use = ++desc_use_counter;
    auto& entry = *slot;
    if (entry.flags != flags || entry.sharp != key ||
        TlgToggle::Disabled(TlgToggle::ImageDescCache)) {
        entry.desc = VideoCore::TextureCache::ImageDesc{sharp, res};
        entry.sharp = key;
        entry.flags = flags;
        entry.found_generation = ~0ULL;
        entry.view_memo = {};
    }
    return entry;
}

void Rasterizer::BindTextures(const Shader::Info& stage, const PreparedStage* prepared,
                              Shader::Backend::Bindings& binding, u32& write_index,
                              bool& barrier, bool on_helper) {
    const u32 first_image_idx = image_infos.size();
    TextureSet* set_slot = nullptr;
    if (!on_helper && BindTexturesFromSet(stage, prepared, first_image_idx, barrier, set_slot)) {
        // Descriptor writes: one per image (no mip arrays in a memoized set).
        for (u32 i = 0; i < stage.images.size(); ++i) {
            auto& set_write = set_writes[write_index++];
            set_write.dstSet = VK_NULL_HANDLE;
            set_write.dstBinding = binding.unified++;
            set_write.dstArrayElement = 0;
            set_write.descriptorCount = 1;
            set_write.descriptorType = vk::DescriptorType::eSampledImage;
            set_write.pImageInfo = &image_infos[first_image_idx + i];
        }
        BindSamplers(stage, prepared, binding, write_index);
        return;
    }
    image_bindings.clear();
    image_binding_entries.clear();
    image_desc_overflow.clear();
    image_desc_storage.clear();
    ++bind_epoch;
    // To emulate storing to explicit mip levels, build a descriptor array with each mip level.
    boost::container::small_vector<u32, 8> image_descriptor_array_sizes;
    // TextureSetMemo: what this call resolves, to remember when the set qualifies.
    std::array<TextureSetEntry, TextureSet::MaxImages> resolved{};
    bool set_ok = set_slot != nullptr;
    // SampleSceneProxies: bindings that may read a reduced scene proxy instead of the native
    // image (normalized sampling only; see Shader::ImageResource::needs_native).
    boost::container::small_vector<bool, 16> binding_proxy_ok;
    const bool phy03_tracking = GpuPhy03::Recording();
    boost::container::small_vector<GpuPhy03::Access, 16> binding_access;
    const bool sample_proxies = !TlgToggle::Disabled(TlgToggle::SampleSceneProxies) &&
                                !on_helper && scene_targets->Reduced();

    for (u32 image_index = 0; image_index < stage.images.size(); ++image_index) {
        const auto& image_desc = stage.images[image_index];
        const auto tsharp =
            prepared ? prepared->image_sharps[image_index] : image_desc.GetSharp(stage);
        // tlg: a hash lookup per texture per draw for a diagnostic only.
        static const bool warn_meta = std::getenv("TLG_WARN_META_TEXTURE") != nullptr;
        if (warn_meta && texture_cache.IsMeta(tsharp.Address())) {
            LOG_WARNING(Render_Vulkan, "Unexpected metadata read by a shader (texture)");
        }

        const auto data_fmt = tsharp.GetDataFmt();
        const auto num_fmt = tsharp.GetNumberFmt();
        if (tsharp.Address() == 0 || data_fmt == AmdGpu::DataFormat::FormatInvalid) {
            if (phy03_tracking) binding_access.push_back(
                GpuPhy03::ShaderAccess(image_desc.is_written, image_desc.is_atomic));
            image_bindings.emplace_back(VideoCore::ImageId{}, &image_desc_storage.emplace_back());
            image_binding_entries.push_back(nullptr);
            binding_proxy_ok.push_back(false);
            image_descriptor_array_sizes.push_back(1);
            continue;
        }

        if (!memory->IsValidGpuMapping(tsharp.Address(), 0) ||
            !IsKnownFormat(data_fmt, num_fmt)) {
            if (phy03_tracking) binding_access.push_back(
                GpuPhy03::ShaderAccess(image_desc.is_written, image_desc.is_atomic));
            LOG_WARNING(Render_Vulkan,
                        "Rejecting invalid T# address={:#x}, pitch={}, width={}, "
                        "data_format={}, num_format={}",
                        tsharp.Address(), tsharp.pitch, tsharp.width, static_cast<u32>(data_fmt),
                        static_cast<u32>(num_fmt));
            image_bindings.emplace_back(VideoCore::ImageId{}, &image_desc_storage.emplace_back());
            image_binding_entries.push_back(nullptr);
            binding_proxy_ok.push_back(false);
            image_descriptor_array_sizes.push_back(1);
            continue;
        }

        const Shader::MipStorageFallbackMode mip_fallback_mode = image_desc.mip_fallback_mode;
        // Same as image_desc.NumBindings(stage), without fetching the T# again.
        const u32 num_bindings =
            mip_fallback_mode == Shader::MipStorageFallbackMode::DynamicIndex
                ? static_cast<u32>(tsharp.last_level - tsharp.base_level + 1)
                : 1u;
        const bool proxy_candidate = sample_proxies && num_bindings == 1 &&
                                     !image_desc.needs_native && !image_desc.is_written &&
                                     mip_fallback_mode == Shader::MipStorageFallbackMode::None;

        auto& desc_entry =
            prepared ? CachedImageDescEntry(tsharp, image_desc, prepared->image_hashes[image_index])
                     : CachedImageDescEntry(tsharp, image_desc);
        // TLG_SCENE_DEBUG: proxied scene targets this binding reads at the native size, and why.
        const auto debug_native = [&](const VideoCore::Image& image) {
            if (!scene_debug_frame || proxy_candidate || !image.scene_proxy) return;
            std::printf("Scene native read: %s %016llx %s %ux%u needs_native %d written %d "
                        "bindings %u mip_fallback %d\n",
                        stage.sw_stage == Shader::SwStage::Compute ? "cs" : "gfx",
                        (unsigned long long)stage.pgm_hash,
                        vk::to_string(image.info.pixel_format).c_str(), image.info.size.width,
                        image.info.size.height, int(image_desc.needs_native),
                        int(image_desc.is_written), num_bindings, int(mip_fallback_mode));
        };
        for (auto i = 0; i < num_bindings; i++) {
            if (phy03_tracking) binding_access.push_back(
                GpuPhy03::ShaderAccess(image_desc.is_written, image_desc.is_atomic));
            // tlg: a plain binding (no mip override) of the same T# resolves to the same image
            // while no image was registered or unregistered.
            if (mip_fallback_mode == Shader::MipStorageFallbackMode::None &&
                desc_entry.found_generation == texture_cache.RegistryGeneration() &&
                !TlgToggle::Disabled(TlgToggle::TextureBindingMemo)) {
                desc_entry.pinned = bind_epoch;
                auto& [image_id, _] =
                    image_bindings.emplace_back(desc_entry.found_id, &desc_entry.found_desc);
                image_binding_entries.push_back(&desc_entry);
                binding_proxy_ok.push_back(proxy_candidate);
                texture_cache.MarkFound(image_id);
                auto* image = &texture_cache.GetImage(image_id);
                if (auto depth_image_id = texture_cache.GetAssociatedDepth(*image)) {
                    image_id = depth_image_id;
                    image = &texture_cache.GetImage(image_id);
                }
                debug_native(*image);
                if (image->binding.is_bound) {
                    image->binding.force_general |= image_desc.is_written;
                }
                image->binding.is_bound = 1u;
                continue;
            }
            auto& desc = image_desc_storage.emplace_back(desc_entry.desc);
            auto& [image_id, _] = image_bindings.emplace_back(VideoCore::ImageId{}, &desc);
            image_binding_entries.push_back(nullptr);
            binding_proxy_ok.push_back(proxy_candidate);

            if (mip_fallback_mode == Shader::MipStorageFallbackMode::ConstantIndex) {
                ASSERT(num_bindings == 1);
                desc.view_info.range.base.level += image_desc.constant_mip_index;
                desc.view_info.range.extent.levels = 1;
            } else if (mip_fallback_mode == Shader::MipStorageFallbackMode::DynamicIndex) {
                desc.view_info.range.base.level += i;
                desc.view_info.range.extent.levels = 1;
            }

            const u64 generation = texture_cache.RegistryGeneration();
            image_id = texture_cache.FindImage(desc);
            if (mip_fallback_mode == Shader::MipStorageFallbackMode::None &&
                generation == texture_cache.RegistryGeneration() &&
                desc_entry.pinned != bind_epoch) {
                desc_entry.found_generation = generation;
                desc_entry.found_id = image_id;
                desc_entry.found_desc = desc;
                desc_entry.view_memo = {};
            }
            auto* image = &texture_cache.GetImage(image_id);
            if (auto depth_image_id = texture_cache.GetAssociatedDepth(*image)) {
                // If this image has an associated depth image, it's a stencil attachment.
                // Redirect the access to the actual depth-stencil buffer.
                image_id = depth_image_id;
                image = &texture_cache.GetImage(image_id);
            }
            debug_native(*image);
            if (image->binding.is_bound) {
                // The image is already bound. In case if it is about to be used as storage we
                // need to force general layout on it.
                image->binding.force_general |= image_desc.is_written;
            }
            image->binding.is_bound = 1u;
        }

        image_descriptor_array_sizes.push_back(num_bindings);
    }

    // Second pass to re-bind images that were updated after binding
    for (u32 binding_index = 0; binding_index < image_bindings.size(); ++binding_index) {
        auto& [image_id, desc_ptr] = image_bindings[binding_index];
        auto* memo_entry = image_binding_entries[binding_index];
        const auto& desc = *desc_ptr;
        bool is_storage = desc.type == VideoCore::TextureCache::BindingType::Storage;
        if (!image_id) {
            image_infos.emplace_back(VK_NULL_HANDLE, VK_NULL_HANDLE, vk::ImageLayout::eGeneral);
        } else {
            if (auto& old_image = texture_cache.GetImage(image_id);
                old_image.binding.needs_rebind) {
                old_image.binding = {};
                auto& rebind_desc = image_desc_storage.emplace_back(desc);
                desc_ptr = &rebind_desc;
                image_id = texture_cache.FindImage(rebind_desc);
                memo_entry = nullptr;
                set_ok = false;
            }

            bound_images.emplace_back(image_id);

            auto& image = texture_cache.GetImage(image_id);
            if (phy03_tracking && is_storage && GpuPhy03::Recording()) [[unlikely]]
                GpuPhy03::BeginImageWrite(texture_cache, image_id);
            auto& image_view = texture_cache.FindTexture(
                image_id, desc, memo_entry ? &memo_entry->view_memo : nullptr, !on_helper);
            if (phy03_tracking && is_storage && GpuPhy03::Recording()) [[unlikely]]
                GpuPhy03::EndImageWrite(texture_cache, image_id);
            const auto binding = image.binding;

            // tlg: a proxied scene image sampled with normalized coordinates reads the proxy:
            // no resample to the native size (the native image is not touched).
            if (binding_proxy_ok[binding_index] && !is_storage && !binding.force_general &&
                !binding.is_target &&
                !(upscaler->Enabled() && upscaler->RedirectsSampled(image_id))) {
                if (const auto proxy = scene_targets->SampleProxy(image, desc.view_info)) {
                    image.usage.texture = 1u;
                    image_infos.emplace_back(VK_NULL_HANDLE, proxy->view, proxy->layout);
                    if (set_ok && binding_index < resolved.size()) {
                        resolved[binding_index] = {image_id, proxy->view, image.backing,
                                                   desc.view_info.range, true};
                    }
                    ++proxy_samples;
                    continue;
                }
            }
            // The image is either bound as storage in a separate descriptor or bound as render
            // target in feedback loop. Depth images are excluded because they can't be bound as
            // storage and feedback loop doesn't make sense for them
            if ((binding.force_general || binding.is_target) && !image.info.props.is_depth) {
                if (instance.IsAttachmentFeedbackLoopLayoutSupported() && image.binding.is_target) {
                    barrier |= runtime.Transit(
                        &image, vk::ImageLayout::eAttachmentFeedbackLoopOptimalEXT,
                        vk::PipelineStageFlagBits2::eAllGraphics, vk::AccessFlagBits2::eShaderRead);
                } else {
                    barrier |= runtime.Transit(
                        &image, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eAllCommands,
                        vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite);
                }
            } else {
                if (is_storage) {
                    barrier |= runtime.Transit(
                        &image, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eAllCommands,
                        vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
                        desc.view_info.range);
                } else {
                    const auto new_layout = image.info.props.is_depth
                                                ? vk::ImageLayout::eDepthStencilReadOnlyOptimal
                                                : vk::ImageLayout::eShaderReadOnlyOptimal;
                    barrier |= runtime.Transit(
                        &image, new_layout, vk::PipelineStageFlagBits2::eAllCommands,
                        vk::AccessFlagBits2::eShaderRead, desc.view_info.range);
                }
            }
            image.usage.storage |= is_storage;
            image.usage.texture |= !is_storage;
            if (phy03_tracking && GpuPhy03::Recording()) [[unlikely]] {
                GpuPhy03::Sample(instance, scheduler, runtime, texture_cache, image_id,
                                 binding_access[binding_index],
                                 desc.view_info.range, stage.pgm_hash);
            }
            if (FrameCapture::Active()) {
                FrameCapture::Sampled(image.info, is_storage);
            }
            upscaler->ObserveKnackImage(stage.pgm_hash, binding_index, image_id, is_storage);

            vk::ImageView view = *image_view.image_view;
            vk::ImageLayout layout = image.backing->state.layout;
            if (upscaler->Enabled()) {
                // tlg: the display pass reads the upscaled frame (scaled presets).
                upscaler->RedirectSampled(image_id, image_view.info, view, layout);
            }
            image_infos.emplace_back(VK_NULL_HANDLE, view, layout);
            if (set_ok && binding_index < resolved.size()) {
                resolved[binding_index] = {image_id, *image_view.image_view, image.backing,
                                           desc.view_info.range};
                set_ok = !is_storage && !binding.force_general && !binding.is_target;
            }
        }
    }
    if (set_ok && image_bindings.size() == stage.images.size()) {
        // Remember the set: the same T#s resolve the same way while no image is (un)registered.
        set_slot->generation = texture_cache.RegistryGeneration();
        set_slot->scene_generation = scene_targets->Generation();
        set_slot->count = static_cast<u32>(stage.images.size());
        std::copy_n(resolved.begin(), set_slot->count, set_slot->entries.begin());
    } else if (set_slot) {
        set_slot->key = 0;
    }

    u32 image_info_idx = first_image_idx;
    u32 image_binding_idx = 0;
    for (u32 array_size : image_descriptor_array_sizes) {
        const auto& desc = *image_bindings[image_binding_idx].second;
        const bool is_storage = desc.type == VideoCore::TextureCache::BindingType::Storage;
        auto& set_write = set_writes[write_index++];
        set_write.dstSet = VK_NULL_HANDLE;
        set_write.dstBinding = binding.unified;
        set_write.dstArrayElement = 0;
        set_write.descriptorCount = array_size;
        set_write.descriptorType =
            is_storage ? vk::DescriptorType::eStorageImage : vk::DescriptorType::eSampledImage;
        set_write.pImageInfo = &image_infos[image_info_idx];

        image_info_idx += array_size;
        image_binding_idx += array_size;
        binding.unified += array_size;
    }

    BindSamplers(stage, prepared, binding, write_index);
}

void Rasterizer::BindSamplers(const Shader::Info& stage, const PreparedStage* prepared,
                              Shader::Backend::Bindings& binding, u32& write_index) {
    for (u32 sampler_index = 0; sampler_index < stage.samplers.size(); ++sampler_index) {
        const auto& sampler = stage.samplers[sampler_index];
        auto ssharp =
            prepared ? prepared->sampler_sharps[sampler_index] : sampler.GetSharp(stage);
        const auto vk_sampler = texture_cache.GetSampler(
            ssharp, Regs().ta_bc_base, sampler.is_depth, sampler.is_depth ? 0.0f : sampler_lod_bias);
        image_infos.emplace_back(vk_sampler, VK_NULL_HANDLE, vk::ImageLayout::eGeneral);
        auto& set_write = set_writes[write_index++];
        set_write.dstSet = VK_NULL_HANDLE;
        set_write.dstBinding = binding.unified++;
        set_write.dstArrayElement = 0;
        set_write.descriptorCount = 1;
        set_write.descriptorType = vk::DescriptorType::eSampler;
        set_write.pImageInfo = &image_infos.back();
    }
}

Rasterizer::BeginSignature Rasterizer::MakeBeginSignature(const GraphicsPipeline* pipeline) const {
    const auto& key = pipeline->GetGraphicsKey();
    const auto& regs = Regs();
    BeginSignature sig;
    const u32 num_color = std::bit_width(key.mrt_mask);
    for (u32 cb = 0; cb < num_color && cb < AmdGpu::NUM_COLOR_BUFFERS; ++cb) {
        sig.ids[cb] = cb_descs[cb].first;
        if (cb_descs[cb].first) {
            sig.views[cb] = cb_descs[cb].second.view_info;
        }
        sig.color_samples[cb] = key.color_samples[cb];
    }
    sig.ids[AmdGpu::NUM_COLOR_BUFFERS] = db_desc.first;
    if (db_desc.first) {
        sig.views[AmdGpu::NUM_COLOR_BUFFERS] = db_desc.second.view_info;
    }
    sig.mrt_mask = key.mrt_mask;
    sig.num_samples = key.num_samples;
    sig.motion = key.motion_vectors;
    sig.scene_started = scene_started;
    sig.raster_scaling = upscaler->RasterScaling();
    sig.upscaler_state = upscaler->RedirectState();
    sig.generation = texture_cache.RegistryGeneration();
    std::memcpy(&sig.depth_control, &regs.depth_control, sizeof(u32));
    sig.depth_valid = regs.depth_buffer.DepthValid();
    sig.stencil_valid = regs.depth_buffer.StencilValid();
    return sig;
}

bool Rasterizer::BindTexturesFromSet(const Shader::Info& stage, const PreparedStage* prepared,
                                     u32 first_image_idx, bool& barrier, TextureSet*& slot) {
    if (GpuPhy03::Recording()) [[unlikely]] return false;
    const u32 count = static_cast<u32>(stage.images.size());
    if (!prepared || count == 0 || count > TextureSet::MaxImages ||
        TlgToggle::Disabled(TlgToggle::TextureSetMemo) || FrameCapture::Active()) {
        return false;
    }
    for (const auto& image : stage.images) {
        if (image.is_written || image.mip_fallback_mode != Shader::MipStorageFallbackMode::None) {
            return false;
        }
    }
    u64 key = XXH3_64bits_withSeed(prepared->image_hashes, count * sizeof(u64),
                                   reinterpret_cast<u64>(&stage));
    key |= 1; // 0 marks an empty slot
    auto& set = texture_sets[key % texture_sets.size()];
    slot = &set;
    const u64 generation = texture_cache.RegistryGeneration();
    const bool match = set.key == key && set.stage == &stage && set.count == count &&
                       set.generation == generation &&
                       set.scene_generation == scene_targets->Generation() &&
                       std::equal(set.hashes.begin(), set.hashes.begin() + count,
                                  prepared->image_hashes);
    if (!match) {
        ++texture_set_why[set.key != key ? 0 : set.generation != generation ? 1 : 3];
        ++texture_set_misses;
        set.key = key;
        set.stage = &stage;
        set.count = 0;
        set.generation = ~0ull;
        std::copy_n(prepared->image_hashes, count, set.hashes.begin());
        return false;
    }
    // Every image still has the backing its view belongs to and needs no refresh.
    for (u32 i = 0; i < count; ++i) {
        const auto& entry = set.entries[i];
        if (!entry.id) {
            continue;
        }
        const auto& image = texture_cache.GetImage(entry.id);
        // Proxy entries need a current proxy; native entries of a proxied image go through
        // BindTextures, which may sample the proxy instead.
        const bool proxies_on = !TlgToggle::Disabled(TlgToggle::SampleSceneProxies);
        if (image.backing != entry.backing || image.binding.needs_rebind ||
            image.binding.is_target || !texture_cache.IsUpToDate(entry.id) ||
            (entry.proxy ? !proxies_on ||
                               !scene_targets->ProxyCurrent(image, entry.range.base.level,
                                                            entry.range.extent.levels)
                         : image.scene_proxy && proxies_on) ||
            (upscaler->Enabled() && upscaler->RedirectsSampled(entry.id))) {
            ++texture_set_why[2];
            ++texture_set_misses;
            set.generation = ~0ull;
            return false;
        }
    }
    ++texture_set_hits;
    slot = nullptr;
    for (u32 i = 0; i < count; ++i) {
        const auto& entry = set.entries[i];
        if (!entry.id) {
            image_infos.emplace_back(VK_NULL_HANDLE, VK_NULL_HANDLE, vk::ImageLayout::eGeneral);
            continue;
        }
        texture_cache.MarkFound(entry.id);
        auto& image = texture_cache.GetImage(entry.id);
        image.binding.is_bound = 1u;
        bound_images.emplace_back(entry.id);
        image.usage.texture = 1u;
        if (entry.proxy) {
            image_infos.emplace_back(VK_NULL_HANDLE, entry.view,
                                     scene_targets->PrepareSample(image, entry.range.base.level));
            ++proxy_samples;
            continue;
        }
        const auto new_layout = image.info.props.is_depth
                                    ? vk::ImageLayout::eDepthStencilReadOnlyOptimal
                                    : vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier |= runtime.Transit(&image, new_layout, vk::PipelineStageFlagBits2::eAllCommands,
                                   vk::AccessFlagBits2::eShaderRead, entry.range);
        image_infos.emplace_back(VK_NULL_HANDLE, entry.view, image.backing->state.layout);
    }
    (void)first_image_idx;
    return true;
}

RenderState Rasterizer::BeginRendering(const GraphicsPipeline* pipeline) {
    // tlg: RenderStateMemo (see BeginMemo).
    const bool memo_on = !TlgToggle::Disabled(TlgToggle::RenderStateMemo) && !FrameCapture::Active();
    if (TlgStats::enabled && ((begin_memo_hits + begin_memo_misses) & 0x3FFFF) == 0x3FFFF) {
        std::printf("Render state memo: %llu hits, %llu misses; texture sets: %llu hits, %llu "
                    "misses\n",
                    static_cast<unsigned long long>(begin_memo_hits),
                    static_cast<unsigned long long>(begin_memo_misses),
                    static_cast<unsigned long long>(texture_set_hits),
                    static_cast<unsigned long long>(texture_set_misses));
        std::printf("  texture set misses: other key %llu, generation %llu, image check %llu, "
                    "other %llu\n",
                    (unsigned long long)texture_set_why[0], (unsigned long long)texture_set_why[1],
                    (unsigned long long)texture_set_why[2], (unsigned long long)texture_set_why[3]);
        std::printf("  scene proxies sampled directly: %llu bindings\n",
                    static_cast<unsigned long long>(proxy_samples));
        proxy_samples = 0;
        texture_set_why = {};
        begin_memo_hits = begin_memo_misses = texture_set_hits = texture_set_misses = 0;
    }
    BeginSignature signature;
    if (memo_on) {
        signature = MakeBeginSignature(pipeline);
        const auto& regs = Regs();
        bool usable = begin_memo.valid && scheduler.IsRenderingWith(begin_memo.state) &&
                      !regs.depth_render_control.depth_clear_enable &&
                      !regs.depth_render_control.stencil_clear_enable &&
                      signature == begin_memo.signature;
        for (const auto id : signature.ids) {
            if (usable && id) {
                const auto& image = texture_cache.GetImage(id);
                usable = !image.binding.is_bound && !image.binding.needs_rebind;
            }
        }
        if (usable) {
            ++begin_memo_hits;
            attachment_feedback_loop = false;
            push_data.scene_size = begin_memo.scene_size;
            target_scale = begin_memo.target_scale;
            return begin_memo.state;
        }
        ++begin_memo_misses;
    }
    begin_memo.valid = false;
    RenderState state = BeginRenderingFull(pipeline);
    if (memo_on && !attachment_feedback_loop) {
        // (is_clear of the depth attachment shares its bytes with has_depth.)
        bool clears = state.depth_stencil_attachment.depth_clear ||
                      state.depth_stencil_attachment.stencil_clear;
        for (u32 i = 0; i < state.num_color_attachments; ++i) {
            clears |= state.color_attachments[i].is_clear != 0;
        }
        if (!clears) {
            // Recomputed after the full path: it may have started the scene (scene_started).
            begin_memo = {true, MakeBeginSignature(pipeline), state, push_data.scene_size,
                          target_scale};
        }
    }
    return state;
}

RenderState Rasterizer::BeginRenderingFull(const GraphicsPipeline* pipeline) {
    attachment_feedback_loop = false;
    // tlg: frame analyzer census of this pass (sample counts and path, stencil, clears).
    const bool census_on = FrameCapture::Active();
    PassCensus::State census;
    using VulkanUpscalerTarget = TemporalUpscaler::Target;
    VulkanUpscalerTarget redirect{};
    bool color_redirected = false, depth_redirected = false;
    std::pair<u32, u32> guest_extent{1, 1};
    std::pair<vk::ImageView, vk::ImageLayout> original_color{};
    const auto& regs = Regs();
    const auto& key = pipeline->GetGraphicsKey();
    if (std::popcount(key.mrt_mask & 0x7f) >= 5 && db_desc.first &&
        scene_targets->EligibleScene(texture_cache.GetImage(db_desc.first))) scene_started = true;
    // A proxy attachment cannot represent MSAA or a feedback loop that reads the
    // same image through the guest's native descriptor during this draw.
    bool reduced = scene_started && upscaler->RasterScaling() && key.num_samples == 1;
    // TLG_SCENE_DEBUG=<file>: once the file exists, why the passes of one frame keep the
    // native size (see NoteFrameStart).
    const bool debug_pass = scene_debug_frame && (scene_started ||
        (upscaler->KnackProfile() && TlgSettings::Get().native_resolution_active));
    std::string why;
    if (debug_pass) {
        why = fmt::format("pass vs {:08x} ps {:08x} mrt {:#x} samples {} raster_scaling {}:",
                          pipeline->GetStage(Shader::SwStage::Vertex).pgm_hash,
                          key.mrt_mask ? pipeline->GetStage(Shader::SwStage::Fragment).pgm_hash
                                       : 0,
                          key.mrt_mask, u32(key.num_samples), upscaler->RasterScaling());
    }
    // Only this pass's attachments: slots past the mask's width keep earlier passes' targets,
    // often the G-buffer images the lighting passes sample (bound), which kept those native.
    const u32 num_attachments = TlgToggle::Disabled(TlgToggle::SceneAttachmentsOnly)
                                    ? u32(cb_descs.size())
                                    : u32(std::bit_width(key.mrt_mask));
    // All attachments of a reduced pass share one proxy size (scene or half resolution).
    std::optional<SceneResolution::Size> pass_size;
    const auto same_size = [&](const VideoCore::Image& image, u32 level) {
        const auto proxy = scene_targets->ProxySize(image, level);
        if (!pass_size) pass_size = proxy;
        return *pass_size == proxy;
    };
    for (u32 cb = 0; cb < num_attachments; ++cb) {
        const auto& [id, desc] = cb_descs[cb];
        if (id) {
            const auto& image = texture_cache.GetImage(id);
            // Mip levels of eligible chains have their own proxies (SceneTargets::Get).
            if (!scene_targets->Eligible(image) || image.binding.is_bound ||
                image.binding.needs_rebind || desc.view_info.range.base.layer ||
                !same_size(image, desc.view_info.range.base.level)) reduced = false;
            if (debug_pass) {
                why += fmt::format(" [{} {}x{} eligible {} bound {} rebind {} level {} layer {}]",
                                   vk::to_string(image.info.pixel_format), image.info.size.width,
                                   image.info.size.height, scene_targets->Eligible(image),
                                   u32(image.binding.is_bound), u32(image.binding.needs_rebind),
                                   desc.view_info.range.base.level,
                                   desc.view_info.range.base.layer);
            }
        }
    }
    if (db_desc.first && (!scene_targets->Eligible(texture_cache.GetImage(db_desc.first)) ||
                          !same_size(texture_cache.GetImage(db_desc.first), 0))) {
        reduced = false;
    }
    if (debug_pass) {
        if (db_desc.first) {
            const auto& depth = texture_cache.GetImage(db_desc.first);
            why += fmt::format(" depth [{} {}x{} eligible {}]",
                               vk::to_string(depth.info.pixel_format), depth.info.size.width,
                               depth.info.size.height, scene_targets->Eligible(depth));
        }
        std::printf("Scene pass: reduced %d %s\n", reduced, why.c_str());
    }
    push_data.scene_size = reduced ? SceneResolution::Pack(scene_targets->Size()) : 0;
    if (TlgStats::enabled) {
        TlgStats::reduced_draws.fetch_add(reduced, std::memory_order_relaxed);
        TlgStats::scene_draws.fetch_add(scene_started, std::memory_order_relaxed);
    }
    RenderState state;
    state.width = instance.GetMaxFramebufferWidth();
    state.height = instance.GetMaxFramebufferHeight();
    state.num_layers = std::numeric_limits<u16>::max();
    state.num_color_attachments = std::bit_width(key.mrt_mask);
    for (auto cb = 0u; cb < state.num_color_attachments; ++cb) {
        auto& [image_id, desc] = cb_descs[cb];
        if (!image_id) {
            state.color_attachments[cb] = {};
            continue;
        }
        auto* image = &texture_cache.GetImage(image_id);
        if (image->binding.needs_rebind) {
            last_targets[cb].generation = ~0ULL; // FindImage may rewrite the description
            image_id = bound_images.emplace_back(texture_cache.FindImage(desc));
            image = &texture_cache.GetImage(image_id);
        }
        if (GpuPhy03::Recording()) [[unlikely]] GpuPhy03::BeginImageWrite(texture_cache, image_id);
        texture_cache.UpdateImage(image_id);
        runtime.SetBackingSamples(image, key.color_samples[cb]);
        const auto& image_view = texture_cache.FindRenderTarget(image_id, desc);
        if (GpuPhy03::Recording()) [[unlikely]] GpuPhy03::EndImageWrite(texture_cache, image_id);
        const auto slice = image_view.info.range.base.layer;
        const auto mip = image_view.info.range.base.level;

        const auto& col_buf = regs.color_buffers[cb];
        const bool is_clear = texture_cache.IsMetaCleared(col_buf.CmaskAddress(), slice);
        texture_cache.TouchMeta(col_buf.CmaskAddress(), slice, false);

        if (reduced) {
            // The native image is resolved lazily when a shader/copy actually reads it.
        } else if (image->binding.is_bound) {
            ASSERT_MSG(!image->binding.force_general,
                       "Having image both as storage and render target is unsupported");
            runtime.FlushBarriers();
            needs_barrier |=
                runtime.Transit(image,
                                instance.IsAttachmentFeedbackLoopLayoutSupported()
                                    ? vk::ImageLayout::eAttachmentFeedbackLoopOptimalEXT
                                    : vk::ImageLayout::eGeneral,
                                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                vk::AccessFlagBits2::eColorAttachmentWrite |
                                    vk::AccessFlagBits2::eColorAttachmentRead);
            attachment_feedback_loop = true;
        } else {
            needs_barrier |= runtime.Transit(image, vk::ImageLayout::eColorAttachmentOptimal,
                                             vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                             vk::AccessFlagBits2::eColorAttachmentWrite |
                                                 vk::AccessFlagBits2::eColorAttachmentRead,
                                             desc.view_info.range);
        }

        state.width = std::min<u32>(state.width, std::max(image->info.size.width >> mip, 1u));
        state.height = std::min<u32>(state.height, std::max(image->info.size.height >> mip, 1u));
        state.num_layers = std::min<u32>(state.num_layers, image_view.info.range.extent.layers);

        const auto clear_value =
            is_clear ? LiverpoolToVK::ColorBufferClearValue(col_buf) : vk::ClearValue{};
        auto& attachment = state.color_attachments[cb];
        attachment.image_view = *image_view.image_view;
        attachment.image_layout = image->backing->state.layout;
        attachment.clear_value = clear_value.color.uint32;
        attachment.is_clear = is_clear;

        if (reduced) {
            const auto target = scene_targets->Attachment(image_id, desc.view_info);
            attachment.image_view = target.view;
            attachment.image_layout = target.layout;
        }
        image->usage.render_target = 1u;
        if (cb == 0 && upscaler->Enabled()) {
            color_redirected = upscaler->RedirectColor(image_id, desc.view_info, redirect);
            if (color_redirected) {
                guest_extent = {image->info.size.width, image->info.size.height};
                original_color = {attachment.image_view, attachment.image_layout};
                attachment.image_view = redirect.view;
                attachment.image_layout = redirect.layout;
            }
        }
    }
    for (u32 cb = state.num_color_attachments; cb < state.color_attachments.size(); ++cb) {
        state.color_attachments[cb] = {};
    }

    if (auto image_id = db_desc.first; image_id) {
        auto& desc = db_desc.second;
        const auto htile_address = regs.depth_htile_data_base.GetAddress();
        const auto& image_view = texture_cache.FindDepthTarget(image_id, desc);
        auto& image = texture_cache.GetImage(image_id);

        const auto slice = image_view.info.range.base.layer;
        const bool is_depth_clear =
            (regs.depth_render_control.depth_clear_enable && regs.depth_control.depth_enable &&
             regs.depth_control.depth_write_enable) ||
            texture_cache.IsMetaCleared(htile_address, slice);
        const bool is_stencil_clear = regs.depth_render_control.stencil_clear_enable;
        texture_cache.TouchMeta(htile_address, slice, false);
        ASSERT(desc.view_info.range.extent.levels == 1 && !image.binding.needs_rebind);

        const bool has_stencil = image.info.props.has_stencil;
        // Stencil writes can be enabled while depth writes are off.
        const bool stencil_write =
            has_stencil && regs.depth_control.stencil_enable && !desc.view_info.is_storage;
        const auto new_layout = desc.view_info.is_storage
                                    ? has_stencil ? vk::ImageLayout::eDepthStencilAttachmentOptimal
                                                  : vk::ImageLayout::eDepthAttachmentOptimal
                                : stencil_write
                                    ? vk::ImageLayout::eDepthReadOnlyStencilAttachmentOptimal
                                : has_stencil ? vk::ImageLayout::eDepthStencilReadOnlyOptimal
                                              : vk::ImageLayout::eDepthReadOnlyOptimal;
        if (census_on) {
            census.depth_samples = u8(regs.depth_buffer.NumSamples());
            census.stencil_test = regs.depth_control.stencil_enable;
            census.stencil_write = stencil_write;
            census.stencil_clear = is_stencil_clear;
            census.depth_clear = !is_depth_clear ? PassCensus::DepthClear::None
                                 : texture_cache.IsMetaCleared(htile_address, slice)
                                     ? PassCensus::DepthClear::Htile
                                     : PassCensus::DepthClear::Register;
            census.depth_layout = vk::to_string(new_layout);
            census.depth_size = {image.info.size.width, image.info.size.height};
        }
        if (!reduced) needs_barrier |= runtime.Transit(&image, new_layout,
                                         vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                             vk::PipelineStageFlagBits2::eLateFragmentTests,
                                         vk::AccessFlagBits2::eDepthStencilAttachmentWrite |
                                             vk::AccessFlagBits2::eDepthStencilAttachmentRead,
                                         desc.view_info.range);

        state.width = std::min<u32>(state.width, image.info.size.width);
        state.height = std::min<u32>(state.height, image.info.size.height);
        state.num_layers = std::min<u32>(state.num_layers, image_view.info.range.extent.layers);

        auto& attachment = state.depth_stencil_attachment;
        attachment.image_view = *image_view.image_view;
        attachment.image_layout = image.backing->state.layout;
        attachment.clear_value = {};
        attachment.is_clear = 0;

        if (regs.depth_buffer.DepthValid()) {
            attachment.clear_value[0] = is_depth_clear ? std::bit_cast<u32>(regs.depth_clear) : 0u;
            attachment.has_depth = true;
            attachment.depth_clear = is_depth_clear;
        }
        if (regs.depth_buffer.StencilValid()) {
            attachment.clear_value[1] = is_stencil_clear ? regs.stencil_clear : 0u;
            attachment.has_stencil = true;
            attachment.stencil_clear = is_stencil_clear;
        }

        if (reduced) {
            const auto target = scene_targets->Attachment(image_id, desc.view_info);
            attachment.image_view = target.view;
            attachment.image_layout = target.layout;
        }
        image.usage.depth_target = true;
        if (upscaler->Enabled()) {
            VulkanUpscalerTarget depth_redirect;
            depth_redirected = upscaler->RedirectDepth(image_id, depth_redirect);
            if (depth_redirected) {
                guest_extent = {image.info.size.width, image.info.size.height};
                attachment.image_view = depth_redirect.view;
                attachment.image_layout = depth_redirect.layout;
                redirect = depth_redirect;
            }
        }
    } else {
        state.depth_stencil_attachment = {};
    }

    // tlg: a pass drawn into the upscaler's output-size images (UI, display pass): every
    // attachment must be redirected, viewports and scissors are scaled.
    target_scale = {1.0f, 1.0f};
    if (color_redirected || depth_redirected) {
        u32 color_targets = 0;
        for (u32 cb = 0; cb < state.num_color_attachments; ++cb) {
            color_targets += state.color_attachments[cb].image_view ? 1 : 0;
        }
        const bool has_depth = bool(db_desc.first);
        if ((color_targets > 0 && !color_redirected) || (has_depth && !depth_redirected) ||
            color_targets > 1) {
            static u32 warned = 0;
            if (warned < 8) {
                ++warned;
                const auto describe = [&](VideoCore::ImageId id) {
                    if (!id) {
                        return std::string{"-"};
                    }
                    const auto& info = texture_cache.GetImage(id).info;
                    return fmt::format("{:#x} {}x{} {}", info.guest_address, info.size.width,
                                       info.size.height, vk::to_string(info.pixel_format));
                };
                std::printf("Upscaler: pass not redirected (color %s: %s, depth %s: %s, %u "
                            "targets)\n",
                            color_redirected ? "yes" : "no", describe(cb_descs[0].first).c_str(),
                            depth_redirected ? "yes" : "no", describe(db_desc.first).c_str(),
                            state.num_color_attachments);
            }
            if (color_redirected) {
                state.color_attachments[0].image_view = original_color.first;
                state.color_attachments[0].image_layout = original_color.second;
            }
            if (depth_redirected) {
                auto& depth_image = texture_cache.GetImage(db_desc.first);
                state.depth_stencil_attachment.image_view =
                    *texture_cache.FindDepthTarget(db_desc.first, db_desc.second).image_view;
                state.depth_stencil_attachment.image_layout = depth_image.backing->state.layout;
            }
        } else {
            state.width = redirect.width;
            state.height = redirect.height;
            // The UI movie viewport is now 1920x1080 even when the scene is 960x540.
            // A native viewport (or native window-space UI vertices) must not be doubled
            // again. The final display copy still uses the guest render-size coordinates.
            const auto& viewport = regs.viewports[0];
            const bool native_coordinates = redirect.native_ui &&
                (UiComposition::NativeViewport(viewport.xscale * 2, viewport.yscale * 2) ||
                 (regs.IsClipDisabled() && !regs.viewport_control.xscale_enable &&
                  !regs.viewport_control.yscale_enable));
            target_scale = UiComposition::Scale(guest_extent.first, guest_extent.second,
                                                redirect.width, redirect.height,
                                                native_coordinates);
        }
    }

    if (state.num_layers == std::numeric_limits<u16>::max()) {
        state.num_layers = 1;
    }

    if (reduced) {
        // Half-resolution passes keep the scene's factor (proxy / native = scene / 1920).
        const auto size = scene_targets->Size();
        const auto proxy = pass_size.value_or(size);
        state.width = proxy.width;
        state.height = proxy.height;
        target_scale = {float(size.width) / 1920.0f, float(size.height) / 1080.0f};
    }
    if (census_on) {
        std::array<const VideoCore::ImageInfo*, AmdGpu::NUM_COLOR_BUFFERS> colors{};
        for (u32 cb = 0; cb < state.num_color_attachments; ++cb) {
            if (cb_descs[cb].first) {
                colors[cb] = &texture_cache.GetImage(cb_descs[cb].first).info;
                census.color_samples.push_back(u8(regs.color_buffers[cb].NumSamples()));
            }
        }
        const auto* depth =
            db_desc.first ? &texture_cache.GetImage(db_desc.first).info : nullptr;
        census.raster_samples = u8(key.num_samples);
        census.path = PassCensus::ResolveSamplePath(
            census.color_samples, census.depth_samples, depth != nullptr,
            instance.IsMixedAnySamplesSupported(), instance.IsMixedDepthSamplesSupported());
        census.sample_shading =
            regs.ps_input_addr.persp_sample_ena || regs.ps_input_addr.linear_sample_ena;
        census.feedback_loop = attachment_feedback_loop;
        census.area = {state.width, state.height};
        auto census_text = PassCensus::Format(census);
        for (u32 cb = 0; cb < state.num_color_attachments; ++cb) {
            const auto& color = regs.color_buffers[cb];
            const auto& attachment = state.color_attachments[cb];
            const auto& blend = regs.blend_control[cb];
            census_text += fmt::format(
                "\n  color{} clear={} words={:08x},{:08x} value={:08x},{:08x},{:08x},{:08x} "
                "cmask={:#x} fast={} target_mask={:x} shader_mask={:x} "
                "alpha_blend={}/{}/{} separate={}",
                cb, attachment.is_clear, color.clear_word0, color.clear_word1,
                attachment.clear_value[0], attachment.clear_value[1],
                attachment.clear_value[2], attachment.clear_value[3],
                color.CmaskAddress(), u32(color.info.fast_clear),
                regs.color_target_mask.GetMask(cb), key.cb_shader_mask.GetMask(cb),
                u32(blend.alpha_src_factor), u32(blend.alpha_func),
                u32(blend.alpha_dst_factor), u32(blend.separate_alpha_blend));
        }
        FrameCapture::BeginPass(colors.data(), state.num_color_attachments, depth,
                                std::move(census_text));
        diag_targets.clear();
        for (u32 cb = 0; cb < state.num_color_attachments; ++cb) {
            if (cb_descs[cb].first) {
                diag_targets.push_back(cb_descs[cb].first);
            }
        }
    }
    // tlg: object motion vector attachment of G-buffer pipelines.
    if (key.motion_vectors) {
        object_motion->Attach(state, state.width, state.height);
    }
    return state;
}

void Rasterizer::Resolve() {
    const auto& mrt0_hint = CbExtent(0);
    const auto& mrt1_hint = CbExtent(1);
    VideoCore::TextureCache::ImageDesc mrt0_desc{Regs().color_buffers[0], mrt0_hint};
    VideoCore::TextureCache::ImageDesc mrt1_desc{Regs().color_buffers[1], mrt1_hint};
    auto& mrt0_image = texture_cache.GetImage(texture_cache.FindImage(mrt0_desc, true));
    auto& mrt1_image = texture_cache.GetImage(texture_cache.FindImage(mrt1_desc, true));

    ScopeMarkerBegin(fmt::format("Resolve:MRT0={:#x}:MRT1={:#x}",
                                 Regs().color_buffers[0].Address(),
                                 Regs().color_buffers[1].Address()));
    runtime.ResolveImage(&mrt0_image, &mrt1_image, mrt0_desc.view_info.range,
                         mrt1_desc.view_info.range);
    ScopeMarkerEnd();
}

void Rasterizer::DepthStencilCopy(bool is_depth, bool is_stencil) {
    auto& regs = Regs();

    auto read_desc = VideoCore::TextureCache::ImageDesc(
        regs.depth_buffer, regs.depth_view, regs.depth_control,
        regs.depth_htile_data_base.GetAddress(), DbExtent(), false);
    auto write_desc = VideoCore::TextureCache::ImageDesc(
        regs.depth_buffer, regs.depth_view, regs.depth_control,
        regs.depth_htile_data_base.GetAddress(), DbExtent(), true);

    auto& read_image = texture_cache.GetImage(texture_cache.FindImage(read_desc));
    auto& write_image = texture_cache.GetImage(texture_cache.FindImage(write_desc));

    VideoCore::SubresourceRange sub_range;
    sub_range.base.layer = Regs().depth_view.slice_start;
    sub_range.extent.layers = Regs().depth_view.NumSlices() - sub_range.base.layer;

    ScopeMarkerBegin(fmt::format(
        "DepthStencilCopy:DR={:#x}:SR={:#x}:DW={:#x}:SW={:#x}", regs.depth_buffer.DepthAddress(),
        regs.depth_buffer.StencilAddress(), regs.depth_buffer.DepthWriteAddress(),
        regs.depth_buffer.StencilWriteAddress()));

    runtime.CopyDepthStencil(&read_image, &write_image, sub_range);

    ScopeMarkerEnd();
}

void Rasterizer::FillBuffer(VAddr address, u32 num_bytes, u32 value, bool is_gds) {
    DrainDrawPipe();
    ASSERT_MSG(address % 4 == 0 && num_bytes % 4 == 0,
               "FillBuffer address and size must be a multiple of 4 bytes");
    if (!is_gds) {
        texture_cache.ClearMeta(address);
        if (!buffer_cache.IsRegionGpuModified(address, num_bytes)) {
            u32* buffer = std::bit_cast<u32*>(address);
            std::fill(buffer, buffer + (num_bytes / sizeof(u32)), value);
            return;
        }
    }
    const auto [buffer, offset] = [&] -> std::pair<const VideoCore::Buffer*, u64> {
        if (is_gds) {
            return {buffer_cache.GetGdsBuffer(), address};
        }
        return buffer_cache.ObtainBuffer(address, num_bytes, true);
    }();
    runtime.FillBuffer(buffer, offset, num_bytes, value);
}

void Rasterizer::CopyBuffer(VAddr dst, VAddr src, u32 num_bytes, bool dst_gds, bool src_gds) {
    DrainDrawPipe();
    if (!dst_gds && !buffer_cache.IsRegionGpuModified(dst, num_bytes)) {
        if (!src_gds && !buffer_cache.IsRegionGpuModified(src, num_bytes) &&
            !texture_cache.FindImageFromRange(src, num_bytes)) {
            // Both buffers were not transferred to GPU yet. Can safely copy in host memory.
            std::memcpy(std::bit_cast<void*>(dst), std::bit_cast<void*>(src), num_bytes);
            return;
        }
    }
    texture_cache.InvalidateMemoryFromGPU(dst, num_bytes);
    const auto* gds_buffer = buffer_cache.GetGdsBuffer();
    const auto [src_buffer, src_offset] = [&] -> std::pair<const VideoCore::Buffer*, u64> {
        if (src_gds) {
            return {gds_buffer, src};
        }
        return buffer_cache.ObtainBuffer(src, num_bytes, false, true);
    }();
    const auto [dst_buffer, dst_offset] = [&] -> std::pair<const VideoCore::Buffer*, u64> {
        if (dst_gds) {
            return {gds_buffer, dst};
        }
        return buffer_cache.ObtainBuffer(dst, num_bytes, true, true);
    }();
    const vk::BufferCopy copy = {
        .srcOffset = src_offset,
        .dstOffset = dst_offset,
        .size = num_bytes,
    };
    runtime.CopyBuffer(src_buffer, dst_buffer, std::span{&copy, 1});
}

u32 Rasterizer::ReadDataFromGds(u32 gds_offset) {
    DrainDrawPipe();
    auto* gds_buf = buffer_cache.GetGdsBuffer();
    u32 value;
    std::memcpy(&value, gds_buf->mapped_data.data() + gds_offset, sizeof(u32));
    return value;
}

bool Rasterizer::InvalidateMemory(VAddr addr, u64 size, bool assume_locks) {
    DrainDrawPipe();
    if (!IsMapped(addr, size)) {
        // Not GPU mapped memory, can skip invalidation logic entirely.
        return false;
    }
    texture_cache.ResolveReadbacks(addr, size, assume_locks, VideoCore::ReadbackSource::CpuWrite);
    buffer_cache.InvalidateMemory(addr, size, assume_locks);
    texture_cache.InvalidateMemory(addr, size);
    return true;
}

bool Rasterizer::OnWriteFault(VAddr addr, bool assume_locks) {
    DrainDrawPipe();
    if (!InvalidateMemory(addr, 8, assume_locks)) {
        return false;
    }
    buffer_cache.ExtendWriteFault(addr);
    return true;
}

bool Rasterizer::ReadMemory(VAddr addr, u64 size, bool assume_locks) {
    DrainDrawPipe();
    if (!IsMapped(addr, size)) {
        // Not GPU mapped memory, can skip invalidation logic entirely.
        return false;
    }
    const bool image_readback =
        texture_cache.ResolveReadbacks(addr, size, assume_locks, VideoCore::ReadbackSource::CpuRead);
    // tlg: without GPU-modified buffer data there is nothing to download: the guest thread no
    // longer waits for the GPU command thread to reach its request (~3 ms per fault in inFAMOUS,
    // hundreds per second). TLG_FAST_READ_FAULT=0 restores the round trip.
    static const bool fast = [] { const char* e = std::getenv("TLG_FAST_READ_FAULT"); return !(e && *e == '0'); }();
    if (fast && !assume_locks && !buffer_cache.IsRegionGpuModified(addr, size)) {
        buffer_cache.ReleaseReadProtection(addr, size);
    } else if (!assume_locks && !image_readback && buffer_cache.TryEagerRead(addr, size)) {
        // tlg: served from the copy made at the last fence (TLG_EAGER_READBACK). Not after an
        // image readback: that resolved newer data the buffer copy would overwrite.
    } else if (!image_readback || buffer_cache.IsRegionGpuModified(addr, size)) {
        if (!assume_locks) {
            buffer_cache.NoteReadback(addr, size);
        }
        buffer_cache.ReadMemory(addr, size, false, assume_locks);
    }
    return true;
}

void Rasterizer::ProcessDownloadImages() {
    DrainDrawPipe();
    texture_cache.ProcessDownloadImages();
}

bool Rasterizer::IsMapped(VAddr addr, u64 size) {
    if (size == 0) {
        // There is no memory, so not mapped.
        return false;
    }
    if (static_cast<u64>(addr) > std::numeric_limits<u64>::max() - size) {
        // Memory range wrapped the address space, cannot be mapped.
        return false;
    }
    const auto range = decltype(mapped_ranges)::interval_type::right_open(addr, addr + size);

    Common::RecursiveSharedLock lock{mapped_ranges_mutex};
    return boost::icl::contains(mapped_ranges, range);
}

void Rasterizer::MapMemory(VAddr addr, u64 size) {
    {
        std::scoped_lock lock{mapped_ranges_mutex};
        mapped_ranges += decltype(mapped_ranges)::interval_type::right_open(addr, addr + size);
    }
}

void Rasterizer::RegisterMemory(VAddr addr, u64 size) {
    page_manager.OnGpuMap(addr, size);
}

void Rasterizer::UnmapMemory(VAddr addr, u64 size) {
    DrainDrawPipe();
    buffer_cache.InvalidateMemory(addr, size);
    texture_cache.UnmapMemory(addr, size);
    {
        std::scoped_lock lock{mapped_ranges_mutex};
        mapped_ranges -= decltype(mapped_ranges)::interval_type::right_open(addr, addr + size);
    }
}

bool Rasterizer::PrepareKnackVideo(const GraphicsPipeline* pipeline, const RenderState& state,
                                  bool indirect) {
    const float aspect = TlgSettings::Get().active_aspect.load();
    const float scale = TlgGraphics::KnackVideoConversionScale(aspect);
    if (scale==1.0f || !upscaler->KnackProfile() || state.num_color_attachments!=1 ||
        !cb_descs[0].first || target_scale[0]!=1.0f || target_scale[1]!=1.0f ||
        state.width<=0 || state.height<=0 || state.width>8192 || state.height>8192) return false;
    const auto* fragment = pipeline->GetStages()[u32(Shader::SwStage::Fragment)];
    if (!fragment) return false;
    const auto vertex = pipeline->GetStage(Shader::SwStage::Vertex).pgm_hash;
    if (vertex!=UINT64_C(0xa4d9b6a0) ||
        (fragment->pgm_hash!=UINT64_C(0x6e46c324) && fragment->pgm_hash!=UINT64_C(0xc437421c))) return false;
    bool sampled = false;
    for (const auto id : bound_images) {
        const auto& image = texture_cache.GetImage(id);
        for (const auto& video : knack_video_images) {
            sampled |= video.Matches(image.image_uid, image.info.guest_address,
                                      image.alias_generation, image.info.size.width, image.info.size.height);
        }
    }
    const auto& regs = Regs();
    const auto pass = TlgGraphics::KnackVideoDraw(true, vertex, fragment->pgm_hash,
        regs.depth_control.depth_enable, regs.IsClipDisabled(), indirect,
        regs.num_indices, regs.num_instances.NumInstances(), sampled);
    if (pass==TlgGraphics::KnackVideoPass::None) return false;
    const auto& target = texture_cache.GetImage(cb_descs[0].first);
    if (pass==TlgGraphics::KnackVideoPass::Conversion) {
        auto viewports = scheduler.GetDynamicState().viewports;
        for (auto& viewport : viewports) {
            if (viewport.width*scale>float(instance.GetMaxViewportWidth())) return false;
            const float center = float(state.width)*0.5f;
            viewport.x = center+(viewport.x-center)*scale;
            viewport.width *= scale;
        }
        auto& dynamic = scheduler.GetDynamicState();
        dynamic.SetViewports(viewports);
        // The ordinary state has already been committed. Emit the correction in
        // this same command stream before BeginRendering and the conversion draw.
        dynamic.CommitWith(instance.IsDepthBoundsSupported(), instance.IsDynamicColorWriteMaskSupported(),
            instance.IsAttachmentFeedbackLoopLayoutSupported(),
            [&](auto&& command) { scheduler.Record(std::move(command)); });
        auto* slot = &knack_video_images[knack_video_next%knack_video_images.size()];
        for (auto& entry : knack_video_images) {
            if (entry.uid==target.image_uid) { slot=&entry; break; }
        }
        if (slot->uid!=target.image_uid) ++knack_video_next;
        *slot={target.image_uid,target.info.guest_address,target.alias_generation,
               target.info.size.width,target.info.size.height};
        static std::atomic<bool> reported{false};
        if (!reported.exchange(true, std::memory_order_relaxed)) {
            LOG_INFO(Render_Vulkan, "Knack video: native conversion projection, scale {}, target {}x{}",
                     scale, state.width, state.height);
        }
        return true;
    }
    const auto& settings = TlgSettings::Get();
    if (state.width!=settings.native_width.load() || state.height!=settings.native_height.load()) return false;
    knack_video_composed = true;
    return false;
}

void Rasterizer::ClearKnackVideoBars() {
    // Stay in the draw-recording stream: identify the display image actually
    // written by this present dispatch, not a global flag read by another thread.
    for (const auto id : bound_images) {
        auto& image = texture_cache.GetImage(id);
        if (!FrameCapture::IsDisplayBuffer(image.info.guest_address) || image.info.num_samples!=1 ||
            image.info.resources.layers!=1 || image.info.resources.levels!=1) continue;
        const auto format = image.info.pixel_format;
        if (format!=vk::Format::eA2B10G10R10UnormPack32 &&
            format!=vk::Format::eA2R10G10B10UnormPack32 &&
            format!=vk::Format::eR8G8B8A8Unorm && format!=vk::Format::eB8G8R8A8Unorm) continue;
        const auto width = image.info.size.width, height = image.info.size.height;
        const auto bounds = TlgGraphics::KnackVideoContentBounds(int(width),TlgSettings::Get().active_aspect);
        if (bounds[0]<=0 || bounds[1]>=int(width)) continue;
        scheduler.EndRendering();
        VideoCore::ImageViewInfo view;
        view.format = format;
        const auto handle = *image.FindView(view).image_view;
        runtime.Transit(&image,vk::ImageLayout::eColorAttachmentOptimal,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::AccessFlagBits2::eColorAttachmentRead|vk::AccessFlagBits2::eColorAttachmentWrite);
        runtime.FlushBarriers();
        scheduler.Record([=](vk::CommandBuffer command) {
            const vk::RenderingAttachmentInfo attachment{
                .imageView=handle,.imageLayout=vk::ImageLayout::eColorAttachmentOptimal,
                .loadOp=vk::AttachmentLoadOp::eLoad,.storeOp=vk::AttachmentStoreOp::eStore};
            const vk::RenderingInfo rendering{
                .renderArea={{0,0},{width,height}},.layerCount=1,
                .colorAttachmentCount=1,.pColorAttachments=&attachment};
            const vk::ClearAttachment black{
                .aspectMask=vk::ImageAspectFlagBits::eColor,.colorAttachment=0,
                .clearValue=vk::ClearValue{vk::ClearColorValue{std::array<float,4>{0,0,0,1}}}};
            const std::array<vk::ClearRect,2> bars{{
                {.rect={{0,0},{u32(bounds[0]),height}},.baseArrayLayer=0,.layerCount=1},
                {.rect={{bounds[1],0},{width-u32(bounds[1]),height}},.baseArrayLayer=0,.layerCount=1}}};
            command.beginRendering(rendering);
            command.clearAttachments(black,bars);
            command.endRendering();
        });
        runtime.Transit(&image,vk::ImageLayout::eGeneral,vk::PipelineStageFlagBits2::eAllCommands,
            vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite);
        runtime.FlushBarriers();
        static std::atomic<bool> reported{false};
        if (!reported.exchange(true,std::memory_order_relaxed)) {
            LOG_INFO(Render_Vulkan,"Knack video: final 16:9 frame, black sides {}+{} pixels",
                bounds[0],int(width)-bounds[1]);
        }
        return;
    }
}

void Rasterizer::UpdateDynamicState(const GraphicsPipeline* pipeline, const bool is_indexed) const {
    UpdateViewportScissorState();
    UpdateDepthStencilState();
    UpdatePrimitiveState(is_indexed);
    UpdateRasterizationState();
    UpdateColorBlendingState(pipeline);

    auto& dynamic_state = scheduler.GetDynamicState();
    dynamic_state.CommitWith(instance.IsDepthBoundsSupported(),
                             instance.IsDynamicColorWriteMaskSupported(),
                             instance.IsAttachmentFeedbackLoopLayoutSupported(),
                             [&](auto&& command) { scheduler.Record(std::move(command)); });
}

void Rasterizer::UpdateViewportScissorState() const {
    const auto& regs = Regs();

    const auto combined_scissor_value_tl = [](s16 scr, s16 win, s16 gen, s16 win_offset) {
        return std::max({scr, s16(win + win_offset), s16(gen + win_offset)});
    };
    const auto combined_scissor_value_br = [](s16 scr, s16 win, s16 gen, s16 win_offset) {
        return std::min({scr, s16(win + win_offset), s16(gen + win_offset)});
    };
    const bool enable_offset = !regs.window_scissor.window_offset_disable;

    AmdGpu::Scissor scsr{};
    scsr.top_left_x = combined_scissor_value_tl(
        regs.screen_scissor.top_left_x, s16(regs.window_scissor.top_left_x),
        s16(regs.generic_scissor.top_left_x),
        enable_offset ? regs.window_offset.window_x_offset : 0);
    scsr.top_left_y = combined_scissor_value_tl(
        regs.screen_scissor.top_left_y, s16(regs.window_scissor.top_left_y),
        s16(regs.generic_scissor.top_left_y),
        enable_offset ? regs.window_offset.window_y_offset : 0);
    scsr.bottom_right_x = combined_scissor_value_br(
        regs.screen_scissor.bottom_right_x, regs.window_scissor.bottom_right_x,
        regs.generic_scissor.bottom_right_x,
        enable_offset ? regs.window_offset.window_x_offset : 0);
    scsr.bottom_right_y = combined_scissor_value_br(
        regs.screen_scissor.bottom_right_y, regs.window_scissor.bottom_right_y,
        regs.generic_scissor.bottom_right_y,
        enable_offset ? regs.window_offset.window_y_offset : 0);

    boost::container::static_vector<vk::Viewport, AmdGpu::NUM_VIEWPORTS> viewports;
    boost::container::static_vector<vk::Rect2D, AmdGpu::NUM_VIEWPORTS> scissors;

    if (regs.polygon_control.enable_window_offset &&
        (regs.window_offset.window_x_offset != 0 || regs.window_offset.window_y_offset != 0)) {
        LOG_ERROR(Render_Vulkan,
                  "PA_SU_SC_MODE_CNTL.VTX_WINDOW_OFFSET_ENABLE support is not yet implemented.");
    }

    const auto& vp_ctl = regs.viewport_control;
    for (u32 i = 0; i < AmdGpu::NUM_VIEWPORTS; i++) {
        const auto& vp = regs.viewports[i];
        const auto& vp_d = regs.viewport_depths[i];
        if (vp.xscale == 0) {
            continue;
        }

        const auto zoffset = vp_ctl.zoffset_enable ? vp.zoffset : 0.f;
        const auto zscale = vp_ctl.zscale_enable ? vp.zscale : 1.f;

        vk::Viewport viewport{};

        // https://gitlab.freedesktop.org/mesa/mesa/-/blob/209a0ed/src/amd/vulkan/radv_pipeline_graphics.c#L688-689
        // https://gitlab.freedesktop.org/mesa/mesa/-/blob/209a0ed/src/amd/vulkan/radv_cmd_buffer.c#L3103-3109
        // When the clip space is ranged [-1...1], the zoffset is centered.
        // By reversing the above viewport calculations, we get the following:
        if (regs.clipper_control.clip_space == AmdGpu::ClipSpace::MinusWToW) {
            viewport.minDepth = zoffset - zscale;
            viewport.maxDepth = zoffset + zscale;
        } else {
            viewport.minDepth = zoffset;
            viewport.maxDepth = zoffset + zscale;
        }

        if (!instance.IsDepthRangeUnrestrictedSupported()) {
            // Unrestricted depth range not supported by device. Restrict to valid range.
            viewport.minDepth = std::max(viewport.minDepth, 0.f);
            viewport.maxDepth = std::min(viewport.maxDepth, 1.f);
        }

        if (regs.IsClipDisabled()) {
            // In case if clipping is disabled we patch the shader to convert vertex position
            // from screen space coordinates to NDC by defining a render space as full hardware
            // window range [0..16383, 0..16383] and setting the viewport to its size.
            viewport.x = 0.f;
            viewport.y = 0.f;
            viewport.width = float(std::min<u32>(instance.GetMaxViewportWidth(), 16_KB));
            viewport.height = float(std::min<u32>(instance.GetMaxViewportHeight(), 16_KB));
        } else {
            const auto xoffset = vp_ctl.xoffset_enable ? vp.xoffset : 0.f;
            const auto xscale = vp_ctl.xscale_enable ? vp.xscale : 1.f;
            const auto yoffset = vp_ctl.yoffset_enable ? vp.yoffset : 0.f;
            const auto yscale = vp_ctl.yscale_enable ? vp.yscale : 1.f;

            // tlg: sub-pixel jitter of scene geometry for the temporal upscaler; the same
            // shift as jittering the projection.
            viewport.x = (xoffset - xscale) * target_scale[0] + draw_jitter[0];
            viewport.y = (yoffset - yscale) * target_scale[1] + draw_jitter[1];
            viewport.width = xscale * 2.0f * target_scale[0];
            viewport.height = yscale * 2.0f * target_scale[1];
        }

        viewports.push_back(viewport);

        auto vp_scsr = scsr;
        if (regs.mode_control.vport_scissor_enable) {
            vp_scsr.top_left_x =
                std::max(vp_scsr.top_left_x, s16(regs.viewport_scissors[i].top_left_x));
            vp_scsr.top_left_y =
                std::max(vp_scsr.top_left_y, s16(regs.viewport_scissors[i].top_left_y));
            vp_scsr.bottom_right_x = std::min(AmdGpu::Scissor::Clamp(vp_scsr.bottom_right_x),
                                              regs.viewport_scissors[i].bottom_right_x);
            vp_scsr.bottom_right_y = std::min(AmdGpu::Scissor::Clamp(vp_scsr.bottom_right_y),
                                              regs.viewport_scissors[i].bottom_right_y);
        }
        if (target_scale[0] != 1.0f || target_scale[1] != 1.0f) {
            const auto scale = [](s32 v, float f) { return s32(std::lround(float(v) * f)); };
            const s32 x0 = scale(vp_scsr.top_left_x, target_scale[0]);
            const s32 y0 = scale(vp_scsr.top_left_y, target_scale[1]);
            const s32 x1 = scale(vp_scsr.top_left_x + s32(vp_scsr.GetWidth()), target_scale[0]);
            const s32 y1 = scale(vp_scsr.top_left_y + s32(vp_scsr.GetHeight()), target_scale[1]);
            scissors.push_back({
                .offset = {x0, y0},
                .extent = {u32(std::max(x1 - x0, 0)), u32(std::max(y1 - y0, 0))},
            });
        } else {
            scissors.push_back({
                .offset = {vp_scsr.top_left_x, vp_scsr.top_left_y},
                .extent = {vp_scsr.GetWidth(), vp_scsr.GetHeight()},
            });
        }
    }

    if (viewports.empty()) {
        // Vulkan requires providing at least one viewport.
        constexpr vk::Viewport empty_viewport = {
            .x = -1.0f,
            .y = -1.0f,
            .width = 1.0f,
            .height = 1.0f,
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        constexpr vk::Rect2D empty_scissor = {
            .offset = {0, 0},
            .extent = {1, 1},
        };
        viewports.push_back(empty_viewport);
        scissors.push_back(empty_scissor);
    }

    auto& dynamic_state = scheduler.GetDynamicState();
    dynamic_state.SetViewports(viewports);
    dynamic_state.SetScissors(scissors);
}

vk::Pipeline Rasterizer::SquarePass(const GraphicsPipeline& pipeline) const {
    const vk::Pipeline square = pipeline.SquarePassHandle();
    static const bool enabled = [] {
        const char* env = std::getenv("TLG_SQUARE_PASS");
        return !env || env[0] != '0';
    }();
    if (!square || !enabled) [[likely]] {
        return {};
    }
    // MIN(s * s, d * d) = MIN(s, d)^2: the second draw needs the first one's coverage, which a
    // depth or stencil write would change.
    const auto& regs = Regs();
    const bool depth_write = regs.depth_control.depth_enable &&
                             regs.depth_control.depth_write_enable && regs.depth_buffer.DepthValid();
    if (depth_write || regs.depth_control.stencil_enable) {
        static bool warned = false;
        if (!std::exchange(warned, true)) {
            LOG_WARNING(Render_Vulkan, "Scaled MIN/MAX blend not squared: the draw writes depth "
                                       "or stencil");
        }
        return {};
    }
    return square;
}

struct StencilFaceState {
    vk::StencilOp fail_op;
    vk::StencilOp pass_op;
    vk::StencilOp depth_fail_op;
    u32 reference;
};

// tlg: from shadPS4-Infamous-FixBuild 03db8f14 (GPL-2.0-or-later). GCN has separate stencil test
// (STENCILTESTVAL) and replace (STENCILOPVAL) values, while a Vulkan face has a single reference
// for both. Finds a reference that keeps the test exact and still writes what every replacing op
// should, turning such ops into equivalent ones (zero, keep, invert, increment, decrement) where
// the stencil value is known. Returns nullopt if it can't. inFAMOUS hits the conflict ~9500 times
// a session (effects masked by the character silhouette). Checked by the fork against a model of
// the GCN stencil unit (400k random face configurations).
static std::optional<StencilFaceState> ResolveStencilFace(AmdGpu::CompareFunc func,
                                                          const AmdGpu::StencilRefMask& ref,
                                                          u32 write_mask, AmdGpu::StencilFunc fail,
                                                          AmdGpu::StencilFunc zpass,
                                                          AmdGpu::StencilFunc zfail) {
    using AmdGpu::CompareFunc;
    using AmdGpu::StencilFunc;
    const u32 test_val = ref.stencil_test_val;
    const u32 compare_mask = ref.stencil_mask;
    write_mask &= 0xFF;
    const bool test_uses_reference = func != CompareFunc::Always && func != CompareFunc::Never;

    const auto written_value = [&](StencilFunc op) -> std::optional<u32> {
        switch (op) {
        case StencilFunc::ReplaceTest:
            return test_val;
        case StencilFunc::ReplaceOp:
            return ref.stencil_op_val;
        case StencilFunc::Ones:
            return 0xFFU;
        default:
            return std::nullopt;
        }
    };

    // An EQUAL test that passed, or a NOTEQUAL test that failed, means the stencil value matches
    // test_val in the compared bits, which is enough to know what a write will change.
    const bool written_bits_compared = (write_mask & ~compare_mask) == 0;
    const bool whole_value_known = compare_mask == 0xFF && write_mask == 0xFF;
    const bool known_on_pass = func == CompareFunc::Equal && written_bits_compared;
    const bool known_on_fail = func == CompareFunc::NotEqual && written_bits_compared;

    const auto resolve = [&](StencilFunc op, u32 reference,
                             bool value_known) -> std::optional<vk::StencilOp> {
        const auto value = written_value(op);
        if (!value) {
            return LiverpoolToVK::StencilOp(op);
        }
        const u32 target = *value & write_mask;
        if (target == (reference & write_mask)) {
            return vk::StencilOp::eReplace;
        }
        if (target == 0) {
            return vk::StencilOp::eZero;
        }
        if (value_known) {
            if (target == (test_val & write_mask)) {
                return vk::StencilOp::eKeep;
            }
            if (target == (~test_val & write_mask)) {
                return vk::StencilOp::eInvert;
            }
            if (whole_value_known && *value == ((test_val + 1) & 0xFF)) {
                return vk::StencilOp::eIncrementAndWrap;
            }
            if (whole_value_known && *value == ((test_val - 1) & 0xFF)) {
                return vk::StencilOp::eDecrementAndWrap;
            }
        }
        return std::nullopt;
    };

    // Try each written value as the reference. When the test reads the reference, its compared
    // bits must stay those of test_val, and only the rest can carry the written value.
    const std::array<std::optional<u32>, 4> candidates = {
        written_value(zpass), written_value(zfail), written_value(fail), test_val};
    for (const auto& candidate : candidates) {
        if (!candidate) {
            continue;
        }
        const u32 reference = test_uses_reference
                                  ? (test_val & compare_mask) | (*candidate & ~compare_mask & 0xFF)
                                  : *candidate;
        const auto fail_op = resolve(fail, reference, known_on_fail);
        const auto pass_op = resolve(zpass, reference, known_on_pass);
        const auto depth_fail_op = resolve(zfail, reference, known_on_pass);
        if (fail_op && pass_op && depth_fail_op) {
            return StencilFaceState{*fail_op, *pass_op, *depth_fail_op, reference};
        }
    }
    return std::nullopt;
}

void Rasterizer::UpdateDepthStencilState() const {
    const auto& regs = Regs();
    auto& dynamic_state = scheduler.GetDynamicState();

    const auto depth_stencil = GetEffectiveDepthStencilState(regs);
    const bool depth_test_enabled = depth_stencil.depth_test_enable;
    dynamic_state.SetDepthTestEnabled(depth_test_enabled);
    dynamic_state.SetDepthWriteEnabled(depth_stencil.depth_write_enable &&
                                       !regs.depth_render_control.depth_clear_enable);
    if (depth_test_enabled) {
        dynamic_state.SetDepthCompareOp(LiverpoolToVK::CompareOp(regs.depth_control.depth_func));
    }

    const bool depth_bounds_test_enabled = depth_stencil.depth_bounds_enable;
    dynamic_state.SetDepthBoundsTestEnabled(depth_bounds_test_enabled);
    if (depth_bounds_test_enabled) {
        dynamic_state.SetDepthBounds(regs.depth_bounds_min, regs.depth_bounds_max);
    }

    const auto depth_bias_enabled = regs.polygon_control.NeedsBias();
    dynamic_state.SetDepthBiasEnabled(depth_bias_enabled);
    if (depth_bias_enabled) {
        const bool front = regs.polygon_control.enable_polygon_offset_front;
        // tlg: TLG_LIGHTING_TEST=bias passes the slope scale as the guest gives it.
        const float slope = front ? regs.poly_offset.front_scale : regs.poly_offset.back_scale;
        dynamic_state.SetDepthBias(
            front ? regs.poly_offset.front_offset : regs.poly_offset.back_offset,
            regs.poly_offset.depth_bias, LightingTest::Current().bias ? slope : slope / 16.f);
    }

    const bool stencil_test_enabled = depth_stencil.stencil_test_enable;
    dynamic_state.SetStencilTestEnabled(stencil_test_enabled);
    if (stencil_test_enabled) {
        const bool stencil_clear = regs.depth_render_control.stencil_clear_enable;
        const auto front = regs.stencil_ref_front;
        const auto back =
            regs.depth_control.backface_enable ? regs.stencil_ref_back : regs.stencil_ref_front;
        const u32 front_write_mask = !stencil_clear ? front.stencil_write_mask : 0U;
        const u32 back_write_mask = !stencil_clear ? back.stencil_write_mask : 0U;

        const auto setup_face = [](AmdGpu::CompareFunc func, const AmdGpu::StencilRefMask& ref,
                                   u32 write_mask, AmdGpu::StencilFunc fail,
                                   AmdGpu::StencilFunc zpass,
                                   AmdGpu::StencilFunc zfail) -> std::pair<StencilOps, u32> {
            if (const auto face = ResolveStencilFace(func, ref, write_mask, fail, zpass, zfail)) {
                return {StencilOps{
                            .fail_op = face->fail_op,
                            .pass_op = face->pass_op,
                            .depth_fail_op = face->depth_fail_op,
                            .compare_op = LiverpoolToVK::CompareOp(func),
                        },
                        face->reference};
            }
            // GCN REPLACE_OP writes DB_STENCILREFMASK.STENCILOPVAL, so when both values can't be
            // honored a face whose stencil ops include ReplaceOp takes its reference from op_val.
            const bool uses_op_val = fail == AmdGpu::StencilFunc::ReplaceOp ||
                                     zpass == AmdGpu::StencilFunc::ReplaceOp ||
                                     zfail == AmdGpu::StencilFunc::ReplaceOp;
            const u32 reference = uses_op_val ? ref.stencil_op_val : ref.stencil_test_val;
            // tlg: per draw; the first 32 are enough to see which configurations remain.
            static std::atomic<u32> warned{0};
            if (warned.fetch_add(1, std::memory_order_relaxed) < 32) {
                LOG_WARNING(Render_Vulkan,
                            "Stencil func {} test_val {:#x} mask {:#x} can't be combined with ops "
                            "{}/{}/{} op_val {:#x} write mask {:#x}; using reference {:#x}",
                            static_cast<u32>(func), ref.stencil_test_val, ref.stencil_mask,
                            static_cast<u32>(fail), static_cast<u32>(zpass), static_cast<u32>(zfail),
                            ref.stencil_op_val, write_mask, reference);
            }
            return {StencilOps{
                        .fail_op = LiverpoolToVK::StencilOp(fail),
                        .pass_op = LiverpoolToVK::StencilOp(zpass),
                        .depth_fail_op = LiverpoolToVK::StencilOp(zfail),
                        .compare_op = LiverpoolToVK::CompareOp(func),
                    },
                    reference};
        };

        const auto& sc = regs.stencil_control;
        const auto [front_ops, front_reference] =
            setup_face(regs.depth_control.stencil_ref_func, front, front_write_mask,
                       sc.stencil_fail_front, sc.stencil_zpass_front, sc.stencil_zfail_front);
        const auto [back_ops, back_reference] =
            regs.depth_control.backface_enable
                ? setup_face(regs.depth_control.stencil_bf_func, back, back_write_mask,
                             sc.stencil_fail_back, sc.stencil_zpass_back, sc.stencil_zfail_back)
                : std::pair{front_ops, front_reference};
        dynamic_state.SetStencilOps(front_ops, back_ops);
        dynamic_state.SetStencilReferences(front_reference, back_reference);
        dynamic_state.SetStencilWriteMasks(front_write_mask, back_write_mask);
        dynamic_state.SetStencilCompareMasks(front.stencil_mask, back.stencil_mask);
    }
}

void Rasterizer::UpdatePrimitiveState(const bool is_indexed) const {
    const auto& regs = Regs();
    auto& dynamic_state = scheduler.GetDynamicState();

    const auto is_list_topology = [](const AmdGpu::PrimitiveType type) {
        const auto topology = LiverpoolToVK::PrimitiveType(type);
        return topology == vk::PrimitiveTopology::ePointList ||
               topology == vk::PrimitiveTopology::eLineList ||
               topology == vk::PrimitiveTopology::eTriangleList ||
               topology == vk::PrimitiveTopology::eLineListWithAdjacency ||
               topology == vk::PrimitiveTopology::eTriangleListWithAdjacency;
    };
    const auto is_patch_list_topology = [](const AmdGpu::PrimitiveType type) {
        // Quad and rect lists are emulated using tessellation.
        return type == AmdGpu::PrimitiveType::PatchPrimitive ||
               type == AmdGpu::PrimitiveType::QuadList || type == AmdGpu::PrimitiveType::RectList;
    };

    const auto prim_restart =
        (regs.enable_primitive_restart & 1) != 0 &&
        (instance.IsListRestartSupported() || !is_list_topology(regs.primitive_type)) &&
        (instance.IsPatchListRestartSupported() || !is_patch_list_topology(regs.primitive_type));
    ASSERT_MSG(!is_indexed || !prim_restart || regs.primitive_restart_index == 0xFFFF ||
                   regs.primitive_restart_index == 0xFFFFFFFF,
               "Primitive restart index other than -1 is not supported yet");

    const auto cull_mode = LiverpoolToVK::IsPrimitiveCulled(regs.primitive_type)
                               ? LiverpoolToVK::CullMode(regs.polygon_control.CullingMode())
                               : vk::CullModeFlagBits::eNone;
    const auto front_face = LiverpoolToVK::FrontFace(regs.polygon_control.front_face);

    dynamic_state.SetPrimitiveRestartEnabled(prim_restart);
    dynamic_state.SetRasterizerDiscardEnabled(regs.clipper_control.dx_rasterization_kill);
    dynamic_state.SetCullMode(cull_mode);
    dynamic_state.SetFrontFace(front_face);
}

void Rasterizer::UpdateRasterizationState() const {
    const auto& regs = Regs();
    auto& dynamic_state = scheduler.GetDynamicState();
    dynamic_state.SetLineWidth(regs.line_control.Width());
}

void Rasterizer::UpdateColorBlendingState(const GraphicsPipeline* pipeline) const {
    const auto& regs = Regs();
    auto& dynamic_state = scheduler.GetDynamicState();
    dynamic_state.SetBlendConstants(regs.blend_constants);
    dynamic_state.SetColorWriteMasks(pipeline->GetGraphicsKey().write_masks);
    dynamic_state.SetAttachmentFeedbackLoopEnabled(attachment_feedback_loop);
}

void Rasterizer::ScopeMarkerBegin(const std::string_view& str, bool from_guest) {
    DrainDrawPipe();
    if ((from_guest && !EmulatorSettings.IsVkGuestMarkersEnabled()) ||
        (!from_guest && !EmulatorSettings.IsVkHostMarkersEnabled())) {
        return;
    }
    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.beginDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{
        .pLabelName = str.data(),
    });
}

void Rasterizer::ScopeMarkerEnd(bool from_guest) {
    DrainDrawPipe();
    if ((from_guest && !EmulatorSettings.IsVkGuestMarkersEnabled()) ||
        (!from_guest && !EmulatorSettings.IsVkHostMarkersEnabled())) {
        return;
    }
    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.endDebugUtilsLabelEXT();
}

void Rasterizer::ScopedMarkerInsert(const std::string_view& str, bool from_guest) {
    DrainDrawPipe();
    if ((from_guest && !EmulatorSettings.IsVkGuestMarkersEnabled()) ||
        (!from_guest && !EmulatorSettings.IsVkHostMarkersEnabled())) {
        return;
    }
    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.insertDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{
        .pLabelName = str.data(),
    });
}

void Rasterizer::ScopedMarkerInsertColor(const std::string_view& str, const u32 color,
                                         bool from_guest) {
    DrainDrawPipe();
    if ((from_guest && !EmulatorSettings.IsVkGuestMarkersEnabled()) ||
        (!from_guest && !EmulatorSettings.IsVkHostMarkersEnabled())) {
        return;
    }
    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.insertDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{
        .pLabelName = str.data(),
        .color = std::array<f32, 4>(
            {(f32)((color >> 16) & 0xff) / 255.0f, (f32)((color >> 8) & 0xff) / 255.0f,
             (f32)(color & 0xff) / 255.0f, (f32)((color >> 24) & 0xff) / 255.0f})});
}

std::thread::id Rasterizer::GetGpuCommandProcessorThread() {
    return liverpool->GetGpuCommandProcessorThread();
}

#if defined(__linux__) || defined(_WIN32)
u32 Rasterizer::GetGpuCommandProcessorThreadId() {
    return liverpool->GetGpuCommandProcessorThreadId();
}
#endif

} // namespace Vulkan

namespace Vulkan {

void Rasterizer::MarkPass(const GraphicsPipeline* pipeline, const RenderState& state) {
    auto* profiler = GpuProfiler::Get();
    if (!profiler || !scheduler.WillBeginRendering(state)) {
        return;
    }
    const auto& vs = pipeline->GetStage(Shader::SwStage::Vertex);
    const auto* ps = pipeline->GetStages()[u32(Shader::SwStage::Fragment)];
    std::array<u64, AmdGpu::NUM_COLOR_BUFFERS + 6> parts{};
    parts[0] = state.width | u64(state.height) << 32;
    parts[1] = vs.pgm_hash;
    parts[2] = ps ? ps->pgm_hash : 0;
    parts[3] = db_desc.first ? u64(texture_cache.GetImage(db_desc.first).info.pixel_format) : 0;
    for (u32 cb = 0; cb < state.num_color_attachments && cb < AmdGpu::NUM_COLOR_BUFFERS; ++cb) {
        parts[4 + cb] = cb_descs[cb].first
                            ? u64(texture_cache.GetImage(cb_descs[cb].first).info.pixel_format) + 1
                            : 0;
    }
    const u64 key = XXH3_64bits(parts.data(), sizeof(parts));
    profiler->Mark(key, [&] {
        std::string targets;
        for (u32 cb = 0; cb < state.num_color_attachments; ++cb) {
            targets += cb_descs[cb].first
                           ? vk::to_string(texture_cache.GetImage(cb_descs[cb].first).info.pixel_format)
                           : std::string{"-"};
            targets += ' ';
        }
        return fmt::format("pass {}x{} [{}] depth {} vs {:016x} ps {:016x}", state.width,
                           state.height, targets,
                           db_desc.first ? vk::to_string(
                                               texture_cache.GetImage(db_desc.first).info.pixel_format)
                                         : std::string{"-"},
                           vs.pgm_hash, ps ? ps->pgm_hash : 0);
    });
}

void Rasterizer::NoteFrameStart() {
    // Both graphics and Knack's compute presentation reach this frame boundary.
    static const char* scene_debug = std::getenv("TLG_SCENE_DEBUG");
    scene_debug_frame = scene_debug && std::remove(scene_debug) == 0;
    scene_targets->debug = scene_debug_frame;
    if (auto* profiler = GpuProfiler::Get()) {
        profiler->BeginFrame();
    }
    const u64 frame = TlgStats::gpu_frames.fetch_add(1, std::memory_order_relaxed) + 1;
    // TLG_BUFFER_STATS=1: how many earlier frames the GPU has not finished when the GPU thread
    // starts a frame — the lag that decides whether guest memory can be read in place.
    static const bool stats = [] {
        const char* value = std::getenv("TLG_BUFFER_STATS");
        return value && value[0] == '1';
    }();
    if (!stats) {
        return;
    }
    static std::array<u64, 16> frame_ticks{};
    static std::array<u64, 17> lag_histogram{};
    u32 lag = 0;
    for (u32 back = 1; back < frame_ticks.size() && back < frame; ++back) {
        const u64 tick = frame_ticks[(frame - back) % frame_ticks.size()];
        if (tick && !scheduler.IsFree(tick)) {
            lag = back;
        }
    }
    ++lag_histogram[lag];
    frame_ticks[frame % frame_ticks.size()] = scheduler.CurrentTick();
    if (frame % 600 == 0) {
        std::printf("GPU lag at frame start (frames not finished: count):");
        for (u32 i = 0; i < lag_histogram.size(); ++i) {
            if (lag_histogram[i]) {
                std::printf(" %u:%llu", i, static_cast<unsigned long long>(lag_histogram[i]));
            }
        }
        std::printf("\n");
        lag_histogram = {};
    }
}

} // namespace Vulkan
