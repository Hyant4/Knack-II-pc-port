# Windows — build e recursos de Knack II

Para baixar e jogar, consulte o [README](../../../README.md).
Para compilar o runtime, siga o [guia de desenvolvimento](../../../docs/DESENVOLVIMENTO_WINDOWS.md).
O launcher tem [instruções próprias](../../../docs/LAUNCHER_WINDOWS.md#reconstruir-somente-o-launcher).

Este diretório mantém os recursos necessários aos alvos Windows do CMake:
manifesto e metadados do runtime (`tlg-probe.rc`, `tlg-probe.manifest`), recursos
da ferramenta de capacidades Vulkan e do auxiliar nativo (`tlg-gpu-capabilities.rc`,
`play.c`, `play.rc`). Os nomes internos são preservados por compatibilidade.

O empacotamento herdado de inFAMOUS foi removido. Publicações de Knack II são
explícitas; o workflow de verificações não gera executáveis nem publica releases.
