# Desenvolvimento Linux — Knack II

## Estado

Suporte Linux em desenvolvimento, para hosts x86-64 com glibc. O runtime
executa os binários originais do dump CUSA08014 01.01. A interface continua
usando Qt/QML; gráficos usam Vulkan e áudio/controles usam SDL3.

Build Linux validado em Ubuntu 26.04.1 LTS, GCC 15.2 e WSL 2 em 24/10/2026.
O executável ELF x86-64 passou na verificação de CPU e num smoke Vulkan com
envio de comandos e leitura de 4096 bytes. O driver disponível no WSL foi
llvmpipe, por software; isso não valida uma GPU física.

Testes nativos Linux cobriram supervisor, LoadExec sintético, reinício,
lock de saves, encerramento real de processos sintéticos, interface Qt e
metadados/backups de SaveData em diretório temporário. A interface abriu
em modo demo e real ocioso, também em X11. Existe pacote local de desenvolvimento
Ubuntu 26.04 com Python/Qt incluídos e bibliotecas nativas do sistema externas.

Após autorização separada, uma sessão de boot de até 60 segundos executou
`eboot.bin`, transferiu para `game.elf` e inicializou Vulkan no llvmpipe.
Usuário relatou FPS muito baixo; isso é esperado na renderização por CPU.
A sessão encerrou pelo watchdog do prazo autorizado. Não há validação de
desempenho com GPU física, áudio audível, controles ou gameplay prolongado.

## Ambiente de compilação

Use um checkout contendo os fontes atuais e os submódulos nas revisões
registradas pelo projeto. Preserve os patches locais de FSR-Vulkan. O caminho
CMake abaixo não baixa dependências, não atualiza submódulos e não aplica
patches automaticamente. O arquivo de build autoritativo é
[`port/gpu/CMakeLists.txt`](../port/gpu/CMakeLists.txt).

Dependências já usadas pelo projeto:

- Compilador C11 e C++23, CMake 3.24 ou posterior, Ninja e pkg-config.
- Vulkan loader e headers, SDL3, X11 e glslang.
- FFmpeg: libavformat, libavcodec, libavutil, libswscale e libswresample.
- FreeType, fmt, Boost, robin-map, xxHash, Vulkan Memory Allocator e Zydis.
- Submódulos/bibliotecas locais, incluindo LibAtrac9, FSR-Vulkan, ImGui,
  magic_enum, miniz, xbyak e sirit.
- Python com `iced_x86` para preparar imagens; PySide6 para abrir a interface.

Pacotes usados no Ubuntu 26.04, instalados com autorização do usuário:

```bash
sudo apt-get install --no-install-recommends \
  build-essential cmake ninja-build pkg-config python3-venv git ca-certificates curl \
  glslang-tools libvulkan-dev libsdl3-dev libavformat-dev libavcodec-dev \
  libavutil-dev libswscale-dev libswresample-dev libfreetype-dev libfmt-dev \
  libboost-dev robin-map-dev libxxhash-dev libvulkan-memory-allocator-dev \
  libzydis-dev libx11-dev mesa-vulkan-drivers vulkan-tools
sudo apt-get install --no-install-recommends libxcb-cursor0 libxcb-shape0 \
  libxcb-icccm4 libxcb-keysyms1 libxcb-render-util0 libxcb-image0 \
  libxcb-xkb1 libxkbcommon-x11-0
```

No ambiente desta máquina, a distro chama-se `KnackLinux`, com usuário de
build `knackdev`. Abra com `wsl -d KnackLinux`. Para administrar pacotes,
abra uma sessão root com `wsl -d KnackLinux -u root`; não foi definida senha
nem configuração sudo para o usuário de build. Nenhum driver Windows foi
substituído.

Os fontes de teste ficam em `/home/knackdev/knack-linux/src`, o build em
`/home/knackdev/knack-linux/build-linux` e o ambiente Python em
`/home/knackdev/knack-linux/venv`. Dentro de `KnackLinux`, a prévia sem jogo
pode ser aberta assim:

```bash
cd /home/knackdev/knack-linux/src
/home/knackdev/knack-linux/venv/bin/python -m port.launcher.knack2 --demo
```

Esse diretório Linux é uma cópia isolada dos fontes usados nos testes.
Alterações posteriores no checkout Windows precisam ser sincronizadas antes
de reconstruir. O checkout Windows continua sendo a fonte de trabalho.

Não use um ambiente MSYS2/MinGW para produzir o executável Linux. Para testar
a janela, comece numa sessão X11 ou XWayland; o build atual exige X11.
Wayland nativo ainda requer validação própria.

## Build isolado, sem iniciar o jogo

Execute na raiz do projeto, num host Linux com as dependências presentes:

```bash
cmake -S port/gpu -B port/out-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTLG_PGO=off -DTLG_LTO=OFF \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build port/out-linux --target tlg-probe --parallel 6
./port/out-linux/tlg-probe --cpu-features
```

Esse build mantém `port/out` e os executáveis Windows existentes intactos.
O launcher prefere o runtime em `port/out-linux`, com fallback para os nomes
Linux legados em `port/out`. Não use `port/run.sh`: esse script mantém fluxo
legado de outro título, sem a supervisão LoadExec específica do Knack II.

Para saída totalmente externa, substitua `port/out-linux` por uma pasta nova
e informe o runtime com `run.py --runtime /caminho/do/build/tlg-probe`.
A compilação do executável Linux preserva `-no-pie` e a exportação dos símbolos
do runtime exigidos por `libtlggpu.so`. O emulador de instruções mantém
`-mgeneral-regs-only`. Nenhuma chamada ABI guest foi alterada nesta etapa.

## Testes sintéticos

Com as dependências Python já instaladas:

```bash
python3 -B -m unittest discover -s tests -p test_knack2_linux.py -v
python3 -B -m unittest discover -s tests -p test_knack2_unlimited.py -v
python3 -B -m unittest discover -s tests -p test_knack2_storage.py -v
python3 -B -m unittest discover -s tests -p test_knack2_runner.py -v
python3 -B -m unittest discover -s tests -p test_savedata.py -v
python3 -B -m unittest discover -s tests -p test_runtime_memory_snapshot.py -v
python3 -B -m unittest discover -s tests -p test_knack2_dialogue_gain.py -v
```

Esses testes não iniciam o jogo. Verificam ambiente host, seleção explícita de
runtime, preservação de argumentos LoadExec, lock de saves, reinício com nova
configuração e prazo compartilhado. No POSIX, o launcher cria uma sessão para
seus subprocessos; uma parada forçada atinge somente o grupo criado por ele.

Os harnesses nativos existentes também estão disponíveis no CMake, fora do
build padrão:

```bash
cmake --build port/out-linux --target runtime-test sema-test freeze-test knack-loadexec-test --parallel 6
export KNACK2_TEST_BUILD_DIR="$PWD/port/out-linux"
python3 -B -m unittest discover -s port/tests -p test_runtime.py -v
python3 -B -m unittest discover -s port/tests -p test_sema.py -v
timeout 15 ./port/out-linux/freeze-test
```

Eles cobrem memória compartilhada, TLS, callbacks, limites de stack, rwlocks,
semáforos e observação de threads com dados sintéticos. Nenhum jogo é iniciado
por esses harnesses.

## Pacote local de desenvolvimento

No próprio Linux, com PyInstaller 6.22.3 instalado no ambiente de build:

```bash
python3 scripts/build_knack_linux.py --native-build port/out-linux --build-dir /pasta/nova/package
```

O script reutiliza o spec do launcher e preserva saídas existentes. Produz uma
pasta `Knack-II-Linux-x86_64-dev` e um `.tar.gz`, com launcher, supervisor,
runtime, perfil, scripts e avisos de licença. Python e Qt acompanham o pacote;
as bibliotecas nativas do sistema continuam externas. Alvo validado: Ubuntu
26.04, glibc 2.43; não é um AppImage universal.

Após extrair, execute `./"Knack II"`. O runtime usa `$ORIGIN` para localizar
`libtlggpu.so` junto ao executável. O pacote foi extraído em pasta nova e passou
em ajuda CLI, encerramento controlado, CPU pelo supervisor e abertura real
ociosa X11. `--packaging-check /caminho/report.json` no launcher verifica caminhos,
encerramento controlado e CPU pelo helper, sem iniciar o jogo.

Nenhum pacote foi publicado. Antes de distribuição pública, revisar proveniência
da arte local, preparar fontes correspondentes e validar numa instalação limpa.

## Interface e execução manual posterior

Smoke visual finito, com dados temporários e sem jogo:

```bash
python3 -m port.launcher.knack2 --demo --offscreen --quit-after-ms 1200
```

Depois de validar build e smokes, a interface pode ser aberta com
`python3 -m port.launcher.knack2 --real`. O padrão de dados Linux é
`$XDG_DATA_HOME/knack-ii-pc`, ou `~/.local/share/knack-ii-pc`.
Solo e cooperativo compartilham `profiles/shared/user`, com lock POSIX para
evitar sessões simultâneas. Não copie nem migre saves reais nesta etapa.

Para uma futura sessão manual autorizada, use saída e perfil de teste separados:

```bash
python3 run.py --game-dir /caminho/do/seu/CUSA08014 \
  --runtime /caminho/do/build/tlg-probe --out /pasta/nova/session-output \
  --user-data /outra/pasta/nova/test-user --timeout 180
```

O supervisor usa o mesmo perfil com verificação de identidade e hashes do
Windows. Ele mantém os estágios `eboot.bin` e `game.elf`. Em Linux, o reinício
do overlay retorna ao supervisor, preservando executável, argumentos, lock e
prazo da sessão. A configuração é lida novamente antes de reiniciar.

## Gates pendentes

- WSL/Dozen foi investigado separadamente: GPU detectada e smoke básico
  aprovado, mas o driver testado expõe Vulkan 1.2 e o renderer exige 1.3.
  Veja [diagnóstico e evidências](LINUX_WSL_GPU.md). Nenhuma checagem foi removida.
- Memória guest, TLS, sinais, threads e instruções reescritas sob Linux.
- Boot, menus, combate, áudio audível, controles, saves e reinício manual.
- Drivers AMD/Intel Mesa e NVIDIA; sessões X11 e Wayland.
- Observadores NGS2 ainda condicionados a `_WIN32`; são diagnósticos opcionais.
  Ganho de diálogo já usa o mesmo caminho Linux/Windows, com stubs testados nos
  dois hosts e instalação observada no boot Linux. Audibilidade não foi validada.
- Distribuição pública, dependências em instalação limpa e fontes correspondentes.
