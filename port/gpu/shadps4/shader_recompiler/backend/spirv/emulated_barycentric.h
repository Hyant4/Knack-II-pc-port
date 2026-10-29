// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <bitset>
#include <stdexcept>
#include <sirit/sirit.h>
#include "shader_recompiler/info.h"

namespace Shader::Backend::SPIRV {

// Triangle geometry supplies the raw parameters and explicit weights without a
// barycentric extension. The fragment module and geometry module share this
// deterministic layout. No guest structures or GPU buffers are changed.
struct EmulatedBarycentricLayout {
    static constexpr u32 Missing = ~0u;
    std::array<u32, IR::NumParams> parameters;
    std::array<u32, 7> weights;
    std::bitset<32> occupied;
    u32 components = 4; // Position

    static constexpr u32 WeightIndex(IR::Attribute attr) {
        return u32(attr) - u32(IR::Attribute::BaryCoordNoPersp);
    }

    EmulatedBarycentricLayout(const HwFragmentRuntimeInfo& fs, const Info& info) {
        parameters.fill(Missing);
        weights.fill(Missing);
        const u32 count = fs.num_inputs - u32(fs.clip_distance_emulation);
        if (fs.clip_distance_emulation) {
            occupied.set(0);
            components += MaxEmulatedClipDistances;
        }
        // Motion interfaces use locations 30/31; currently excluded at selection.
        occupied.set(30); occupied.set(31);
        for (u32 i = 0; i < count; ++i) {
            const auto n = info.loads.NumComponents(IR::Attribute::Param0 + i);
            if (fs.inputs[i].IsDefault() || n == 0 ||
                info.fs_interpolation[i].primary == Qualifier::PerVertex) continue;
            const u32 location = fs.inputs[i].param_index + u32(fs.clip_distance_emulation);
            if (location >= 30) throw std::runtime_error("barycentric fallback: input location exhausted");
            occupied.set(location);
            components += n;
        }
        for (u32 i = 0; i < count; ++i) {
            const auto n = info.loads.NumComponents(IR::Attribute::Param0 + i);
            if (fs.inputs[i].IsDefault() || n == 0 ||
                info.fs_interpolation[i].primary != Qualifier::PerVertex) continue;
            parameters[i] = Allocate(3);
            components += 3 * n;
        }
        for (u32 i = 0; i < 6; ++i) {
            const auto attr = static_cast<IR::Attribute>(u32(IR::Attribute::BaryCoordNoPersp) + i);
            if (info.loads.GetAny(attr) ||
                (attr == IR::Attribute::BaryCoordSmooth &&
                 info.loads.GetAny(IR::Attribute::BaryCoordPullModel))) {
                weights[i] = Allocate(1);
                components += 2;
            }
        }
        weights[6] = weights[WeightIndex(IR::Attribute::BaryCoordSmooth)];
    }

    u32 Allocate(u32 count) {
        for (u32 first = 0; first + count <= 30; ++first) {
            bool free = true;
            for (u32 j = 0; j < count; ++j) free &= !occupied.test(first + j);
            if (!free) continue;
            for (u32 j = 0; j < count; ++j) occupied.set(first + j);
            return first;
        }
        throw std::runtime_error("barycentric fallback: not enough varying locations");
    }
};

inline bool NeedsBarycentricGeometry(const Info& info) {
    for (u32 i = 0; i < 7; ++i)
        if (info.loads.GetAny(static_cast<IR::Attribute>(u32(IR::Attribute::BaryCoordNoPersp) + i))) return true;
    for (u32 i = 0; i < IR::NumParams; ++i)
        if (info.loads.GetAny(IR::Attribute::Param0 + i) &&
            info.fs_interpolation[i].primary == Qualifier::PerVertex) return true;
    return false;
}

// Header-only to keep the existing build/source lists intact.
inline std::vector<u32> EmitBarycentricGeometry(const HwFragmentRuntimeInfo& fs,
                                               const Info& fragment, const Info& vertex,
                                               bool emulate_clip, bool provoking_last,
                                               u32 max_components, u32 max_total_components) {
    using Sirit::Id;
    const EmulatedBarycentricLayout layout(fs, fragment);
    const bool native_clip = vertex.stores.GetAny(IR::Attribute::ClipDistance) && !emulate_clip;
    const bool native_cull = vertex.stores.GetAny(IR::Attribute::CullDistance);
    const u32 components = layout.components + 8 * (u32(native_clip) + u32(native_cull));
    if (components > max_components || 3 * components > max_total_components) {
        throw std::runtime_error("barycentric fallback: geometry output limits exceeded");
    }
    Sirit::Module m{0x00010500};
    m.AddCapability(spv::Capability::Shader);
    m.AddCapability(spv::Capability::Geometry);
    m.SetMemoryModel(spv::AddressingModel::Logical, spv::MemoryModel::GLSL450);
    const Id f = m.TypeFloat(32), u = m.TypeInt(32, false), v = m.TypeVoid();
    const auto integer = [&](u32 n) { return m.Constant(u, n); };
    const Id zero = m.Constant(f, 0.f), one = m.Constant(f, 1.f);
    const auto type = [&](u32 n) { return n == 1 ? f : m.TypeVector(f, n); };
    const auto uint_type = [&](u32 n) { return n == 1 ? u : m.TypeVector(u, n); };
    const Id vec2 = type(2), vec4 = type(4), three = integer(3);
    std::vector<Id> interfaces;
    const auto variable = [&](Id t, spv::StorageClass storage) {
        Id id = m.AddGlobalVariable(m.TypePointer(storage, t), storage);
        interfaces.push_back(id);
        return id;
    };
    const auto input = [&](Id t, u32 location) {
        Id id = variable(m.TypeArray(t, three), spv::StorageClass::Input);
        m.Decorate(id, spv::Decoration::Location, location);
        return id;
    };
    const auto output = [&](Id t, u32 location, bool flat) {
        Id id = variable(t, spv::StorageClass::Output);
        m.Decorate(id, spv::Decoration::Location, location);
        if (flat) m.Decorate(id, spv::Decoration::Flat);
        return id;
    };
    const auto builtin = [&](Id t, spv::StorageClass storage, spv::BuiltIn built) {
        Id id = variable(t, storage);
        m.Decorate(id, spv::Decoration::BuiltIn, u32(built));
        return id;
    };
    const Id position_in = builtin(m.TypeArray(vec4, three), spv::StorageClass::Input, spv::BuiltIn::Position);
    const Id position_out = builtin(vec4, spv::StorageClass::Output, spv::BuiltIn::Position);
    struct Parameter { Id in, out, in_type, out_type; u32 n; bool per_vertex, flat; };
    std::vector<Parameter> parameters;
    const u32 count = fs.num_inputs - u32(fs.clip_distance_emulation);
    const u32 shift = u32(emulate_clip && vertex.stores.GetAny(IR::Attribute::ClipDistance));
    for (u32 i = 0; i < count; ++i) {
        const auto& sharp = fs.inputs[i];
        const u32 n = fragment.loads.NumComponents(IR::Attribute::Param0 + i);
        if (sharp.IsDefault() || n == 0) continue;
        const u32 source_n = vertex.stores.NumComponents(IR::Attribute::Param0 + sharp.param_index);
        if (source_n < n) throw std::runtime_error("barycentric fallback: vertex parameter missing");
        const bool per_vertex = layout.parameters[i] != EmulatedBarycentricLayout::Missing;
        const bool flat = fragment.fs_interpolation[i].primary == Qualifier::Flat;
        const u32 location = per_vertex ? layout.parameters[i] : sharp.param_index + u32(fs.clip_distance_emulation);
        Id in_t = type(source_n), out_t = per_vertex ? uint_type(n) : type(n);
        Id out = output(per_vertex ? m.TypeArray(out_t, three) : out_t, location, flat || per_vertex);
        parameters.push_back({input(in_t, sharp.param_index + shift), out, in_t, out_t, n, per_vertex, flat});
    }
    if (fs.clip_distance_emulation) {
        if (!shift) throw std::runtime_error("barycentric fallback: clip interface mismatch");
        parameters.push_back({input(vec4, 0), output(vec4, 0, false), vec4, vec4, 4, false, false});
    }
    std::array<Id, 6> weight_outputs{};
    for (u32 i = 0; i < 6; ++i) {
        if (layout.weights[i] == EmulatedBarycentricLayout::Missing) continue;
        weight_outputs[i] = output(vec2, layout.weights[i], false);
    }
    struct BuiltinCopy { Id in, out, t; };
    std::vector<BuiltinCopy> copies;
    for (const auto [enabled, kind] : std::array{
            std::pair{native_clip, spv::BuiltIn::ClipDistance},
            std::pair{native_cull, spv::BuiltIn::CullDistance}}) {
        if (!enabled) continue;
        m.AddCapability(kind == spv::BuiltIn::ClipDistance ? spv::Capability::ClipDistance : spv::Capability::CullDistance);
        const Id t = m.TypeArray(f, integer(8));
        copies.push_back({builtin(m.TypeArray(t, three), spv::StorageClass::Input, kind),
                          builtin(t, spv::StorageClass::Output, kind), t});
    }
    Id primitive_in{}, primitive_out{};
    if (fragment.loads.GetAny(IR::Attribute::PrimitiveId)) {
        const Id signed_int = m.TypeInt(32, true);
        primitive_in = builtin(signed_int, spv::StorageClass::Input, spv::BuiltIn::PrimitiveId);
        primitive_out = builtin(signed_int, spv::StorageClass::Output, spv::BuiltIn::PrimitiveId);
    }
    const Id main = m.OpFunction(v, spv::FunctionControlMask::MaskNone, m.TypeFunction(v));
    m.AddLabel();
    for (u32 index = 0; index < 3; ++index) {
        m.OpStore(position_out, m.OpLoad(vec4, m.OpAccessChain(m.TypePointer(spv::StorageClass::Input, vec4), position_in, integer(index))));
        for (const auto& p : parameters) {
            const auto load = [&](u32 vertex_index) {
                Id value = m.OpLoad(p.in_type, m.OpAccessChain(m.TypePointer(spv::StorageClass::Input, p.in_type), p.in, integer(vertex_index)));
                const Id float_type = type(p.n);
                if (p.in_type.value != float_type.value) {
                    if (p.n == 1) value = m.OpCompositeExtract(f, value, 0u);
                    else {
                        std::array<Id, 4> parts;
                        for (u32 c = 0; c < p.n; ++c) parts[c] = m.OpCompositeExtract(f, value, c);
                        value = m.OpCompositeConstruct(float_type, std::span(parts).first(p.n));
                    }
                }
                return p.per_vertex ? m.OpBitcast(p.out_type, value) : value;
            };
            if (p.per_vertex) {
                for (u32 j = 0; j < 3; ++j) {
                    m.OpStore(m.OpAccessChain(m.TypePointer(spv::StorageClass::Output, p.out_type), p.out, integer(j)), load(j));
                }
            } else m.OpStore(p.out, load(p.flat ? (provoking_last ? 2u : 0u) : index));
        }
        const Id weight = m.ConstantComposite(vec2, index == 1 ? one : zero, index == 2 ? one : zero);
        for (Id out : weight_outputs) if (out.value) m.OpStore(out, weight);
        for (const auto& copy : copies) m.OpStore(copy.out, m.OpLoad(copy.t, m.OpAccessChain(m.TypePointer(spv::StorageClass::Input, copy.t), copy.in, integer(index))));
        if (primitive_in.value) m.OpStore(primitive_out, m.OpLoad(m.TypeInt(32, true), primitive_in));
        m.OpEmitVertex();
    }
    m.OpEndPrimitive();
    m.OpReturn();
    m.OpFunctionEnd();
    m.AddEntryPoint(spv::ExecutionModel::Geometry, main, "main", interfaces);
    m.AddExecutionMode(main, spv::ExecutionMode::Triangles);
    m.AddExecutionMode(main, spv::ExecutionMode::OutputTriangleStrip);
    m.AddExecutionMode(main, spv::ExecutionMode::OutputVertices, 3u);
    m.AddExecutionMode(main, spv::ExecutionMode::Invocations, 1u);
    return m.Assemble();
}

} // namespace Shader::Backend::SPIRV
