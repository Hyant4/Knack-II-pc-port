# Knack II PC — V0.17.3

## Português

Atualização do runtime para corrigir reaproveitamento do cache de shaders e pipelines. O launcher mantém os idiomas PT / EN / RU, com identificação da versão atualizada.

- **Pré-carregamento experimental de shaders:** ativado por padrão. Em **Configurações → Vídeo**, desligue o botão e salve as configurações para voltar ao carregamento sob demanda. A preferência vale na próxima sessão.
- Variantes compatíveis em índices diferentes deixam de provocar falsas recusas do cache; conflitos reais continuam sendo rejeitados.
- O modo sob demanda passa a ler os shaders já salvos quando o cache é compatível.
- Corrigida referência temporária nos metadados dos shaders carregados.

**Limites:** pré-carregamento pode aumentar a espera antes de entrar no jogo. A melhora dos engasgos é parcial: cache vazio ainda precisa de compilação; pausas e variação de FPS podem continuar. Esta atualização não garante 60 FPS estáveis.

**Validação:** build nativa, 22 verificações sintéticas do cache aprovadas, replay local de 1.219 registros, testes do botão/persistência/modos de inicialização e sessões curtas na edição europeia 01.01, incluindo pular a abertura e movimentação. Campanha completa, outras GPUs e cooperativo não foram revalidados nesta atualização.

**Instalação:** baixe `Knack-II-Windows-x64-V0.17.3.zip`, extraia a pasta inteira e abra `Knack II.exe`. Use seu próprio dump CUSA08014 ou CUSA07670, versão 01.01. Saves e preferências continuam em `%LOCALAPPDATA%\KnackII-PC`. Não é necessário apagar o cache existente. A V0.17.2 permanece disponível para retorno à versão anterior.

Fontes correspondentes: `Knack-II-sources-V0.17.3.zip`. Confira `SHA256SUMS.txt`. O pacote não contém jogo, dump ou saves.

## English

Runtime update fixing reuse of the shader and pipeline cache. The launcher keeps its PT / EN / RU languages and displays the updated version.

- **Experimental shader preloading:** enabled by default. In **Settings → Video**, disable the toggle and save settings to return to on-demand loading. Your preference applies next session.
- Compatible variants at different indices no longer cause false cache rejections; actual conflicts remain rejected.
- On-demand mode now reads previously saved shaders when the cache is compatible.
- Fixed a temporary reference in loaded shader metadata.

**Limitations:** preloading may increase the wait before entering the game. Stutter improvements are partial: a cold cache still needs compilation; pauses and frame-rate variation may remain. This update does not guarantee stable 60 FPS.

**Validation:** native build, 22 passing synthetic cache checks, a local replay of 1,219 records, tests of the toggle/persistence/startup modes, and short sessions with the European 01.01 edition, including skipping the opening and movement. The full campaign, other GPUs, and co-op were not revalidated in this update.

**Installation:** download `Knack-II-Windows-x64-V0.17.3.zip`, extract the entire folder, and open `Knack II.exe`. Use your own CUSA08014 or CUSA07670 version 01.01 dump. Saves and preferences remain in `%LOCALAPPDATA%\KnackII-PC`. You do not need to clear your existing cache. V0.17.2 remains available as a rollback.

Matching sources: `Knack-II-sources-V0.17.3.zip`. Check `SHA256SUMS.txt`. No game, dump, or saves are included.
