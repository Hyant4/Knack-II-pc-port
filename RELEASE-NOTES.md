## Português

Pré-release experimental com correção do caminho de interpolação usado por
determinados materiais do Knack II em GPUs sem suporte baricêntrico nativo,
incluindo a GTX 1060 da issue #12.

O fallback preserva os dados dos três vértices e os pesos de interpolação.
Ele é selecionado automaticamente nos pipelines compatíveis; não depende do
preset GTX 1060. Os caminhos com suporte nativo continuam usando esse suporte.
Cache e metadados foram versionados para recompilar os shaders necessários.

**Teste recomendado:** extraia o ZIP Windows numa pasta nova, abra
`Test GTX 1060.cmd`, mantenha os ajustes gráficos usados anteriormente e deixe
o diagnóstico GTX em **Desligado**. Retorne à mesma rua e confira asfalto e
fachadas. Não é necessário repetir os modos 3/4 nem fazer outra captura F11.

Validação: build isolado; 2.100 pixels sintéticos comparados em sete modos;
397 shaders locais validados; gameplay local com capacidades baricêntricas
restritas e fallback confirmado em 61 pipelines. A rua apresentou cor e a
sessão encerrou normalmente. **Ainda falta confirmação em uma GTX 1060 física.**
Campanha, desempenho na GTX e pipelines fora do escopo não foram certificados.

Este pacote mantém a base da RC3 e acrescenta a correção. Não inclui jogo,
update, saves ou capturas do relator. Stable/latest 0.17.5 permanece disponível.

## English

Experimental prerelease fixing an interpolation path used by certain Knack II
materials on GPUs without native barycentric support, including the GTX 1060
reported in issue #12.

The fallback preserves the data from all three vertices and the interpolation
weights. It is selected automatically for compatible pipelines and does not
depend on the GTX 1060 preset. Native-capability paths retain native support.
Shader cache metadata was versioned for the changed compiler path.

**Recommended test:** extract the Windows ZIP into a new folder, start
`Test GTX 1060.cmd`, keep your previous graphics settings and set the GTX
diagnostic mode to **Off**. Revisit the same road and inspect the asphalt and
building surfaces. No mode 3/4 comparison or additional F11 capture is required.

Validation: isolated build; 2,100 synthetic pixels compared across seven modes;
397 local shaders validated; local gameplay with restricted barycentric
capabilities and the fallback confirmed on 61 pipelines. The road rendered with
color and the session closed normally. **A physical GTX 1060 result is still
pending.** GTX performance, the full campaign and paths outside this scope have
not been certified.

This package retains the RC3 baseline and adds the correction. No game, update,
saves or reporter captures are included. Stable/latest 0.17.5 remains available.
