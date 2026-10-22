# port/ — loader, runtime e vídeo do Knack II para PC

Este diretório reúne a infraestrutura usada para executar `eboot.bin` e `game.elf`
do Knack II (CUSA08014 01.01). O fluxo ativo começa no supervisor `run.py` da raiz:

- `scripts/`: preparação e link do eboot com os módulos do jogo e do console (`prepare.py`, `link_modules.py`);
- `src/`: loader (`probe.c`) e runtime que reimplementa as bibliotecas do PS4 (`runtime_*.c`, `runtime_tlg.c`);
- `gpu/`: tradução de GPU para Vulkan (núcleo do shadPS4, mais o overlay/menu em `gpu/shim/`);
- `launcher/knack2/`: launcher Qt, validação e supervisão específicos de Knack II;
- `packaging/windows/`: recursos de ícone, manifesto e metadados do runtime Windows.

Arquivos com nomes `tlg_*`, `infamous_*` e componentes de outros ports preservam
compatibilidade e histórico. Os launchers legados não são a entrada de Knack II.
O runtime público usa `knack2-runtime.exe`, com fallback para builds diagnósticos
`tlg-probe.exe`. Não se realizou renomeação de ABI, exports ou opções internas.

Documentação, status e instruções de uso estão no [README principal](../README.md).
A origem do código (gow3_pc → bloodborne_pc, shadPS4) e a licença (GPL-2.0-or-later) estão em
[UPSTREAM.md](UPSTREAM.md) e [LICENSE](LICENSE).
