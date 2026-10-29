# Instalador local de atualizações PKG

Para extrair o jogo-base, use a ação separada **Extrair jogo-base (.pkg)** na
aba Instalação. Ela grava diretamente na pasta do PKG; veja
[extração do jogo-base](EXTRACAO_PKG_BASE.md). A ação de updates abaixo continua
recusando PKGs-base e DLC.

Implementação em desenvolvimento nos fontes. A inclusão neste checkout não
modifica launchers já instalados nem downloads publicados. Não há download de
atualizações do jogo: o usuário fornece seu próprio PKG local.

## Uso

Na tela **Instalação**, selecione a pasta do jogo e **Instalar update (.pkg)**.
O launcher lê os metadados do pacote e compara CUSA, CONTENT_ID e versão com a
instalação selecionada. Base e DLC são rejeitados. Somente os perfis já
autorizados pelo validador são aceitos; versões iguais ou anteriores à instalada
não são reaplicadas nesta primeira implementação.

Após a confirmação, o extrator trabalha em uma pasta temporária ao lado da pasta
do jogo. O launcher confere os executáveis completos e os módulos necessários
usando a atualização sobreposta à base. Isso não exige duplicar a instalação.
Variantes de executável já permitidas pelo perfil continuam aceitas. Um hash
desconhecido bloqueia a aplicação, com os hashes encontrados e aceitos no erro.
SHA diferente, isoladamente, não comprova que o update seja de outra região.

Somente depois dessa conferência os arquivos anteriores são movimentados para
backup, no mesmo volume. Os arquivos novos ocupam seus lugares. Arquivos idênticos
não são substituídos. O resultado passa novamente pelo validador; os saves do
launcher não são alterados. A interface impede jogar ou mudar a pasta enquanto
a operação está em andamento. Instâncias deste launcher usam um lock comum da
instalação; outros programas e execução direta do supervisor não participam desse
lock. Encerre essas sessões antes de atualizar.

**Cancelar operação PKG** interrompe a extração ou inicia a restauração dos arquivos
já substituídos. O fechamento normal do launcher aguarda a operação. Uma falha
de aplicação também tenta restaurar o estado anterior. Falhas de recuperação
preservam o registro e o backup, bloqueando a partida até a restauração.

## Espaço e backup

O instalador faz uma conferência inicial com o tamanho do PKG e uma margem de
64 MiB. Depois que o extrator lê a tabela PFS, ele calcula os bytes dos arquivos
descompactados e verifica espaço antes da extração desses arquivos. Metadados
pequenos do pacote já podem ter sido escritos na pasta temporária nessa etapa.
O tamanho comprimido não é apresentado como tamanho descompactado.

A movimentação dos originais para backup não cria uma segunda cópia dos bytes
antigos. O espaço adicional principal é o update extraído, mais a margem e os
metadados da operação. Não há cópia integral do jogo.

O registro fica em `.knack-update-<nome da pasta do jogo>/journal.json`, ao lado
da instalação; os arquivos anteriores ficam em `backup/`. **Abrir backup** abre
essa área. **Restaurar backup** solicita confirmação e desfaz a atualização,
inclusive removendo arquivos novos que ainda correspondem aos bytes instalados.
Arquivos modificados posteriormente ou backups divergentes bloqueiam a
restauração para preservar mudanças. Uma restauração parcial continua registrada
e pode ser retomada após resolver o conflito.

Após sucesso, o backup é mantido. Ao iniciar outra operação, o registro concluído
anterior é arquivado com sufixo `-history-<identificador>`, sem apagar backups.
A restauração pela interface atende o registro atual; históricos são preservados
para recuperação manual. Não apague uma área com recuperação pendente.

O registro é escrito antes das movimentações. Ele permite detectar e recuperar
interrupções do processo. Isso não certifica recuperação contra todos os efeitos
de falta de energia, corrupção do disco ou alterações concorrentes externas.

## Extrator e build

O executável auxiliar `KnackPkg.exe` usa o núcleo de
[PKGInstall](https://github.com/Muggle345/PKGInstall) no commit
`902d14c1a3c277a586e4c0c4db0774d06bda3501`, adaptado em uma cópia externa ao
checkout. O adaptador acrescenta checagens de limites, nomes e destinos, leitura
e escrita completas, falhas de descriptografia, espaço livre e saída de erro.
Ele aceita somente diretório temporário vazio. Não utiliza a interface Qt do
PKGInstall nem procura o jogo pelo nome da pasta.

O código de PKG/PFS declara GPL-2.0-or-later, com atribuição ao shadPS4;
o código de I/O também preserva a atribuição ao projeto yuzu. Crypto++ 8.9.0
tem seu arquivo de licença incluído. O bundle acompanha avisos, texto GPL e
`helper-sources.zip`, com fontes originais, adaptadas e script de build.

Com os fontes locais de PKGInstall nesse commit, Crypto++ na tag
`CRYPTOPP_8_9_0` e a toolchain CLANG64 já existente:

```powershell
.\.venv\Scripts\python.exe scripts/build_knack_pkg_helper.py `
  --source-dir C:\build\PKGInstall `
  --cryptopp-source C:\build\cryptopp-CRYPTOPP_8_9_0 `
  --build-dir C:\build\knack-pkg-helper --jobs 4 --selftest

.\.venv\Scripts\python.exe scripts/build_knack_launcher.py `
  --build-dir C:\build\knack-launcher-pkg `
  --stage-dir C:\build\knack-launcher-pkg-stage `
  --pkg-helper-dir C:\build\knack-pkg-helper
```

Os scripts não baixam nem instalam dependências. O primeiro exige diretório novo
fora do checkout e verifica os hashes dos fontes de entrada. O segundo inclui o
helper em `port/out/knack-pkg-helper/` no staging; o restante do pacote de execução
continua necessário para uma distribuição completa. Não há alteração de CMake,
submódulos, decoder, loader ou runtime do jogo.

Sem o helper no pacote, a seleção e o diagnóstico de metadados continuam
disponíveis, mas a instalação falha antes de alterar o jogo, informando que o
extrator está ausente.

## Evidência e limites

Testes sintéticos cobrem região e versão, bounds de PKG, espaço insuficiente,
SHA desconhecido, validação dos módulos da base, escrita interrompida,
cancelamento, recuperação e preservação de arquivos alterados depois do update.
Testes Qt conferem confirmação, traduções, bloqueio de operações e fechamento.

O selftest nativo gera um PKG pequeno criptografado, composto exclusivamente por
bytes inventados. Ele prova extração, rejeição de travessia de diretórios,
truncamento e destino não vazio. Também executa o helper pelo instalador Python
e restaura o backup. Não é evidência de instalação de um update real de Knack II.

Em 27/10/2026, o usuário forneceu um PKG americano 01.01 de 24.838.144 bytes.
A extração local produziu 24 arquivos, totalizando 26.823.512 bytes. A validação
com a base somente leitura passou com o layout `elf-0b7843d3`: o pacote americano
contém a variante já permitida, sem comprovar mistura regional.

O primeiro teste bloqueou uma entrada NP de 532 bytes. A adaptação passou a ler
os 544 bytes de ciphertext AES-CBC presentes no pacote, descriptografar blocos
completos e gravar somente os 532 bytes lógicos. O teste sintético também confere
todos os bytes dessa entrada, inclusive a cauda. O problema do tamanho físico
está descrito na [issue #36 do extrator de mesma origem](https://github.com/AzaharPlus/shadPS4Plus/issues/36).

O ciclo completo usou uma cópia privada apenas dos arquivos afetados, executáveis
e módulos necessários. A versão 01.00 foi simulada somente no SFO dessa cópia,
pois a instalação habitual já estava em 01.01. Foram aplicadas 13 alterações e
preservados 12 arquivos em backup. A restauração reproduziu exatamente os hashes
iniciais da cópia. Os arquivos lidos da instalação habitual e o PKG original
mantiveram seus hashes. Nenhum jogo foi executado.

Essa cópia não constitui um dump completo e independente da base 01.00. Outros
PKGs, layouts PFS, updates incrementais/delta, formatos criptográficos não
suportados, grandes volumes e falhas reais de armazenamento não são certificados.
A conferência dos executáveis e módulos não verifica todos os assets nem
certifica gameplay.
