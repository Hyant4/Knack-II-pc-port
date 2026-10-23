# port/ — loader, runtime e vídeo do Knack II para PC

Este diretório reúne a infraestrutura usada para executar `eboot.bin` e `game.elf`
do Knack II (CUSA08014 01.01). O fluxo ativo começa no supervisor `run.py` da raiz:

- `scripts/`: preparação e link do eboot com os módulos do jogo e do console (`prepare.py`, `link_modules.py`);
- `src/`: loader (`probe.c`) e runtime que reimplementa as bibliotecas do PS4 (`runtime_*.c`, `runtime_tlg.c`);
- `gpu/`: tradução de GPU para Vulkan (núcleo do shadPS4, mais o overlay/menu em `gpu/shim/`);
- `launcher/knack2/`: launcher Qt, validação e supervisão específicos de Knack II;
- `packaging/windows/`: recursos de ícone, manifesto e metadados do runtime Windows.

Na instalação Windows local, a entrada para o usuário é **Knack II.exe**, na
raiz do repositório. O launcher inclui Qt/Python, lembra automaticamente a pasta
selecionada e usa `port/out/knack-launcher-supervisor/KnackSupervisor.exe` para
executar o supervisor oficial. O build da interface está em
`launcher/knack2/packaging/knack2.spec` e `../scripts/build_knack_launcher.py`;
ele não recompila este Core.

Saves ficam em `%LOCALAPPDATA%\KnackII-PC`, em perfis separados `single` e `coop`.
P1 é o padrão; P2=4099 exige seleção explícita. A interface inicia sessões sem
limite de tempo. Uso, logs e reconstrução do launcher estão no
[guia Windows](../docs/LAUNCHER_WINDOWS.md).

Os launchers e o empacotamento legados de inFAMOUS foram removidos. Componentes
compartilhados do runtime, ferramentas nativas e recursos exigidos pelo build
continuam preservados, mesmo quando seus nomes refletem projetos anteriores.
O runtime público usa `knack2-runtime.exe`, com fallback para builds diagnósticos
`tlg-probe.exe`. Não se realizou renomeação de ABI, exports ou opções internas.

Documentação, status e instruções de uso estão no [README principal](../README.md).
A origem do código (gow3_pc → bloodborne_pc, shadPS4) e a licença (GPL-2.0-or-later) estão em
[UPSTREAM.md](UPSTREAM.md) e [LICENSE](LICENSE).
