# Knack II PC — launcher F3B

## Identidade e saves persistentes — 21/10/2026

Estado atual: [identidade e execução local](../../../../docs/KNACK_IDENTITY.md).
Ícone da aplicação usa a imagem original `sce_sys/icon0.png` do dump local,
convertida tecnicamente para ICO/BMP, sem arte gerada. Imagens originais ficam
fora do Git, em `%LOCALAPPDATA%\KnackII-PC\identity`.

O padrão de dados agora é `%LOCALAPPDATA%\KnackII-PC`, comum às versões do
launcher. P1 usa `profiles/single/user`; cooperativo usa `profiles/coop/user`.
O proprietário autorizou perfis fixos separados. Saídas cooperativas continuam
novas por execução, mas seus saves persistem. O supervisor recebe `--user-data`
explícito e mantém o perfil entre estágios LoadExec, com bloqueio de uso simultâneo.
As seções abaixo registram entregas anteriores; seus caminhos descartáveis de
dados e ícones vetoriais de janela foram substituídos por este contrato.

## F3B.2 — perfil de áudio recuperado e quatro idiomas — 21/10/2026

Entrega local em nova worktree, sem substituir o executável ou os dados F3B.1.
O pacote de áudio mais recente localizado nos Downloads foi
`knack2_B2F2C_zero_matrix_package.zip`; seu patch coincide com o arquivo local e
já está compilado no runtime COOP-05. Essa implementação foi reutilizada, sem
reaplicar patches ao Core e sem implementar um segundo mixer.

O INI de cada lançamento inclui o perfil histórico B2-F2C: codec e mixer NGS2
habilitados, diagnóstico habilitado, stable cohort B2-F2A desligado e bypass de
matriz zero ligado. O snapshot fica fora da saída P2 inicialmente vazia.
Esse preparo corrige a seleção do binário silencioso e a ausência dos opt-ins.
Som audível e comportamento no esgoto ainda exigem teste real autorizado.

O dump foi inspecionado somente leitura: quatro idiomas instalados em `data/lang`:
`pta`, `ena`, `esa`, `fre`. O seletor **Idioma do launcher e do jogo** oferece
português do Brasil, inglês, espanhol latino-americano e francês. A interface muda
imediatamente; o jogo recebe a seleção na próxima sessão. Salvar configurações
persiste a escolha. Após validar o dump, só os idiomas com assets instalados ficam
disponíveis. O botão Salvar permanece no rodapé, fora da área rolável.

O supervisor recebe `--language pt-BR|en-US|es-419|fr-FR`, valida a opção e os assets
antes de preparar/escrever a saída, e encaminha apenas o valor explícito em
`TLG_LANGUAGE` após o filtro de ambiente. LoadExec mantém o valor. Sem a opção,
a CLI continua usando o comportamento anterior (inglês padrão no runtime).
IDs SystemService: pt-BR=17, en-US=1, es-419=20, fr-FR=2; conferidos no enum público
OpenOrbis `_types/sys_service.h`. Não houve alteração em UserService ou SystemService.
Logs de execução e erros externos permanecem no idioma original para diagnóstico;
o seletor de pastas nativo segue o idioma do sistema operacional.

Runtime local: COOP-05, SHA-256
`3db2a9d1240a20e7de1815ddf0946c34277808543f1969c076b533239d252d65`.
O script externo `ABRIR_F3B2.ps1` verifica esse hash antes de abrir a interface.
**Proveniência:** esse binário veio da branch de áudio `21dadbb` com COOP, enquanto
os fontes C/C++ desta nova worktree ainda são os de main. Não recompilar essa árvore
como substituta do artefato: a integração dos fontes de áudio à main exige etapa
própria e escopo de Core autorizado. Não é uma entrega READY FOR COMMIT do áudio.

33 testes focados passaram: perfil INI, quatro locales, mudança da UI, persistência,
idiomas instalados, filtro, LoadExec, P2, isolamento, modo ilimitado e encerramento.
Nenhum jogo, compilação ou campanha NGS2/SDL extensa nesta etapa.

## F3B.1 — seleção simples e partidas sem limite — 21/10/2026

Jogadores e sessão apresenta somente a seleção de um ou dois jogadores. P1 continua
com ID 1; ao selecionar dois jogadores, a interface usa automaticamente P2=4099.
Conecte dois controles para jogar em cooperativo. Não há campos de ID ou duração.
Cada execução cooperativa continua recebendo uma pasta isolada nova.

O launcher passa explicitamente `--timeout 0` ao supervisor oficial. Zero significa
sem limite: `run.py` não cria um prazo e espera o filho com `timeout=None`; ambos os
estágios recebem `--timeout 0`, evitando o padrão próprio do executável. No Windows,
o probe já não instala watchdog; no caminho POSIX, zero desativa `alarm()`.
Nenhum fonte do runtime precisa mudar. Encerrar sessão e fechar a janela mantêm
o controle dos filhos e o recolhimento dos processos.

Chamadas CLI de diagnóstico mantêm o padrão de 180 segundos. Valores positivos
continuam limitando a sessão inteira; `--timeout 0` permite execução ilimitada
também pela CLI. Valores negativos são rejeitados antes de efeitos colaterais.
Validação de IDs na CLI permanece intacta. 27 verificações focadas passaram,
incluindo LoadExec ilimitado/finito, parada de filho Python sem prazo e UI simplificada.
Nenhum jogo foi iniciado.

## Visual inspirado em Knack — 21/10/2026

A interface inclui ilustração vetorial original do personagem, composta de relíquias
facetadas, crista âmbar e energia ciano. Ícones vetoriais locais acompanham navegação,
ações, configurações e logs. `Knack.svg` fornece o ícone da janela; não altera o
empacotamento nem os atalhos do launcher antigo. Não há downloads, assets extraídos,
fontes de ícones ou animação contínua. As ações e contratos F3B permanecem iguais.
O destaque adapta a altura em janelas menores, mantendo Jogar visível em 980×700.

Seis verificações focadas existentes passaram após o ajuste visual. As três telas,
o ícone SVG e os tamanhos 1180×810/980×700 foram renderizados pelo Qt e inspecionados.
Nenhum jogo foi iniciado nesta validação.

## Integração funcional isolada — 21/10/2026

O modo padrão agora usa o supervisor oficial `run.py`. Abrir a interface não
inicia o jogo: escolha a pasta raiz do seu dump, clique em **Validar** e depois
em **Jogar**. O validador F1 confere `param.sfo`, CUSA08014 / 01.01, SHA-256 dos
executáveis e módulos ELF. O supervisor repete a validação antes de preparar o dump.
Não há descoberta recursiva do disco nem inferência de versão pelo nome da pasta.

```powershell
python -B -m port.launcher.knack2 --real
# Área explícita, fora dos saves habituais:
python -B -m port.launcher.knack2 --real --data-dir 'C:\caminho\dados-f3b'
# F3A preservado, sem processos reais:
python -B -m port.launcher.knack2 --demo
```

Use o Python do projeto com PySide6 e dependências de preparação já instaladas.
O executável aprovado deve existir em `port/out/tlg-probe.exe`. Não é instalado
ou compilado automaticamente. O launcher antigo e seu empacotamento não mudam.

**Configurações:** janela, VSync, FPS, FSR1, RCAS e mouse usam o INI já lido pelo
runtime. **Salvar configurações** mantém preferências somente na área F3B.
Cada lançamento recebe um INI próprio em `launch-configs/UUID.ini`, passado por
`run.py --config`. Essa opção valida a existência do arquivo antes de efeitos
colaterais e preserva o filtro de ambiente e o caminho LoadExec. O INI fica fora
da saída P2 para manter a exigência COOP-04 de diretório inicialmente vazio.
Sem `--config`, a CLI conserva `OUT/knack2.ini` como padrão.

**Jogadores:** um jogador é o padrão em toda abertura da interface. Na tela
Configurações, selecione dois jogadores e conecte dois controles. A interface
passa P2=4099 automaticamente, diferente de P1=1. A opção de dois jogadores nunca
é restaurada das preferências ou habilitada pelo ambiente. Partidas iniciadas
pela interface não têm limite de duração; diagnósticos CLI podem usar prazo explícito.

**Dados:** a área padrão é `out/launcher-f3b` deste checkout. P1 usa `single/user`,
sem ler/copiar saves habituais. P2 usa `coop-UUID/user`, sempre novo a cada clique
em Jogar, sem reutilizar os saves da execução anterior. Saídas descartáveis são
preservadas após o encerramento para diagnóstico; não há limpeza automática.

**Sessão:** estados, executável atual, PID, código de saída, pasta de evidências e
logs ficam em Logs e diagnóstico. Os painéis separados mostram supervisor e jogo,
limitados a 500 linhas cada. O stdout completo do supervisor fica em
`logs/UUID-supervisor.log`; logs do jogo e `session.json` ficam em
`single/session-*` ou `coop-UUID/session-*`. **Abrir pasta de logs** e **Copiar logs**
estão disponíveis. A troca eboot.bin → game.elf drena o log anterior antes de trocar.

**Encerrar sessão** e fechar a janela solicitam cancelamento fora da thread Qt.
No Windows, CTRL_BREAK permite ao supervisor recolher seu filho e publicar evidências;
se necessário, a escalada usa `taskkill /PID <supervisor> /T /F`. Falha de encerramento
mantém o runner e permite nova tentativa, sem liberar outro lançamento ou fechar a UI.
Callbacks antigos não podem substituir o estado ou os logs de outra sessão.

Validação F3B: 26 verificações focadas passaram, incluindo UI Qt, contratos COOP,
LoadExec com filhos simulados, processo Python com erro real e cancelamento de um
filho Python via `run_child`. Capturas offscreen das três telas foram inspecionadas.
Nenhum jogo foi iniciado. O gameplay cooperativo anterior foi confirmado pelo
proprietário em COOP-06; lançamento real por esta interface ainda precisa de teste manual.

---

## Histórico F3A — aplicável ao modo `--demo`

## Correção final RCAS e notificações — 20/10/2026

Escopo desta correção: somente `backend.py`, `Main.qml`, este README e
`tests/test_knack2_ui.py`. Branch `experiment/gpu-small-sync-prebind-20261020`,
HEAD `21dadbb`. Core, F1/F2.1 e handoff preservados.

Reprodução antes do ajuste: clique no Slider gerou erro F1 para `50.0`.
`Math.round()` no QML ainda atravessava o slot QVariant como float Python,
mesma origem do erro relatado para `100.0`. O Slider agora chama o slot tipado
`setRcasStrength(float)`, que rejeita valores não finitos, limita a 0–100 e
arredonda para inteiro antes de delegar à validação F1 existente. `config.py`
não mudou e continua rejeitando `100.0` quando recebido diretamente.

O rótulo percentual usa o valor normalizado do backend, o mesmo gravado no INI.
Salvamento válido substitui a notificação de erro por confirmação. Notificações
também podem ser fechadas; erros anteriores ficam apenas no histórico explícito
de diagnóstico, sem reaparecer ao navegar.

Validação final: **86 testes passaram**, sendo 26 F1, 28 F2.1, 21 F3A e 11
regressões sintéticas anteriores. Comando:

```powershell
.\.venv\Scripts\python.exe -m unittest discover -s tests -p 'test_knack2*.py' -q
```

Novas provas: Slider em 0%, 50% e 100% via teclado/mouse; clique no botão de salvar;
valor INI e percentual idênticos; ausência do erro após salvar e navegar entre
as três telas; fechamento da notificação; conversão na fronteira preservando F1.

Validação de instalação conferida usando o validador F1 real, parser SFO real e
SHA-256 real, com perfil sintético injetado e somente parser de módulos ELF
simulado: pasta chamada `Named_01.00` com metadados 01.01 e hashes corretos aceita;
pasta `Named_01.01` com APP_VER 01.00 rejeitada; APP_VER 01.01 com executável
adulterado rejeitado pelo hash. O nome da pasta não define a versão. Nenhum dump
ou jogo real foi executado.

Captura offscreen inspecionada: `%TEMP%\knack2-f3a-review\settings-rcas-100-saved.png`.
Mostra RCAS 100% e confirmação do salvamento, sem erro, após interação real com
Slider e botão. Configuração gravada exclusivamente na área temporária sintética.

Interface independente em PySide6/Qt Quick, em português, para **CUSA08014 / 01.01**.
Esta entrega implementa somente demonstração e integração sintética. Não há caminho
de execução real, nem controle de processos do jogo.

## Abrir no Windows

Na raiz do repositório:

```powershell
.\.venv\Scripts\python.exe -m port.launcher.knack2 --demo
```

Na entrega F3A original, o padrão era demonstração. F3B acrescenta `--real` como padrão.
Não é necessário possuir o dump para abrir a interface ou simular sessões.

Dependência: `PySide6-Essentials>=6.7`, já declarada pelo projeto em
`port/launcher/qt/requirements.txt`. Nesta sessão, o proprietário autorizou a
instalação de PySide6 na `.venv`; foi instalada a versão 6.12.0. Nenhum arquivo de
dependências ou build foi modificado. Se Qt estiver ausente, o entrypoint retorna
2 e apresenta a dependência necessária, sem traceback nem execução do jogo.

Smoke finito e captura, sem jogo:

```powershell
.\.venv\Scripts\python.exe -m port.launcher.knack2 --demo --offscreen --quit-after-ms 1200 --screenshot "$env:TEMP\knack2-f3a.png"
```

Remova `--offscreen` para usar a plataforma Windows normal. A captura exige
`--quit-after-ms`; o diretório de destino deve existir. Falha de captura retorna 4;
falha ao carregar QML retorna 3. Nenhum script instala dependências automaticamente.

## Fluxo disponível

- **Início:** escolha ou digite uma pasta; valide para ver metadados ou o erro.
  A validação usa `validator.validate_knack2_dump`, incluindo hashes e módulos,
  em thread secundária. Resultado inválido não impede uma sessão sintética.
- **Jogar · simulação:** inicialização, execução com eventos, encerramento natural
  em aproximadamente seis segundos e repetição com identificador novo.
  **Encerrar simulação** solicita cancelamento apenas da thread sintética.
- **Configurações:** janela, VSync, limite de apresentação, FSR1 espacial, RCAS,
  intensidade e opções de mouse presentes em F1 e no runtime. Teclado padrão e
  detecção automática de controle são informativos. Não há remapeamento editável,
  FSR temporal nem alegação de dispositivo conectado.
- **Logs e diagnóstico:** supervisor e guest sintético separados, cópia para
  clipboard, consulta de erros e falha sintética durante execução.

As configurações iniciais são **exemplos**, não leitura das preferências reais:
janela, VSync ligado, limite de 60 FPS, FSR1 e RCAS desligados, intensidade 50%,
mouse desligado. Todas as gravações usam `config.py` de F1, com destinos privados
em `TemporaryDirectory(prefix='knack2-f3a-')`: `knack2.ini` e
`launcher_settings.json`. A aplicação não aceita um destino de configuração externo.
Os exemplos são descartados ao fechar. Nenhuma configuração em AppData, `out/`,
save ou perfil oficial é modificada.

## Arquitetura e arquivos novos

Todos os arquivos abaixo são novos; F1/F2.1 não foram alterados.

| Arquivo | Responsabilidade |
| --- | --- |
| `../__main__.py` | Entrada via `python -m port.launcher.knack2` |
| `__init__.py` | Pacote sem dependência de Qt no import |
| `app.py` | Import tardio de Qt, engine, fontes Windows, smoke e screenshot |
| `backend.py` | Estado de apresentação, validação assíncrona, gravação temporária e modelos de logs |
| `demo.py` | Contrato sintético injetável e runner de demonstração sem processos |
| `Main.qml` | Navegação, instalação, configurações, notificações e diálogo de erros |
| `ActionButton.qml`, `OptionSwitch.qml`, `OptionSelect.qml` | Controles com foco e estados visíveis |
| `LogPanel.qml` | Lista de logs com acompanhamento do final |
| `Relic.qml` | Ilustração vetorial original, sem assets do jogo |
| `README.md` | Uso, contratos, validação e limites desta entrega |
| `tests/test_knack2_ui.py` na raiz | Testes sintéticos e gráficos |

O backend recebe uma fábrica compatível com `DemoRunnerPort`. O padrão instancia
somente `SimulatedRunner`. Os tipos `RunnerState` e `SessionInfo` vêm de F2.1,
mas `KnackRunner` nunca é instanciado pela interface.

Callbacks de estado emitem sinal Qt com conexão explicitamente enfileirada.
O slot executa na thread principal. Callbacks de logs escrevem apenas em uma fila
protegida por lock; um timer Qt drena no máximo 100 eventos a cada 50 ms.
Nenhum callback inicia ou encerra sessão. Cliques de início reservam o estado
imediatamente; estados atrasados de início não desfazem cancelamento solicitado.

Limites: 1.000 eventos pendentes, 2.048 caracteres por linha, 500 linhas em cada
modelo, 500 linhas em cada histórico do simulador e 40 erros recentes. Excesso
na fila descarta as entradas mais antigas e incrementa contador visível.
Textos externos são exibidos como texto simples. Fechar a janela cancela e reúne
a thread sintética, para o timer e remove a área temporária.

## Evidências de validação — 20/10/2026

```powershell
.\.venv\Scripts\python.exe -m unittest discover -s tests -p test_knack2_launcher.py -v
.\.venv\Scripts\python.exe -m unittest discover -s tests -p test_knack2_runner.py -v
.\.venv\Scripts\python.exe -m unittest discover -s tests -p test_knack2_ui.py -v
```

Resultados: **26 F1, 28 F2.1 e 17 F3A passaram**. O conjunto
`-p 'test_knack2*.py'` passou com **82 testes**, incluindo 11 testes sintéticos
anteriores do supervisor/LoadExec. Esses testes anteriores importam `run.py`
como biblioteca e usam filhos Python sintéticos; não executam o launcher oficial
como processo, binários do Core ou jogo. Nenhuma compilação foi realizada.

F3A cobre componentes QML, cliques em botões e switches, estados, caminho com
espaços/Unicode e URL local, instalação inválida, validação lenta com heartbeat,
gravação temporária por F1, proteção das opções, entrega de callbacks na thread Qt,
erro e repetição, fila sob 8.000 eventos, clipboard, fechamento durante início e
validação, reabertura, capturas das três telas e tamanho mínimo 980×700.
Os testes de UI proíbem `subprocess.Popen`; Qt ausente é testado por Python `-S`.
Sem Qt, os testes gráficos são explicitamente ignorados, não declarados aprovados.
Execução adicional da suíte F3A com `python -S`: três testes passaram e 14 testes
gráficos foram explicitamente ignorados; total de 17 descobertos, sem falhas.

Smokes offscreen/software e Windows normal retornaram 0, com PNG. Capturas locais
em `%TEMP%\knack2-f3a-review\`: `home.png`, `settings.png`,
`invalid-installation.png`, `logs-running.png`, `logs-error.png`,
`home-minimum.png`, `windows-home.png`. Home, configurações, execução, erro e
tamanho mínimo foram inspecionados visualmente. As telas menores usam rolagem;
as opções inferiores e os botões podem exigir rolagem. Não foi realizado teste
manual do seletor nativo de pastas ou com hardware de controle.

## Limites e pendências para F3B

F3B exige autorização posterior. Permanecem pendentes a conexão com supervisor
real, persistência real, política de encerramento do jogo e validação com dump
válido real. Nenhum desempenho, áudio ou gameplay foi validado nesta entrega.

F2.1 entrega stdout do supervisor e logs guest pelo mesmo callback de texto.
O contrato sintético exige origens explícitas; uma futura integração precisa
resolver proveniência e identidade de sessão sem inferir a origem pelo conteúdo.
Não existe adaptador real escondido nesta entrega.

Validação F1 não oferece cancelamento do hash em andamento. Fechar a janela
descarta o resultado; uma thread daemon termina a leitura já iniciada, sem tocar
Qt nem gravar arquivos. Não foi alterado o contrato de F1 para interrompê-la.

O grafo Graphify existente foi consultado, mas não atualizado: a autorização desta
etapa limita arquivos novos a `port/launcher/knack2/` e `tests/`. A consulta estava
truncada e tinha referências antigas; código local prevaleceu. Não houve instalação,
regeneração de grafo ou gravação de relatórios fora do escopo.
