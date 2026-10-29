## Português

Pré-release **Windows x64 0.17.7-rc5**, para comparar o crash de carregamento
da RX 580 relatado na issue #20. Mantém a base publicada da RC4 e acrescenta
junção experimental de barreiras de imagem, desligada por padrão.

O workaround vem da investigação inFAMOUS #232/#278. Combina transições
equivalentes, preservando dependências diferentes. **Correção da issue #20
ainda não confirmada na RX 580 afetada.** Não há promessa de desempenho.

### Teste na RX 580

1. Baixe o ZIP **Windows**, extraia tudo em uma pasta nova e abra
   **`Test RX 580.cmd`**. Essa entrada usa preferências e saves de teste em
   `%LOCALAPPDATA%\KnackII-PC-Issue20-Test`; não migra seu progresso habitual.
2. Valide seu dump. Reproduza os ajustes gráficos usados no relato e mantenha
   esses ajustes iguais nas duas sessões. Não aplique outros presets entre elas.
3. Em **Configurações > Vídeo > Diagnóstico avançado > Mostrar ajustes**, ligue
   **Diagnóstico de barreiras de imagem**. Deixe **Junção de barreiras de imagem**
   em **Desligado**, salve e inicie o jogo. Repita o carregamento afetado.
4. Encerre a sessão e guarde `00-eboot.bin.log`, `01-game.elf.log` e
   `session.json` dessa execução. Vá a **Logs e diagnóstico** para localizar a pasta.
5. Altere somente **Junção de barreiras de imagem** para **Ligado**, salve,
   reinicie e repita o mesmo carregamento. O preset
   **RX 580: compatibilidade experimental** também liga somente essa opção.
6. Na issue #20, informe se chegou ao gameplay em cada caso e anexe os arquivos
   das duas sessões, identificados como desligado/ligado. Revise nomes e caminhos
   pessoais antes de publicar. Não envie jogo, assets, saves ou dumps de memória.

O diagnóstico registra quantidades antes/depois e mensagens limitadas de gravação
Vulkan. Ele pode atingir o limite antes da falha. Não é necessário apagar caches
ou saves. Ao terminar, desligue o diagnóstico. O guia completo acompanha o pacote
em `docs/DIAGNOSTICO_RX_580.md`.

Launcher e runtime são compilados para esta RC5. A validação automática e os
smokes de pacote não certificam gameplay, campanha ou estabilidade na RX 580.
Nenhum jogo, firmware ou save acompanha o download. Stable/latest 0.17.5 permanece.

Esta build preserva os recursos atuais, incluindo preset GTX 1060 de desempenho,
resolu??o interna nativa e perfis regionais existentes. O ?udio padr?o continua
HLE; m?dulo NGS2 do sistema PS4 ? opcional, n?o inclu?do e n?o necess?rio para
o teste RX 580. Esta pr?-release oferece somente pacote Windows.

## English

Experimental **Windows x64 0.17.7-rc5** prerelease for the RX 580 loading crash
reported in issue #20. Includes the current consolidated baseline (V0.17.7-dev / PR #19) and adds optional
image barrier merging, **off by default**.

The workaround comes from the inFAMOUS #232/#278 investigation. It combines
equivalent transitions while keeping different dependencies separate.
**A fix for issue #20 has not been confirmed on the affected RX 580.**
No performance improvement is promised.

### RX 580 test

1. Download the **Windows** ZIP, extract everything into a new folder and run
   **`Test RX 580.cmd`**. This uses separate test preferences and saves under
   `%LOCALAPPDATA%\KnackII-PC-Issue20-Test`; your normal progress is not migrated.
2. Validate your own dump. Match the graphics settings from your report and keep
   them identical in both sessions. Do not apply other presets between sessions.
3. Open **Settings > Video > Advanced diagnostics > Show settings**. Turn
   **Image barrier diagnostics** on. Set **Image barrier merging** to **Off**,
   save and start the game. Repeat the affected loading sequence.
4. Close the session and keep its `00-eboot.bin.log`, `01-game.elf.log` and
   `session.json`. Use **Logs and diagnostics** to locate the session folder.
5. Change only **Image barrier merging** to **On**, save, restart and repeat the
   same loading sequence. The **RX 580: experimental compatibility** preset
   also enables only that option.
6. In issue #20, report whether gameplay loaded in each case and attach both
   sets of files, labeled off/on. Review personal names and paths before posting.
   Do not upload game files, assets, saves or memory dumps.

Diagnostics log before/after counts and bounded Vulkan recording messages;
the log limit may be reached before the crash. No cache or save deletion is
needed. Turn diagnostics off afterwards. The included Portuguese guide is
`docs/DIAGNOSTICO_RX_580.md`.

Both launcher and runtime are rebuilt for RC5. Automated tests and package
smokes do not certify RX 580 gameplay, the campaign or stability. No game,
firmware or saves are included. Stable/latest 0.17.5 remains available.

Current features are retained, including the GTX 1060 performance preset, native
internal resolution and existing regional profiles. Default audio remains HLE.
The optional PS4 NGS2 system module is not included and is not required for this
RX 580 test. This prerelease provides a Windows package only.
