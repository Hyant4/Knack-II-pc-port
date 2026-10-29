# AA desligado em 4K — gate de recursos — 27/10/2026

## Resultado após reinício — A/B 4K aprovado no trecho testado

Após o usuário reiniciar Windows e avisar, a leitura de WMI confirmou o
pagefile ativo com 16384 MiB, configuração inicial/máxima 16384/32768 MiB
e limite efetivo de compromisso 47,8728 GiB. A capacidade adicional passou
a estar disponível. Nenhuma configuração adicional do Windows foi alterada.

Foram executadas duas sessões isoladas de até 60 segundos, incluindo abertura
e navegação automática pelos menus até o checkpoint de Chapter 2-4. O runtime
experimental preservado `5bb51687...`, o dump US 01.01 e as opções do gate
anterior foram mantidos: 3840×2160 internos, FSR temporal/FSR1/RCAS desligados,
readbacks 2, `TLG_DMEM_MB=6592` e argumento `-2160p`. Os dados do usuário foram
copiados para cada sessão; saves habituais não foram usados para gravação.

| Evidência | AA desligado | AA nativo |
| --- | --- | --- |
| Sessão | `session-20261027-015754` | `session-20261027-015916` |
| Census de gameplay | 4 | 12 |
| Observações de shader/câmera | 40 | 120 |
| Maior jitter observado, em pixels | 0 | 0,46875072 |
| Dispatches do TAA original nos census | 0 | 12 |
| Pico de compromisso do sistema | 29,3237 GiB | 29,6897 GiB |
| Menor margem de compromisso | 18,5491 GiB | 18,1831 GiB |
| Saída do guest | 124, timeout previsto | 124, timeout previsto |

Os dois modos produziram framebuffer final 3840×2160 com exatamente
33.177.600 bytes por captura. Os census registraram alvos principais
3840×2160 e efeitos a meia resolução 1920×1080. Com AA desligado, o log
confirmou `applied=off render=3840x2160 destinations=2 history=0` até o marcador
de 1200 dispatches substituídos. O controle nativo não instalou esse patch.
Não houve erro de alocação, assertion ou disparo do watchdog de memória.

Capturas finais de ambos os modos foram decodificadas como RGB30 e inspecionadas:
cena 3D e personagem renderizados, sem tela preta ou corrupção grosseira de cor.
Um par de census tem matriz de vista igual nos valores arredondados registrados;
as fases das animações não foram sincronizadas. A comparação não quantifica
redução de borrão, desempenho ou tremulação durante movimento.

As sessões de 60 segundos incluem abertura e carregamento; não representam
60 segundos contínuos de gameplay. Esse resultado confirma o desligamento
de TAA/jitter em US 01.01/4K no trecho observado. Estabilidade prolongada,
campanha completa, QHD, EU, US 01.00 e outros layouts continuam sem validação
específica desta feature. O resultado não atribui retrospectivamente toda
falha Vulkan anterior à paginação.

Evidências privadas em
`C:/Users/dartv/Documents/Codex/Knack-AA-4K-postboot-20261027/`:
`verified-4k-summary.json`, `feature-probe-summary.json`, `camera-audit.json`
por modo, `memory.jsonl`, logs, capturas e `comparison-overview.png`.
Comandos: `run_probe.py off`, `run_probe.py native`, `verify_artifacts.py` e
`render_comparison.py`, com o Python da `.venv` existente.
Não houve alteração do produto, instalação ou promoção da feature para release.

## Atualização — paginação configurada, reinício pendente

Após revisar a proposta, o usuário autorizou explicitamente o ajuste de
paginação inicial 16 GiB e máxima 32 GiB em C:. O script foi executado com
elevação e os valores foram conferidos por WMI e registro:
`InitialSize=16384`, `MaximumSize=32768`,
`PagingFiles = c:\pagefile.sys 16384 32768`.
`AutomaticManagedPagefile` permaneceu falso; não houve alteração nessa política.

O estado anterior foi salvo em `pagefile-backup.json`, dentro da pasta privada
de evidências. Ele registra InitialSize/MaximumSize 0 e a política automática
anterior. O script oferece rollback restrito a esse estado se os valores atuais
ainda corresponderem aos desta aplicação. Rollback não foi executado.

A primeira tentativa parou antes de qualquer gravação, por seleção incorreta
de overload Int32 em `Math.Max` no cálculo do espaço adicional. O cálculo foi
corrigido para Int64 explícito e conferido com o valor de 11.963.203.584 bytes.
A segunda execução elevada terminou com saída 0 e
`ChangesApplied=true`. Logs de ambas as tentativas foram preservados.

A alocação efetiva ainda é 4975 MiB e o limite de compromisso permanece
36,7312 GiB. Portanto, a configuração foi salva, mas o aumento de capacidade
ainda depende do próximo reinício controlado pelo usuário. Nenhum reinício
foi executado ou autorizado; o teste 4K não foi repetido nessa condição.
O próximo passo é reiniciar Windows e reconferir o tamanho ativo e o limite
antes de retomar o A/B 4K.

Evidências privadas adicionais: `pagefile-application-result.json`,
`pagefile-verified-state.json`, `pagefile-backup.json`,
`pagefile-apply-output.log` e `pagefile-apply-attempt1-error.log`.
A configuração autorizada do Windows foi a única mudança operacional desta
etapa; fontes do produto, instalação do port e saves habituais permanecem intactos.

## Resultado desta etapa

O usuário pediu continuar após a confirmação do AA desligado em gameplay
US 01.01/1080p. Foi iniciada a validação em 4K verdadeiro, mantendo a janela
independente da resolução interna. O primeiro teste foi interrompido pelo
monitor de proteção da sessão: a margem de memória comprometida do Windows
caiu abaixo de 512 MiB.

Isso não é um crash espontâneo do jogo nem uma falha comprovada da feature.
Também não é confirmação de funcionamento em 4K: não foi obtida captura de
gameplay nessa resolução. A validação em 1080p permanece a registrada no
[diagnóstico anterior](DIAGNOSTICO_ALOCACAO_GPU.md).

## Reprodução e evidência

- Branch/HEAD: `experiment/gpu-small-sync-prebind-20261020`, `3d23fb7`.
- Runtime experimental preservado: SHA-256
  `5bb51687a95201615f8a9347cf7491088c1e641822d5d7f775b7c8e3f57cd697`.
- Dump: CUSA08014/01.01, `game.elf` SHA-256
  `80ccc22819fa4eeb5e1824e585f3ac5d5168e9ebc82f629e44cdb63fd573fb20`.
- Preferências: `internal_resolution=3840x2160`, `antialiasing=off`,
  `window_resolution=1920x1080`, FSR temporal/FSR1/RCAS desligados,
  readbacks precisos 2 e pré-carregamento síncrono. O supervisor selecionou
  `TLG_DMEM_MB=6592` e passou `-2160p` ao `game.elf` pelo caminho já existente.
- O log confirmou resolução nativa solicitada, assinaturas aceitas, reserva
  adicional de 1536 MiB no pool e patch de jitter instalado. Esses marcadores
  não substituem captura do framebuffer final.
- Sessão: `probe-off-3840x2160/out/session-20261027-010252`.
- Saída 1: encerramento do processo de teste pelo watchdog, não timeout nem
  assertion do renderer. O monitor marcou
  `safety_abort=system_commit_margin_below_512_MiB` antes de encerrar apenas
  processos ativos pertencentes ao binário dessa sessão.

Na amostra que disparou a interrupção:

| Medida | Valor |
| --- | --- |
| Compromisso total do sistema | 36,3604 GiB |
| Limite de compromisso | 36,7312 GiB |
| Margem restante | 0,3708 GiB, aproximadamente 380 MiB |
| RAM física disponível | 10,2345 GiB |
| Uso dedicado GPU amostrado do processo | aproximadamente 0,861 GiB |

As medidas mostram pouca margem de compromisso apesar da RAM física livre.
`CommitLimit` é o limite atual antes de expandir a paginação, conforme a
[documentação Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-performance_information).
O teste de controle com AA nativo não foi iniciado depois desse bloqueio.
Não se reduziu a resolução, o orçamento guest ou a precisão dos readbacks para
produzir um resultado artificialmente aprovado.

## Proposta revisável, ainda não aplicada

Foi preparado um ajuste apenas para `c:\pagefile.sys`:

- tamanho inicial: 16384 MiB, equivalente a 16 GiB;
- tamanho máximo: 32768 MiB, equivalente a 32 GiB;
- alocação atual observada: 4975 MiB; aumento inicial estimado de 11409 MiB,
  aproximadamente 11,14 GiB;
- espaço livre observado em C: aproximadamente 183,73 GiB.

Esses valores são uma proposta de margem baseada no consumo medido, não uma
garantia de conclusão do A/B. O objetivo é aumentar a capacidade de compromisso
sem alterar contratos de memória do port. A política global de gerenciamento
automático, atualmente falsa, não é alterada pelo script preparado.

O script privado `configure_pagefile.ps1` oferece ações `Inspect`, `Apply` e
`Rollback`. Foi executada somente `Inspect`, após análise de sintaxe sem erros.
`Apply` requer PowerShell elevado, compara o estado atual com o estado revisado
e registra os valores anteriores antes da mudança. `Rollback` restaura somente
esses valores se a configuração ainda corresponder à proposta aplicada. Não há
reinício automático, exclusão de pagefile, encerramento de outros aplicativos
ou sobrescrita do backup.

A mudança depende de autorização específica por afetar configuração global do
Windows. O tamanho efetivo pode permanecer inalterado até reiniciar o sistema;
as configurações e o uso atual são classes WMI distintas, como documentado em
[Win32_PageFileSetting](https://learn.microsoft.com/en-us/windows/win32/cimwin32prov/win32-pagefilesetting).
Não foi solicitado nem executado reinício nesta etapa.

Artefatos privados:
`C:/Users/dartv/Documents/Codex/Knack-AA-4K-20261027/`.
Incluem `run_probe.py`, `memory_monitor.py`, `pagefile-proposal.json`,
`configure_pagefile.ps1`, baseline dos fontes e telemetria da execução.
Código de produto, dumps, saves habituais, executáveis instalados, dependências
e mudanças preexistentes foram preservados. Nenhum commit, push, instalação ou
publicação foi realizado.
