# Diagnóstico opcional GTX 1060 — issue #12

O preset **GTX 1060 black texture** oferece dois experimentos opcionais para
investigar superfícies pretas. Nenhum deles é uma correção gráfica confirmada.
O diagnóstico começa **desligado**. As demais predefinições não o ativam.

## Como comparar

1. Selecione o preset GTX 1060 em **Configurações → Vídeo**.
2. Com diagnóstico **Desligado**, registre a cena e os ajustes atuais.
3. Escolha **1 — Cache novo por sessão**, salve e inicie outra partida. Compare
   a mesma cena e câmera, mantendo resolução, MSAA, readbacks e demais ajustes.
4. Escolha **2 — Cache novo + compilação síncrona**, salve e repita a cena.
   Esse caso acrescenta somente a compilação síncrona ao experimento de cache.
   Pode haver pausas enquanto os shaders ficam prontos.
5. Para retornar ao comportamento habitual, escolha **Desligado** ou outro
   preset. Salve para manter a escolha após fechar o launcher.

Cada partida de diagnóstico cria uma pasta GPU nova em
`<saída>/session-<data-hora>/gtx1060-gpu`. O runtime guarda nela seu cache gráfico
e demais arquivos auxiliares da GPU. Não copia, apaga ou reutiliza o cache
habitual. O perfil de saves continua sendo o selecionado pelo launcher.
Os arquivos de diagnóstico permanecem no disco para inspeção; não há limpeza
automática. Uma nova sessão começa com outra pasta vazia.

A escolha fica em `launcher_settings.json`, separada do INI gráfico. Não modifica
os padrões permanentes de shaders ou exige trocar executáveis/SFO do jogo.
Ao abandonar o preset GTX, o diagnóstico é desativado e não reaparece sozinho
ao selecionar GTX novamente. Metadados inválidos também não o ativam.

## Evidência produzida

O supervisor registra modo e caminho no log. `session.json` inclui
`gtx1060_diagnostic`, com modo, indicador de cache novo e variáveis solicitadas:

- `cache`: somente `TLG_GPU_USER_DIR`, apontando para a pasta privada da sessão;
- `cache_sync`: acrescenta `TLG_ASYNC_SHADERS=0` e
  `TLG_COMPUTE_ASYNC_BUILD=0`.

O log nativo `Config env` permite conferir os valores recebidos. Eles continuam
valendo após a passagem de `eboot.bin` para `game.elf` e no reinício do overlay
Linux. O modo normal não ganha essas variáveis nem metadados de diagnóstico.
Se o menu nativo alterar opções durante a partida, registre isso: os metadados
representam a configuração de início, não uma medição contínua do estado gráfico.

Para uso manual explícito do supervisor:

```powershell
.\.venv\Scripts\python.exe .\run.py --game-dir "D:\Meu dump" --out "D:\Diagnostico Knack\cache" --config "D:\Diagnostico Knack\teste.ini" --gtx1060-diagnostic cache
```

Troque o último valor por `cache_sync` para o segundo caso. O argumento exige
saída explícita e execução normal; é recusado em análise/imports e quando a
saída sobrepõe dump ou dados habituais do repositório. O launcher só o envia
quando o preset GTX e o diagnóstico opcional estão selecionados.

## Limites da implementação

Os testes usam processos e dados sintéticos. Conferem UI, persistência,
isolamento, variáveis, metadados, LoadExec e retorno ao modo habitual. Não
confirmam melhoria na GTX 1060, estabilidade de gameplay, FPS ou ausência de
falhas visuais. A implementação não altera o backend Vulkan, a tradução de
shaders, MSAA ou os hashes de validação.

Esta opção existe nos fontes atualizados; a pré-release RC1 já publicada não
recebe alterações automaticamente.
