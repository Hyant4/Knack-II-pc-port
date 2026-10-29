"""GPU startup choices reach the child environment without running the game."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from port.launcher.knack2 import config

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('readback_supervisor', ROOT / 'run.py')
run = importlib.util.module_from_spec(spec)
spec.loader.exec_module(run)


class ReadbackSettingsTests(unittest.TestCase):
    def test_default_precise_replaces_inherited_overrides(self):
        with tempfile.TemporaryDirectory() as temporary, patch.dict(os.environ, {
                'TLG_READBACKS': '0', 'TLG_EAGER_READBACK': '1', 'TLG_READBACK_LINEAR': '1'}):
            env = run.runtime_environment(Path(temporary))
            self.assertEqual(env['TLG_READBACKS'], '2')
            self.assertEqual(env['TLG_EAGER_READBACK'], '0')
            self.assertNotIn('TLG_READBACK_LINEAR', env)

    def test_all_modes_roundtrip_and_explicit_ini_wins(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'knack2.ini').write_text('gpu_readbacks=0\n')
            ini = root / 'selected.ini'
            ini.write_bytes(b'# keep\r\nngs2_render_mix=1\r\nunknown_gpu_option=42\r\n')
            for mode in ('0', '1', '2'):
                for eager in ('0', '1'):
                    with self.subTest(mode=mode, eager=eager):
                        config.update_knack2_ini(ini, {'gpu_readbacks': mode, 'eager_readback': eager})
                        env = run.runtime_environment(root, config_file=ini)
                        self.assertEqual(env['TLG_READBACKS'], mode)
                        self.assertEqual(env['TLG_EAGER_READBACK'], eager if mode == '2' else '0')
                        saved = config.load_knack2_ini(ini)
                        self.assertEqual(saved['gpu_readbacks'], mode)
                        self.assertEqual(saved['eager_readback'], eager)
                        self.assertIn(b'# keep\r\nngs2_render_mix=1\r\nunknown_gpu_option=42\r\n', ini.read_bytes())
            self.assertEqual(run.runtime_environment(root)['TLG_READBACKS'], '0')

    def test_invalid_choices_do_not_write_or_create_capture_directory(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            ini = root / 'knack2.ini'
            original = b'# unchanged\ngpu_readbacks=2\neager_readback=0\n'
            ini.write_bytes(original)
            for key, values in [('gpu_readbacks', ('-1', '3', 'precise', '2.5', '2\ninjected=1')),
                                ('eager_readback', ('2', 'invalid', '1\ninjected=1'))]:
                for value in values:
                    with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                        config.update_knack2_ini(ini, {key: value})
                    self.assertEqual(ini.read_bytes(), original)
            ini.write_text('gpu_readbacks=99\n')
            with self.assertRaises(ValueError):
                run.runtime_environment(root, capture_dir=root / 'captures')
            self.assertFalse((root / 'captures').exists())


TOOLCHAIN = Path(os.environ.get('MSYS2_ROOT') or
                 Path(os.environ.get('LOCALAPPDATA', '')) / 'knack2-toolchain/msys64') / 'clang64'
COMPILER = TOOLCHAIN / 'bin/clang++.exe'


@unittest.skipUnless(COMPILER.is_file(), 'Existing Windows clang++ toolchain unavailable')
class NativeReadbackDefaultTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='knack-readback-native-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name)
        source = cls.directory / 'default.cpp'
        source.write_text('#include <cstdio>\n#include "core/emulator_settings.h"\n'
                          'int main() { std::printf("%u", EmulatorSettings.GetReadbacksMode()); }\n')
        cls.executable = cls.directory / 'default.exe'
        cls.env = dict(os.environ, PATH=str(TOOLCHAIN / 'bin') + os.pathsep + os.environ.get('PATH', ''))
        result = subprocess.run([str(COMPILER), '-std=c++20', '-I', str(ROOT / 'port/gpu/shim'),
                                 '-I', str(ROOT / 'port/gpu/shadps4'), str(source), '-o', str(cls.executable)],
                                env=cls.env, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        settings_source = cls.directory / 'settings.cpp'
        settings_source.write_text(r'''
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include "tlg_settings.h"
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "FAIL: %s\n", #expr); return 1; } } while (0)
std::string read(const char* path) { std::ifstream f(path); std::ostringstream s; s << f.rdbuf(); return s.str(); }
int main(int argc, char** argv) {
    CHECK(argc == 2);
    _putenv_s("TLG_CONFIG", argv[1]);
    _putenv_s("TLG_READBACKS", "");
    _putenv_s("TLG_EAGER_READBACK", "");
    auto& v = TlgSettings::Get();
    CHECK(v.gpu_readbacks == 2 && !v.eager_readback);
    { std::ofstream f(argv[1]); f << "gpu_readbacks=1\neager_readback=1\n"; }
    TlgSettings::Load();
    CHECK(v.gpu_readbacks == 1 && v.eager_readback);
    TlgSettings::Save();
    CHECK(read(argv[1]).find("gpu_readbacks=1\neager_readback=1\n") != std::string::npos);
    _putenv_s("TLG_READBACKS", "2");
    _putenv_s("TLG_EAGER_READBACK", "0");
    TlgSettings::Load();
    CHECK(v.gpu_readbacks == 2 && !v.eager_readback);
    TlgSettings::Save();
    CHECK(read(argv[1]).find("gpu_readbacks=2\neager_readback=0\n") != std::string::npos);
    _putenv_s("TLG_READBACKS", "");
    _putenv_s("TLG_EAGER_READBACK", "");
    { std::ofstream f(argv[1]); f << "gpu_readbacks=99\n"; }
    TlgSettings::Load();
    CHECK(v.gpu_readbacks == 2);
    return 0;
}
''')
        cls.settings_executable = cls.directory / 'settings.exe'
        result = subprocess.run([str(COMPILER), '-std=c++23', '-I', str(ROOT / 'port/gpu/shim'),
                                 str(settings_source), str(ROOT / 'port/gpu/shim/tlg_settings.cpp'),
                                 '-o', str(cls.settings_executable)], env=cls.env,
                                capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def test_native_ini_load_save_and_environment_precedence(self):
        result = subprocess.run([str(self.settings_executable), str(self.directory / 'settings.ini')],
                                env=self.env, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_actual_native_header_defaults_to_precise_and_accepts_modes(self):
        for mode in (None, '0', '1', '2'):
            with self.subTest(mode=mode):
                env = dict(self.env)
                env.pop('TLG_READBACKS', None)
                if mode is not None:
                    env['TLG_READBACKS'] = mode
                result = subprocess.run([str(self.executable)], env=env, capture_output=True,
                                        text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, '2' if mode is None else mode)


if __name__ == '__main__':
    unittest.main()
