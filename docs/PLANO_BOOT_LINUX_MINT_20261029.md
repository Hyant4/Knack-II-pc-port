# Proposta de boot Linux Mint — 29/10/2026

**Preparado, aguardando autorização específica; jogo não iniciado.**

Após os gates de build, CPU, testes sintéticos, Vulkan e inicialização completa
do renderer, esta etapa localizou o dump real, executou as validações existentes
e preparou imagens em saída isolada. Não houve execução de guest, alteração de
perfil, core, driver, executável Windows, preferências ou saves reais.

## Dump e validação

A configuração atual do launcher Windows aponta para uma pasta de release com
validação inválida; ela foi preservada. Caminhos históricos de extrações e da
edição europeia também não existem mais. A inspeção limitada das pastas de jogo
encontrou a instalação americana atual. O caminho exato fica apenas no plano
local da área externa, sem ser incorporado a este relatório.

- Perfil selecionado automaticamente por SFO: **CUSA08014 01.01**.
- Layout validado: `elf-80ccc228`.
- `validate_knack2_dump` do launcher: aprovado.
- `validate_game` do supervisor: aprovado.
- SHA-256 de eboot/game, identidade SFO e parsing dos módulos obrigatórios:
  conferidos pelas implementações atuais, sem flexibilizar hashes/guardas.
- Inventário: 4.775 arquivos, 52.155.874.937 bytes.
- Idiomas encontrados: pt-BR, en-US, es-419 e fr-FR.

Isso não certifica a integridade de cada asset do dump ou sua origem. Os
validadores comprovam os contratos de identidade, executáveis e módulos que
implementam. Nenhuma cópia do jogo foi baixada, enviada ou extraída nesta etapa.

## Preparação já executada, sem jogo

O supervisor autoritativo rodou com `--analyze`, saída 0. Preparou e ligou os
dois estágios em `boot-preparation-20261029/output/`, fora do repositório.
`libc _init_env` foi verificado como RET pelos scripts existentes. Não foi
executado ISA rewriting dinâmico, `--check-imports`, eboot ou game.elf.

A análise registrou 115 imports do eboot e 617 do game deixados para o runtime
host. Isso não significa esse número de imports ausentes ou bloqueios de boot;
nenhum stub foi adicionado. Também registrou metadata ELF não carregável
indisponível; a saída não se apresenta como reconstrução byte a byte do ELF.
Essas mensagens foram mantidas no log privado.

## Sessão proposta

| Item | Valor |
| --- | --- |
| Objetivo | observar inicialização/LoadExec e eventual título; sem gameplay automatizado |
| Runtime | tlg-probe Linux atual, conferido por SHA-256 |
| GPU | RX 6650 XT, ICD RADV restrito ao processo |
| Ambiente | Mint/X11, um jogador |
| Idioma | português brasileiro |
| Janela / resolução interna | 1280x720 / 1280x720 |
| Proporção / VSync | 16:9 / ligado |
| AA / upscaler temporal | nativo / desligado |
| Precisão GPU / eager readback | gpu_readbacks=2 / desligado |
| Preload de shaders | desligado, cache novo; primeiro uso pode compilar shaders |
| Áudio | perfil de opt-ins já usado pelo launcher, sem mudanças no decoder/mixer |
| Duração de execução guest | **até 60 segundos**, compartilhados entre estágios/reinícios |
| Watchdog externo | 180 segundos para o comando inteiro, com cinco segundos de graça para kill |
| Saves / preferências | diretórios novos, sem copiar dados habituais |

O watchdog externo inclui preparação/ISA rewriting e limpeza; não amplia os
60 segundos de execução guest controlados pelo supervisor. Um retorno 124 por
prazo não será descrito como crash. Se a compilação inicial de shaders consumir
a sessão, isso será registrado sem inferir desempenho ou estender o prazo.

A inspeção visual, áudio audível, controles e medição de FPS serão reportados
separadamente; aprovação de boot não é certificação dessas funções.

## Plano revisável e controles

Área persistente externa:

```text
/media/mint/0C3CE8FC3CE8E226/knack-linux-mint-20261028/boot-preparation-20261029
```

- `boot-plan.json`: dump, comando exato, parâmetros e hashes protegidos.
- `boot.ini`: configuração exclusiva, validada pelas funções existentes.
- `dump-validation.json`: resultados, inventário e hashes dos arquivos necessários.
- `analysis.log` / `analysis-result.json`: preparação estática já concluída.
- `launch-boot.py`: comando preparado, **não executado com --execute**.
- `launch-dry-review.log`: execução sem argumento que apenas descreve o plano.

Sem argumento, o script não inicia processos do jogo. Após autorização específica,
`--execute` revalida hashes dos artefatos, scripts e arquivos necessários do dump,
rejeita reutilização do perfil de usuário/sessões e chama o supervisor com
`--runtime` explícito e `--timeout 60`. O comando externo usa o timeout padrão
com grupo de processos para conter seus próprios descendentes. O ambiente é
limitado às variáveis host necessárias; caches e seleção de driver são locais
ao teste. Não foram adicionados overrides de sincronização, BP ou shader.

O script ainda não foi exercitado numa sessão real. Seu ramo de lançamento
permanece pendente de autorização. O dump será validado novamente pelo supervisor
antes de qualquer execução. A pasta `test-user` ainda não existe.

## Preservação e decisão pendente

Branch/HEAD: `experiment/gpu-small-sync-prebind-20261020`,
`3d23fb71a48e290e0387d2d80e4c04bf6babdb17`. Baseline, índice completo e revisões
de submódulos registrados antes da tarefa. Alterações no checkout: este relatório
novo e seção no handoff; código e perfis do port preservados. Artefatos privados
ficam na área externa. Nenhum commit, stage, merge, push ou publicação.

O [documento de retomada](CONTINUAR_LINUX_MINT.md#8-sequência-de-implementação-e-validação)
pede: “obter autorização específica para sessão finita”. A aprovação antiga WSL
já foi utilizada. A decisão solicitada é autorizar **uma sessão de boot de até
60 segundos nas condições acima**, sem reaproveitar saves reais. Até essa
confirmação, não executar o ramo de lançamento.
