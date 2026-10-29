# Revisão das alterações locais e builds Windows/Linux — 30/10/2026

## Resultado

Os fontes locais consolidados produziram runtime Windows, runtime ELF Linux,
launcher/supervisor empacotados para ambos e extrator PKG Windows recompilado.
As verificações dos pacotes e abertura ociosa da interface passaram sem jogo.
Os artefatos são candidatos locais de revisão, não uma release aprovada.

O usuário dispensou teste na própria configuração como requisito. Esta revisão
não exige novo boot pessoal. As limitações abaixo descrevem evidências e lacunas,
sem impor esse gate novamente.

## Achados ainda abertos

### 1. Dois testes nativos GPU terminam com violação de acesso

`taa-shader-test` e `camera-motion-test` compilaram, mas ambos encerraram com
código Windows `3221225477` (`0xC0000005`) na execução com dados sintéticos.
Não houve diagnóstico suficiente para localizar a instrução inicial da falha.
Não atribuir automaticamente o problema ao renderer do jogo, ao driver ou
somente ao harness. Não foi iniciado jogo nem reproduzido o device loss do Mint.

Esses resultados impedem afirmar aprovação integral da regressão de GPU desta
mistura de fontes. A investigação exige tarefa focada, sem retirar assertions
ou alterar sincronização para fazer o teste passar.

### 2. Dois testes nativos têm expectativas desatualizadas

- `pipeline-binary-cache-test`: falha na expectativa `compiled == 6 && released == 4`.
  A implementação de retry existente faz até cinco tentativas; teste e função
  de retry não tinham alterações locais contra HEAD. Não foram modificados.
- `graphics-settings-test`: espera fallback RCAS 75, enquanto a função retorna
  60 para valor inválido. O comentário também ainda diz 75. O teste não tinha
  alteração local. O comportamento não foi alterado nesta revisão.

Os testes falharam explicitamente; não foram excluídos do relatório para
produzir uma aprovação aparente.

### 3. Suíte Linux ampla contém runners específicos de Windows

Após as correções descritas abaixo, 22 wrappers NGS2 ainda exigem
`MSYS2 CLANG64` e `clang.exe`. Falham antes de compilar o contrato no Linux.
Alguns harnesses C também usam APIs de teste Windows. Isso é uma lacuna de
portabilidade dos testes, não prova de falha de áudio Linux.

Um caso em `port/tests/test_mods.py` executa o `run.sh` legado com CRLF e falha
em `pipefail\r`. Esse fluxo não é o supervisor Knack usado nos pacotes. Não foi
alterado nem apresentado como aprovado.

### 4. Limites dos pacotes e identificação de versão

- O pacote Linux é gerado para Ubuntu 26.04/glibc do host, com bibliotecas
  nativas externas. Não é AppImage nem pacote universal validado para Mint.
- O extrator nativo PKG desta árvore tem build Windows; não foi produzido
  `KnackPkg` Linux. O launcher Linux requer dump já extraído para jogar.
- A interface mantém texto histórico `0.15`; os recursos PE do launcher têm
  `0.1.0`. Os candidatos são identificados externamente como
  `local-review-20261030`, sem inventar uma versão pública.
- O verificador de regravação de recursos rejeitou a identidade derivada do
  runtime Windows. O artefato rejeitado foi separado. O pacote contém a saída
  original e íntegra do compilador, sem branding posterior nem strip de debug.
  Não houve contorno do guard para promover a imagem rejeitada.
- O PR #19 de SkippySteve continua em branch isolada. Os candidatos desta
  revisão contêm a árvore local consolidada e mixer HLE atual; não são o PR
  original nem incluem automaticamente seu opt-in de firmware NGS2.

## Falhas corrigidas nesta revisão

1. **Controles GPU não chegavam ao runtime.** O supervisor agora aplica
   `gpu_diagnostic_environment` já existente após defaults de startup. Valores
   explícitos validados prevalecem; variáveis herdadas continuam filtradas.
   Opções inválidas falham antes de validar/preparar dump ou criar saída.
   Escolhas efetivas são registradas em `session.json`. LoadExec e reinício
   mantêm o contrato. Default de preload não foi removido.
2. **Quatro rótulos PKG fora do catálogo estático.** Foram incluídas traduções
   EN/ES/FR/RU no catálogo da tela de instalação, preservando mensagens externas.
3. **Fixtures Python dependiam do Windows.** Extração sintética usa
   `sys.executable`; testes de idioma/LoadExec/GTX usam runtime sintético com
   nome e permissão corretos no POSIX. Nenhum binário sintético é executado
   como jogo. As verificações de produto foram preservadas.
4. **Testes de interface estavam presos ao menu AA anterior.** Expectativas
   agora verificam seletor unificado Native AA/Desligado com FSR temporal off
   e default de preload atual; não desligam cobertura de comportamento.
5. **Targets nativos não compilavam no Windows.** Os testes de TAA/movimento
   incluem o shim Windows existente; três targets usam `compat_win.c`/onecore
   já presentes. O stub GPU de teste inclui a definição correta de jump buffer
   mesmo quando a macro vem do build. Renderer e ABI guest não foram editados.

## Escopo e preservação

Baseline: `experiment/gpu-small-sync-prebind-20261020`, HEAD
`3d23fb71a48e290e0387d2d80e4c04bf6babdb17`.

Inventário: 377 entradas de status, com 132 exclusões, 107 modificações,
25 adições no índice e 113 arquivos novos. Snapshot de 2.607 arquivos, cerca
de 120 MB, expandiu dependências e preservou seis patches locais de FSR-Vulkan.
Os outros seis submódulos estavam limpos, nas revisões já registradas.

A revisão inventariou todas as entradas e cruzou arquivos novos/excluídos,
referências de build e testes. A leitura manual concentrou-se em código de
produção: supervisor, perfis/layouts e patches guest, áudio/NGS2, pad/serviços,
PKG/recuperação, integração de shaders/renderer/cache e empacotamento.
Não foi uma prova formal linha a linha de cada arquivo de teste/documentação
ou de todos os caminhos possíveis. Exclusões de launchers/documentos legados
foram mantidas; nenhuma mudança anterior foi apagada para limpar Git.

Builds e staging ficam fora do checkout, em
`Documents/Codex/Knack-consolidated-review-20261030/`. O build Linux usou uma
cópia em `/home/knackdev/knack-linux/consolidated-review-20261030/`.
Nenhuma dependência foi instalada pela rede, driver foi trocado, jogo iniciado,
save migrado ou executável instalado substituído. Merge/publicação anteriores
foram suspensos antes de acontecer; nenhum commit, stage ou push foi feito.

## Testes e evidências

| Verificação | Resultado |
| --- | --- |
| CMake/runtime Windows, Clang CLANG64, seis jobs | passou |
| CMake/runtime Linux, GCC 15.2/WSL, quatro jobs | passou |
| Python Windows: 80 suítes, 546 testes considerados | 532 passaram, 14 skips, zero falhas finais |
| Python Linux: 80 suítes, 523 testes considerados | 414 passaram, 86 skips, 23 falhas de portabilidade/legado |
| Extrator PKG Windows real com pacotes sintéticos | nove testes passaram |
| Barycentric shaders, captura, resolução de cena, H.264 e downmix nativos | passaram |
| SPIR-V emitido pelo teste baricêntrico | validações passaram |
| TAA, movimento, pipeline binary e defaults gráficos nativos | quatro falhas, descritas acima |
| Pacote Windows: ajuda, helper/parada/CPU, demo e real ocioso | passou |
| Pacote Linux extraído: CPU, ldd -r, helper/parada, demo e real ocioso | passou |
| Capturas offscreen dos launchers | inspecionadas; não são capturas do jogo |

Os totais finais substituem apenas suítes repetidas após correções; não somam
tentativas como novos testes. A falha transitória do teste reentrante de parada
Windows foi preservada no primeiro log; repetição passou sem mudar lógica do
runner. Não foi declarada causa definitiva para a ocorrência sob carga de build.

Baseline, manifestos, diffs, comandos, logs das tentativas, resultados, capturas
e hashes finais ficam na área externa. Nenhum teste skipped ou falho é contado
como aprovação. Esta revisão entrega compilações candidatas e pendências
concretas; não declara estabilidade de campanha, áudio audível novo, FPS ou
correção do travamento Linux anterior.
