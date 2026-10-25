"""Synthetic SPIR-V preparation contracts; no game, GPU or installed assets."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('fsr4_prepare_windows', ROOT / 'port/tools/fsr4_prepare_windows.py')
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)

SHADER = '''OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "fsr4_model_v07_i8_pass11" %gid
OpExecutionMode %main LocalSize 64 1 1
OpDecorate %gid BuiltIn GlobalInvocationId
%void = OpTypeVoid
%uint = OpTypeInt 32 0
%bool = OpTypeBool
%v3uint = OpTypeVector %uint 3
%v3bool = OpTypeVector %bool 3
%fn = OpTypeFunction %void
%input = OpTypePointer Input %v3uint
%local = OpTypePointer Function %uint
%zero = OpConstant %uint 0
%one = OpConstant %uint 1
%width = OpConstant %uint 480
%height = OpConstant %uint 270
%channels = OpConstant %uint 32
%bounds = OpConstantComposite %v3uint %width %height %channels
%gid = OpVariable %input Input
%main = OpFunction %void None %fn
%original_entry = OpLabel
%temporary = OpVariable %local Function
%coords = OpLoad %v3uint %gid
%inside = OpULessThan %v3bool %coords %bounds
OpBranch %loop
%loop = OpLabel
%index = OpPhi %uint %zero %original_entry %next %continue
%condition = OpULessThan %bool %index %width
OpLoopMerge %exit %continue None
OpBranchConditional %condition %body %exit
%body = OpLabel
OpStore %temporary %index
OpBranch %continue
%continue = OpLabel
%next = OpIAdd %uint %index %one
OpBranch %loop
%exit = OpLabel
OpReturn
OpFunctionEnd
'''


class GuardTests(unittest.TestCase):
    def test_preserves_shader_body_phi_predecessors_and_function_variables(self):
        patched, bounds = assets.guard_assembly(SHADER.replace('\n', '\r\n'))
        self.assertEqual(bounds, (480, 270))
        body = SHADER[SHADER.index('%coords = OpLoad'):]
        self.assertTrue(patched.endswith(body))
        self.assertIn('%index = OpPhi %uint %zero %original_entry %next %continue', patched)
        self.assertLess(patched.index('%temporary = OpVariable'), patched.index('%tlg_fsr4_guard_gid = OpLoad'))
        self.assertIn('OpBranch %original_entry', patched)

    def test_rejects_barriers_and_double_guard(self):
        for opcode in ('OpControlBarrier', 'OpMemoryBarrier', 'OpGroupNonUniformBroadcast',
                       'OpTypePointer Workgroup'):
            with self.subTest(opcode=opcode), self.assertRaises(ValueError):
                assets.guard_assembly(SHADER + opcode)
        patched, _ = assets.guard_assembly(SHADER)
        with self.assertRaises(ValueError):
            assets.guard_assembly(patched)

    def test_rejects_unknown_and_ambiguous_bounds(self):
        for shader in (SHADER.replace('%uint 32', '%uint 16'),
                       SHADER.replace('fsr4_model_v07_i8_pass11', 'other_pass'),
                       SHADER.replace('%inside =', '%other_bounds = OpConstantComposite %v3uint %height %width %channels\n'
                                      '%other_test = OpULessThan %v3bool %coords %other_bounds\n%inside =')):
            with self.assertRaises(ValueError):
                assets.guard_assembly(shader)

    def test_rejects_output_over_originals(self):
        with tempfile.TemporaryDirectory() as temporary, self.assertRaises(ValueError):
            assets.prepare(Path(temporary), Path(temporary))


TOOLS = Path(os.environ.get('LOCALAPPDATA', '')) / 'knack2-toolchain/msys64/clang64/bin'
HAS_TOOLS = all((TOOLS / (tool + '.exe')).is_file() for tool in ('spirv-as', 'spirv-dis', 'spirv-val'))


@unittest.skipUnless(HAS_TOOLS, 'Existing SPIR-V toolchain unavailable')
class NativePreparationTests(unittest.TestCase):
    def assemble(self, root, name, shader=SHADER):
        text = root / (name + '.spvasm')
        text.write_text(shader, encoding='utf-8')
        binary = root / name
        subprocess.run([str(TOOLS / 'spirv-as.exe'), '--target-env', 'vulkan1.3', str(text), '-o', str(binary)],
                       check=True, capture_output=True, timeout=10)
        return binary

    def test_generated_guard_validates_with_original_loop_phi(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            patched, _ = assets.guard_assembly(SHADER)
            binary = self.assemble(root, 'guard.spv', patched)
            result = subprocess.run([str(TOOLS / 'spirv-val.exe'), '--target-env', 'vulkan1.3', str(binary)],
                                    capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_preparation_keeps_post_bytes_and_does_not_publish_partial_set(self):
        with tempfile.TemporaryDirectory() as temporary, patch.dict(os.environ, {
                'PATH': str(TOOLS) + os.pathsep + os.environ.get('PATH', '')}):
            root = Path(temporary)
            source = root / 'assets'
            source.mkdir()
            model = self.assemble(source, 'fsr4_model_v07_i8_test_pass11.spv')
            original = model.read_bytes()
            post = self.assemble(source, 'fsr4_model_v07_i8_test_post.spv')
            output = root / 'prepared'
            records = assets.prepare(source, output)
            self.assertEqual(len(records), 2)
            self.assertEqual((output / post.name).read_bytes(), post.read_bytes())
            self.assertEqual(model.read_bytes(), original)
            previous = {p.name: p.read_bytes() for p in output.iterdir()}
            (source / 'fsr4_model_v07_i8_other_pass11.spv').write_bytes(original)
            with self.assertRaises(FileNotFoundError):
                assets.prepare(source, output)
            self.assertEqual({p.name: p.read_bytes() for p in output.iterdir()}, previous)


if __name__ == '__main__':
    unittest.main()
