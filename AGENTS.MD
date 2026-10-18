# AGENTS.md — Governança dos agentes e segurança do port Knack 2 para PC

> **Versão 2.2 — Governança com dois Tech Leads**  
> **Projeto:** Knack 2, CUSA08014, perfil 01.01  
> **Objetivo:** preservar os marcos já atingidos enquanto tarefas de menor risco são delegadas ao Gemini, mantendo o núcleo complexo com GPT-5.6 Sol, com dois Tech Leads independentes (ChatGPT GPT-6 e Argon no Gemini Chat), e GPT-6 Astra para escalonamentos excepcionais.
>
> **Escopo deste documento:** orientação operacional de agentes trabalhando neste repositório. Não substitui o pedido específico do usuário, nem concede autorização para operações destrutivas.

## 1. Contexto e fontes de verdade

O projeto executa os binários x86-64 originais do jogo para Orbis OS (`eboot.bin` e `game.elf`) por meio de runtime C/C++ e backend Vulkan derivado do ecossistema shadPS4. Não é uma reimplementação do jogo nem uma simples aplicação que usa assets extraídos.

Antes de qualquer implementação:

1. Ler este `AGENTS.MD`, `README.md` e as seções **mais recentes** de `HANDOFF_ASTRA.md` (o handoff contém histórico, inclusive problemas antigos já corrigidos).
2. Confirmar branch, HEAD, alterações locais e submódulos: `git branch --show-current`, `git rev-parse --short HEAD`, `git status --short`, `git submodule status`.
3. Examinar apenas os arquivos relacionados ao pedido e localizar testes já existentes.
4. Separar **comportamento comprovado em log/teste**, **relato do usuário**, **hipótese** e **pendência não testada**.
5. Registrar a situação inicial dos arquivos modificados para não confundir mudanças anteriores com alterações desta tarefa. Se ChatGPT e Argon apresentarem diretrizes conflitantes, suspender apenas a decisão controvertida e solicitar arbitragem do usuário.

Referência conhecida em 17/10/2026: `main` e `knack2-windows` apontavam para `8fac797` (marco documentado de combate/transição); essa referência é **histórica**, não substitui a verificação do HEAD atual.

### Estado técnico conhecido (não extrapolar)

- Houve boot, título, menus, carregamento e testes de gameplay no início de Chapter 1-1, incluindo combate e transição cinematográfica, conforme `HANDOFF_ASTRA.md`.
- As correções gráficas de transparência e descritores de vértices foram registradas no commit `44bf1e6`. Não as reverter ou “simplificar” sem evidência, testes e autorização explícita para uma tarefa do núcleo atribuída ao Sol (ou ao Astra em escalonamento excepcional).
- `runtime_ajm.c` **já contém** decodificação ATRAC9 por LibAtrac9 e MP3 por FFmpeg; `runtime_audio.c` **já contém** saída de áudio SDL3. Não propor implementá-las do zero como se estivessem ausentes.
- O relatório de ação encontrou portas AudioOut ativas e sem starvation nas amostras analisadas, **mas não confirmou áudio audível**, conteúdo PCM ou funcionamento efetivo dos jobs AJM.
- O resultado de análise de **61 imports ausentes em `game.elf`** não significa 61 bloqueios exercitados. Não criar stubs em massa, nem devolver sucesso fictício.
- FPS próximo de 60 em amostras não certifica estabilidade contínua, nem conclusão da campanha.

## 2. Responsáveis e regra de decisão

### GPT-5.6 Sol — proprietário principal do núcleo crítico

O **GPT-5.6 Sol** assume implementação e engenharia reversa complexas: kernel, ABI, memory mapping, relocations/linker, bootstrap, hooks/detours, sincronização entre threads, execução guest, GPU/Vulkan, shaders, cache de texturas/buffers, readbacks, NGS2, codecs e correções delicadas de áudio. **Ser proprietário não significa licença para editar tudo:** cada tarefa de núcleo precisa de objetivo e escopo autorizados pelo usuário, baseline Git, avaliação de risco, testes proporcionais e revisão do diff antes de integrar.

O Sol pode ler e, **apenas no escopo aprovado da tarefa**, modificar arquivos da zona bloqueada para Gemini. Não deve delegar uma alteração crítica ao Gemini nem permitir que um script auxiliar a realize indiretamente.

### GPT-6 Astra — escalonamento técnico excepcional

O **Astra** pode assumir investigações de risco particularmente alto, impasses técnicos e revisão independente de alterações críticas **quando o usuário solicitar ou autorizar o escalonamento**. Sua participação não é pré-requisito para cada tarefa do Sol.

### Gemini (Google Antigravity) — implementação de baixo risco

O **Gemini** realiza triagem de logs, automação, testes, documentação, launcher, UI auxiliar, diagnósticos não invasivos e **ajustes HLE pequenos e isolados** dentro da lista autorizada na seção 4.

### Usuário — direção e aprovação final

O usuário define objetivos, aceita riscos e aprova escopos de implementação crítica. Nenhum Tech Lead ou executor pode presumir autorização para editar arquivos bloqueados, realizar ações destrutivas, criar commits, fazer merges ou enviar pushes por conta própria.

### ChatGPT (GPT-6 neste chat) — Tech Lead de arquitetura e integração

Responsável por priorização, análise de evidências e riscos, revisão da arquitetura e dos contratos, formulação de tarefas e revisão dos resultados. Atua como **Tech Lead e revisor independente**, sem permissão automática para editar o núcleo.

### Argon (Gemini Chat) — Tech Lead de segunda revisão independente

Responsável por uma segunda análise de decisões arquiteturais, hipóteses, alternativas HLE/LLE, contratos de integração e testes. **Argon no Gemini Chat não é o Gemini executor no Antigravity**: são funções distintas, mesmo quando utilizam modelos da mesma família. Argon também não recebe direitos automáticos sobre arquivos protegidos.

### Protocolo de colaboração dos dois Tech Leads

1. **Autoridade:** o usuário aprova a decisão final. ChatGPT e Argon recomendam; Sol e Gemini executam somente tarefas com escopo atribuído e autorizado. Nenhuma análise de Tech Lead amplia as permissões de um agente.
2. **Independência:** em decisões de grande risco (kernel, ABI, memória, GPU/Vulkan, NGS2, threads, sincronização), é recomendado solicitar uma segunda revisão independente antes de uma grande implementação ou integração. Não exigir duas revisões para pequenos diagnósticos e tarefas de baixo risco.
3. **Uma especificação aprovada:** os dois Tech Leads não devem enviar instruções concorrentes e conflitantes aos executores. Havendo discordância, expor ao usuário as opções, evidências, riscos e impactos. Não iniciar uma implementação controvertida sem decisão do usuário.
4. **Comunicação manual:** ChatGPT, Gemini Chat e Antigravity não compartilham automaticamente conversas, arquivos locais, aprovações ou resultados. O usuário faz a transferência de prompts/relatórios; os revisores devem receber o mesmo contexto técnico e confirmar branch, HEAD e fontes.
5. **Fonte de verdade:** código, Git, logs, testes e os documentos atuais prevalecem sobre resumos de agentes. Registrar em `HANDOFF_ASTRA.md` decisões aceitas, trade-offs, evidências, autoria, pendências e divergências relevantes, sem apagar histórico.
6. **Fluxo usual:** diagnóstico/documentação do Gemini Antigravity → proposta do Sol para mudanças críticas → revisão de ChatGPT e/ou Argon quando útil → aprovação do usuário → implementação do Sol → testes → revisão dos resultados e handoff.
7. **Mudança de escopo:** alterar arquitetura aprovada, atingir novos arquivos críticos ou ampliar riscos exige nova autorização. Uma revisão técnica não substitui a aprovação humana.

**Critério decisivo:** classificar pelo **risco da alteração**, e não apenas pela palavra “áudio”, “fonte”, “kernel” ou “stub”. Se a correção exigir um arquivo protegido ou um contrato global, **Gemini para, documenta e encaminha ao Sol; o Sol pode solicitar escalonamento ao Astra com aprovação do usuário**. ChatGPT e Argon podem revisar independentemente a estratégia crítica; só uma proposta aprovada pelo usuário deve ser entregue como instrução ao executor. Não contornar a regra criando um segundo caminho, duplicando implementação ou escondendo alteração sob outro arquivo.

## 3. Zona bloqueada — READ-ONLY para Gemini; propriedade do Sol

**Proibido modificar, reformatar, renomear, mover, apagar, regenerar ou aplicar patches**, inclusive por scripts, formatadores, builds que reescrevam fontes ou commits automáticos. É permitido ler e citar trechos.

### 3.1 Núcleo de execução, memória, ABI e linker

- `port/src/probe.c`
- `port/src/runtime.c`, `port/src/runtime.h`
- `port/src/runtime_kernel.c`, `port/src/runtime_memory.c`, `port/src/runtime_thread.c`
- `port/src/runtime_mutex.c`, `port/src/runtime_rwlock.c`, `port/src/runtime_sema.c`, `port/src/runtime_freeze.c`
- `port/src/runtime_file.c`, `port/src/runtime_knack.c`, `port/src/runtime_tlg.c`
- `port/scripts/prepare.py`, `port/scripts/link_modules.py`, `port/scripts/isa_rewrite.py` e outros componentes de preparação/linking de imagens guest
- Qualquer `*.s`/`*.asm`, trampoline, wrapper ABI, hook, detour, patch de instrução ou endereço guest fixo, independentemente do caminho
- `profiles/CUSA08014-01.01.json` (versão, hashes e identidade do dump suportado)

### 3.2 GPU, Vulkan, shaders e dependências

- **Todo** `port/gpu/**`, inclusive `port/gpu/shadps4/**`, `port/gpu/shim/**`, `port/gpu/patches/**` e `port/gpu/CMakeLists.txt`
- `port/third_party/**`, incluindo a dependência/submódulo `LibAtrac9`
- `.gitmodules`, ponteiros de submódulos, dependências vendorizadas, locks de dependências e arquivos de licença

### 3.3 Módulos HLE ou de mídia de maior risco

- `port/src/runtime_ajm.c`, `port/src/runtime_mp3.c`, `port/src/runtime_videodec.c`
- `port/src/runtime_gr2_audio3d.c`, `port/src/runtime_gr2_font.c` e os demais `port/src/runtime_gr2_*.c`, salvo autorização específica
- `port/src/knack_import_names.inc` e tabelas NID/exports geradas ou compartilhadas
- Outros `port/src/runtime_*.c` **não explicitamente liberados** na seção 4

**Regra de precedência:** esta zona bloqueada prevalece sobre descrições genéricas como “HLE permitido”, “CMake permitido” ou “melhoria simples”. Um arquivo que não esteja explicitamente permitido deve ser tratado como **somente leitura**, até autorização expressa do usuário e atribuição a Sol ou Astra para aquela tarefa específica.

### Exceções

Somente **autorização explícita e delimitada do usuário para a tarefa concreta** pode ampliar o escopo do Gemini; a autorização deve identificar arquivos e tipo de mudança. Uma aprovação pontual não altera permanentemente estas regras. Mudanças de ABI, memória, GPU ou hooks pertencem ao Sol quando houver tarefa autorizada; podem ser escaladas ao Astra em caso de impasse ou risco excepcional. A aprovação de uma tarefa Sol nunca amplia o escopo do Gemini.

## 4. Zona permitida — Gemini, sob condições

**Permissão não equivale a refatoração livre.** Antes de editar, o Gemini deve confirmar que a alteração cabe no contrato atual, não afeta structs guest, não muda temporização/sincronização e possui teste/validação proporcionais.

| Caminho real / área | Permitido | Limites |
| --- | --- | --- |
| `docs/**`, `README.md`, `HANDOFF_ASTRA.md` | Documentação técnica e handoff | Preservar histórico; não reescrever evidências nem afirmar teste não executado |
| `tests/**`, `port/tests/**` | Testes novos e atualização de testes específicos | Usar dados sintéticos; não editar core para “fazer o teste passar” |
| `scripts/**` (na raiz), novos scripts de diagnóstico em diretórios de ferramentas | Análise de logs, estatísticas, capturas e reprodução | Não manipular executáveis do jogo nem sobrescrever saves ou arquivos versionados |
| `port/launcher/**` | Launcher, interface e configuração isolada | Não modificar bootstrap, guest arguments críticos ou ABI |
| `port/src/runtime_pad.c` | Ajustes isolados de mapeamento de botões e dispositivos SDL | Não alterar ABI, layout guest, threads centrais ou timing sem escalação |
| `port/src/runtime_savedata.c`, `port/src/knack_savedata_backup.inc` | Correções locais de validação, erros e testes de SaveData | Não mudar layout/offsets, política global de filesystem, persistência sem teste; preservar backups |
| `port/src/runtime_content.c` | Ajustes específicos e comprovados de `sceAppContent` | Não alterar montagem global de paths ou memória guest |
| `port/src/runtime_audio.c` | Diagnóstico e, **após evidência**, ajuste estreito do backend SDL3 (ex.: mensagem de erro, seleção de dispositivo) | Proibido mudar pacing, buffering/cadência, sincronização, ABI, formato PCM ou lógica de mix sem Sol (ou Astra em escalonamento autorizado) |
| `port/src/runtime_services.c` | Apenas correções periféricas pequenas e bem delimitadas de serviços/troféus | Não mexer em kernel, callbacks, ABI, estado global ou fluxo de boot; encaminhar se houver dúvida |
| `run.py`, `port/run.py` | Somente opções explícitas de observabilidade, documentação de uso e testes dessas opções | Não alterar preparação/linking, ambiente crítico, caminho de saves nem sequência LoadExec |
| `AGENTS.MD` | **Leitura apenas** | Só editar com pedido explícito do usuário para alterar as regras |

**Integração que exija editar `port/src/runtime.c`, `runtime.h`, `probe.c`, tabela global de exports ou arquivo protegido:** entregar implementação isolada/contrato e solicitar ao Sol a conexão; o Astra atua somente quando houver escalonamento autorizado. Não modificar arquivo protegido “só uma linha”.

**Build:** arquivos `CMakeLists.txt` e `build.ps1` são somente leitura por padrão. Para adicionar teste/arquivo ao build, pedir escopo específico; não generalizar permissão de CMake. É aceitável executar comandos de build documentados, desde que preservadas as mudanças locais.

## 5. Regras obrigatórias de engenharia

1. **Preservar ABI e binários:** não alterar `sysv_abi`, `ms_abi`, `ABI`, `volatile`, `alignas`, `#pragma pack`, offsets, tamanho e ordem de `Sce*`, tipos de retorno ou convenções de chamada.
2. **Sem stubs de sucesso indiscriminados:** não silenciar `STOP`, assertions, erros de API ou validação; implementar só contratos compreendidos e exercitados, com evidência.
3. **Mudanças cirúrgicas:** sem refatoração cosmética, alteração massiva de formatação, renomeação geral ou atualização oportunista de bibliotecas.
4. **Sem duplicar soluções:** verificar se funcionalidade já existe (especialmente ATRAC9, AudioOut, FreeType, SaveData e correções Vulkan) antes de propor implementação.
5. **Isolamento e causalidade:** formular hipótese, reproduzir, coletar prova, alterar a menor superfície possível e comparar antes/depois.
6. **Proteção de dados:** não publicar `eboot.bin`, `game.elf`, dumps, saves, capturas com dados privados, logs contendo caminhos locais sensíveis nem assets proprietários.
7. **Sem trabalho não solicitado:** não instalar dependências pela rede, atualizar submódulos, migrar build, iniciar testes longos, consumir serviços externos ou modificar outras branches sem pedido/aprovação.
8. **Não destruir estado:** nunca executar `git reset --hard`, `git clean -fd`, `git checkout -- .`, `git restore` em mudanças preexistentes, `git push --force` ou sobrescrever `out/`/save sem autorização específica.
9. **Git sob controle:** por padrão não criar commits, não realizar merges e não fazer push. Se autorizado, usar branch indicada, alterações focadas e reportar SHA. Não editar `main` implicitamente.
10. **Não afirmar o que não foi testado:** “build passou”, “sem som”, “60 FPS estáveis”, “cutscene funcional” e “compatível” exigem evidência correspondente. Timeout manual não é crash espontâneo.

## 6. Procedimento antes, durante e depois da tarefa

### 6.1 Antes

- Ler o pedido atual, `README.md`, `AGENTS.MD` e o **topo recente** de `HANDOFF_ASTRA.md`.
- Executar `git status --short`, `git branch --show-current`, `git rev-parse --short HEAD` e `git submodule status`; registrar arquivos já sujos.
- Definir em uma frase **o objetivo verificável**, **arquivos que pretende tocar**, **riscos** e **teste mínimo**.
- **Gemini:** se arquivos protegidos forem necessários, **não editar**; preparar diagnóstico para o Sol.
- **Sol:** antes de editar núcleo protegido, verificar autorização concreta, escopo, branch, risco de regressão e testes; sem autorização, apresentar plano e aguardar. O Astra só assume por escalonamento autorizado.

### 6.2 Durante

- Inspecionar código antes de editar. Preferir testes que não exigem jogo, GPU ou download.
- Trabalhar só nos arquivos autorizados; usar logs e instrumentação existente antes de inventar novos hooks.
- Ao envolver execução do jogo, usar dump local do próprio usuário, timeouts finitos e diretório de saída isolado; não dirigir longas sessões automáticas sem aprovação.
- Testes que modificam saves reais devem ser evitados; preferir harness com dados sintéticos e saída temporária.
- Se resultado contradisser a hipótese, parar e relatar, em vez de empilhar patches experimentais.

### 6.3 Depois

- Executar testes focados apropriados e `git diff --check`.
- Executar `git status --short`, `git diff --name-only` e `git diff --stat`; comparar com o estado inicial. Inspecionar também arquivos não rastreados e submódulos.
- Confirmar ausência de alterações em arquivos bloqueados e nenhuma modificação inesperada em `.vscode/`, patches ou saves.
- Não apagar mudanças preexistentes para “limpar” o status. Não declarar sucesso se build/teste não foi executado ou falhou.
- Registrar resultado e, havendo mudança relevante ou escalonamento, atualizar `HANDOFF_ASTRA.md` **sem sobrescrever o histórico**.

### Testes documentados (selecionar, não executar tudo automaticamente)

Na raiz do projeto, com toolchain existente:

```powershell
.\.venv\Scripts\python.exe -m unittest discover -s tests -v
.\.venv\Scripts\python.exe -m unittest discover -s port/tests -p test_prepare.py -v
.\.venv\Scripts\python.exe -m unittest discover -s port/tests -p test_link_modules.py -v
.\.venv\Scripts\python.exe -m unittest discover -s port/tests -p test_link_libc.py -v
```

Para alterações que realmente exijam compilação completa, o comando documentado é `./build.ps1 -Jobs 6` em PowerShell. Antes, confirmar dependências, custo e necessidade. Testes de GPU requerem ambiente apropriado e pertencem, em regra, ao fluxo do Sol; casos excepcionalmente difíceis podem ser escalados ao Astra.

## 7. Investigação de áudio — protocolo específico

**Status inicial:** `port/src/runtime_ajm.c` implementa ATRAC9 por `LibAtrac9`, MP3 via FFmpeg; `port/src/runtime_audio.c` produz saída SDL3. Há relatos de ausência de som, mas o handoff **não comprova** que o PCM produzido é silencioso, nem que o AJM não funciona.

**Gemini pode:**

1. Inspecionar o código e logs, identificar inicialização de áudio, portas, erros SDL e jobs AJM; **não editar decoder**.
2. Usar os diagnósticos já presentes: `TLG_AUDIO_TRACE`, `TLG_AUDIO_STATS`, `TLG_AUDIO_DUMP` e relatórios de `runtime_report`. Manter dumps locais, de tamanho controlado e fora do Git.
3. Lembrar que `run.py` filtra variáveis de ambiente herdadas com prefixo `TLG_`. Habilitar diagnósticos por opção de execução explicitamente aprovada ou mecanismo temporário **não versionado**; jamais afirmar que um `$env:TLG_*` isolado chegará ao guest.
4. Diferenciar: (a) dispositivo SDL não abriu; (b) SDL abriu, mas PCM entregue é silencioso; (c) PCM não silencioso não toca; (d) jobs AJM não foram exercitados; (e) codec/batch falha de forma comprovada.
5. Gerar um relatório com excertos de logs, caminhos de evidência, condição de reprodução, medidas PCM relevantes (sem publicar áudio do jogo) e proposta limitada.

**Sol assume**, mediante tarefa autorizada, se for necessário alterar `runtime_tlg.c` (NGS2), `runtime_ajm.c`, batches/ATRAC9, sincronização ou tempo de playback, contratos ABI, threads do motor, hooks, gerenciamento de memória ou código protegido. A escolha entre LLE e HLE exige diagnóstico de imports, dependências, formatos e testes; a presença de `TLG_NGS2=native` não prova integração LLE funcional. **Astra** fica reservado para revisão ou bloqueios excepcionais autorizados.

Não desabilitar processamento, forçar retorno de sucesso ou gerar silêncio artificial para mascarar o problema.

## 8. Escalonamento: Gemini → Sol → Astra (quando autorizado)

**Gemini deve parar e encaminhar ao Sol** antes de editar se ocorrer qualquer uma destas situações. **Sol** deve avaliar o diagnóstico e só implementar a correção com autorização e escopo próprios; se o problema for excepcionalmente difícil ou permanecer sem causa isolada, recomendar escalonamento ao **Astra** com aprovação do usuário:



- Segfault, crash em trecho guest, corrupção de memória, race/deadlock, bloqueio de thread, comportamento ABI ambíguo ou necessidade de hook.
- Vulkan validation error, shader incorreto, renderização quebrada, dispositivo perdido, buffer-cache, readback ou sincronização GPU.
- Mudança de estrutura binária, tabela global NID/import/export, linker/loader/relocations ou tratamento de instrução.
- Codec/decoder e sincronismo de áudio que dependam do caminho crítico ou de APIs do PS4 ainda não compreendidas.
- Teste sem causa isolada, necessidade de desligar asserções, resultados contraditórios, risco de corromper save ou de regredir um marco funcional.

**Formato de encaminhamento:**

```markdown
### Handoff ao Sol (ou Astra, se escalado) — [título curto] — [data]
- Objetivo e sintoma observado:
- Branch, HEAD e estado Git preexistente:
- Ambiente e comando mínimo de reprodução:
- Resultado esperado versus resultado observado:
- Logs relevantes (arquivo e linha/timestamp):
- Hipóteses com evidência; hipóteses descartadas:
- Arquivos/funções implicados (somente leitura):
- Mudanças feitas fora da zona bloqueada (se houver):
- Testes executados, resultados e limitações:
- Decisão solicitada ao Sol (ou Astra, se escalado):
```

O Gemini não deve executar a alteração protegida depois de preparar o handoff. O Sol (ou Astra, se escalado) só deve executá-la dentro do escopo autorizado pelo usuário.

## 9. Atualização de HANDOFF_ASTRA.md

Adicionar seção **nova no topo** com data, título e agente; manter o histórico anterior intacto. Incluir:

- Objetivo, escopo autorizado, branch e commit inicial.
- Arquivos exatamente criados/modificados e interfaces novas (ou “nenhuma”).
- Evidências dos testes: comandos, resultados, logs e imagens locais, distinguindo validação visual, automática e relato.
- Anomalias e limitações, inclusive o que **não** foi validado.
- Pendências para Gemini versus pendências reservadas ao Sol; registrar separadamente escalonamentos excepcionais ao Astra e, quando relevante, recomendações divergentes de ChatGPT/Argon e decisão expressa do usuário.
- Eventuais mudanças já existentes em `.vscode/`, `fsr-vulkan` ou outras partes do worktree que foram preservadas.

Não usar o handoff para atribuir sucesso a uma sessão sem inspeção correspondente. Não incorporar dados proprietários ou grandes dumps ao Git.

## 10. Critérios de conclusão por responsável

### 10.1 Tarefa Gemini

Uma tarefa só está concluída quando todos estes pontos forem satisfeitos:

- [ ] O escopo era autorizado e nenhum arquivo bloqueado foi modificado.
- [ ] As alterações são mínimas, explicadas e localizadas.
- [ ] Os testes apropriados passaram, **ou** limitações/falhas foram reportadas sem ocultação.
- [ ] O diff foi revisado e não há alteração acidental de submódulo, save, profile, recurso do jogo ou configuração local.
- [ ] O comportamento observado foi descrito com evidência; hipóteses permanecem identificadas como hipóteses.
- [ ] Handoff atualizado quando necessário; próximos responsáveis definidos.
- [ ] Nenhum commit, merge ou push foi feito sem autorização.

### 10.2 Tarefa GPT-5.6 Sol (núcleo)

- [ ] O agente responsável foi explicitamente designado como Sol e o escopo crítico foi autorizado pelo usuário; uma alteração em arquivo protegido não é presumida a partir do nome da sessão.
- [ ] Baseline de Git, branch, submódulos, testes e evidência do problema foram registrados antes da edição.
- [ ] Contratos ABI, structs guest, ownership, concorrência, timing e possíveis regressões foram revisados.
- [ ] A solução altera somente a menor superfície necessária e possui testes focados com resultados documentados; nenhuma função foi silenciada para mascarar falhas.
- [ ] O diff integral foi inspecionado; scripts auxiliares, arquivos não rastreados, saves e patches existentes foram preservados.
- [ ] O marco funcional anterior foi reavaliado na medida necessária; limitações não testadas estão explícitas.
- [ ] O `HANDOFF_ASTRA.md` foi atualizado no topo com autoria **GPT-5.6 Sol**, evidências e tarefas para Gemini ou Astra, se houver.
- [ ] Nenhum commit, merge, push ou modificação de outra branch foi feito sem autorização específica.

### 10.3 Uso excepcional do GPT-6 Astra

O Astra recebe um handoff com hipóteses, diffs, logs e tentativas já verificadas. O escopo e a autorização para alterações devem ser explícitos, tal como no fluxo do Sol; sua participação não libera arquivos protegidos ao Gemini.

**Regra final:** preservar o último estado comprovadamente funcional é mais importante do que resolver rapidamente um problema isolado. Diante da dúvida, **ler, medir, documentar e escalar** — o Gemini não modifica o núcleo; o Sol só o modifica em tarefa expressamente autorizada.
