# Origem do código

O projeto atual é o port experimental de **Knack II CUSA08014 01.01**, derivado
diretamente de HayatoG/iss_pc. Os parágrafos abaixo preservam a genealogia herdada
do runtime, incluindo a etapa The Last Guardian; não descrevem o jogo alvo atual.
Créditos completos e referências de cada camada estão no [README principal](../README.md#bases-e-créditos)
e em [gpu/VENDOR.txt](gpu/VENDOR.txt). Renomear a apresentação não altera essas autorias.

Este diretório começou como cópia de **[FelipePrado0/gow3_pc](https://github.com/FelipePrado0/gow3_pc)**
no commit `9a6d17cdb54109b88f9296c0643d6384b62a1aaf` (2026-10-12). Esse projeto é fork de
Supermedo/bloodborne_pc, que por sua vez deriva de [deadinside28/bloodborne_pc](https://github.com/deadinside28/bloodborne_pc).
O núcleo de vídeo em `gpu/shadps4/` vem do [shadPS4](https://github.com/shadps4-emu/shadPS4).

Licença: **GNU GPL v2 ou posterior** (`LICENSE`). Componentes de terceiros mantêm suas licenças.

As mudanças para The Last Guardian (CUSA03627 v1.03) são feitas aqui e registradas no histórico do git.
Para trazer correções do upstream, compare com o último commit sincronizado.

**Sincronizado com o upstream** (só a parte de núcleo, no `ps4_core`, branch `upstream/gow3-sync`) até
`2a0eac6522d6e2e5b0fbfe25f49abd3dbe1a644b` (2026-10-13): TSC pelo relógio do processo, cursor escondido,
reescrita ISA para CPUs sem MOVBE/BMI1/LZCNT, multisampling por driver (linhas pretas na RDNA2/Intel),
opções de depuração da GPU, captura de quadro, `--gamepad`/`--mount`, diagnósticos da preparação.
Ficaram de fora os patches, cheats, launcher, atalho da Steam e arte do God of War III (lista em
`ps4_core/docs/CHANGELOG.md`). A próxima comparação parte de `2a0eac6`.
