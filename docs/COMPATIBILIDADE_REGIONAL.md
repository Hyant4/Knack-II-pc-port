# Compatibilidade regional do Knack II

## Americana 01.00 integrada ao launcher principal local — 28/10/2026

CUSA08014/01.00 usa o fluxo normal de seleção, validação, Jogar e início direto
após validação persistida. Não exige update nem um launcher separado. A detecção
continua baseada em TITLE_ID/APP_VER do `param.sfo`, SHA-256 integrais e módulos
obrigatórios. O cache preparado permanece separado de 01.01. Europa 01.00 e
versões americanas desconhecidas continuam rejeitadas.

O launcher e o supervisor locais foram recompilados e instalados com backup.
O runtime permaneceu inalterado. O check do pacote agora carrega e confere todos
os perfis suportados, incluindo 01.00; um pacote sem esse perfil falha antes de
iniciar processos. Passaram 47 testes de perfis, empacotamento, início direto,
LoadExec e guardas de resolução nativa, ultrawide, antialiasing e ganho de diálogo.

O executável compilado validou o dump-base real, habilitou Jogar e iniciou
`eboot.bin`/`game.elf` com o perfil normal de áudio gerado pelo launcher. O log
confirmou mixer habilitado, WASAPI e PCM não silencioso. Uma cópia das configurações
anteriores também abriu e validou a instalação europeia 01.01. Isso complementa
as evidências anteriores de controles e som audível, sem certificar campanha
completa, todas as combinações gráficas ou ausência de cortes de áudio.

Evidências e backup local em `Documents/Codex/Knack-USA100-integration-20261028/`.
Esta integração local não criou commit nem publicou uma nova versão.

## Base americana 01.00: áudio audível no teste — 28/10/2026

Um teste posterior usou o perfil normal de áudio do launcher na configuração
privada. A configuração do teste gráfico anterior não habilitava os opt-ins de
decodificação e mixagem NGS2. Nenhum código, dump ou perfil regional foi alterado.

O runtime abriu WASAPI/SDL e produziu PCM não silencioso. O usuário confirmou
som audível durante a sessão. Após pausa e retomada no Chapter 1-1, os diagnósticos
continuaram registrando saída não silenciosa. A sessão recebeu timeout de 120
segundos e terminou com código 124 previsto. Evidências privadas em
`Documents/Codex/Knack-base-audio-20261028/`.

Isso confirma áudio audível nesse teste, sem certificar todas as categorias de
áudio ou ausência de cortes. O log registrou três underruns NGS2 e contadores
do caminho legado de virtualização do runtime testado. Campanha completa e
Europa 01.00 permanecem fora desta evidência.

## Base americana 01.00: interação real em Chapter 1-1 — 28/10/2026

O teste posterior avançou além do título, sem aplicar update. Com o dump-base
completo e dados de usuário isolados, Continue carregou Chapter 1-1 em Newhaven,
com dificuldade Normal. Foram inspecionados cenário, personagem, menu de pausa
e retorno ao jogo. Movimento analógico deslocou personagem e câmera; salto e
animação de ataque responderam, com capturas reais do runtime.

O resultado confirma funcionamento inicial nesse trecho na configuração testada.
Não certifica campanha, combate com inimigos, estabilidade prolongada ou todos
os efeitos gráficos. O usuário relatou ausência de som e autorizou continuar
somente a validação gráfica e de controles; áudio não foi investigado nem alterado.
A sessão recebeu limite de 180 segundos. Logs, capturas e dados de teste estão
na pasta privada `Documents/Codex/Knack-base-gameplay-20261028/`.

Nenhum código ou perfil foi alterado nesta retomada. Dump e saves habituais
foram preservados. Europa 01.00 continua pendente de evidência própria.

## Base americana 01.00: dump completo e boot real — 28/10/2026

Foi testado o PKG-base americano fornecido pelo usuário: CUSA08014,
APP_VER=01.00, CATEGORY=gd, 36.490.510.336 bytes. Nenhum update foi aplicado.
A extração completa isolada produziu 4.769 arquivos, totalizando
52.158.097.075 bytes. Os executáveis correspondem exatamente aos hashes já
permitidos pelo perfil `CUSA08014-01.00`; nenhum hash ou perfil foi alterado.
Os módulos obrigatórios passaram pelo validador real.

O helper de extração tinha um problema de offsets grandes no Windows MinGW:
o mapeamento para `_fseeki64`/`_ftelli64` estava condicionado a `_MSC_VER`.
Na toolchain Clang utilizada, isso truncava acessos acima de 4 GiB. A adaptação
agora utiliza essas funções em Windows. Um teste nativo confere Seek/Tell em
5 GiB + 123 bytes sem criar um arquivo grande. A extração da base foi feita por
um diagnóstico privado que permite PKG-base; o instalador de **updates** do
launcher continua recusando base e DLC. Jogar uma base extraída é uma operação
distinta de instalá-la pelo botão de atualização.

Duas sessões reais de até 60 segundos usaram o runtime existente, copiado para
área privada e conferido por SHA-256, com novos dados de usuário e saídas isoladas.
`eboot.bin` retornou 75 para LoadExec; `game.elf` executou sem update e terminou
com 124 pelo timeout previsto. Capturas reais foram inspecionadas: abertura do
Japan Studio e tela de título de Knack II com “Press Any Button”. Não houve
interação de gameplay, teste auditivo, combate ou certificação da campanha.

O launcher compilado candidato validou a instalação completa como 01.00, com
registro persistido válido. A interface passou a explicitar América 01.00/01.01
e Europa 01.01, com traduções. A base europeia 01.00 ainda não foi investigada
nesta etapa e permanece fora dos perfis permitidos.

Evidências locais em `Documents/Codex/Knack-base-pkg-20261028/`, incluindo hashes,
validação, logs, sessões e capturas. Dump habitual, saves, pacote-base original,
runtime instalado e submódulos foram preservados. O pacote original conservou
tamanho e data de modificação; seu SHA-256 foi calculado após o teste para
identificação. Não houve commit ou publicação.

## Variante de executável da issue #12 nos fontes — 26/10/2026

O perfil americano `CUSA08014/01.01` mantém o hash principal de `game.elf`
`80ccc22819fa4eeb5e1824e585f3ac5d5168e9ebc82f629e44cdb63fd573fb20` e passa
a reconhecer explicitamente a variante
`0b7843d39d11c5630a72727f286cde563aa74b7f6b22941bcbe05d907fbadf8b`.
Qualquer outro hash continua rejeitado, assim como eboot/módulos inválidos.

A região vem do SFO e permanece americana: argumentos `-region=a`, idiomas e
identidade de saves não são convertidos para a edição europeia. A variante usa
o layout de código `elf-0b7843d3`, já conhecido no executável europeu. Essa
coincidência não comprova a origem regional do pacote do relator.

`game_elf_variants` no perfil relaciona hashes exatos a layouts permitidos.
O supervisor registra layout/hash no `session.json`, fornece
`KNACK2_EXECUTABLE_LAYOUT` validado ao loader e mantém `KNACK2_PROFILE` regional.
Overrides herdados não escolhem layout. Os caches preparados principais são
preservados; a variante usa `CUSA08014-01.01/elf-0b7843d3/` sob a saída escolhida.

O loader seleciona as tabelas existentes de resolução, câmera e AA pelo código
identificado, mantendo todas as verificações de assinatura/segmento. Ganho de
diálogo e probe opcional de fatores restritos ao código americano não são
instalados no layout compartilhado. Não houve mudança de decoder, mix ou ABI.

Para impedir mistura de versões, executar a variante exige runtime que anuncie
suporte a esse layout no diagnóstico `--cpu-features`. Runtime antigo bloqueia
a partida antes da preparação, com mensagem para usar build correspondente.
Análise estática continua disponível sem runtime novo. Caminhos principais e
lançamentos antigos mantêm comportamento compatível. Para retorno, o hash
principal e seus caches continuam intactos; não é necessário migrar saves.

Validação desta etapa: hashes/módulos e linking em fixture externo com bytes
conhecidos e SFO americano anexado à issue, testes sintéticos de LoadExec/região,
harness C das assinaturas reais e build nativa isolada. Não é prova de que todos
os assets de uma instalação americana atualizada foram validados, nem de boot,
gameplay ou correção das superfícies pretas na GTX 1060. A alteração ainda está
nos fontes; não acompanha automaticamente o pacote público 0.17.6.

## USA 01.00 nos fontes — 26/10/2026

O perfil `CUSA08014-01.00` foi acrescentado separadamente dos perfis 01.01.
O launcher seleciona a versão instalada por `APP_VER` de `param.sfo`; o campo
`VERSION=01.00` de um dump atualizado não o transforma em uma instalação-base.
Europa 01.00 continua fora da lista de perfis suportados.

Na [issue #12](https://github.com/Hyant4/Knack-II-pc-port/issues/12#issuecomment-6425540228),
o usuário informou estes executáveis para a base americana 01.00:

| Arquivo | Tamanho | SHA-256 |
| --- | ---: | --- |
| `eboot.bin` | 37.006 bytes | `e65b5fd039833ada7dddd8c197b8b47c283a3f46320cd13ece5ff3973a4041db` |
| `game.elf` | 20.205.618 bytes | `80ccc22819fa4eeb5e1824e585f3ac5d5168e9ebc82f629e44cdb63fd573fb20` |

Os hashes são idênticos aos executáveis locais já aceitos pelo perfil americano
01.01. Os dois SFO anexados foram lidos: ambos identificam CUSA08014, com
`APP_VER=01.00`/`CATEGORY=gd` na base e `APP_VER=01.01`/`CATEGORY=gp` no update.
Isso permite conferir os bytes conhecidos, mas não comprova a origem de todos
os arquivos presentes na instalação do usuário.

O novo perfil mantém validação integral dos dois executáveis e dos módulos.
O cache preparado fica em `CUSA08014-01.00/`, sem substituir o cache 01.01.
As verificações de perfil para resolução nativa, ultrawide, ganho de diálogo,
FSR temporal e o protótipo de AA foram estendidas somente ao identificador
americano exato. Endereços, assinaturas, formato dos dados e ABI permanecem iguais.
O probe opcional de fatores NGS2 mantém seu escopo diagnóstico 01.01.

Validação: 99 testes passaram, sem skips, incluindo persistência do launcher,
LoadExec simulado, isolamento de cache, rejeição de hashes divergentes e os
harnesses de câmera/resolução/AA/diálogo. Quatro unidades C/C++ afetadas foram
compiladas com as opções do build Windows existente, em novos arquivos externos.
As quatro famílias de assinaturas foram conferidas nos bytes reais locais.
Preparação e linking passaram em um fixture isolado que combina esses executáveis,
os módulos locais e o SFO-base anexado. Nenhum jogo foi iniciado.

Limites: esse fixture não é uma instalação-base completa e independente. Assets
e módulos originais de uma cópia 01.00 completa, gameplay, áudio audível e
renderização ainda precisam de validação. Não houve link de um novo runtime
completo, reconstrução do launcher executável, instalação ou publicação de pacote.
Os binários já instalados ou publicados não recebem essa mudança automaticamente.

## Estado em 24/10/2026

O launcher e o supervisor da versão 0.16 reconhecem dois perfis exatos:

| Perfil | Validação dos arquivos | Preparação e linking | Boot e gameplay nesta etapa |
| --- | --- | --- | --- |
| CUSA08014 / 01.01 — América | Aprovada | Aprovados | Abertura e cutscenes em 21:9; usuário relatou bom gameplay |
| CUSA07670 / 01.01 — Europa | Aprovada | Aprovados | Boot, introdução e título em 32:9; gameplay não validado |

O boot europeu tem evidência real na etapa REGION-03. Compatibilidade completa
continua dependente de gameplay, avaliação auditiva e recursos temporais. O
launcher e o runtime já instalados foram preservados. A 0.16 é um pacote novo;
downloads de versões anteriores não são modificados.

## Seleção e integridade

A seleção usa `TITLE_ID` e `APP_VER` de `sce_sys/param.sfo`, nunca o nome da pasta.
Cada perfil conserva os hashes próprios de `eboot.bin` e `game.elf`; títulos e
versões fora da lista são rejeitados. Arquivos misturados entre regiões continuam
sujeitos à validação de hashes e dos módulos ELF.

O perfil americano existente permanece intacto. O novo perfil é
`profiles/CUSA07670-01.01.json`; SHA-256 do `game.elf` europeu avaliado:

```text
0b7843d39d11c5630a72727f286cde563aa74b7f6b22941bcbe05d907fbadf8b
```

Nas instalações inspecionadas, `eboot.bin`, `libc.prx`, `libSceFios2.prx`,
`libSceJobManager.prx`, os dez arquivos de `data/shader_binary` e
`data/scp_def/func_sound.def` são idênticos entre regiões. `game.elf` é diferente:
tem endereços de código e relocações diferentes. A igualdade de shaders e imports
é evidência para investigação, não prova de execução compatível.

## Cache e saves

O caminho americano de imagens preparadas foi preservado. Sob o mesmo `--out`,
as imagens europeias ficam em `CUSA07670-01.01/eboot_bin` e
`CUSA07670-01.01/game_elf`, sem substituir as imagens americanas.

O armazenamento de saves não foi modificado. O runtime existente compõe o
caminho de SaveData com o título, em `savedata/<usuário>/<TITLE_ID>`; compartilhar
a raiz de solo/cooperativo não converte nem transfere saves entre regiões.
A compatibilidade de conteúdo de saves entre regiões não foi testada.

## Idiomas

O catálogo mantém os quatro idiomas americanos e acrescenta os quatorze idiomas
encontrados na instalação europeia, contando o francês comum às duas regiões.
Somente diretórios com arquivos são oferecidos depois da validação.

Para a Europa: `fr-FR`, `pt-PT`, `en-GB`, `es-ES`, `de-DE`, `it-IT`, `nl-NL`,
`ru-RU`, `fi-FI`, `sv-SE`, `da-DK`, `nb-NO`, `pl-PL` e `ar-SA`.

Os IDs de idioma seguem `OrbisSystemParamLanguage`, verificado no
[header do OpenOrbis](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/_types/sys_service.h).
O idioma do launcher usa os catálogos já existentes: português, inglês, espanhol
e francês. Outros idiomas do jogo usam interface em inglês. As variantes
regionais compartilham o catálogo correspondente do launcher.

## Primeiro boot europeu real — REGION-03

Em 24/10/2026, usuário autorizou o teste. Uma única sessão usou o runtime isolado
da REGION-02, um jogador, português de Portugal, janela 1280x720, readbacks=2 e
FSR desligado. Saves e configurações ficaram em área temporária nova, sem copiar
ou migrar progresso americano.

`eboot.bin` saiu 75 e solicitou LoadExec de `game.elf` com quatro argumentos,
incluindo `-region=e`. O executável original europeu entrou, inicializou o
presenter Vulkan e renderizou o aviso de gravação automática e a introdução.
Esses dois momentos foram inspecionados visualmente pelo agente. Usuário relatou
"ta funcionando normalmente pelo o meu olhar superficial"; esse relato é uma
observação inicial, sem ampliar a cobertura para campanha ou estabilidade.

Sessão tinha limite compartilhado de 60 segundos. O supervisor encerrou o filho
por timeout e retornou 124, com 72,85 segundos totais incluindo preparação e
tolerância de encerramento. Não foi observado crash espontâneo. Não houve
inputs automatizados de gameplay nem segunda sessão. Todos os processos do
teste terminaram.

Log não apresentou STOP, import ausente, assertion falha ou device-lost. Houve
mensagem de cache `profile.bin` ainda ausente no perfil novo e chamada stub
preexistente de `sceAvPlayerSetLogCallback`; o boot prosseguiu. AudioOut registrou
blocos PCM não zerados, mas audibilidade não foi verificada. O ganho específico
de diálogo permaneceu desativado pela verificação de perfil/assinatura.

14.758 arquivos foram comparados por hash antes/depois, sem diferença ou
ausência, e nenhum arquivo novo surgiu no perfil habitual. Evidências, imagens
e saves temporários permanecem locais, fora do Git. O campo automático
`session.json.visual_verified` continua falso; a inspeção humana e a evidência
visual do agente estão registradas separadamente em `observed-boot.json`.

## Pendências do runtime

Na etapa REGION-02, autorizada ao Sol, três condições de seleção passaram a
reconhecer os identificadores exatos americano e europeu: controles de FSR
temporal no overlay, caminho temporal específico de Knack e preparação
assíncrona FSR4. Nenhum shader, formato, contrato de câmera, sincronização ou
hook foi modificado. O runtime Windows foi compilado em saída externa nova;
a instalação existente não foi atualizada.

Dezenove testes focados passaram, incluindo 24 combinações de perfil, provider
e preset e o contrato sintético de câmera/movimento. O harness de configurações
e fallback FSR passou em cópia externa adaptada para Windows, com todas as
assertions originais preservadas. Os smokes do runtime `--cpu-features` e
`--vulkan-only` passaram; este último enviou e leu 4096 bytes na RX 6650 XT.

O alvo auxiliar `camera-motion-test` não passou. O alvo CMake original apresentou
incompatibilidades de compilação Windows; uma adaptação externa linkou, mas
terminou com `0xC0000005` durante a criação da instância Vulkan, antes de alcançar
os caminhos modificados nesta etapa. A causa não foi isolada. O smoke Vulkan
básico aprovado não substitui esse teste de shader. O erro auxiliar não se
reproduziu no boot real da REGION-03; FSR temporal europeu segue sem validação
de execução, pois esse boot usou FSR desligado.

O ganho de diálogo e os probes de áudio têm endereços e verificações específicos
americanos. As verificações não foram relaxadas. Duas assinaturas curtas de loads
foram encontradas em endereços europeus diferentes, mas isso não valida chamadas,
registradores, stack ou o setter completo. Nenhum hook europeu foi instalado.

Próximos gates: gameplay europeu, áudio audível, FSR temporal e contratos dos
hooks europeus; o harness auxiliar de câmera Windows também permanece pendente.
REGION-01 e REGION-02 não abriram sessão guest. REGION-03 acrescentou somente o
primeiro boot europeu curto descrito acima.

## Release 0.16: idiomas e ultrawide

O pacote contém os dois perfis e todos os idiomas listados acima. Testes
sintéticos cobriram troca de região, idioma indisponível e seleção regional nos
argumentos do supervisor. Isso não certifica dublagem ou legendas de todos os
idiomas em gameplay. A interface do launcher mantém seus quatro catálogos.

Dois boots iniciais usaram dados temporários, um jogador, FSR desligado e
limite guest de 45 segundos. O americano abriu em janela 21:9; essa primeira
compilação revelou compressão dupla da interface. A compensação extra de
viewport foi retirada. O europeu abriu com a compilação corrigida em 32:9,
chegando à introdução e ao título com interface central proporcional.
Ambas as sessões terminaram pelo limite configurado, código 124; preparação
e encerramento elevaram o tempo total para aproximadamente 58 e 59 segundos.

Testes posteriores exercitaram a abertura e cutscenes americanas em 21:9.
Usuário relatou bom gameplay e enviou capturas com contador em 60 FPS;
isso não comprova estabilidade contínua. As capturas também confirmaram
laterais azuis, faixas incompletas e imagem estreita nas cutscenes.
As tentativas de corrigir esse enquadramento não integram a release 0.16.

Ultrawide permanece experimental. A ampliação horizontal da câmera tem prova
matemática e validação de quatro assinaturas por região. Campanha, HUD completo,
cutscenes corrigidas e FSR temporal em ultrawide ainda precisam de avaliação.
Nenhum monitor ultrawide físico foi usado. Instalação habitual e saves reais
foram preservados; testes usaram perfis temporários.
