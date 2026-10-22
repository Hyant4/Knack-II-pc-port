# Identidade, launcher e dados persistentes

O launcher usa o supervisor oficial `run.py`. Abrir a interface não inicia o jogo.
Com o Python do projeto e PySide6 disponíveis, execute na raiz:

```powershell
python -B -m port.launcher.knack2 --real
```

## Saves fixos

No Windows, dados ficam em `%LOCALAPPDATA%\KnackII-PC`, independentemente da
branch, worktree ou versão do launcher. Os perfis são separados:

- P1: `profiles\single\user\savedata`.
- Cooperativo: `profiles\coop\user\savedata`.
- Backups do runtime: `savedata-backups`, ao lado de `savedata` em cada perfil.

P1 continua sendo o padrão; P2 exige seleção explícita e recebe ID 4099, com
P1=1. O perfil cooperativo persiste por pedido do proprietário. Cada execução
cooperativa ainda cria uma saída `coop-UUID` nova para preparação, capturas e logs.
Os dois perfis nunca compartilham o mesmo diretório de saves.

O launcher passa `--user-data` ao supervisor. O mesmo caminho acompanha todas as
transições LoadExec. Um bloqueio do sistema operacional impede duas sessões de
usarem simultaneamente o mesmo perfil. Erros e encerramento liberam o bloqueio.
Não remova ou edite saves enquanto o jogo estiver aberto.

`--data-dir` continua disponível para testes isolados: ele também muda os perfis
dessa interface. Não use uma pasta descartável nessa opção para partidas normais.
Sem `--user-data`, comandos CLI antigos mantêm `OUT/user`, inclusive diagnósticos
cooperativos com saída nova. A CLI não migra ou procura saves automaticamente.

A migração local inicial copiou os saves P1 da sessão F3B.2 e os saves cooperativos
da sessão F3B confirmada. Originais foram preservados. Cópias e backups foram
conferidos por SHA-256; o manifesto fica fora do Git. Novas instalações devem copiar
o conteúdo dos próprios diretórios `savedata` e `savedata-backups` para o perfil
correto, com o jogo encerrado, sem sobrescrever um perfil já preenchido.

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
nova compilação do Core. Os fontes C/C++ desta branch ainda seguem a main e não
incluem toda a linha de áudio experimental do artefato. Reconstruir a main não
substitui esse build de áudio; sua integração exige tarefa específica.

Áudio audível, idioma efetivo dentro do jogo e comportamento desta identidade
em gameplay ainda dependem de teste real. Nesta etapa, somente testes focados e
recursos Windows foram validados; nenhum jogo foi iniciado.

Nomes internos `TLG_*`, target `tlg-probe`, diretórios de terceiros, créditos,
licenças e documentação histórica permanecem por compatibilidade e atribuição.
As bases estão creditadas no README principal. Não há vínculo oficial com a Sony.
