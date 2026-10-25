import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(r'C:\Users\dartv\OneDrive\Documentos\GitHub\knack 2 port pc')
AREA = Path(r'C:\Users\dartv\Documents\Codex\Knack-release-0.17-20261025-004700')
BASE = Path(r'C:\Users\dartv\Documents\Codex\Knack-release-0.16-20261024-190859')
DEPS = Path(r'C:\Users\dartv\Documents\Codex\Knack-release-0.11-20261023\build-deps')
PE_DIR = Path(r'C:\Users\dartv\Documents\Codex\Knack-release-0.14-20261024')

sys.path.insert(0, str(PE_DIR))
sys.path.insert(0, str(DEPS))
from pe_utils import pe, remove_icons

def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def copy(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)

def stamp_runtime(path):
    from PyInstaller.utils.win32.versioninfo import (
        VSVersionInfo, FixedFileInfo, StringFileInfo, StringTable, StringStruct, VarFileInfo, VarStruct)
    values = {'CompanyName': 'Knack II PC Port contributors', 'FileDescription': 'Knack II PC Port Runtime',
              'FileVersion': '0.17.0.0', 'InternalName': 'knack2-runtime', 'OriginalFilename': 'knack2-runtime.exe',
              'ProductName': 'Knack II PC Port', 'ProductVersion': '0.17.0',
              'LegalCopyright': 'Upstream authors credited in README.md and LICENSE files.'}
    version = VSVersionInfo(ffi=FixedFileInfo(filevers=(0,17,0,0), prodvers=(0,17,0,0), mask=0x3f,
                        flags=2, OS=0x40004, fileType=1, subtype=0, date=(0,0)), kids=[
                        StringFileInfo([StringTable('040904B0', [StringStruct(k, v) for k, v in values.items()])]),
                        VarFileInfo([VarStruct('Translation', [1033, 1200])])]).toRaw()
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.BeginUpdateResourceW.argtypes = [wintypes.LPCWSTR, wintypes.BOOL]
    kernel.BeginUpdateResourceW.restype = wintypes.HANDLE
    kernel.UpdateResourceW.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, wintypes.WORD, ctypes.c_void_p, wintypes.DWORD]
    kernel.UpdateResourceW.restype = wintypes.BOOL
    kernel.EndUpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.BOOL]
    kernel.EndUpdateResourceW.restype = wintypes.BOOL
    handle = kernel.BeginUpdateResourceW(str(path), False)
    if not handle: raise ctypes.WinError(ctypes.get_last_error())
    buffer = ctypes.create_string_buffer(version)
    if not kernel.UpdateResourceW(handle, ctypes.c_void_p(16), ctypes.c_void_p(1), 0x409, buffer, len(version)):
        kernel.EndUpdateResourceW(handle, True)
        raise ctypes.WinError(ctypes.get_last_error())
    if not kernel.EndUpdateResourceW(handle, False): raise ctypes.WinError(ctypes.get_last_error())

def main():
    stage = AREA / 'stage/Knack-II-Windows-x64'
    if stage.exists():
        shutil.rmtree(stage)
    stage.parent.mkdir(parents=True, exist_ok=True)

    old_stage = BASE / 'stage/Knack-II-Windows-x64'
    manifest = json.loads((old_stage / 'FILE-MANIFEST.json').read_text(encoding='utf-8'))
    for name, info in manifest.items():
        assert sha(old_stage / name) == info['sha256'], f"Manifest mismatch in base stage: {name}"

    shutil.copytree(old_stage, stage, ignore=lambda directory, names:
                    ['knack-launcher-supervisor'] if Path(directory) == old_stage / 'port/out' else [])

    # Update files from repository root
    for name in ['run.py', 'README.md']:
        copy(ROOT / name, stage / name)
    for folder in ['profiles', 'port/scripts', 'port/launcher/knack2', 'docs']:
        for source in (ROOT / folder).rglob('*'):
            if source.is_file() and '__pycache__' not in source.parts and source.suffix.lower() in (
                    '.py', '.qml', '.js', '.json', '.md', '.png', '.jpg', '.webp'):
                copy(source, stage / source.relative_to(ROOT))

    # Place newly built launcher, supervisor, and runtime
    launcher_src = AREA / 'launcher-dist/Knack II.exe'
    supervisor_src = AREA / 'launcher-dist/KnackSupervisor'
    runtime_src = AREA / 'native-build/tlg-probe.exe'

    copy(launcher_src, stage / 'Knack II.exe')
    shutil.copytree(supervisor_src, stage / 'port/out/knack-launcher-supervisor')
    copy(runtime_src, stage / 'port/out/knack2-runtime.exe')

    # Toolchain strip and stamp runtime
    tc = Path.home() / 'AppData/Local/knack2-toolchain/msys64/clang64/bin'
    env = dict(os.environ, PATH=str(tc) + os.pathsep + os.environ.get('PATH', ''))
    runtime_dst = stage / 'port/out/knack2-runtime.exe'
    raw_text_sha = pe(runtime_dst.read_bytes())['text_sha256']
    subprocess.run([str(tc / 'llvm-strip.exe'), '--strip-debug', str(runtime_dst)], env=env, check=True)
    stamp_runtime(runtime_dst)
    sanitized = {name: remove_icons(stage / name) for name in [
        'Knack II.exe', 'port/out/knack-launcher-supervisor/KnackSupervisor.exe', 'port/out/knack2-runtime.exe']}
    final_text_sha = pe(runtime_dst.read_bytes())['text_sha256']
    assert raw_text_sha == final_text_sha, f"Runtime .text changed during strip/stamp: {raw_text_sha} != {final_text_sha}"

    # Reconcile DLLs
    queue = [runtime_dst]
    seen = set()
    dependency_updates = []
    while queue:
        file = queue.pop()
        if file.name.lower() in seen: continue
        seen.add(file.name.lower())
        for name in pe(file.read_bytes())['imports']:
            candidate = tc / name
            target = stage / 'port/out' / name
            if candidate.is_file():
                if not target.is_file() or sha(candidate) != sha(target):
                    copy(candidate, target)
                    dependency_updates.append(name)
                queue.append(target)
            elif target.is_file():
                queue.append(target)
            elif name.lower().startswith(('api-ms-', 'ext-ms-')):
                continue
            elif not (Path(os.environ.get('WINDIR', 'C:/Windows')) / 'System32' / name).is_file():
                raise RuntimeError('Missing DLL: ' + name)

    release_body = '''# Knack II PC — versão 0.17

Resolução interna nativa em Quad HD (2560×1440) e 4K (3840×2160) até o framebuffer final, com empacotamento Windows completo.

- **Resolução interna nativa QHD e 4K:** os alvos de renderização e o framebuffer final são configurados na resolução escolhida pelo seletor de resolução interna do launcher.
- **Apresentação adaptativa:** a janela adapta o framebuffer final ao monitor existente (incluindo telas 1080p), preservando a composição final em alta fidelidade.
- **Aplicação da resolução:** a preferência de resolução interna é aplicada na reinicialização da sessão.
- **Reserva de memória gráfica dedicada:** aumento da reserva do pool gráfico (768 MiB para QHD e 1536 MiB para 4K, configurando `TLG_DMEM_MB=5824/6592`) para evitar esgotamento de memória (OOM).
- Launcher, supervisor e runtime Windows nativo recompilados e verificados.
- Perfis **CUSA08014 (América)** e **CUSA07670 (Europa)**, versão **01.01**.

## Como atualizar

Baixe `Knack-II-Windows-x64-V0.17.zip`, extraia a pasta inteira e abra `Knack II.exe`.
Selecione o diretório do seu jogo contendo `eboot.bin`, `game.elf`, `sce_sys` e `sce_module` e clique em **Validar**.
O pacote inclui todas as DLLs e recursos FSR necessários; não exige instalar Python, Qt ou MSYS2.

Saves e preferências permanecem em `%LOCALAPPDATA%\\KnackII-PC`. Os testes não substituíram a instalação habitual nem os saves reais.

## Limitações conhecidas

- **Resolução interna alta:** testada pontualmente em Chapter 3-2 no perfil americano CUSA08014 com FSR desligado. Campanha completa, ultrawide em 4K nativo e gameplay europeu em resolução nativa alta permanecem sem validação extensiva.
- **Ultrawide continua experimental:** cutscenes e artes de abertura ainda podem exibir laterais azuis, faixas incompletas ou enquadramento estreito da versão anterior. Use 16:9 se precisar preservar o enquadramento exato das cenas.
- Não há anúncio ou garantia de ganhos de desempenho; o objetivo desta versão é fidelidade visual e resolução interna nativa.

## Validação

- 194 testes unitários automatizados: **190 aprovados e 4 ignorados** (skips documentados de ambiente POSIX e artefatos de build explícitos).
- Smokes do pacote extraído aprovados em ambiente isolado (abertura demo/real, supervisor, preparação ISA, CPU features, Vulkan smoke, encerramento controlado e packaging-check).
- 265 recursos FSR conferidos por tamanho e SHA-256.

## Fontes correspondentes

Use `Knack-II-sources-V0.17.zip` para obter o snapshot completo dos fontes correspondentes a esta compilação.
Confira os downloads com `SHA256SUMS.txt`.
'''
    (stage / 'LEIA-ME.md').write_text(release_body, encoding='utf-8')
    (stage / 'RELEASE-NOTES.md').write_text(release_body, encoding='utf-8')

    tests_result = json.loads((AREA / 'tests-full-result.json').read_text(encoding='utf-8'))
    info = json.loads((old_stage / 'BUILDINFO.json').read_text(encoding='utf-8'))
    info.update({
        'release': '0.17',
        'tag': 'knack2-Version-0.17',
        'launcher_version': '0.17.0.0',
        'runtime_original_sha256': sha(runtime_src),
        'runtime_release_sha256': sha(runtime_dst),
        'runtime_text_sha256': final_text_sha,
        'res03_tested_runtime_sha256': '15b40a9843ad39af26fa2c891ae7a8002222390df5d5cb76312f1e22713d32b0',
        'res03_tested_text_sha256': '16eb8d30f95deaed7bf767db1414d80d273d3986a7c199f7faf29214505a12ac',
        'source_base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'branch': subprocess.check_output(['git', 'branch', '--show-current'], cwd=ROOT, text=True).strip(),
        'dirty_source_snapshot': True,
        'publication': 'release package',
        'profiles': ['CUSA08014-01.01', 'CUSA07670-01.01'],
        'native_resolution': {
            'feature': 'RES-03 native internal resolution (QHD 2560x1440 and 4K 3840x2160)',
            'header': 'port/src/knack_native_resolution.h',
            'dmem_mb': {'2560x1440': 5824, '3840x2160': 6592},
            'restart_required': True
        },
        'release_tests': tests_result
    })
    (stage / 'BUILDINFO.json').write_text(json.dumps(info, indent=2), encoding='utf-8')
    (stage / 'FILE-MANIFEST.json').unlink(missing_ok=True)

    stage_manifest = {}
    for p in sorted(stage.rglob('*')):
        if p.is_file():
            name = p.relative_to(stage).as_posix()
            stage_manifest[name] = {'bytes': p.stat().st_size, 'sha256': sha(p)}
    (stage / 'FILE-MANIFEST.json').write_text(json.dumps(stage_manifest, indent=2), encoding='utf-8')

    report = {
        'stage': str(stage),
        'base_files_verified': len(manifest),
        'stage_files_total': len(stage_manifest),
        'sanitization': sanitized,
        'native_dependencies': sorted(seen),
        'dependency_updates': dependency_updates,
        'runtime_text_sha256': final_text_sha,
        'runtime_release_sha256': sha(runtime_dst)
    }
    (AREA / 'stage-result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print("Stage successfully prepared:", json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
