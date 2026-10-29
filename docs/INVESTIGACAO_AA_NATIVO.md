# Desligamento do AA nativo — investigação da issue #17

Data: 26/10/2026. Agente: Codex, investigação e protótipo na função Sol autorizada.

## Integração no repositório local e menu unificado — 28/10/2026

O usuário pediu integrar a feature ao repositório local. Os fontes de AA já
estavam conectados ao loader, renderer, supervisor e launcher no workspace.
Foram adicionados ao índice Git os oito arquivos novos específicos: header
do patch guest, testes C/C++/Python, analisador de census e três documentos
de investigação/validação. As modificações em arquivos compartilhados continuam
no worktree: não foram estagiadas por inteiro, pois incluem trabalho paralelo
de PKG, compatibilidade regional e GPU. Isso não é um commit autônomo da feature.

Foi preservada a correção de interface solicitada pelo usuário: o menu existente
mostra `Native AA` e `Desligado` com FSR temporal desligado, sem seletor adicional.
Com FSR temporal ativo, o mesmo menu mantém os presets de qualidade. A seleção
visual persiste ao trocar idioma, e as mensagens novas têm traduções nos
catálogos existentes. O teste específico verifica ausência do controle separado,
opções do menu, seleção em PT/EN/RU e alternância dos modos.

Rodada atual: 90 testes passaram — AA 4, temporal 5, resolução 2, launcher 27,
interface 24 e runner 28. Os guards C/C++ foram compilados e executados usando
a toolchain existente. Na primeira rodada, um teste antigo de ausência de Qt
esperava português, mas leu inglês das preferências reais; repetindo a suíte
de interface com LOCALAPPDATA isolado, os 24 testes passaram sem editar produto
ou teste para mascarar o resultado. Logs de ambas as execuções foram preservados.

Nenhum executável foi substituído nesta etapa. O checkout recebeu mudanças
posteriores de PKG/GPU desde os candidatos de 27/10; copiar aquele pacote
antigo sobre a instalação atual poderia regredir essas mudanças. O pedido
foi concluído como integração de fontes no repositório, sem commit, merge,
push, publicação ou promessa de instalação compilada atualizada.
Não houve novo build ou gameplay: as provas anteriores continuam atribuídas
aos hashes e cenários descritos nas seções históricas.

Baseline, lista dos arquivos, hashes de fontes/instalação, estado anterior do
índice e relatório final em
`Documents/Codex/Knack-AA-repository-integration-20261028/`.
Saves, configurações, submódulos, índice preexistente e histórico do handoff
foram preservados. O encerramento anterior do Computer Use não foi contornado;
nenhuma automação da interface foi retomada nesta etapa.

## Candidato dos fontes atuais compilado e validado em 4K — 27/10/2026

Após o pedido de continuar, a revisão comparou os fontes atuais ao snapshot
AA original, reconstruído privadamente a partir do baseline e patch preservados.
Seis arquivos de produto diferem desse snapshot, por mudanças preexistentes de
identidade de executável/layout, perfil US 01.00, seleção de preset e seus testes.
As mudanças foram preservadas. A revisão foi feita pelo próprio agente e não
é apresentada como revisão independente. Não foi encontrado novo bloqueio nos
contratos de AA, configuração, guard de imagem e cópia dos destinos examinados.

O binário externo `4508fcb...` estava desatualizado. O build incremental,
com duas tarefas e toolchain existente, recompilou `probe.c`, fez o link e
produziu candidato SHA-256
`25dc25ae192198fda3249bcc868d6dc65f4dd21f12bfd72d9d602c13cc80c047`.
O candidato foi preservado fora da instalação; depende das DLLs/toolchain
existentes, cujo PATH é preparado pelo supervisor. Não é pacote autônomo.

Novo A/B do candidato atual, US 01.01/Chapter 2-4/3840×2160: off teve 8 census,
80 observações de shader/câmera, jitter zero e nenhum dispatch do TAA original;
nativo teve 9 census, 90 observações, jitter variável até 0,46296252 pixel e
9 dispatches do TAA original. Ambos produziram framebuffers finais reais de
33.177.600 bytes e terminaram no limite previsto de 60 segundos, incluindo
abertura/carregamento, sem erro de alocação, assertion ou watchdog. Menor
margem de compromisso: 16,4166 GiB. Capturas inspecionadas, sem tela preta ou
corrupção grosseira de cor; câmeras/animações finais não foram sincronizadas.

Seis testes existentes de layout passaram nesta etapa. Os 90 testes da etapa
QHD anterior não foram repetidos. A matriz 1080p/QHD/4K anterior continua
atribuída ao protótipo `5bb51687...`; o novo candidato recebeu este A/B 4K.
Não se transfere automaticamente toda cobertura de gameplay entre binários.

Pacote local de revisão em
`C:/Users/dartv/Documents/Codex/Knack-AA-integration-20261027/`: `REVIEW.md`,
candidato, manifestos SHA-256, snapshot dos fontes relacionados, diff original
da feature, delta posterior, build e provas do A/B. O patch original depende
do baseline pré-AA; não deve ser aplicado cegamente sobre o HEAD histórico.
Snapshot de fontes e patch não contêm dump, saves ou capturas do jogo.
Instalação e releases não foram alterados; não houve commit, merge ou push.
Permanecem pendentes estabilidade prolongada, outros perfis/layouts, campanha,
avaliação de movimento/desempenho e promoção explícita para instalação/release.

## Matriz básica concluída com QHD — 27/10/2026

O usuário pediu continuar após o gate 4K. A validação complementar em QHD usou
o mesmo runtime experimental preservado `5bb51687...`, dump US 01.01,
checkpoint de Chapter 2-4, dados copiados e duas sessões de até 60 segundos.
Foram mantidos FSR temporal/FSR1/RCAS desligados, readbacks precisos 2 e o
orçamento nativo existente de DMEM 5824 MiB. Nenhum código foi alterado.

| Resolução interna | AA desligado | Controle nativo | Estado |
| --- | --- | --- | --- |
| 1920×1080 | jitter zero; 0 TAA em 12 census | jitter variável; 13 TAA em 13 census | Confirmado no trecho |
| 2560×1440 | jitter zero; 0 TAA em 4 census | jitter variável; 11 TAA em 11 census | Confirmado no trecho |
| 3840×2160 | jitter zero; 0 TAA em 4 census | jitter variável; 12 TAA em 12 census | Confirmado no trecho |

Em QHD foram observadas 40 câmeras/shaders no modo off e 110 no nativo,
com maior jitter nativo de 0,46875008 pixel. Os alvos principais eram
2560×1440; os efeitos a meia resolução permaneceram em 1280×720. Capturas
finais tinham exatamente 14.745.600 bytes, correspondentes a 2560×1440×4.
O log off confirmou `destinations=2 history=0`; o controle não aplicou o patch.
As imagens RGB30 foram inspecionadas: cena 3D e personagem renderizados,
sem tela preta ou corrupção grosseira de cor. Isso não quantifica borrão ou
tremulação em movimento; animações não foram sincronizadas por frame.

Ambas as sessões QHD terminaram com saída 124 pelo timeout previsto, sem erro
de alocação, assertion ou disparo do watchdog. Menores margens de compromisso:
20,5423 GiB off e 20,2320 GiB nativo. Os 60 segundos incluem abertura e menus,
não são 60 segundos contínuos de gameplay.

Também foram repetidos 90 testes existentes: AA 4, temporal 5, resolução nativa
2, launcher 27, interface 24 e runner 28. Todos passaram, incluindo os harnesses
C/C++ dos guards. Não foi feito novo build integral; a execução do jogo continua
referindo-se ao binário experimental congelado, distinto de uma promoção dos
fontes atuais para instalação ou release.

Evidências privadas em `C:/Users/dartv/Documents/Codex/Knack-AA-QHD-20261027/`:
`verified-qhd-summary.json`, `focused-test-results.json`, logs por suíte,
auditorias de câmera, telemetria, capturas e `comparison-overview.png`.
Comandos: `run_probe.py off --resolution 2560x1440`, equivalente `native`,
`verify_artifacts.py` e `run_focused_tests.py`, com o Python da `.venv` existente.
Pendentes: estabilidade prolongada, campanha, EU/US 01.00, outros layouts e
avaliação de desempenho/movimento. Instalação e releases não foram alterados.
As seções seguintes preservam a sequência histórica dos gates.

## A/B 4K confirmado após reinício — 27/10/2026

O pagefile de 16 GiB tornou-se ativo após o reinício feito pelo usuário;
o limite de compromisso subiu para 47,8728 GiB. Duas sessões de até 60 segundos,
incluindo menus e carregamento, chegaram ao checkpoint US 01.01/Chapter 2-4
em 3840×2160 internos, com framebuffers finais desse tamanho conferidos.

Sem AA: 4 census, 40 observações de shader/câmera, jitter zero e nenhum
dispatch do TAA original. Controle nativo: 12 census, 120 observações,
jitter variável até 0,46875072 pixel e 12 dispatches do TAA original.
O log sem AA confirmou cópia para dois destinos HDR, sem histórico temporal.
Ambos terminaram pelo timeout previsto, sem erro de alocação; a menor margem
de compromisso foi 18,1831 GiB. Capturas de gameplay foram inspecionadas.

O funcionamento básico está confirmado em 1080p e agora também em 4K no
trecho testado. Isso não certifica estabilidade prolongada, campanha, QHD,
EU/US 01.00, outros layouts ou desempenho. A instalação continua sem promoção
do AA experimental. Detalhes e evidências: [validação 4K](VALIDACAO_AA_4K.md).
As seções seguintes preservam o histórico, inclusive o estado anterior ao reinício.

## Paginação autorizada e configurada — 27/10/2026

O usuário autorizou o ajuste global preparado para o gate 4K. A paginação em
C: foi configurada para 16384 MiB iniciais e 32768 MiB máximos, com backup do
estado anterior e verificação por WMI/registro. A alocação ativa continua em
4975 MiB e o limite de compromisso em 36,7312 GiB; o aumento depende de reinício.
Não houve reinício automático nem nova execução 4K antes dessa efetivação.
Detalhes e rollback estão na [validação 4K](VALIDACAO_AA_4K.md).

## Gate 4K — interrupção preventiva por memória — 27/10/2026

Após o usuário pedir continuar, foi iniciado o teste de 3840×2160 internos.
O loader aceitou os guards de resolução e de jitter, mas a execução foi
encerrada preventivamente quando restaram aproximadamente 380 MiB de margem
de memória comprometida no Windows. Não houve captura válida de gameplay 4K;
esse resultado não altera a confirmação anterior em US 01.01/1080p.

Foi preparado, sem aplicar, ajuste reversível de paginação inicial 16 GiB e
máxima 32 GiB para revisão do usuário. A mudança global exige autorização
específica e pode depender de reinício, que não será automático. Fontes,
valores, script e limites estão na [validação 4K](VALIDACAO_AA_4K.md).

## Validação de gameplay obtida — 27/10/2026

O usuário autorizou iniciar o diagnóstico da alocação que bloqueava o A/B. Hoje
o controle monitorado não reproduziu a falha anterior; nenhuma correção no
alocador ou configuração do Windows foi aplicada. Em seguida, os modos nativo
e sem AA chegaram ao mesmo checkpoint 3D de Chapter 2-4, US 01.01/1080p, e
executaram até seus timeouts previstos de 60 segundos, sem erro de alocação.

O modo sem AA produziu 12 census, totalizando 120 observações de shader/câmera
com jitter zero e nenhum dispatch do TAA original. O controle nativo produziu
13 census, 130 observações, jitter variável e 13 dispatches do TAA original.
Ambos mantiveram 1920×1080 nos alvos principais e 960×540 nos efeitos que já
usavam meia resolução. Um par de capturas tem matriz de vista igual nos valores
registrados pelo census; animações não foram sincronizadas por frame.

As imagens foram inspecionadas: cena, personagem e vegetação renderizados;
sem AA há mais serrilhado nos detalhes finos, como esperado. Essa etapa confirma
funcionamento básico do desligamento do TAA/jitter no trecho testado. Ela não
certifica campanha, estabilidade prolongada, tremulação em movimentos variados,
EU/USA 01.00/outros layouts, QHD/4K ou desempenho. Foi usado o build experimental
preservado `5bb51687...`; instalação e releases continuam sem promoção do AA.

Telemetria apontou forte pressão de memória comprometida do sistema, com
margens amostradas próximas de 1 GiB, mas sem uma nova falha acompanhada pelos
contadores não se atribui definitivamente toda falha Vulkan anterior a essa
causa. Detalhes, fontes Microsoft, comandos, hashes e evidências estão no
[diagnóstico de alocação e A/B](DIAGNOSTICO_ALOCACAO_GPU.md).

## Retomada da execução — evidência parcial e bloqueio comum aos modos

O usuário fechou Valorant, autorizou duas sessões adicionais curtas e depois
pediu retomar. O bloqueio inicial de reserva da memória guest foi superado:
as sessões `ab-off-memory-retry` e `ab-native-memory-retry` chegaram ao jogo e
terminaram no timeout controlado de 60 segundos, com saída 124. O modo sem AA
registrou instalação do patch e cópias dos dois destinos até o contador de 900
dispatches. Duas capturas de constantes do shader temporal mostraram jitter
`[0, 0]`. O modo nativo não registrou aplicação do patch de AA.

Isso é prova de execução inicial do mecanismo, não comparação de qualidade no
gameplay. As primeiras capturas pegaram abertura, aviso de rede e título em
momentos diferentes. Uma sessão posterior sem AA chegou ao menu principal e
identificou o checkpoint Chapter 2-4, mas não produziu uma comparação de gameplay
no mesmo ponto e câmera. A primeira conversão das imagens `display_*.raw` como
RGBA8 mostrou cores erradas; a decodificação foi corrigida para o formato
10-bit do buffer guest. As imagens `output_*.raw` da apresentação foram lidas
como RGBA8. Nenhum shader foi alterado para corrigir essa leitura offline.

As tentativas seguintes falharam em buffers do backend, tanto com compilação
de pipelines em segundo plano quanto com pré-carregamento síncrono. Na retomada
foram preservadas mudanças paralelas do suporte USA 01.00 e recompilado somente
o runtime externo, usando dois jobs. O build passou e os quatro testes focados
de `test_knack2_native_aa.py` passaram novamente no worktree atual. Não houve
novo patch de produto nesta retomada.

Resultados das verificações finais, todas com orçamento de até 60 segundos:

| Execução | Configuração | Resultado |
| --- | --- | --- |
| `ab-off-resume-sync` | Sem AA, cache de teste copiado, pré-carregamento síncrono | Saída 23; falha ao alocar buffer de 16 MiB, após cerca de 33 s |
| `ab-native-resume-sync` | AA nativo, mesmo cache e demais opções | Saída 23; falha ao tornar 16 MiB residentes, após cerca de 29 s |
| `ab-off-frozen-control` | Binário anterior e perfil de teste da execução bem-sucedida | Log registra a mesma falha de alocação de 16 MiB, após cerca de 27 s; sem relatório final do supervisor |

O controle com AA nativo demonstra que a falha não ocorre exclusivamente no
caminho novo sem AA. O binário anterior também a reproduziu. Isso não determina
a causa raiz da pressão de memória nem certifica ausência de regressões.
Durante a última tentativa o Windows também recusou criar PowerShell com
`0x800705af`; comandos leves foram usados para recuperar os logs. Ao final,
nenhum processo do runtime de teste permanecia. Não foram encerrados aplicativos
do usuário, alterada paginação, reduzido orçamento guest ou removidos dados.

Build externo atual: SHA-256
`4508fcbdb058a1ce10e8a8a02dd8157f223cd54865e4ed2196251505b8b5369b`.
O build anterior `5bb51687...` foi preservado separadamente como
`tlg-probe-tested-5bb51687.exe`. Evidências adicionais na pasta privada do
protótipo: `build-resume.log`, `test-native-aa-resume.log`,
`resume-validation-report.json`, `memory-after-resume.json` e pastas de execução
listadas na tabela. SHA-256 da instalação e do perfil de teste de origem,
exceto seu cache, permaneceram iguais aos baselines desta retomada.

Estado final: build, testes sintéticos e execução inicial em abertura/menu têm
evidência. Jitter zerado foi observado nas constantes capturadas. Permanecem
sem validação comparação de nitidez em gameplay, tremulação/rastros, cobertura
de câmeras e efeitos, EU/QHD/4K em execução e desempenho. Validation layers
continuam indisponíveis. O próximo bloqueio é diagnosticar a alocação/residência
de buffers comum aos dois modos. O protótipo não foi promovido à instalação,
publicado ou certificado como feature pronta.

## Protótipo autorizado — implementação local e execução pendente

Após a investigação, o usuário autorizou explicitamente a tarefa Sol no escopo
listado abaixo, incluindo build isolado e duas sessões A/B de até 60 segundos.
Codex implementou o protótipo nessa função. O texto das etapas de investigação
foi mantido como registro do estado anterior.

O launcher de fontes agora oferece **Suavização nativa (próxima sessão)**.
`antialiasing=native` preserva o comportamento anterior; `off` pede o modo sem
AA temporal nativo. `TLG_ANTIALIASING` é produzido pelo supervisor a partir da
configuração validada, sem aceitar um override herdado não validado.

```ini
upscaler=off
antialiasing=off
internal_resolution=1920x1080
fsr1=0
rcas=0
```

FSR1 e RCAS continuam independentes; os zeros acima servem para uma comparação
sem esses filtros. `off` exige resolução interna `auto`, 1080p, QHD ou 4K e
fornecedor temporal desligado. A alteração exige nova sessão. O primeiro
protótipo rejeita proxies de 720p/900p e alterações incompatíveis de fornecedor
durante a sessão. O menu nativo não persiste FSR ativo enquanto o jitter guest
estiver neutralizado.

O loader valida perfil 01.01, segmento executável e 80 bytes de contexto antes
de substituir quatro bytes apenas na imagem privada. O renderer recebe um
indicador separado depois do patch validado. Cada dispatch reconhecido do TAA é
substituído por cópias HDR sem filtro para os dois destinos. O contrato verifica
formato, tamanho, amostras, camadas, mipmaps, uso de transferência, handles e
sobreposição das faixas de memória guest. Uma incompatibilidade encerra o
protótipo com diagnóstico explícito; não executa TAA silenciosamente com jitter
desligado. Os logs distinguem patch instalado de cópias efetivamente aplicadas.

Validação concluída nesta implementação:

- Build Windows externo, `RelWithDebInfo`, LTO desligado, sem alterar CMake,
  dependências ou submódulos. O primeiro build passou com seis jobs. Um rebuild
  incremental esgotou memória no Clang; retomada com dois jobs passou. O build
  final está em `Documents/Codex/Knack-AA-prototype-20261026/build/tlg-probe.exe`,
  SHA-256 `5bb51687a95201615f8a9347cf7491088c1e641822d5d7f775b7c8e3f57cd697`.
- 90 testes passaram: 4 novos de AA, 5 temporais, 2 de resolução nativa,
  27 de launcher, 24 de interface e 28 do runner. Incluem harness C com
  160 corrupções do contexto, limites e escrita restrita, além de harness C++
  dos contratos de cópia em 1080p/QHD/4K. A primeira rodada de interface
  detectou uma validação redundante interferindo no contrato do seletor RCAS;
  a rodada final passou após removê-la.
- O mesmo helper C foi aplicado a cópias em memória das duas imagens reais
  suportadas, reconstruídas por segmentos. Apenas o intervalo autorizado de
  quatro bytes mudou; reaplicação foi rejeitada e hashes dos dumps originais
  permaneceram iguais. Isso não executa instruções guest.
- Captura offscreen da interface conferida visualmente:
  `Documents/Codex/Knack-AA-prototype-20261026/launcher-native-aa-off.png`.
  Ela valida apresentação do controle, não imagem do jogo.

Execução bloqueada: a tentativa `ab-off` falhou no `eboot.bin`, com saída 23 e
`cannot create 6080 MiB of guest memory (error 1455)`, antes de exercer o patch,
o renderer ou saves. Na inspeção havia cerca de 2,1 GiB disponíveis no limite de
memória comprometida do Windows e Valorant aberto com cerca de 7,2 GiB privados.
Nenhum aplicativo do usuário foi encerrado e nenhuma configuração de paginação
ou orçamento guest foi alterada. O A/B foi interrompido; a sessão comparativa
nativa não foi iniciada.

`VK_LAYER_KHRONOS_validation` também não está instalado: enumeração dos layers
foi registrada em `validation-availability.json`. Não houve download ou teste
com validation layers. Continuam pendentes: execução efetiva, jitter zerado em
capturas novas, destinos escritos em GPU, avaliação de nitidez/tremulação,
regressão visual US/EU/QHD/4K, cutscenes e desempenho.

Artefatos e logs privados ficam em
`C:/Users/dartv/Documents/Codex/Knack-AA-prototype-20261026/`.
Nenhum executável instalado foi substituído. Não houve commit, merge, push ou
publicação. Este estado é um protótipo compilado, ainda sem prova de gameplay.

## Escopo e baseline

O usuário autorizou iniciar a investigação da feature solicitada na
[issue #17](https://github.com/Hyant4/Knack-II-pc-port/issues/17): desligar AA e
upscaling. Esta etapa lê código, executável local e capturas existentes. Não
inicia o jogo, altera binários, muda configurações, toca saves ou implementa
alterações no renderer.

Baseline: `experiment/gpu-small-sync-prebind-20261020`, HEAD `3d23fb7`, 302
entradas preexistentes no status. Os fontes locais já diferem do commit e dos
pacotes publicados. As conclusões sobre código referem-se a esse worktree.
O relato não identifica a versão usada pelo autor.

## Continuação — origem nativa localizada em US e EU

A continuação de 26/10 localizou estaticamente o produtor do jitter nos dois
`game.elf` suportados, cujos hashes foram conferidos com os perfis. Os endereços
abaixo são RVAs da imagem guest reconstruída, não offsets do arquivo SELF.

| Perfil 01.01 | Início da geração examinada | Multiplicação candidata |
| --- | --- | --- |
| CUSA08014 | `0x1db88d` | `0x1db9ac` |
| CUSA07670 | `0x1dac8d` | `0x1dadac` |

O código obtém um fator de amplitude de um objeto de configuração, calcula
`contador % 17`, gera Halton nas bases 2 e 3 e subtrai `0.5`. A multiplicação
candidata produz o vetor em pixels. Em seguida, o código divide suas componentes
pela largura/altura, forma uma transformação de projeção e preserva o vetor em
uma constante compartilhada de renderização, no deslocamento `+0x7340` do objeto
destino examinado. Dois ramos inspecionados usam o mesmo vetor calculado antes
da bifurcação. Isso é mais restrito que alterar todos os viewports do renderer.

As quatro capturas correlacionam com as fases 14, 12, 15 e 0 da sequência, com
amplitude unitária. O modelo aritmético da divisão por 17 foi conferido para
1024 contadores não negativos. Nenhum desses testes executa código guest.

A instrução original nos dois locais é `vmulps xmm3, xmm1, xmm0`, em sintaxe
Intel, quatro bytes `c5 f0 59 d8`. A candidata de neutralização é
`vxorps xmm3, xmm3, xmm3`, quatro bytes `c5 e0 57 db`; LLVM confirmou a
decodificação dessa sequência separadamente. Não houve substituição de bytes,
nem mesmo para executar um protótipo. A hipótese é zerar o vetor antes de seus
consumidores, mantendo contador, tamanho da instrução, fluxo, pilha e ABI.
Isso ainda não prova cobertura de todas as câmeras, cenas ou caminhos do jogo.

A assinatura de oito bytes que inclui a instrução e a duplicação seguinte foi
única no segmento executável de cada imagem. Também foram conferidos 80 bytes
de contexto, começando 16 bytes antes da instrução:

- US: SHA-256 `60c12b69a578876acafc69a6a8f61340449aca5ba9cebbe5054d12288d47197f`.
- EU: SHA-256 `f8dcd64b3eaf89da25cb6cb120efbb01df23ad2e7cffa5b97f9845f2350fbb8c`.

O dump EU lido corresponde a
`0b7843d39d11c5630a72727f286cde563aa74b7f6b22941bcbe05d907fbadf8b`.
Os dois dumps foram novamente hasheados após a leitura e permaneceram intactos.

A amplitude vem do campo `+0x6d78`, sob condições que incluem `+0x6d4d`.
Não foi comprovado que esses campos sejam uma API completa para desligar TAA.
A proposta não altera esses dados globais nem assume que o shader deixe de
acumular histórico quando o jitter é zero. O bypass dos dois destinos continua
necessário para o modo sem AA.

Artefatos privados desta continuação:
`C:/Users/dartv/Documents/Codex/Knack-AA-investigation-20261026/`, com
`disassemble_guest.py`, `us-camera-jitter.asm` e `native-jitter-audit.json`.
O helper reconstrói o SELF em memória, adapta somente metadados da cópia para
leitura pelo LLVM e envia tudo por stdin. Não grava ELF, não executa a imagem e
não altera o arquivo original. Disassembly permanece fora do Git.

## Evidências verificadas nesta etapa

1. `TemporalUpscaler::Active()` não ativa o fornecedor com `upscaler=off`.
   `ReplaceKnackDispatch()` então retorna falso; `Rasterizer::DispatchRecord()`
   continua com o compute original. Desligar FSR não desliga TAA nativo.
2. `ReplaceKnackDispatch()` já identifica o shader `0x5f9b889c`, observa recursos
   e encerra a fase de rasterização redimensionada. A integração atual só pula
   esse shader depois de produzir os dois destinos. A nova opção deve preservar
   essa fronteira, inclusive em caminhos de falha.
3. Nas quatro capturas examinadas, esse compute lê cor atual B10G11R11,
   movimento RGBA16F e histórico B10G11R11; escreve dois destinos B10G11R11.
   A fonte não pode simplesmente deixar de executar o dispatch sem escrever
   resultados coerentes para ambos os destinos.
4. O disassembly SPIR-V guardado do shader acumula histórico, limitado pela
   vizinhança. O peso do quadro atual é `0.015 + min(0.15, 0.25 * movimento)`,
   limitado a `[0, 1]`. Assim, o intervalo é 1,5% a 16,5%; isso não significa
   que 98,5% da imagem final venha sempre do histórico, pois há limitação por
   vizinhança. Não foi identificado um parâmetro simples de desativação nesse
   disassembly. O inteiro no offset 24 das constantes multiplica movimento e
   correção de jitter; zerá-lo não elimina a mistura com histórico.
5. `OnFrameStart()` zera o jitter do host no perfil Knack, mas a projeção guest
   já contém jitter. `CameraMotion::OnKnackConstants()` apenas o observa.
   Desligar o controle de jitter do host não elimina o deslocamento nativo.
6. A reanálise de quatro capturas antigas encontrou 9, 9, 9 e 10 combinações
   distintas de shader/câmera. Todas coincidem com o jitter do TAA após normalizar
   dimensões, com erro absoluto inferior a `2e-5` pixel no alvo 1080p. Há
   constantes em 1920×1080 e 960×540. Jitters do TAA: `(-0.0625, 0.314815)`,
   `(-0.3125, -0.351852)`, `(0.4375, -0.240741)` e `(-0.5, -0.5)` pixels.
   Isso cobre os buffers encontrados, não certifica todos os draws ou efeitos.
7. Busca textual no `game.elf` americano suportado não encontrou um argumento
   explícito para AA/TAA/jitter. Isso não prova ausência de uma variável interna
   ou de controles em outros arquivos. SHA-256 do arquivo lido:
   `80ccc22819fa4eeb5e1824e585f3ac5d5168e9ebc82f629e44cdb63fd573fb20`,
   correspondente ao perfil local CUSA08014/01.01.

## Hipóteses e decisões ainda abertas

O TAA nativo explica a existência de processamento temporal com FSR desligado.
Ele ainda não foi isolado como causa única do borrão relatado. Escala da janela,
motion blur, profundidade de campo e outros filtros podem contribuir. O
executável contém referências a motion blur; não foi demonstrado que esse efeito
esteja ativo no cenário da issue.

O bypass candidato copia a cor HDR atual para os dois destinos existentes,
mantendo formatos, dimensões, barreiras, pós-processamento e interface. Isso
remove a acumulação naquele passe, mas, sozinho, não remove jitter. Reutilizar
diretamente `Run()` seria incorreto: esse método ainda prepara recursos e chama
um fornecedor temporal. O bypass deve ser explícito e independente de FSR.

Há duas alternativas para o jitter:

- Controlar a origem nativa do jitter, candidata localizada na continuação acima.
  É a alternativa preferida para o protótipo, pois evita corrigir cada cópia de
  projeção ou viewport separadamente. Os locais US/EU estão identificados;
  coerência de todos os consumidores e cobertura em execução ainda exigem prova.
- Compensar o deslocamento no renderer. O viewport já aceita deslocamentos,
  mas a seleção atual usa profundidade da cena principal e condições diferentes
  para draws diretos e indiretos. Não há prova de cobertura de efeitos em meia
  resolução, transparências, partículas e consumidores das matrizes. Não aplicar
  uma subtração global ou alterar somente `P[8]`/`P[9]` sem resolver esses contratos.

Reamostrar a cor já jitterada é outra experiência possível, mas introduz filtro
espacial e não equivale a renderização originalmente sem jitter. Não deve ser
vendida como solução definitiva para nitidez.

## Contrato proposto para a feature

Adicionar preferência separada, por exemplo `antialiasing=native|off`, com
`native` como padrão e efeito apenas quando o fornecedor temporal estiver
desligado. FSR possui seu próprio processamento temporal; a interface deve
explicar isso e desabilitar a escolha independente enquanto FSR estiver ativo.
O seletor `Native AA` continua sendo um preset de FSR, não sinônimo de AA desligado.

FSR1 espacial permanece uma escolha independente. O modo sem AA não deve mudar
FSR1 ou RCAS silenciosamente. Também não pode prometer ausência de toda escala
espacial quando resolução da cena e janela diferem. Inicialmente, investigar
1080p/QHD/4K nativos; resoluções menores usam proxies e exigem contrato próprio
para a passagem à saída 1080p.

A configuração deve atravessar validação INI, supervisor, settings nativos e
launcher. A interface deve informar se a opção foi efetivamente aplicada.
Recursos, assinaturas ou perfis desconhecidos devem produzir diagnóstico
explícito; nunca indicar AA desligado quando o renderer executou TAA como fallback.
Usar reinício da sessão no primeiro protótipo evita incluir troca ao vivo de
jitter e histórico antes de compreender os contratos.

## Próxima implementação candidata e critérios de aceitação

Antes de editar o núcleo: revisar o candidato de origem descrito acima e obter
autorização concreta para o protótipo, conforme `AGENTS.MD`, seção 6.1. Nenhum
endereço de patch está aprovado por este documento.

Escopo proposto para uma tarefa Sol, sujeito à autorização do usuário:

- Novo `port/src/knack_native_aa.h` e integração em `port/src/probe.c`: validar
  perfil, segmento e contexto antes de neutralizar somente a instrução candidata
  na imagem privada do processo, quando o modo sem AA for solicitado.
- `port/gpu/shim/tlg_settings.{h,cpp}`, `port/gpu/shim/tlggpu.cpp` e
  `port/gpu/tlggpu.h`: configuração de início e comunicação de que a neutralização
  nativa foi aplicada. Não deduzir aplicação apenas da preferência salva.
- `port/gpu/shadps4/video_core/renderer_vulkan/vk_temporal_upscaler.{h,cpp}`:
  caminho específico sem histórico que escreve a cor atual nos dois destinos,
  verificando formatos, dimensões, aliases e sincronização Vulkan. Inicialmente
  apenas tamanhos nativos, sem proxies de baixa resolução.
- `run.py`, `port/launcher/knack2/config.py`,
  `port/launcher/knack2/ui/backend.py`, `port/launcher/knack2/ui/Main.qml`:
  preferência separada e validada, efetiva somente após reinício e incompatível
  com fornecedor temporal ativo. Mudanças de fornecedor ao vivo precisam ser
  bloqueadas ou exigir reinício enquanto o patch guest estiver instalado.
- Testes específicos em `tests/` e `port/tests/`, este relatório e handoff.
  Nenhuma alteração de CMake, dependência, perfil, submódulo ou ABI.

Um modo sem AA não pode voltar silenciosamente ao TAA depois de neutralizar
jitter. Falhas devem encerrar ou recusar explicitamente esse modo. O estado
padrão mantém o comportamento atual. Primeiro gate: testes sintéticos e build
externo com toolchain existente; depois duas sessões A/B de até 60 segundos em
1080p, com saída e dados de usuário isolados. Cobertura visual EU/QHD/4K,
partículas, cutscenes e transições permanece um gate posterior à primeira prova.
Nenhum executável instalado deve ser promovido por esse protótipo.

O primeiro protótipo deve provar cópia correta dos dois destinos e controle de
jitter, preservar US/EU 01.01 e manter padrão idêntico ao atual. Testes sintéticos
devem cobrir seleção, incompatibilidades, dimensões, aliases, falhas e ausência
de interferência em FSR3/4. Validação Vulkan e captura real devem comprovar que
o passe temporal foi substituído e que os destinos foram escritos.

A comparação visual deve usar o mesmo ponto e câmera, com AA nativo ligado e
desligado: imagem parada, câmera em movimento, transparências/partículas, HUD,
cutscene e retorno ao gameplay, em 1080p, QHD e 4K. Medir nitidez, tremulação,
rastros e alinhamento. Não alterar motion blur ou outros efeitos nesse A/B.

## Reprodução e limites

Analisador somente leitura:

```powershell
.\.venv\Scripts\python.exe -B scripts/analyze_knack2_native_aa.py --capture-dir <pasta-do-census>
```

Evidências locais existentes, fora do Git:
`C:/Users/dartv/Documents/Codex/KNACK2-FSR-TEMPORAL-20261023/`.
Entradas usadas: `5f9b889c.spvasm` e quatro `frame_*.txt` de
`run02/control/census/`. São capturas de 23/10/2026 em 1080p; não são novos
testes de gameplay, prova visual desta feature ou evidência EU/QHD/4K.

O analisador processou os quatro arquivos e rejeitou diretório vazio e captura
sem dispatch temporal com saída 2. A revisão anterior executou cinco testes de
configuração/câmera em `tests/test_knack2_temporal.py`, todos aprovados. Eles não
testam um modo sem AA. Não houve nova execução do jogo, build ou medição de FPS.
O grafo Graphify consultado não contém os métodos específicos encontrados no
worktree; as conclusões acima usam fontes e artefatos locais diretamente.
