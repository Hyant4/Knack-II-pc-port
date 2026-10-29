# Identidade, launcher e dados persistentes

Uso diário, localização de logs e build do launcher estão no
[guia Windows](LAUNCHER_WINDOWS.md). Este documento detalha identidade,
preservação de saves e proveniência do runtime.

O launcher usa o supervisor oficial `run.py`. Abrir a interface não inicia o jogo.
Nesta instalação Windows, o launcher compilado é **Knack II.exe**, na raiz do
repositório. Ele inclui Qt/Python e usa o supervisor compilado em
`port/out/knack-launcher-supervisor`. Não depende da `.venv` para abrir ou executar
o supervisor; mantém os arquivos oficiais de preparação e o runtime do projeto.
A pasta selecionada é lembrada automaticamente. Seu caminho é restaurado na
próxima abertura, mas a instalação precisa ser validada novamente antes de jogar.
Com o Python do projeto e PySide6 disponíveis, execute na raiz:

```powershell
python -B -m port.launcher.knack2 --real
```

Para reconstruir somente o launcher no Windows, com PyInstaller 6.22.3 disponível
no Python de build e o ícone original já preparado:

```powershell
python -B scripts/build_knack_launcher.py --build-dir 'C:\builds\knack-launcher'
```

O script recusa sobrescrever executáveis existentes. O build usa diretório
isolado, não compila o Core, não inicia o jogo e registra hashes de ambos os EXEs.
O console do supervisor é compartilhado e ocultado pelo bootloader para manter
CTRL_BREAK e encerramento controlado, sem janela de terminal nas partidas.
O teste do EXE confirmou esse encerramento com um filho sintético. Não foi
realizada uma nova sessão de gameplay para validar esta entrega.

## Saves fixos

No Windows, dados ficam em `%LOCALAPPDATA%\KnackII-PC`, independentemente da
branch, worktree ou versão do launcher. Solo e cooperativo usam o mesmo perfil:

- Saves: `profiles\shared\user\savedata`.
- Backups do runtime: `profiles\shared\user\savedata-backups`.

P1 continua sendo o padrão; P2 exige seleção explícita e recebe ID 4099, com
P1=1. Cada execução cooperativa ainda cria uma saída `coop-UUID` nova para
preparação, capturas e logs. Trocar o número de jogadores mantém o mesmo save.

O launcher passa `--user-data` ao supervisor. O mesmo caminho acompanha todas as
transições LoadExec. Um bloqueio do sistema operacional impede duas sessões de
usarem simultaneamente o mesmo perfil. Erros e encerramento liberam o bloqueio.
Não remova ou edite saves enquanto o jogo estiver aberto.

`--data-dir` continua disponível para testes isolados: ele também muda os perfis
dessa interface. Não use uma pasta descartável nessa opção para partidas normais.
Sem `--user-data`, comandos CLI antigos mantêm `OUT/user`, inclusive diagnósticos
cooperativos com saída nova. A CLI não migra ou procura saves automaticamente.

A separação de perfis da migração inicial foi substituída em 22/10/2026, a pedido
do proprietário. O save cooperativo foi primeiro aplicado ao solo; depois o
perfil comum passou a usar os dados cooperativos. Originais e históricos de
backup foram preservados e conferidos por SHA-256, com manifestos fora do Git.
Os caminhos antigos foram mantidos como junções Windows para o perfil comum,
permitindo usar o executável instalado sem reconstruí-lo.

Em outra instalação antiga, faça backup dos dois diretórios antes de escolher o
save a importar. Copie o conjunto completo de `savedata` e seu histórico de
backups para o perfil comum, com o jogo encerrado, sem sobrescrever progresso
já existente. O código atualizado detecta saves antigos sem um save comum e
avisa antes de iniciar uma partida vazia.

## Ícone original e executável

Por escolha do proprietário, o ícone da aplicação vem de `sce_sys/icon0.png` do
dump local Knack II. Não há redesenho ou geração de arte. A conversão técnica cria
ICO com tamanhos de 16 a 128 pixels e BMP para SDL, mantendo a imagem original.
Os arquivos ficam em `%LOCALAPPDATA%\KnackII-PC\identity`, fora do repositório.
Arte do jogo, executáveis e saves não são distribuídos com os fontes.

O launcher e a janela do jogo usam os ícones locais e os identificadores
`KnackII.PCPort.Launcher` e `KnackII.PCPort.Game`. O runtime preferido é
`port/out/knack2-runtime.exe`; `port/out/tlg-probe.exe` permanece como fallback
para builds diagnósticos existentes. Sem configuração local de ícones, não se
substitui a imagem por outra arte de Knack inventada.

Para preparar uma **nova cópia**, sem executar ou sobrescrever o runtime:

```powershell
python -B -m port.launcher.knack2.branding `
  --source 'C:\build-aprovado\tlg-probe.exe' `
  --output 'port\out\knack2-runtime.exe' `
  --expected-sha256 '<SHA-256 do build aprovado>' `
  --icon-source 'C:\dump\CUSA08014\sce_sys\icon0.png' `
  --report 'C:\evidencias\runtime-branding.json'
```

A ferramenta Windows altera somente recursos de ícone, versão e identidade do
manifesto. Verifica bytes e endereços de todas as demais seções PE, diretórios do
loader, símbolos COFF e capacidades do manifesto antes de publicar a cópia.
Mudança de contrato faz a preparação falhar sem selecionar o arquivo incompleto.
Ícones grandes são limitados ao espaço existente de recursos, preservando RVAs.
Para builds novos, `tlg-probe.rc` permite um ícone local pelo define
`KNACK2_ICON_FILE`; não incorpora arte proprietária no repositório.

## Proveniência e limites

A entrega local reutiliza o build COOP-05 / B2-F2C, SHA-256 original
`3db2a9d1240a20e7de1815ddf0946c34277808543f1969c076b533239d252d65`.
A cópia com recursos Knack preserva código e contratos desse build. Não é uma
nova compilação do Core. Os fontes C/C++ da branch de integração BRAND-01 seguem a main e não
incluem toda a linha de áudio experimental do artefato. Reconstruir a main não
substitui esse build de áudio; sua integração exige tarefa específica.

Áudio audível, idioma efetivo dentro do jogo e comportamento desta identidade
em gameplay ainda dependem de teste real. Nesta etapa, somente testes focados e
recursos Windows foram validados; nenhum jogo foi iniciado.

O registro de 21/10/2026 inclui 46 testes focados aprovados e cinco gates dos
executáveis compilados: ajuda oficial, ajuda ISA, rejeição de dump ausente,
abertura da interface e encerramento controlado. Os hashes abaixo identificam
os artefatos locais dessa entrega, sem afirmar equivalência de futuros builds:

| Artefato | SHA-256 |
| --- | --- |
| `Knack II.exe` | `944cfe124c3452554597f87d6bbfec1a0d2d80a6138491ac2fb866b899e5bbb0` |
| `KnackSupervisor.exe` | `26ea85580cd34af9e35dacc660f639fe8ad50ad50678f6fa49448e5b7bbf50fb` |
| `knack2-runtime.exe` com recursos Knack | `a73cd77cbc018cf19441207d45ed84fb8744f5615797f9d88c381989af4a44ea` |

Nomes internos `TLG_*`, target `tlg-probe`, diretórios de terceiros, créditos,
licenças e documentação histórica permanecem por compatibilidade e atribuição.
As bases estão creditadas no README principal. Não há vínculo oficial com a Sony.
