# Desenvolvimento no Windows

Este guia reúne compilação e diagnóstico para quem deseja trabalhar nos fontes.
Para baixar o pacote pronto e jogar, consulte o [README](../README.md).

## Fontes da versão distribuída

O ZIP `Knack-II-sources-audio-clock09.zip` anexado à
[release alpha](https://github.com/Hyant4/Knack-II-pc-port/releases/tag/knack2-alpha-2026.10.22-audio-clock09)
contém as alterações locais dessa compilação e os submódulos expandidos.
Os arquivos automáticos “Source code” do GitHub correspondem à tag de documentação
e não substituem esse snapshot. `BUILDINFO.json` registra a proveniência.

## Compilar no Windows

Use Git, Python, MSYS2 **CLANG64**, CMake, Ninja e as dependências nativas do runtime. Em um **checkout novo**, sem alterações locais a preservar:

```powershell
git clone --recurse-submodules https://github.com/Hyant4/Knack-II-pc-port.git
cd Knack-II-pc-port
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install iced-x86==1.21.0
```

No shell **MSYS2 CLANG64**, instale as dependências:

```bash
pacman -S --needed git patch \
  mingw-w64-clang-x86_64-{toolchain,cmake,ninja,pkgconf,python} \
  mingw-w64-clang-x86_64-{vulkan-headers,vulkan-loader,vulkan-memory-allocator,glslang} \
  mingw-w64-clang-x86_64-{sdl3,fmt,boost,robin-map,xxhash,ffmpeg,zydis,freetype} \
  mingw-w64-clang-x86_64-{spirv-tools,spirv-headers,spirv-cross}
```

Depois, na raiz do projeto, em PowerShell:

```powershell
# Ajuste apenas se o MSYS2 estiver em outro local.
# $env:MSYS2_ROOT = 'C:\msys64'
.\build.ps1 -Jobs 6
```

O wrapper usa `%LOCALAPPDATA%\knack2-toolchain\msys64` por padrão. O build gera `port/out/tlg-probe.exe` e registra o log em `port/out/gpu-build.log`. Reconstruir o runtime não substitui automaticamente o executável de nome público que já exista no launcher.

O build aplica os patches FSR versionados e pode deixar esse submódulo modificado por desenho; não descarte essas mudanças para “limpar” o checkout. A lista do ambiente observado está em [knack2-toolchain-packages.txt](knack2-toolchain-packages.txt).

Para construir a interface Qt/Python, siga [Reconstruir somente o launcher](LAUNCHER_WINDOWS.md#reconstruir-somente-o-launcher). Essa etapa é separada da compilação do runtime.

## Diagnóstico e testes

A CLI oficial é o `run.py` da raiz:

```powershell
$game = 'D:\Jogos\KnackII\CUSA08014'
.\.venv\Scripts\python.exe -B run.py --game-dir $game --analyze
.\.venv\Scripts\python.exe -B run.py --game-dir $game --check-imports
```

`--check-imports` pode retornar 1 para símbolos ausentes em caminhos não exercitados. Uma contagem estática de imports não equivale à mesma quantidade de bloqueios de gameplay.

Execuções de diagnóstico usam timeout padrão de 180 segundos. Código 124 indica timeout do supervisor e precisa ser distinguido de crash. O launcher normal usa sessões sem esse limite.

Exemplos de regressões focadas, sem iniciar o jogo:

```powershell
.\.venv\Scripts\python.exe -B -m unittest discover -s port/tests -p test_prepare.py -v
.\.venv\Scripts\python.exe -B -m unittest discover -s port/tests -p test_link_modules.py -v
```

Alguns runners dependem da toolchain ou de fixtures locais. Testes sintéticos, build, validação visual e confirmação auditiva são evidências diferentes.

## Referências

- [Guia do launcher](LAUNCHER_WINDOWS.md)
- [Ambiente de compilação observado](knack2-toolchain-packages.txt)
- [Créditos e proveniência](CREDITS.md)
- [Publicação e dados locais](PUBLICACAO.md)
