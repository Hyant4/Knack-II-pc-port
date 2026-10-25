# FSR e resolução no Knack II

## Recursos disponíveis nos fontes

O launcher e o menu interno oferecem resolução inicial da janela: automática,
1280×720, 1600×900, 1920×1080, 2560×1440, 3840×2160, 2560×1080,
3440×1440, 3840×1600, 5120×1440, 5120×2160 e 7680×2160. A escolha aplica na próxima
sessão. A janela é reduzida proporcionalmente quando não cabe na área útil do
monitor. Tela cheia e janela sem borda usam o modo do desktop.

Essa configuração controla a apresentação. Não altera os alvos internos do jogo,
não transforma a renderização em 4K nativo e não garante aumento de FPS.
A versão 0.16 acrescenta proporção e campo de visão ultrawide experimentais.

O FSR 1 espacial amplia a imagem quando a saída é maior. A nitidez RCAS pode ser
usada depois da ampliação ou sozinha quando entrada e saída têm o mesmo tamanho.
Na redução de tamanho, o passe FSR é ignorado. O menu informa quando FSR e RCAS
foram aplicados e permite desligar RCAS mesmo quando a configuração não produz efeito.

As preferências usam o INI de lançamento já existente:

```ini
window_resolution=auto
aspect_ratio=auto
fsr1=0
rcas=0
rcas_strength=50
```

`window_resolution` aceita `auto`, `1280x720`, `1600x900`, `1920x1080`,
`2560x1440`, `3840x2160` e os seis presets ultrawide listados acima.
As configurações de áudio e precisão da GPU não são alteradas.

## Resolução interna nativa — QHD e 4K — RES-03

Nos fontes atuais, `internal_resolution=2560x1440` e `internal_resolution=3840x2160`
selecionam a resolução nativa do jogo até a composição final. A preferência exige
reiniciar a sessão. A resolução da janela continua independente: em monitor
1080p, o apresentador adapta a imagem final maior à área de exibição disponível.

```ini
internal_resolution=3840x2160
window_resolution=1920x1080
upscaler=off
```

O supervisor passa a opção nativa `-2160p` somente ao `game.elf`. Na imagem privada
do processo, o preset correspondente recebe as dimensões escolhidas e os limites
originais de dimensão passam a aceitar esse tamanho. Versão, assinaturas das
instruções, constantes e limites dos segmentos são conferidos antes de qualquer
escrita. Falha interrompe a inicialização. O dump original permanece intacto.
O modo de console continua base; nenhuma função de kernel foi alterada para
simular outro hardware.

O próprio jogo passa a dimensionar seus alvos, constantes, histórico temporal,
pós-processamento e framebuffer. Proxies de rasterização do host ficam
desativados nesse modo. `Imagem do jogo` reflete a resolução final recebida pelo
apresentador. Efeitos que o jogo normalmente executa em meia resolução ou em
mapas de sombra próprios mantêm suas proporções; a opção não muda os assets.

O pool gráfico do jogo precisa de espaço adicional para esses alvos. A reserva
é ampliada em 768 MiB para QHD e 1536 MiB para 4K, mantendo o pool gráfico abaixo
de 4 GiB. O supervisor configura a capacidade de memória direta já existente
no runtime em 5824/6592 MiB, respectivamente. Não há ajuste global em
`runtime_memory.c`; configurações normais preservam a capacidade anterior.

Validação americana CUSA08014/01.01, Chapter 3-2, FSR desligado:

| Seleção | Captura final do renderer | Resultado |
| --- | --- | --- |
| QHD | 2560×1440, 14.745.600 bytes raw | Cena observada; encerrou pelo limite de 90 s. |
| 4K | 3840×2160, 33.177.600 bytes raw | Cena observada; encerrou pelo limite de 90 s. |

Logs registraram attachments nativos nessas dimensões, com rasterização por
proxies desativada. As capturas foram inspecionadas; saída da janela foi
1704×958 ajustada ao monitor. A primeira tentativa QHD esgotou o pool gráfico
fixo e encerrou com código 3; após a reserva adicional, a mesma cena ultrapassou
esse ponto e encerrou pelo timeout previsto. Essa falha inicial permanece
registrada no handoff.

Runtime compilado; suíte com 194 testes executados, 190 aprovados e quatro
ignorados. Testes sintéticos cobrem os dois perfis, rejeição sem escrita parcial,
limites, reserva de memória, argumentos e persistência. Região europeia,
campanha completa, combinação com FSR e ultrawide em resolução nativa alta
continuam sem validação real nesta etapa. Saves e preferências habituais foram
preservados; recurso permanece no candidato local, sem substituir o pacote 0.16.

`auto`, 720p, 900p e 1080p preservam os caminhos anteriores. Os registros abaixo
descrevem as etapas RES-01/RES-02; o comportamento parcial de QHD/4K foi
substituído pelo caminho nativo desta seção.

## Histórico — resolução parcial da cena — RES-01/RES-02

Após a 0.16, os fontes oferecem `internal_resolution=auto|1280x720|1600x900|1920x1080|2560x1440|3840x2160`.
Essa opção é independente de `window_resolution`. O padrão `auto` preserva o
comportamento anterior: cena em 1080p com FSR desligado ou no tamanho do preset
temporal quando o FSR está ativo. A escolha manual define exatamente o tamanho
dos alvos de rasterização elegíveis e substitui o preset de qualidade temporal.
O launcher aplica a preferência na próxima sessão; o overlay aplica na fronteira
do próximo quadro. Os dois perfis regionais usam o mesmo contrato.

Esta primeira etapa não altera alocações guest, shaders compute nativos,
pós-processamento, interface ou framebuffer final de 1920×1080. Texturas do host
recebem a rasterização na dimensão escolhida; o caminho de coerência existente
resolve os dados para acesso nativo. Passes incompatíveis com esse caminho,
como MSAA ou feedback de attachment, continuam na resolução nativa. O campo
`Alvo de rasterização da cena` informa o tamanho configurado, não certifica que
todos os passes o usaram. A linha `Imagem do jogo` continua mostrando a saída real.

1440p e 2160p são supersampling da cena, com redução para 1080p nas fronteiras
nativas; não são saída final nativa em 1440p/4K. Resoluções menores também retornam
ao framebuffer nativo. FSR temporal reconstrói para pelo menos 1080p; quando a
cena está acima de 1080p, processa nesse tamanho antes da redução, sem pedir
downscale ao fornecedor. A redução de cor usa filtro linear quando o formato
suporta; profundidade, stencil e formatos sem esse recurso mantêm nearest.
A rasterização redimensionada termina no passe temporal, mesmo com FSR desligado
ou em falha, preservando pós-processamento e interface.

Validação inicial (RES-01): compilação do runtime Windows e 192 testes automáticos,
com 188 aprovados e quatro ignorados. Cobrem persistência Python/C++, rejeição de
valores inválidos, filtro de ambiente, planejamento das dimensões e seleção por
teclado. O harness de alvos Vulkan compilou e vinculou por comando externo com a
compatibilidade Windows existente, mas sofreu violação de acesso antes das
verificações. As novas verificações de resolve linear não foram executadas.
Não houve boot, captura do renderer ou medição de desempenho nesta etapa.
Qualidade visual, FSR em resoluções altas e transições em gameplay permanecem
pendentes; nenhuma release ou instalação habitual foi substituída.

### Validação seguinte no monitor 1080p — RES-02

Sessões americanas isoladas chegaram à cena do save em Chapter 3-2 com FSR
desligado em 720p, 1080p, 1440p e 2160p. Capturas do renderer foram inspecionadas;
o cenário continuou coerente e a interface permaneceu na composição nativa.
A janela ajustada à área útil mediu 1704×958; o framebuffer capturado continuou
1920×1080. Assim, os tamanhos maiores exercitam supersampling, não uma saída
física 4K. As capturas são de momentos diferentes, sem comparação quantitativa
de qualidade ou desempenho.

FSR 3.1 também aplicou reconstrução real em 1440p: o log registrou entrada e
saída temporal 2560×1440, com `guest-output=1920x1080`, exercitando a redução
para os dois históricos nativos. A primeira tentativa ficou no menu até o
timeout e não conta como teste desse passe; uma segunda sessão chegou à cena.
FSR 4, 900p, região europeia, combate prolongado e campanha completa não foram
revalidados nesta rodada.

O harness Vulkan agora passa na RX 6650 XT: dez roundtrips de cor, profundidade e
stencil, redução linear 2160p e proxy de mip em meia resolução. O acesso inválido
anterior foi isolado na inicialização do dispatcher local do teste. Carregar
apenas suas funções Vulkan evita a colisão com o dispatcher da biblioteca no
Windows; o renderer não precisou ser alterado para essa correção.
O link externo ainda usa a compatibilidade Windows existente e `onecore`.

O gatilho existente `TLG_SCENE_DEBUG` passou a ser lido na fronteira comum de
quadro, incluindo a apresentação compute do Knack. Logs de 1440p/2160p confirmam
draws redirecionados e passes que continuam nativos. Essa alteração é de
observabilidade. Saves e preferências habituais são conferidos por hash; os
testes usam cópias temporárias e timeouts finitos. Evidências e limitações estão
registradas no topo do `HANDOFF_ASTRA.md`; não houve publicação de pacote.

## Ultrawide experimental na 0.16

`aspect_ratio` aceita `auto`, `16:9`, `21:9` e `32:9`. O automático usa a proporção
do monitor principal quando ela é maior que 16:9 e até 32:9; fora dessa faixa,
mantém 16:9. A opção explícita permite testar janelas largas sobre monitor comum.
Selecionar apenas o tamanho da janela não força outro campo de visão.
Mudanças de proporção exigem nova sessão.

As versões 0.16 e 0.17 alteram quatro constantes de projeções auxiliares, mas a
câmera principal continua calculando sua proporção pela resolução interna 16:9.
Por isso, o quadro final pode ser esticado: a instalação das quatro constantes
não comprova que o campo horizontal do gameplay foi ampliado (issue #7).

Correção distribuída no hotfix 0.17.1: além das quatro constantes auxiliares,
uma instrução guardada na inicialização da câmera principal fornece a proporção
selecionada. O jogo constrói sua projeção e frustum a partir dessa entrada; o
campo vertical e as alocações internas permanecem iguais. Assinaturas regionais,
valores originais e segmentos executáveis são verificados antes de qualquer
escrita. Falha conserva câmera e apresentação em 16:9. Apenas a imagem privada
é modificada; dump original e ABI permanecem intactos.

Em duas sessões americanas de até 90 segundos, com cópias dos saves e FSR
desligado, a captura de constantes da GPU comprovou a causa e a correção na
mesma câmera de Chapter 3-2: proporção 1,77777 antes e 2,33334 depois; campo
horizontal 72,73° e 88,05°, respectivamente, mantendo o vertical em 45°.
As capturas finais mostraram mais cenário lateral e o menu de pausa proporcional.
Isso não certifica campanha completa, região europeia em runtime, resolução
interna alta com ultrawide ou monitor ultrawide físico.
O teste sintético executa a sequência x86 real da câmera para as duas regiões,
incluindo 3440/1440, 21:9 e 32:9, em dimensões internas 1080p/QHD/4K.

O hotfix 0.17.1 mantém abertura e vídeos pré-renderizados na área original 16:9,
centralizada, com sobras pretas. Vídeos de proporção cinematográfica preservam
suas barras internas. A conversão NV12 recupera projeção original; composição de
interface, legendas e logos segue o jogo. O hotfix limpa barras laterais depois
da composição final. Ao pular, o quadro 16:9 permanece durante quadros vazios e
carregamento; uma câmera de cena válida libera gameplay ultrawide. Configuração
escolhida pelo jogador não muda.

Revalidação de 25/10/2026 no candidato final: sessões americanas de 75 segundos,
com saves copiados e FSR desligado, alcançaram Chapter 3-2 em 21:9 e 32:9.
Matrizes capturadas confirmaram proporções 2,33334 e 3,55555, com campo vertical
45° e horizontais 88,05° e 111,65°. Capturas de apresentação 1780×763 e 1824×513
preservaram proporções; a diferença geométrica foi inferior a um pixel de saída.
Os seis presets de janela ultrawide passaram 480 combinações sintéticas de
proporção, resolução interna e limites da janela. Essas provas simulam janelas
largas sobre monitor 1080p; não certificam monitor ultrawide físico.

O usuário confirmou funcionamento após a correção da transição ao pular.
Capturas de abertura/vídeo mostraram laterais pretas; as capturas posteriores
de gameplay voltaram a ocupar a área larga. Permanecem sem certificação a
campanha inteira, todos os filmes/menus, cutscenes em tempo real forçadas a
16:9, fim natural do filme inicial e combinações com FSR temporal/resolução
interna alta. As releases 0.16/0.17 continuam com o comportamento anterior.

## Alterações no passe espacial

- O caminho EASU sem RCAS prepara a imagem de saída correta para escrita.
- RCAS em tamanho nativo lê a imagem de origem diretamente, dispensando EASU.
- A origem produzida pelo jogo fica visível também ao estágio de compute que
  executa o filtro.
- A seleção rejeita dimensões inválidas e aceita ampliação em um eixo quando o
  outro permanece igual. A área de apresentação usa cálculo inteiro proporcional.

Essas correções são locais ao apresentador. Não modificam shaders do jogo,
readbacks precisos, sincronização das threads guest ou endereços guest.

## FSR temporal no perfil CUSA08014/01.01

FSR 3.1 e FSR 4 estão conectados ao passe temporal do Knack II, antes do
pós-processamento e da interface. Launcher e overlay oferecem seleção de fornecedor
e quatro modos. No padrão `internal_resolution=auto`, a saída da reconstrução permanece 1920×1080:

| Modo | Renderização da cena | Saída temporal |
| --- | --- | --- |
| Native AA | 1920×1080 | 1920×1080 |
| Qualidade | 1280×720 | 1920×1080 |
| Equilibrado | 1130×636 | 1920×1080 |
| Desempenho | 960×540 | 1920×1080 |

```ini
upscaler=fsr3
preset=1
```

`upscaler` aceita `off`, `fsr3` e `fsr4`; `preset` aceita 0 a 3, na ordem da
tabela. O padrão continua desligado. Resolução da janela e qualidade temporal são
preferências diferentes. Pedir janela 4K não muda a saída temporal para 4K.
Não há geração de quadros nesta implementação.

A integração usa cor HDR, profundidade, vetores nativos de objetos/câmera e jitter
do próprio jogo. O passe `0x5f9b889c` é substituído somente quando o fornecedor
produz saída válida; os dois destinos de histórico nativos recebem o resultado.
O passe `0x446884a0` marca a composição final. Constantes de câmera de 432 bytes
são reconhecidas e validadas antes de uso. Readbacks precisos permanecem ativos;
não há replay adicional de geometria para fabricar vetores de movimento.

FSR 4 usa o fornecedor experimental Vulkan v07 INT8 já herdado do projeto. Exige
recursos compatíveis da GPU e modelos locais. Preparação do modelo ocorre em uma
thread própria, sem registrar comandos no scheduler do renderer. Enquanto prepara,
o passe temporal nativo continua. Assets ausentes, GPU incompatível ou falha
permanente provocam retorno ao FSR 3.1; o overlay mostra o motivo. Encerramento
aguarda a thread para preservar a vida útil do dispositivo Vulkan.

Os modelos acompanham os ZIPs da release 0.12 e foram instalados no projeto local.
O diretório padrão é `port/fsr4_shaders`, relativo ao diretório de execução `port`.
O pacote externo desta tarefa contém modelos verificados, licença e variantes
`opt/` validadas com `spirv-val`; preserve essa subpasta. Os scripts existentes
`port/tools/fetch_fsr4_assets.sh` e `port/tools/fsr4_optimize.sh` documentam aquisição
e otimização. Modelos nesta tarefa vieram de Q2RTX, revisão fixa
`ae8d628fae208813172446d1e49ed94150b04658`, com tamanho e Git blob conferidos antes
do uso. Nenhum submódulo ou arquivo de licença do repositório foi alterado.

### Preparação corrigida para Windows

A investigação posterior à 0.12 isolou uma regressão nas variantes que passavam
por SPIRV-Cross/GLSL/glslang. Na RX 6650 XT com driver AMD 26.10.1, o benchmark
sintético Native AA 1080p mediu 78,238 ms por quadro: 36,340 ms no passe 11 e
39,076 ms no pós-passe. Os demais passes somaram aproximadamente 2,8 ms.
Otimização adicional com `spirv-opt -O` não resolveu (78,926 ms).

O novo `port/tools/fsr4_prepare_windows.py` preserva o pós-passe original e insere
somente a proteção de limites no SPIR-V original do passe 11. Não remove a
proteção contra a corrida de escrita na borda; mantém os cálculos originais e
os produtos escalares INT8. O wrapper `fsr4_optimize.sh` usa esse caminho em
MSYS/MINGW/Cygwin; os demais sistemas mantêm o caminho anterior.

```powershell
# Com Python e SPIR-V Tools já disponíveis no PATH:
python port/tools/fsr4_prepare_windows.py port/fsr4_shaders
```

O candidato mediu 6,219 ms por quadro no mesmo benchmark, com passe 11 em
2,878 ms e pós-passe em 0,702 ms. Em seis quadros com entradas pseudoaleatórias,
os 16.588.800 bytes da saída final foram idênticos aos da variante anterior.
Trinta arquivos preparados passaram `spirv-val`. Os testes sintéticos de
preparação não iniciam o jogo. Isso não equivale a medição de FPS em gameplay;
processos gráficos simultâneos podem afetar os tempos.

Essa correção é posterior aos assets publicados na release 0.12. Contextos FSR 4
já abertos mantêm seus shaders na memória: desligue e ligue o FSR 4 no overlay
ou reinicie o jogo para carregar arquivos atualizados. Não é necessário trocar
o executável para esta correção. Máscara reativa e cobertura de gameplay continuam
com os limites descritos abaixo.

Limitações: máscara reativa de transparências ainda não foi calibrada para Knack;
qualidade em água, partículas, desoclusões, cortes de câmera e campanha completa
continua exigindo cobertura adicional. O fornecedor FSR 4 v07 não equivale a
certificação oficial de hardware nem garante ganho de desempenho. FSR 4.1.1 não
foi exposto nesta tarefa.

Referências: [HayatoG/iss_pc, revisão consultada](https://github.com/HayatoG/iss_pc/tree/7a48167dcba8b25ef66fb3f99d712415f225790f)
e [contrato de integração AMD](https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-upscaler/).
A implementação reaproveita os fornecedores locais; não importa outro renderer.
O header temporal correspondia ao upstream antes da adaptação específica do Knack.

## Evidências da integração temporal — 23/10/2026

Build completo Windows passou em diretório externo, incluindo a preparação
assíncrona de FSR 4. A suíte `test_knack2*.py` executou 148 testes: 145 passaram
e 3 foram ignorados por exigirem artefatos de runtime explícitos. Inclui seleção
dos dois novos controles QML por teclado, persistência e reabertura, filtro de
ambiente do supervisor e contratos sintéticos C++ de câmera/movimento.
O teste C++ de captura também passou: a fronteira SubmitDone encerra o registro
e libera as otimizações que ficam suspensas durante uma captura ativa.

Foram usadas as cinco sessões finitas autorizadas, com perfil e saída isolados.
FSR 3.1 aplicou saída real em Native AA e Qualidade; FSR 4 v07 aplicou saída real
em Native AA e Desempenho. A primeira tentativa síncrona de FSR 4 não concluiu
a preparação no intervalo disponível. Depois da correção, o jogo continuou
renderizando durante a preparação, inclusive ao desligar e religar o fornecedor.
Não houve conclusão da preparação do modelo Qualidade na janela de teste;
Equilibrado e outros modelos ainda não têm validação visual em gameplay.

Cobertura real: carregamento de Chapter 2-4, cena com água/vegetação e movimento
curto de câmera. Capturas inspecionadas mantêm cena coerente; isso não certifica
ausência de ghosting, estabilidade prolongada, combate ou campanha completa.
Na RX 6650 XT testada, FSR 4 v07 teve amostras próximas de 10–12 FPS, tanto em
Native AA quanto em Desempenho. FSR 3.1 Qualidade voltou a amostras próximas de
60 FPS na última sessão. O usuário relatou aproximadamente 30 FPS e uso
simultâneo de Valorant; essas condições não permitem um benchmark controlado.

Pedidos de janela 2560×1440 e 3840×2160 resultaram em 1704×958, limitados à área
útil do monitor; imagem do jogo e saída temporal permaneceram 1920×1080.
Oito hashes dos saves originais permaneceram iguais. Nenhum EXE instalado foi
substituído e o launcher não foi reempacotado. Build, logs, capturas locais,
modelos verificados e originais estão em
`Documents\Codex\KNACK2-FSR-TEMPORAL-20261023`.

Uma execução da suíte durante o jogo falhou na espera de logs de um teste Qt
preexistente; a repetição completa após encerrar o jogo passou. Nenhum ajuste
de core ou teste foi feito para esconder essa falha. Camada Khronos indisponível;
validação de SPIR-V e execução real não substituem essa cobertura.

## Evidências do passe espacial — etapa anterior

Em 23/10/2026, a descoberta `test_knack2*.py` executou 145 testes: 142 passaram
e 3 foram ignorados por exigirem artefatos de runtime explícitos. Inclui teste C++
do header e do leitor/escritor nativo de INI, seleção real por teclado no QML,
persistência, reabertura e regressões de launcher, supervisor, armazenamento e RCAS.
O build Windows completo de `tlg-probe` passou em diretório externo, com seis jobs,
RelWithDebInfo, LTO e PGO desligados. Interface inspecionada em 1180×810 e 980×700.

Um harness Vulkan adicional não alcançou os filtros: após problemas de integração
da fixture, a inicialização excedeu o timeout de 30 segundos. A camada Khronos de
validação não está instalada. A tentativa e seus logs foram preservados fora do
projeto; a fixture compartilhada foi devolvida aos bytes originais. Isso não
constitui validação GPU dos novos caminhos nem evidência de crash do jogo.

Comando reproduzível para os testes entregues, sem iniciar o jogo:

```powershell
$env:PYTHONUTF8 = '1'
.\.venv\Scripts\python.exe -B -m unittest discover -s tests -p 'test_knack2*.py' -v
```

O executável instalado não foi substituído. O runtime compilado, as capturas de
interface, os originais e os registros desta tarefa ficam no pacote local
`Documents\Codex\KNACK2-FSR-RESOLUTION-20261023` do usuário.
