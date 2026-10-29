// SPDX-License-Identifier: GPL-2.0-or-later
// Compile synthetic interpolation with the production backend and geometry emitter.
#include <cassert>
#include <filesystem>
#include <fstream>
#include "shader_recompiler/backend/spirv/emulated_barycentric.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/translate/translate.h"
#include "shader_recompiler/ir/ir_emitter.h"

using namespace Shader;

static void Write(const std::filesystem::path& path, const std::vector<u32>& code) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(code.data()), code.size()*4);
    assert(file);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::filesystem::path dir(argv[1]);
    std::filesystem::create_directories(dir);
    for (const auto [label, attr] : std::array{
        std::pair{"smooth", IR::Attribute::BaryCoordSmooth},
        std::pair{"sample", IR::Attribute::BaryCoordSmoothSample},
        std::pair{"centroid", IR::Attribute::BaryCoordSmoothCentroid},
        std::pair{"linear", IR::Attribute::BaryCoordNoPersp},
        std::pair{"linear-sample", IR::Attribute::BaryCoordNoPerspSample},
        std::pair{"linear-centroid", IR::Attribute::BaryCoordNoPerspCentroid},
        std::pair{"pull", IR::Attribute::BaryCoordPullModel}}) {
        Info fragment{}, vertex{};
        fragment.hw_stage = HwStage::Fragment; fragment.sw_stage = SwStage::Fragment;
        RuntimeInfo runtime{}; runtime.Initialize(HwStage::Fragment, SwStage::Fragment);
        auto& fs = runtime.hw.fs;
        fs.emulated_barycentric = true;
        fs.num_inputs = 2;
        fs.inputs[0].param_index = 0; fs.inputs[1].param_index = 1;
        fs.color_buffers[0].num_format = AmdGpu::NumberFormat::Float;
        fragment.fs_interpolation[0].primary = Qualifier::PerVertex;
        // A previous P2 can leave this qualifier on an attribute later read by MOV.
        // Raw flat integer payloads must not inherit a Sample decoration.
        fragment.fs_interpolation[0].auxiliary = Qualifier::Sample;
        fragment.fs_interpolation[1].primary = Qualifier::Smooth;
        fragment.loads.Set(IR::Attribute::Param0, 0); fragment.loads.Set(IR::Attribute::Param0, 1);
        fragment.loads.Set(IR::Attribute::Param1, 0);
        fragment.loads.Set(attr, 0); fragment.loads.Set(attr, 1);
        if (attr == IR::Attribute::BaryCoordPullModel) fragment.loads.Set(attr, 2);
        for (u32 c=0; c<2; ++c) vertex.stores.Set(IR::Attribute::Param0, c);
        vertex.stores.Set(IR::Attribute::Param1, 0);
        Common::ObjectPool<IR::Inst> pool;
        IR::Block block(pool); IR::IREmitter ir(block);
        ir.Prologue();
        const auto decode = [&](u32 corner, u32 component) {
            const IR::U32 packed = ir.BitCast<IR::U32>(ir.GetAttribute(IR::Attribute::Param0, component/2, corner));
            return IR::F32{ir.CompositeExtract(ir.Unpack2x16(AmdGpu::NumberFormat::Float, packed), component%2)};
        };
        IR::F32 i = ir.GetAttribute(attr, 0), j = ir.GetAttribute(attr, 1);
        if (attr == IR::Attribute::BaryCoordPullModel) {
            const auto w = ir.GetAttribute(attr, 2);
            i = ir.FPDiv(i, w); j = ir.FPDiv(j, w);
        }
        for (u32 c=0; c<3; ++c) {
            const auto p0=decode(0,c), p1=decode(1,c), p2=decode(2,c);
            ir.SetAttribute(IR::Attribute::RenderTarget0,
                ir.FPAdd(ir.FPAdd(p0, ir.FPMul(ir.FPSub(p1,p0),i)), ir.FPMul(ir.FPSub(p2,p0),j)), c);
            fragment.stores.Set(IR::Attribute::RenderTarget0, c);
        }
        ir.SetAttribute(IR::Attribute::RenderTarget0, ir.GetAttribute(IR::Attribute::Param1,0), 3);
        fragment.stores.Set(IR::Attribute::RenderTarget0, 3);
        ir.Epilogue();
        IR::Program program(fragment); program.blocks.push_back(&block);
        program.syntax_list.push_back({.data={.block=&block}, .type=IR::AbstractSyntaxNode::Type::Block});
        program.syntax_list.push_back({.type=IR::AbstractSyntaxNode::Type::Return});
        Profile profile{}; profile.supported_spirv=0x00010600; profile.support_int64=true;
        Backend::Bindings binding{};
        Write(dir/(std::string(label)+".frag.spv"), Backend::SPIRV::EmitSPIRV(profile,runtime,program,binding));
        Write(dir/(std::string(label)+".geom.spv"), Backend::SPIRV::EmitBarycentricGeometry(fs,fragment,vertex,false,false,128,1024));
        assert(Backend::SPIRV::NeedsBarycentricGeometry(fragment));
        bool rejected=false;
        try { Backend::SPIRV::EmitBarycentricGeometry(fs,fragment,vertex,false,false,4,12); }
        catch(const std::runtime_error&) { rejected=true; }
        assert(rejected);
    }
    // Exercise the translator's actual prologue rather than only creating IR by hand.
    for (const bool emulated : {false,true}) {
        Info info{}; info.hw_stage=HwStage::Fragment; info.sw_stage=SwStage::Fragment;
        RuntimeInfo runtime{}; runtime.Initialize(HwStage::Fragment,SwStage::Fragment);
        runtime.hw.fs.emulated_barycentric=emulated;
        runtime.hw.fs.addr_flags.persp_sample_ena=1;
        Profile profile{};
        Common::ObjectPool<IR::Inst> pool; IR::Block block(pool);
        Gcn::Translator translator(info,runtime,profile); translator.EmitPrologue(&block);
        u32 bary=0;
        for(const auto& inst:block.Instructions())
            if(inst.GetOpcode()==IR::Opcode::GetAttribute && inst.Arg(0).Attribute()==IR::Attribute::BaryCoordSmoothSample) ++bary;
        assert(bary==(emulated ? 2u : 0u));
    }
    std::puts("barycentric shaders: all checks passed");
}
