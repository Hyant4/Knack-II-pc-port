# Vulkan acelerado no WSL — diagnóstico de 24/10/2026

## Resultado comprovado

Dozen/Mesa 26.0.8 conseguiu acessar a AMD Radeon RX 6650 XT deste PC pelo
Direct3D 12 do WSL. `vulkaninfo --summary` identificou:

```text
deviceName = Microsoft Direct3D12 (AMD Radeon RX 6650 XT)
deviceType = PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
driverName = Dozen
apiVersion = 1.2.335
```

O smoke independente do runtime passou no envio de comandos e leitura de
4096 bytes usando essa GPU. Isso prova acesso Vulkan básico à GPU; não prova
compatibilidade com o renderer do jogo.

Inicialização do backend completo, num harness sem imagem guest ou save,
parou na validação existente:

```text
Vulkan 1.3 is required, but only 1.2 is supported by device!
STOP: GPU library assertion failed (see GPU log above)
```

O requisito está em `vk_platform.h` e é validado por `vk_instance.cpp`.
Ele não foi reduzido, e nenhuma assertion foi removida. Alterar a versão
anunciada pelo driver não implementa recursos ausentes. O Dozen testado também
avisa que não é uma implementação Vulkan conformante e se destina a testes.

Portanto, este caminho experimental não serve como gate de desempenho do port.
Para continuar, é necessário ambiente com GPU e driver Vulkan compatíveis com
o requisito real do renderer, por exemplo Linux nativo. Não há medição de FPS
do jogo usando Dozen; nenhum jogo foi iniciado nessa experiência.

## Isolamento aplicado

Experimento autorizado separadamente pelo usuário. Fonte oficial Mesa 26.0.8,
com SHA-256 conferido contra as
[notas oficiais da release](https://docs.mesa3d.org/relnotes/26.0.8.html):

```text
caf1c0061a68e88dfa74967a7e780c0e85d65b6c4e334cd69095a5dc54ad78bc
```

Fonte, build e instalação ficam sob
`/home/knackdev/knack-linux/dozen-26.0.8`. O prefixo de instalação foi
`install/`, sem permissão root. Não foram alterados os drivers Mesa instalados,
manifests ICD do sistema, `.wslconfig`, perfis de shell ou driver Windows.
Hashes dos manifests Vulkan, dos drivers lavapipe/RADV e das bibliotecas WSL
Direct3D 12 permaneceram iguais antes/depois.

Foram instaladas dependências de compilação no Ubuntu, incluindo Meson,
Mako, Flex/Bison, DirectX-Headers e headers XCB. A instalação do pacote de
desenvolvimento Expat também atualizou `libexpat1`; isso não foi uma troca de
Mesa ou do driver Windows.

Opções Meson efetivamente usadas:

```text
--buildtype=release --wrap-mode=nofallback --libdir=lib
-Dvulkan-drivers=microsoft-experimental -Dgallium-drivers=
-Dplatforms=x11 -Dglx=disabled -Degl=disabled -Dgbm=disabled
-Dopengl=false -Dllvm=disabled -Dbuild-tests=false -Dvalgrind=disabled
```

A seleção foi restrita a cada comando por `VK_DRIVER_FILES`, com
`LD_LIBRARY_PATH` apontando ao prefixo e a `/usr/lib/wsl/lib`. O mecanismo de
prefixo local e seleção explícita de ICD segue a
[documentação Mesa para builds locais](https://docs.mesa3d.org/install.html#running-against-a-local-build-hard-way).
Não copiar o ICD para configuração global como solução deste bloqueio.

## Distinção dos testes

- Boot real anterior: llvmpipe, renderização na CPU, FPS baixo relatado pelo
  usuário e timeout autorizado de 60 segundos.
- Experiência Dozen: GPU real detectada; smoke Vulkan básico aprovado;
  backend do port rejeitado por versão, antes de qualquer código do jogo.
- Pendente: boot e desempenho em driver Vulkan acelerado compatível,
  áudio audível, controles e gameplay.
