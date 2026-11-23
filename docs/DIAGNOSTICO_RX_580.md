# RX 580: compatibilidade e diagnóstico experimentais

## Ativação exclusiva do preset

Os ajustes específicos são ativados somente pela seleção explícita de
**RX 580: compatibilidade experimental**. Não há ativação automática por marca
ou modelo da GPU. Fora desse preset, flags residuais de junção/diagnóstico são
ignoradas e o caminho anterior do renderer permanece em uso.

Ao trocar para outro preset, inclusive GTX 1060, os ajustes RX 580 são
desativados na próxima sessão. Salve e reinicie para aplicar. A seleção RX 580
salva por versões anteriores é migrada somente quando existe o metadado explícito
do preset; uma chave de junção isolada não ativa o modo.

Além da junção de barreiras, o preset verifica formatos de vértices antes de
reutilizar pipelines preparados em GPUs sem entrada de vértices dinâmica. Se
o formato atual divergir do capturado, a preparação é rejeitada e o caminho
normal recalcula a chave. Formatos iguais e entrada dinâmica mantêm seu fluxo.
Esse guard passou em cenário sintético; a correção visual na RX 580 ainda não
foi confirmada. Builds anteriormente publicadas não contêm esse guard novo.

O marcador `rx580_preset=1` é capturado no INI de cada lançamento e encaminhado
como `TLG_RX580_PRESET=1`. É um estado do preset, não um controle de detecção de
hardware. `session.json` registra `rx580_preset` e os diagnósticos efetivos.
Não misture um launcher antigo com runtime novo: o pacote completo precisa
fornecer o marcador de ativação. Downloads existentes não mudam automaticamente.

## Contexto do crash

A issue [#20](https://github.com/Hyant4/Knack-II-pc-port/issues/20) registra
um crash no carregamento em `amdvlk64.dll+0xebbbb`, com RX 580 e driver Vulkan
AMD `2.0.279`. O mesmo endereço aparece na investigação do inFAMOUS que deu
origem aos [PRs #232](https://github.com/HayatoG/iss_pc/pull/232) e
[#278](https://github.com/HayatoG/iss_pc/pull/278). Essa correspondência orienta
o teste; ainda não comprova a mesma causa no Knack.

O workaround combina barreiras adjacentes que têm a mesma imagem, dependência
e transição. No caso sintético do upstream, 9.216 barreiras tornam-se uma.
Layouts, acessos, estágios, aspectos ou filas diferentes não são combinados.
Não são removidas transições necessárias. A opção fica desligada por padrão
e não é ativada automaticamente pela detecção de uma placa AMD.

## Comparação na máquina afetada

Use uma build que contenha **launcher e runtime desta alteração**. Os downloads
publicados anteriormente não recebem o controle nem o workaround automaticamente.
Não é necessário apagar cache, saves ou preferências.

1. Mantenha resolução, FSR, precisão da GPU, cache e compilação iguais nas duas
   sessões. Selecione o preset RX 580. Em **Configurações > Vídeo > Diagnóstico
   avançado > Mostrar ajustes**, ligue **Diagnóstico de barreiras de imagem**.
2. Selecione **Desligado** em **Junção de barreiras de imagem**, salve as
   configurações e reinicie o jogo. Repita o carregamento que falha. Guarde os
   três arquivos dessa sessão: `00-eboot.bin.log`, `01-game.elf.log` e `session.json`.
3. Altere somente **Junção de barreiras de imagem** para **Ligado**, salve e
   reinicie. Repita o mesmo carregamento e guarde os arquivos da nova sessão
   separadamente. Essa comparação muda apenas a junção dentro do preset; o
   guard de formatos continua ativo. Ela não é um A/B do guard novo.
4. Informe se o carregamento chegou ao gameplay em cada sessão. Se a sessão
   ligada passar, isso confirma apenas o trecho testado; não certifica campanha,
   desempenho ou estabilidade contínua.
5. Antes de anexar os logs publicamente, revise nomes e caminhos pessoais.
   Não envie saves, executáveis do jogo, assets ou dumps de memória. Ao terminar,
   desligue o diagnóstico. Selecionar o preset **Padrão do port** desativa todas
   as correções RX 580 na próxima sessão. Não apague cache ou saves.

O `session.json` registra `barrier_merge` e `barrier_trace` em `gpu_diagnostics`
quando houver escolha explícita. O log do supervisor também lista os overrides.
A seleção é capturada para cada lançamento e preservada na transferência de
`eboot.bin` para `game.elf`.

Se um pipeline preparado for rejeitado pelo guard e diagnóstico estiver ligado,
o log informa `RX580 formats: rejected prepared pipeline`. São no máximo 16
mensagens por processo. Isso comprova que a condição ocorreu; não demonstra,
isoladamente, que ela causava a deformação da imagem.

Com diagnóstico ligado, `GPU barriers: generation` registra quantas barreiras
foram geradas e quantas restaram após a junção. São registrados até 16 grupos
de pelo menos 256 barreiras geradas. `GPU barriers: batch` marca início/fim de
`EndRendering` e da gravação de `pipelineBarrier2`, em até 16 lotes que ainda
tenham pelo menos 256 barreiras após a junção. Um lote reduzido abaixo desse
limiar aparece apenas no registro de geração. Os limites são independentes,
reiniciam por processo e podem ser atingidos antes do crash. Não ver uma marca
de fim não comprova, isoladamente, que aquela chamada causou a falha; os logs
podem intercalar mensagens de threads diferentes. A marca de fim confirma o
retorno da chamada que grava comandos Vulkan, não a conclusão da execução GPU.

O launcher encaminha as chaves estritas `barrier_merge=auto/0/1` e
`barrier_trace=auto/0/1` pelo mecanismo existente de configuração. Variáveis
`TLG_` herdadas são filtradas pelo supervisor; definir apenas uma variável no
PowerShell não ativa o teste. O runtime requer exatamente `1` para ativar.

## Limites e validação

A integração preserva o caminho original quando a junção está desligada,
os estados por subrecurso, a memoização de acessos parciais e o rastreamento
de barreiras do runtime. Não importa os diagnósticos `EXPLICIT_RANGE` e
`BARRIER_SPLIT` nem renderer v2, LOD ou ajustes de câmera do inFAMOUS.
O cache BP08 e `pipeline_binaries` são mecanismos distintos; este preset não
altera nenhum deles, permitindo comparação isolada da junção.

Os testes sintéticos verificam equivalência das transições, prefixo preservado,
intervalos descontínuos, dependências distintas, valores de ativação e limite
de logs. Testes de configuração/UI verificam persistência, idiomas, ambiente
filtrado e metadados de sessão. Esses testes e a compilação não reproduzem
o crash nem substituem o teste na RX 580 do relator.
