# Continuidade do port Linux no Linux Mint

> Atualização de 29/10/2026: o usuário escolheu a base original do PR #19 de
> SkippySteve e dispensou teste na configuração pessoal como requisito para
> integrá-lo. A preparação atual está em
> `Documents/Codex/Knack-linux-pr19-20261029/REVISAO_INTEGRACAO.md`.
> Os gates de boot no Mint abaixo são históricos/opcionais para diagnóstico;
> não devem ser impostos novamente como condição de integração do PR.

Documento de retomada para VS Code com a extensão oficial do Codex.
Preparado em 28/10/2026. Este arquivo registra contexto; não inicia builds,
instalações, execução do jogo nem alterações de partição.

## 1. Pedido e próximo resultado esperado

O usuário decidiu experimentar Linux Mint por Live Boot para continuar o port
Linux e avaliar a GPU física. O primeiro resultado esperado é um diagnóstico
do ambiente Mint e um plano de build compatível, sem iniciar o jogo.
O trabalho Linux já existe e deve ser continuado, preservando o port Windows.
AppImage é uma possível etapa posterior de distribuição, não substitui o port.

Prompt para colar na nova conversa do Codex:

> Estou no Linux Mint Live, usando VS Code com Codex. Continue o port Linux
> deste projeto a partir de docs/CONTINUAR_LINUX_MINT.md. Leia AGENTS.MD,
> README.md, o topo recente de HANDOFF_ASTRA.md e os documentos Linux citados.
> Primeiro confirme ambiente, acesso à pasta, branch, HEAD, alterações locais,
> submódulos, armazenamento persistente e GPU/Vulkan. Não inicie o jogo nem
> altere drivers ou partições. Separe resultados atuais das evidências antigas
> do WSL. Preserve todas as alterações preexistentes. Depois apresente o menor
> próximo passo para compilar e validar no Mint.

Uma nova conversa na extensão não deve presumir acesso ao histórico desta
sessão. Os arquivos locais e este documento são o ponto de continuidade.

## 2. Fonte de trabalho e estado Git

Checkout Windows de origem:

```text
C:\Users\SEU_USUARIO\OneDrive\Documentos\GitHub\knack 2 port pc
branch: experiment/gpu-small-sync-prebind-20261020
HEAD: 3d23fb71a48e290e0387d2d80e4c04bf6babdb17
```

Antes deste documento, `git status --short` tinha 366 entradas, incluindo
arquivos modificados, excluídos, novos e alterações já no índice. Não limpar,
restaurar nem estagiar esse conjunto. O HEAD não representa sozinho o produto
local atual. Um clone do GitHub pode não conter o trabalho Linux e as mudanças
mais recentes; copiar somente arquivos rastreados também perde arquivos novos.

Transfira uma cópia completa do checkout de trabalho, com arquivos novos e
submódulos, preservando o original. Se copiar `.git`, preserve também os
metadados dos submódulos. Confirme o resultado no Linux antes de editar.
Não use `git submodule update` para corrigir divergências automaticamente:
há patches locais a preservar, inclusive em FSR-Vulkan.

Revisões observadas em 28/10/2026:

| Submódulo | SHA |
| --- | --- |
| port/gpu/third_party/fsr-vulkan | c64f093404125e960813f95f1996c14780f0dd69 |
| port/gpu/third_party/imgui | f1cc2ae15e53a861a874c3034aae6798fde194ab |
| port/gpu/third_party/magic_enum | e046b69a3736d314fad813e159b1c192eaef92cd |
| port/gpu/third_party/miniz | 2948eebafc5b9f1c69bebed80333e403a09bd62d |
| port/gpu/third_party/vulkan-headers | 01393c3df0e5285b54ee6527466513f9e614be94 |
| port/gpu/third_party/xbyak | cd6bad3cad2e07a3e277c23d2b1f065320024717 |
| port/third_party/LibAtrac9 | efca2e3af35562a09a9bb6deed90e45b4b824dc4 |

## 3. Fontes atuais prevalecem sobre o teste Linux antigo

Leia estas referências:

- [Desenvolvimento Linux](DESENVOLVIMENTO_LINUX.md): dependências, build e testes.
- [Diagnóstico WSL/Dozen](LINUX_WSL_GPU.md): bloqueio gráfico já investigado.
- [Compatibilidade regional](COMPATIBILIDADE_REGIONAL.md): perfis e provas por edição.
- `../AGENTS.MD` e `../HANDOFF_ASTRA.md`: escopo, preservação e trabalho recente.

Desde 24/10, o projeto recebeu perfis regionais, resolução interna,
antialiasing, mudanças de launcher, extração de PKG e correções/diagnósticos
de GPU. O suporte atual reconhece CUSA08014 01.00/01.01 e CUSA07670 01.01,
conforme o README e a documentação regional. Isso não certifica esses perfis
no Linux. O teste Linux antigo usou CUSA08014 01.01.

O script atual `scripts/build_knack_linux.py` já copia os perfis de
`SUPPORTED_PROFILES`. Não voltar ao empacotamento de um único perfil nem
enfraquecer validação de identidade, hashes, assinaturas ou endereços.

## 4. O que foi comprovado em 24/10/2026

Ambiente antigo: Ubuntu 26.04.1 LTS no WSL2, GCC 15.2. Os fontes ficavam em
`/home/knackdev/knack-linux/src`, com build e venv separados. Esse caminho
pertence à distribuição WSL `KnackLinux`; não existe automaticamente no Mint.
A cópia WSL não acompanha mudanças posteriores no checkout Windows.

Provas históricas, referentes aos fontes usados naquela data:

- Build ELF Linux e verificação de CPU passaram.
- Testes sintéticos de supervisor, LoadExec, reinício, encerramento de processos,
  memória, semáforos, freeze e SaveData passaram nos conjuntos documentados.
- Launcher Qt abriu em X11, em modo demo e ocioso. Pacote extraído passou nos
  checks de ajuda, CPU pelo supervisor e encerramento controlado.
- Um boot real autorizado por até 60 segundos passou de `eboot.bin` para
  `game.elf`, usando llvmpipe: renderização na CPU. O usuário relatou FPS baixo.
  O encerramento ocorreu pelo watchdog; não foi crash espontâneo.
- Dozen/Mesa isolado detectou RX 6650 XT e passou num smoke de leitura Vulkan,
  mas anunciou Vulkan 1.2.335. O backend completo exige Vulkan 1.3 e rejeitou
  esse driver antes do jogo. Nenhuma checagem foi removida.

Não há prova Linux de FPS com GPU física compatível, áudio audível, controles,
campanha, saves reais ou estabilidade prolongada. Sucesso de Windows não
substitui esses gates. O conjunto atual de fontes precisa de nova validação.

## 5. Pacote antigo: referência, não solução pronta para Mint

Arquivo observado no Windows em 28/10/2026, com 173994785 bytes:

```text
C:\Users\SEU_USUARIO\Documents\Codex\Knack-linux-mvp04-20261024\Knack-II-Linux-x86_64-dev-checked.tar.gz
```

Esse pacote foi feito para Ubuntu 26.04, glibc 2.43, e deixa bibliotecas nativas
externas. Mint pode usar versões mais antigas de glibc, SDL, FFmpeg e outras
bibliotecas. Não prometer que o pacote rodará. Não substituir glibc do Mint
nem adicionar repositórios de outra distribuição para fazê-lo funcionar.
Prefira avaliar build nativo com dependências compatíveis ou um empacotamento
com baseline apropriada. Isso exige investigar versões disponíveis primeiro.
O pacote antigo não contém as mudanças posteriores de 28/10.

## 6. Primeiro diagnóstico no Mint: somente leitura

Na raiz da cópia do projeto, execute os comandos disponíveis. A ausência de
uma ferramenta é um resultado a registrar, não motivo para instalar tudo.

```bash
cat /etc/os-release
uname -m
getconf GNU_LIBC_VERSION
printf '%s\n' "$XDG_SESSION_TYPE"
free -h
df -h . "$HOME"
findmnt -T .
git branch --show-current
git rev-parse HEAD
git status --short
git submodule status
git -C port/gpu/third_party/fsr-vulkan status --short
lspci -nnk
command -v vulkaninfo
command -v cmake
command -v ninja
command -v pkg-config
```

Se `vulkaninfo` existir, execute `vulkaninfo --summary`. Confirme GPU física,
driver e versão Vulkan do dispositivo. A versão do loader, sozinha, não basta.
`llvmpipe`/`lavapipe` não servem para medir desempenho da Radeon. Não herdar
`VK_DRIVER_FILES` ou `LD_LIBRARY_PATH` do experimento Dozen no WSL.

Confira versões de compilador, CMake, Python e dependências disponíveis. O
projeto exige C++23, SDL3 e bibliotecas listadas no guia Linux. Não execute
cegamente no Mint a lista `apt` validada em Ubuntu 26.04.

## 7. Live Boot e preservação

Uma sessão Live comum é temporária. Instalações, venvs, fontes e builds dentro
do ambiente Live podem desaparecer ao reiniciar. Confirme onde cada saída
fica e copie logs e alterações para armazenamento persistente antes de sair.
Evite compilar um projeto grande no disco temporário em RAM sem avaliar espaço.

Prefira uma pasta de trabalho em filesystem Linux, com espaço suficiente.
Não reutilize `port/out` do Windows nem misture artefatos ELF e PE. Se acessar
o disco Windows, preserve saves e arquivos originais. Se NTFS estiver bloqueado,
hibernado ou criptografado, não force escrita nem remova a hibernação para montar.
Este escopo não autoriza reparticionar, formatar ou instalar Mint no SSD.

## 8. Sequência de implementação e validação

1. Registrar baseline e verificar se a cópia inclui o trabalho atual.
2. Identificar GPU/driver Vulkan, dependências e armazenamento persistente.
3. Definir instalação mínima de dependências com o usuário. Aprovações antigas
   de instalação no WSL não autorizam alterações indiscriminadas no Mint.
4. Configurar build Linux em pasta nova e isolada. Exemplo existente, depois
   de conferir dependências e custo:

   ```bash
   cmake -S port/gpu -B port/out-linux -G Ninja \
     -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTLG_PGO=off -DTLG_LTO=OFF \
     -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
   cmake --build port/out-linux --target tlg-probe --parallel 6
   ./port/out-linux/tlg-probe --cpu-features
   ```

   Ajuste paralelismo à RAM/CPU disponíveis. Se a pasta já existir, inspecione-a
   antes de usar; não sobrescreva build anterior como se fosse descartável.
5. Executar testes focados do guia Linux, começando por `test_knack2_linux.py`,
   `test_knack2_runner.py`, `test_knack2_storage.py` e testes de perfis. As provas
   antigas não garantem que a mistura atual passe. Registrar falhas sem mascará-las.
6. Validar dependências dos ELF, launcher sem jogo e smoke Vulkan sem guest.
   O smoke básico não substitui inicialização do renderer completo.
7. Para qualquer nova correção de núcleo, definir arquivo, hipótese, risco e
   teste conforme AGENTS.MD; este documento não amplia autorizações críticas.
8. Somente depois preparar um teste real: obter autorização específica para
   sessão finita, usar dump local validado, perfil e saída novos, sem saves reais.
   A aprovação antiga de boot por 60 segundos já foi usada; não a reutilizar.
9. Medir FPS e tempo de frame com resolução, AA, upscaler, precisão GPU, cena,
   driver e cache registrados. Separar compilação inicial de shaders das amostras
   aquecidas e comparar a mesma cena. Verificar áudio e controles separadamente.
10. Registrar resultados no topo do handoff, revisar diff e preservar Windows.
    AppImage e publicação ficam para uma etapa posterior, após compatibilidade.

Não executar `port/run.sh` ou `port/build.sh` por conveniência: há caminhos
legados que não correspondem ao fluxo atual ou escrevem na saída compartilhada.
O caminho autoritativo é CMake em `port/gpu/CMakeLists.txt` e supervisor `run.py`.

## 9. Limites obrigatórios

- Não baixar/enviar dump, assets ou saves para repositórios ou ambientes públicos.
- Não iniciar jogo automaticamente, migrar saves, trocar driver ou reduzir
  requisitos Vulkan para passar uma validação.
- Não modificar ABI, patches guest, sincronização ou GPU sem escopo concreto.
- Não criar commit, merge, push ou release sem autorização específica.
- Não confundir build, smoke, boot, gameplay e desempenho: cada um exige prova.
- Nunca imprimir o ambiente inteiro; pode conter chaves e outros segredos.

## 10. Critério de entrega da retomada

A primeira etapa termina com ambiente identificado, baseline preservada,
viabilidade de GPU/dependências conhecida e próximo gate concreto. Build atual,
testes sintéticos, boot autorizado e gameplay devem ser reportados separadamente.
Se o Mint não fornecer a toolchain necessária, apresentar alternativa mínima
antes de instalar dependências externas ou alterar arquitetura.
