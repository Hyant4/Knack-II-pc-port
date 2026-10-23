# Launcher Knack II no Windows

## Abrir e jogar

O pacote [alpha Windows audio-clock09](https://github.com/Hyant4/Knack-II-pc-port/releases/tag/knack2-alpha-Version-0.10) inclui launcher, supervisor, runtime e DLLs. Extraia a pasta inteira antes de abrir; não execute de dentro do ZIP. O pacote não exige instalar Python, Qt ou MSYS2 para jogar. Dump e driver Vulkan atualizado continuam necessários.

Na instalação local, abra **Knack II.exe**, na raiz do projeto. Em PowerShell:

```powershell
& '.\Knack II.exe'
```

1. Em **Escolher pasta**, selecione a raiz do dump CUSA08014 versão 01.01,
   contendo `sce_sys`, `sce_module`, `eboot.bin` e `game.elf`.
2. Clique em **Validar**. O launcher verifica identidade, hashes e módulos.
3. Em **Configurações**, escolha idioma, jogadores e ajustes gráficos.
4. Clique em **Jogar**. Para encerrar, use **Encerrar sessão**.

Abrir o launcher não inicia o jogo. A pasta selecionada é salva automaticamente;
na próxima abertura, o caminho reaparece, mas a validação continua obrigatória.
Essa gravação não salva outros ajustes ainda não confirmados. Use **Salvar
configurações** para persistir idioma e ajustes gráficos.

## Jogadores e duração

O padrão é um jogador, P1=1. Para cooperativo, selecione explicitamente dois
jogadores e conecte dois controles. P2 usa o ID interno fixo 4099; não há campo
de ID na interface. A opção de dois jogadores não é restaurada automaticamente.

Partidas iniciadas pelo launcher não têm limite de duração. Ele envia
`--timeout 0` ao supervisor e mantém o controle de encerramento dos filhos.
A CLI de diagnóstico mantém timeout padrão de 180 segundos, aceita valores
positivos e também permite zero para execução ilimitada.

## Idiomas

O dump inspecionado contém português do Brasil, inglês, espanhol latino-americano
e francês. Após validar a instalação, o seletor oferece somente idiomas cujos
arquivos estão presentes nesse dump. A interface muda imediatamente; o jogo
recebe a escolha no próximo lançamento. Logs externos permanecem no idioma
original para diagnóstico.

## Saves, preferências e logs

A pasta padrão é `%LOCALAPPDATA%\KnackII-PC`, fora do checkout e dos diretórios
descartáveis de sessão. Trocar de worktree ou reconstruir o launcher não muda
essa raiz.

| Conteúdo | Caminho relativo à pasta padrão |
| --- | --- |
| Saves de um jogador | `profiles\single\user\savedata` |
| Saves cooperativos | `profiles\coop\user\savedata` |
| Backups do runtime | `savedata-backups`, ao lado de `savedata` em cada perfil |
| Pasta lembrada e preferências | `launcher_settings.json` |
| Configuração de cada lançamento | `launch-configs\UUID.ini` |
| Logs de abertura do launcher | `logs\launcher-UUID.log` |
| Log completo do supervisor | `logs\UUID-supervisor.log` |
| Preparação e logs de P1 | `single\session-*` para os logs; imagens preparadas em `single` |
| Preparação e logs de coop | `coop-UUID\session-*` para os logs; imagens preparadas em `coop-UUID` |
| Ícones originais convertidos | `identity\Knack.ico` e `identity\Knack.bmp` |

Os perfis P1 e coop são separados. Cada partida cooperativa cria uma saída nova
para preparação e logs, mantendo seus saves no perfil fixo. LoadExec preserva o
caminho do perfil entre executáveis. Um bloqueio impede uso simultâneo do mesmo
perfil por dois supervisores com `--user-data`.

Use **Ver logs** ou **Logs e diagnóstico** para encontrar os registros da sessão.
Um código de saída isolado não basta para concluir que houve crash. Por exemplo,
o supervisor pode devolver 124 após seu timeout; confira os logs e `session.json`.

`--data-dir` é uma opção de desenvolvimento: ela muda também a raiz dos perfis
dessa interface. Use uma pasta temporária apenas em testes isolados.

## Arquivos necessários

O executável do launcher inclui Python e Qt. Não exige a `.venv` para abrir ou
executar o supervisor, mas não é o port inteiro em um arquivo: mantenha a estrutura
do projeto, incluindo:

- `Knack II.exe`;
- `port/out/knack-launcher-supervisor/`, com `KnackSupervisor.exe` e suas bibliotecas;
- `run.py`, `profiles/` e os scripts de preparação em `port/scripts/`;
- `port/out/knack2-runtime.exe`, ou o fallback diagnóstico `tlg-probe.exe`;
- as DLLs nativas ao lado do runtime; a alpha já as inclui. Builds próprios podem exigir dependências locais adicionais.

O supervisor compilado executa o `run.py` oficial. Não utiliza o runner do
inFAMOUS. Arquivos legados permanecem por compatibilidade e créditos.
Os fontes não incluem o dump, saves, imagem original do jogo ou binários gerados.
Um clone novo precisa de build/preparo local; não contém `Knack II.exe` pronto.

## Reconstruir somente o launcher

Esta seção descreve o empacotamento do build local de desenvolvimento. Antes de
seguir os comandos, confira se seu checkout contém `scripts/build_knack_launcher.py`
e `port/launcher/knack2/packaging/knack2.spec`; esses arquivos podem ainda não estar
integrados em `main`. Um executável validado localmente não representa uma release
pública do launcher.
Ambiente observado: Python 3.14.5, PySide6-Essentials 6.12.0, PyInstaller 6.22.3
e iced-x86 1.21.0. O ícone original deve estar preparado no diretório `identity`;
veja [identidade e proveniência](KNACK_IDENTITY.md).

Com essas dependências disponíveis no Python de empacotamento, execute na raiz:

```powershell
python -B scripts/build_knack_launcher.py --build-dir 'C:\builds\knack-launcher'
```

O script recusa sobrescrever `Knack II.exe` ou o diretório do supervisor já
existentes. Preserve os builds anteriores antes de preparar uma nova implantação.
Ele grava `build-report.json` no diretório de build, com hashes dos dois EXEs,
versões e indicação de que não iniciou o jogo. Não recompila o Core.

No build local entregue, PyInstaller foi instalado em ferramentas externas
isoladas; a `.venv` do projeto permaneceu preservada. A integração dos fontes de
áudio do runtime COOP-05 à main é uma tarefa separada do empacotamento do launcher.

## Evidências e limites

Entrega de 21/10/2026: 46 testes focados passaram, sem skips. O binário compilado
abriu a interface, executou a ajuda do supervisor e da etapa ISA, rejeitou um
dump ausente antes de criar saída e encerrou cooperativamente um filho sintético.
Foram conferidos P1 padrão, P2=4099, duração ilimitada, perfis separados, memória
da pasta e ícone original embutido. Esse é um registro da validação já realizada,
não uma nova execução desses testes nesta atualização documental.

O proprietário confirmou gameplay cooperativo e recursos gráficos em teste
anterior. A validação do launcher compilado não iniciou o jogo. Áudio audível,
idioma efetivo dentro do jogo e gameplay usando este novo EXE ainda precisam
de confirmação real; build e smoke não demonstram esses resultados.

Documentação pública: [identidade e proveniência](KNACK_IDENTITY.md), [créditos](CREDITS.md) e [guia de publicação](PUBLICACAO.md). Handoffs de desenvolvimento permanecem locais.
