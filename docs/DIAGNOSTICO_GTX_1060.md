# Diagnóstico opcional GTX 1060 — issue #12

O preset **GTX 1060 black texture** oferece dois experimentos opcionais para
investigar superfícies pretas. Nenhum deles é uma correção gráfica confirmada.
O diagnóstico começa **desligado**. As demais predefinições não o ativam.

## Como comparar

1. Selecione o preset GTX 1060 em **Configurações → Vídeo**.
2. Com diagnóstico **Desligado**, registre a cena e os ajustes atuais.
3. Escolha **1 — Cache novo por sessão**, salve e inicie outra partida. Compare
   a mesma cena e câmera, mantendo resolução, MSAA, readbacks e demais ajustes.
4. Escolha **2 — Cache novo + compilação síncrona**, salve e repita a cena.
   Esse caso acrescenta somente a compilação síncrona ao experimento de cache.
   Pode haver pausas enquanto os shaders ficam prontos.
5. Para retornar ao comportamento habitual, escolha **Desligado** ou outro
   preset. Salve para manter a escolha após fechar o launcher.

Cada partida de diagnóstico cria uma pasta GPU nova em
`<saída>/session-<data-hora>/gtx1060-gpu`. O runtime guarda nela seu cache gráfico
e demais arquivos auxiliares da GPU. Não copia, apaga ou reutiliza o cache
habitual. O perfil de saves continua sendo o selecionado pelo launcher.
Os arquivos de diagnóstico permanecem no disco para inspeção; não há limpeza
automática. Uma nova sessão começa com outra pasta vazia.

A escolha fica em `launcher_settings.json`, separada do INI gráfico. Não modifica
os padrões permanentes de shaders ou exige trocar executáveis/SFO do jogo.
Ao abandonar o preset GTX, o diagnóstico é desativado e não reaparece sozinho
ao selecionar GTX novamente. Metadados inválidos também não o ativam.

## Evidência produzida

O supervisor registra modo e caminho no log. `session.json` inclui
`gtx1060_diagnostic`, com modo, indicador de cache novo e variáveis solicitadas:

- `cache`: somente `TLG_GPU_USER_DIR`, apontando para a pasta privada da sessão;
- `cache_sync`: acrescenta `TLG_ASYNC_SHADERS=0` e
  `TLG_COMPUTE_ASYNC_BUILD=0`.

O log nativo `Config env` permite conferir os valores recebidos. Eles continuam
valendo após a passagem de `eboot.bin` para `game.elf` e no reinício do overlay
Linux. O modo normal não ganha essas variáveis nem metadados de diagnóstico.
Se o menu nativo alterar opções durante a partida, registre isso: os metadados
representam a configuração de início, não uma medição contínua do estado gráfico.

Para uso manual explícito do supervisor:

```powershell
.\.venv\Scripts\python.exe .\run.py --game-dir "D:\Meu dump" --out "D:\Diagnostico Knack\cache" --config "D:\Diagnostico Knack\teste.ini" --gtx1060-diagnostic cache
```

Troque o último valor por `cache_sync` para o segundo caso. O argumento exige
saída explícita e execução normal; é recusado em análise/imports e quando a
saída sobrepõe dump ou dados habituais do repositório. O launcher só o envia
quando o preset GTX e o diagnóstico opcional estão selecionados.

## Limites da implementação

Os testes usam processos e dados sintéticos. Conferem UI, persistência,
isolamento, variáveis, metadados, LoadExec e retorno ao modo habitual. Não
confirmam melhoria na GTX 1060, estabilidade de gameplay, FPS ou ausência de
falhas visuais. A implementação não altera o backend Vulkan, a tradução de
shaders, MSAA ou os hashes de validação.

Esta opção existe nos fontes atualizados; a pré-release RC1 já publicada não
recebe alterações automaticamente.

## Captura dirigida de um quadro — fontes posteriores à RC2

O modo **3 — Capturar um quadro (F11)** habilita uma coleta opcional na cena
afetada. Selecione, salve, inicie o jogo e pressione F11 uma única vez quando
as superfícies pretas estiverem visíveis. A captura pode pausar temporariamente
o jogo para concluir leituras da GPU. Para repetir, inicie outra sessão.

Esse modo mantém cache, compilação, MSAA, resolução e demais ajustes escolhidos.
Arquivos vão para `session-*/gtx1060-capture`. São um relatório de passes e
imagens RAW identificadas por etapa, endereço, dimensões, pitch e formato Vulkan.
O RAW não é um PNG: deve ser interpretado no formato indicado pelo diagnóstico.

### Onde encontrar e o que enviar

Depois de F11, encerre o jogo e abra **Logs e diagnóstico → Abrir pasta para
enviar**. O launcher mostra o caminho completo da captura e abre a pasta da
sessão, que reúne os arquivos necessários. Comprima a pasta `session-*` inteira
e anexe o ZIP à issue. Ela deve conter:

- `gtx1060-capture/`, com relatório `frame_*.txt` e imagens `.raw`;
- `session.json`;
- `00-eboot.bin.log` e `01-game.elf.log`.

Não envie somente o cache de shaders nem apenas uma imagem final. O relatório
e os targets são necessários para comparar as etapas gráficas. O marcador
`.requested` dentro da captura impede repetição também após reinício do guest;
deixe-o intacto e abra outra sessão para uma nova coleta.

No uso solo normal do Windows, a pasta está em:

```text
%LOCALAPPDATA%\KnackII-PC\single\session-<data-hora>\gtx1060-capture
```

Ao abrir `Test GTX 1060.cmd`, a raiz é `%LOCALAPPDATA%\KnackII-PC-Issue12-Test`.
Cooperativo e `--data-dir` usam outra saída; o caminho mostrado pelo launcher é
a referência exata. A pasta existe antes da captura; os arquivos aparecem após
pressionar F11 e concluir a coleta. `session.json` é finalizado ao encerrar o jogo.

Limites fixos por sessão:

- Uma solicitação, janela entre dois SubmitDone na sequência de comandos GPU;
- G-buffer: até cinco imagens e 64 MiB;
- Intermediários HDR: até quatro imagens e 16 MiB, reservando espaço para o final;
- Saídas dos shaders conhecidos de TAA/composição: até quatro imagens e 48 MiB;
- Total máximo de pixels gravados: 128 MiB em 13 arquivos;
- Census limitado a 512 entradas, sem copiar conteúdo dos buffers guest.

O orçamento conta tentativas reservadas, inclusive gravações que falhem. Imagens
repetidas por endereço/categoria não são gravadas novamente. Depth, formatos
comprimidos, volumes/arrays e imagens multisampled são recusados antes de alocar
staging ou registrar cópia. O helper também recusa discrepância entre a imagem
host e seu formato/extent descrito. Os limites podem deixar targets sem captura;
o relatório registra skips e truncamento. Não se presume captura completa de
todos os recursos de um frame.

No momento da coleta, o primeiro passe com cinco ou mais targets marca o G-buffer;
intermediários são saídas HDR em pelo menos 960×540; a seleção final usa os hashes
de TAA/composição já conhecidos pelo port. O relatório é a evidência para confirmar
se essa seleção corresponde ao quadro desejado. Não há resolução de MSAA nem
conversão de textura adicionada para produzir a captura.

Launcher e runtime precisam ser correspondentes. Antes de preparar imagens ou
executar o guest, o supervisor exige o marcador
`KNACK2_BOUNDED_CAPTURE_V1=128MiB,13images,1frame` em `--cpu-features`. Runtime
antigo é recusado; não tente habilitar esse modo misturando executáveis de releases.
`session.json` registra modo, caminho, limites e tecla de ativação.

O modo não corrige a renderização. Sua finalidade é localizar a primeira etapa
que introduz preto. Os testes automáticos de limites e protocolo não substituem
uma captura real na GTX 1060.

### Comparação de readback linear

O modo **3 — Capturar um quadro (F11)** fixa `TLG_READBACK_LINEAR=0`.
O modo **4 — Captura + readback linear (F11)** fixa `TLG_READBACK_LINEAR=1`.
Ambos mantêm cache, compilação, resolução, FSR, nitidez e sincronização nos
ajustes existentes. A única diferença de configuração GPU entre esses dois
modos é o readback linear. Esse recurso não é sinônimo de leitura precisa.

Execute cada modo em uma sessão nova, na mesma cena afetada. Pressione F11,
encerre o jogo e use **Logs e diagnóstico → Abrir pasta para enviar**. Envie
os dois ZIPs completos de `session-*`, identificando modo 3 e modo 4, e informe
se as superfícies pretas mudaram. `session.json` registra o modo e o valor exato
de `TLG_READBACK_LINEAR`; pasta e limites de captura são iguais nos dois modos.

Esse teste não importa o preset completo do inFAMOUS: não ativa readbacks
adiados, dados do quadro anterior ou ajustes específicos de distância/LOD.
Pode aumentar cópias e pausas. Benefício visual ainda não foi demonstrado na
GTX 1060. Os modos desligado/cache/cache síncrono não recebem a nova variável.
