# Launcher Knack II

O launcher atual está em [`knack2/`](knack2/). Para jogar, use o pacote Windows
da [release](https://github.com/Hyant4/Knack-II-pc-port/releases) e abra `Knack II.exe`.
O supervisor oficial é o `run.py` da raiz. Uso e opções estão no
[guia do launcher](../../docs/LAUNCHER_WINDOWS.md).

## Componentes preservados

- `knack2/`: interface, validação, configuração e supervisão de Knack II.
- `logtail.py`: leitura incremental dos logs, usada pelo runner de Knack II.
- `userdata.py` e `savemanager.py`: dependências do diagnóstico nativo `port/run.py`.
- `tlg_vulkan.py`: seleção de Vulkan para ferramentas de diagnóstico nativas.
- `infamous.ico`: recurso ainda referenciado por `play.rc` e `tlg-gpu-capabilities.rc`,
  compilados pelo CMake. Não é um launcher executável.

As interfaces e o runner de inFAMOUS foram retirados. Os avisos de licença das
fontes herdadas continuam em `qt/fonts/` para preservar atribuição.
O empacotamento de Knack II usa `knack2/packaging/knack2.spec` e
`scripts/build_knack_launcher.py`, na raiz. Dependências da interface:
`knack2/requirements.txt`.
