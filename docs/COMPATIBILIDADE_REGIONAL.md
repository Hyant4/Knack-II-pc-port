# Compatibilidade regional do Knack II

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
