# Alocação de buffers e validação do AA — 27/10/2026

## Resultado

A falha de alocação de 16 MiB registrada anteriormente não se repetiu hoje nas
três execuções monitoradas. Não foi aplicado patch no alocador, no runtime ou
no Windows. O A/B conseguiu chegar ao trecho 3D de Chapter 2-4, em US 01.01 e
1080p, confirmando o funcionamento básico do modo sem AA nesse cenário.

A pressão de memória comprometida do Windows é uma hipótese forte para os
bloqueios anteriores: existe registro explícito do erro Windows 1455, e a carga
atual se aproxima do limite de compromisso do sistema mesmo com RAM física
disponível. Não houve uma nova falha Vulkan acompanhada pelos contadores;
portanto, não se atribui definitivamente toda ocorrência anterior de
`ErrorOutOfDeviceMemory` ao mesmo mecanismo.

## Escopo e estado inicial

O usuário pediu iniciar a investigação do bloqueio de buffers. Trabalho limitado
a leitura do código, observabilidade externa, execuções curtas com dados de
teste copiados e documentação. Nenhuma alteração adicional no núcleo foi feita.
Branch e HEAD: `experiment/gpu-small-sync-prebind-20261020`, `3d23fb7`.
Alterações locais de outras tarefas, perfis regionais, dependências e instalação
foram preservados.

Os testes usaram o binário AA já preservado de 26/10, SHA-256
`5bb51687a95201615f8a9347cf7491088c1e641822d5d7f775b7c8e3f57cd697`,
e o dump local CUSA08014/01.01 com `game.elf` SHA-256
`80ccc22819fa4eeb5e1824e585f3ac5d5168e9ebc82f629e44cdb63fd573fb20`.
Isso valida esse build e esse layout; não certifica automaticamente mudanças
paralelas ou executáveis publicados sem o AA experimental.

## Inspeção do caminho de falha

- `BufferCache::EnsureResident()` tenta alocar todos os intervalos antes de
  registrar binds. Se uma reserva falha, desfaz a tentativa, pede liberação de
  texturas e aguarda operações pendentes antes de tentar novamente.
- `AllocateResidency()` normalmente usa chunks de 256 MiB para intervalos
  pequenos. Se o chunk não cabe, tenta somente o tamanho do intervalo, como os
  16 MiB mostrados nos logs. Esse número não representa consumo total de memória.
- `UniqueBuffer::Create()` já tenta novamente sem a restrição de orçamento do
  VMA quando recebe `VK_ERROR_OUT_OF_DEVICE_MEMORY`. Remover essa restrição
  novamente não acrescentaria o mecanismo de recuperação que já existe.
- O número `memory type` do erro desse helper é o enum interno `MemoryType`,
  não o índice Vulkan: 0 corresponde a DeviceLocal e 1 a HostUncached. Os logs
  anteriores incluem ambos, além de falhas de residência de buffers sparse.
- O grafo Graphify ajudou a localizar esses componentes; as conclusões acima
  foram verificadas nos fontes atuais, sem assumir como fato ligações inferidas
  automaticamente pelo grafo.

## Telemetria externa

Foi criado um monitor privado usando `GetPerformanceInfo`,
`GetProcessMemoryInfo` e contadores PDH de memória GPU por processo. Amostras
aproximadamente a cada 200 ms registram limite e uso comprometido do sistema,
RAM física, memória privada do processo e contadores GPU. Não houve injeção,
hook, alteração de driver ou instalação de dependências.

O controle inicial, sem captura de imagens ou entrada de controle, chegou ao
timeout de 45 segundos previsto pelo supervisor. A saída 124 foi parada
controlada, não crash espontâneo. Seu maior compromisso amostrado foi
34,9448 GiB, para limite de 36,7312 GiB, com margem mínima de 1,7864 GiB. Nesse
instante havia aproximadamente 9,63 GiB de RAM física disponível. O maior uso
dedicado GPU amostrado para o processo foi aproximadamente 1,40 GiB.

Configuração de paginação lida, sem alterações:

- RAM física: aproximadamente 31,87 GiB;
- pagefile atualmente alocado: 4975 MiB;
- `AutomaticManagedPagefile=false`;
- registro `PagingFiles = c:\pagefile.sys 0 0`, com InitialSize/MaximumSize 0;
- portanto, os cerca de 5 GB alocados no momento não foram tratados como um
  teto fixo de 5 GB. A descrição inicial de "manual de 5 GB" foi corrigida.

`FormatError(1455)` retornou que o arquivo de paginação é pequeno demais para
concluir a operação. `0x800705af`, registrado anteriormente ao criar PowerShell,
contém esse mesmo código Win32. Essa evidência confirma falha de compromisso
naquelas operações Windows; o erro Vulkan isolado não fornece essa distinção.

O limite medido por `PERFORMANCE_INFORMATION.CommitLimit` pode mudar quando a
paginação cresce; RAM disponível e compromisso disponível são medidas distintas.
Referência: [Microsoft — PERFORMANCE_INFORMATION](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-performance_information).
A Microsoft também documenta falhas de alocação relacionadas à demora no
crescimento automático da paginação, mas essa demora não foi demonstrada neste
teste: [Microsoft — falhas durante crescimento da paginação](https://learn.microsoft.com/en-us/troubleshoot/windows-client/performance/slow-page-file-growth-memory-allocation-errors).

## A/B de gameplay

Ambas as execuções usaram cópias independentes do mesmo perfil de teste, com
`internal_resolution=1920x1080`, FSR temporal, FSR1 e RCAS desligados, readbacks
precisos (`2`), mesmo binário e demais preferências. Somente `antialiasing`
mudou. Navegação pelo arquivo de entrada existente reconheceu aviso, título e
Continue nas imagens apresentadas; a interação ficou restrita à cópia de teste.

| Evidência | AA desligado | AA nativo |
| --- | --- | --- |
| Sessão | `session-20261027-004233` | `session-20261027-004520` |
| Saída do jogo | 124, timeout previsto de 60 s | 124, timeout previsto de 60 s |
| Falhas de alocação registradas | Nenhuma | Nenhuma |
| Census do trecho 3D | 12 | 13 |
| Observações distintas de shader/câmera por captura, somadas | 120 | 130 |
| Maior jitter de câmera registrado, em pixels | 0 | 0,46874976 |
| Dispatches do TAA nativo nos census | 0 | 13 |
| Maior compromisso do sistema amostrado | 35,6650 GiB | 35,5602 GiB |
| Menor margem de compromisso amostrada | 1,0662 GiB | 1,1710 GiB |
| Maior uso dedicado GPU do processo amostrado | 2,0490 GiB | 2,0489 GiB |

No modo sem AA, os logs registraram patch instalado e cópia dos dois destinos
até o contador de 1800 dispatches. Todos os valores de jitter das matrizes
examinadas foram zero, inclusive nas constantes 960×540 usadas por efeitos em
meia resolução. No modo nativo, o patch não foi aplicado e o passe original de
TAA continuou ativo, com jitter variável. A resolução principal continuou
1920×1080; o apresentador ajustou a janela ao monitor, independentemente dela.

O par representativo de imagens tem matrizes de vista iguais nos valores
registrados pelo census, com diferença máxima 0. Os dumps de imagem foram
associados por horário próximo (cerca de 0,3 s antes da gravação do census),
não por um identificador único de frame. A fase exata das animações não foi
igualada. Por isso, a comparação visual serve como inspeção da mesma vista,
não como medição isolada de ganho de nitidez por diferença de pixels.

As imagens foram inspecionadas. Sem AA, a vegetação e bordas finas aparecem
menos suavizadas, com mais serrilhado, como esperado para essa opção. Não foram
observados tela preta, corrupção grosseira de cores ou perda aparente da cena
nessas capturas. Não foi feita comparação byte a byte dos dois destinos HDR;
o caminho de cópia foi observado pela instrumentação e pela imagem resultante.

## Evidências e limites

Artefatos privados, fora do Git:
`C:/Users/dartv/Documents/Codex/Knack-buffer-investigation-20261027/`.
Arquivos principais: `memory_monitor.py`, `reproduce_memory.py`,
`run_feature_probe.py`, `analyze_probes.py`, `memory-summary.json`,
`feature-probe-summary.json` e `comparison-pair.json`. As pastas `control-native`,
`probe-off` e `probe-native` contêm logs, contadores, dados de usuário copiados
e capturas. `comparison-static-roi.png` apresenta um recorte sem redimensionar
os pixels da cena; imagens e dumps proprietários permanecem fora do repositório.

Conclusão limitada: funcionamento básico do desligamento do TAA e do jitter
confirmado no trecho testado, US 01.01/1080p. Nenhum patch adicional de memória
foi necessário para obter essa evidência hoje. A causa exata de cada falha
Vulkan anterior não foi isolada por uma reprodução instrumentada com erro.
Não foram validados campanha, combate prolongado, tremulação em movimentos
variados, EU/USA 01.00/outros layouts, QHD/4K ou desempenho. Não há validation
layers instalados. Nenhum ajuste de paginação, commit, push, publicação ou
substituição de executáveis instalados foi feito.
