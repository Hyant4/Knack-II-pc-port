# Renderer Linux Mint sem guest — 29/10/2026

**Inicialização completa do renderer aprovada na RX 6650 XT/RADV**, seguida de
três segundos em repouso e fechamento pelo evento SDL. Saída 0, dentro do
limite externo de 30 segundos. Nenhum jogo foi carregado ou executado.

Esta etapa continua [o build Mint atual](BUILD_LINUX_MINT_20261029.md). A prova
anterior criava apenas o dispositivo Vulkan; este teste chamou `tlggpu_init`
e percorreu os construtores reais de janela X11, dispositivo, schedulers,
swapchain, presenter, rasterizer, caches, pipeline cache, overlay e VideoOut.
Liverpool e os workers do renderer foram iniciados sem comandos guest.

## Teste e isolamento

Foi criado um pequeno main C na área externa:

```text
/media/mint/0C3CE8FC3CE8E226/knack-linux-mint-20261028/renderer-validation-20261029
```

O link reutilizou todos os objetos reais do target CMake `runtime-test`,
substituindo somente o objeto do main de testes pelo main de diagnóstico.
Runtime e biblioteca GPU permanecem inalterados. Não foram usados stubs de
memória que fingem sucesso; registro de hooks e serviços usam o runtime real.
O único adaptador externo é `runtime_restart`, que aborta se for chamado,
conforme o contrato do harness nativo existente. Ele não foi exercitado.

O main inicializou runtime/kernel, chamou `tlggpu_init`, aguardou três segundos,
leu os contadores e enviou `SDL_EVENT_QUIT`. A thread real da janela executou
`runtime_report` e encerrou o processo com saída 0. Não houve encerramento pelo
watchdog. Esse caminho usa `_Exit` já existente no loop da janela; não comprova
uma API de desmontagem do renderer dentro de um processo que continua vivo.

Configuração exclusiva: janela 640x360, windowed, resolução de janela automática,
upscaler off, VSync FIFO. Dados, configuração, caches e cache Mesa foram dirigidos
para `session-01/` nova. ICD RADV escolhido somente no processo. Overrides de
outros testes não foram herdados. Core dumps desativados no processo do teste.
Nenhum driver, perfil, biblioteca do sistema, save real ou fonte foi alterado.

## Evidências

| Observação | Resultado |
| --- | --- |
| GPU selecionada | AMD Radeon RX 6650 XT (RADV NAVI23), discrete GPU |
| Janela | X11, Windowed |
| Swapchain | VSync on, FIFO |
| Buffer arenas | 512 MiB, driver buffer limit 4095 MiB |
| Imports GPU registrados | 327 símbolos |
| Frames submitted / shown / presented | 0 / 0 / 0 |
| Vblanks observados | 180 em três segundos |
| Falhas de página ativas / totais | 0 / 0 |
| Threads guest | zero criadas |
| Alocações/maps de memória guest | zero |
| Arquivos do runtime | zero abertos/lidos/escritos |
| Áudio | zero portas abertas e buffers enviados |
| SaveData | zero mounts e escritas de memória |
| Encerramento | evento SDL, runtime report, saída 0 |

Vblanks são o contador de cadência do VideoOut neste teste; **não são FPS de
renderização**. Nenhuma imagem guest foi desenhada ou apresentada. O teste
valida inicialização e funcionamento ocioso dos componentes, sem certificar
rasterização de uma cena, shaders guest, readback do renderer, áudio ou controles.
O smoke independente anterior já confirmou submissão/readback básico na Radeon.

O log manteve avisos de extensões opcionais e formatos não integralmente
suportados. Também registrou uma mensagem de erro ao tentar abrir `profile.bin`
inexistente no cache novo. Ela não interrompeu a inicialização; o backend seguiu
com `Pipeline startup: on demand, no bulk warm-up`. Não foi criado um perfil
fictício nem removida a mensagem para esconder a ocorrência.

Arquivos de evidência na área externa: `renderer-session-01.log`,
`renderer-session-01-result.json`, `renderer-smoke.c`, `compile-command.json`,
`link-command.json`, baseline e verificação final. Os comandos de compilação/link
não reconstroem nem sobrescrevem os executáveis Windows ou o runtime Linux atual.

## Preservação e próximo gate

Branch `experiment/gpu-small-sync-prebind-20261020`, HEAD
`3d23fb71a48e290e0387d2d80e4c04bf6babdb17`. Baseline inicial: 369 entradas ao
considerar CRLF. Alterações desta etapa no checkout: este relatório novo e
seção no topo de `HANDOFF_ASTRA.md`; histórico anterior mantido. Nenhum stage,
commit, merge, push, instalação ou download nesta etapa.

O gate de inicialização completa sem guest está atendido. O próximo gate é
preparar e autorizar uma sessão real finita, com dump local validado, perfil
compatível, runtime atual e novos diretórios de saída/dados de usuário. Sugestão
de limite inicial: 60 segundos, sem saves reais. Isso é uma proposta para etapa
posterior, não autorização de boot. A aprovação WSL antiga não se aplica.
Boot, gameplay, FPS, áudio audível, controles, saves reais e campanha continuam
sem prova Mint. AppImage e publicação continuam posteriores à compatibilidade.
