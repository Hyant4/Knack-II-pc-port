# Contrato de escrita de texto observado em Knack 2

Dump: CUSA08014 / 01.01, hash de `game.elf` registrado em `profiles/CUSA08014-01.01.json`.
Análise realizada sobre o executável original, sem patch no código do jogo.

O trecho próximo de `0x5ed0fc` inicializa a escrita e mede a frase. O loop em
`0x5f1db0` consome os passos para desenhar os glifos. O layout existente da struct
já estava correto; os valores usados pelo segundo loop não eram preenchidos.

| Offset em FontWritingStep | Uso observado |
| --- | --- |
| `0x08` | Avanço individual X; somado ao final em `0x5f1ee3` |
| `0x1c` | Código do glifo; lido em `0x5f1dc4` |
| `0x20` | Âncora acumulada X; lida em `0x5f1dc8` e somada ao espaçamento da engine |
| `0x24` | Âncora Y; lida em `0x5f1e58` e somada à linha de base |

`sceFontWritingGetRenderMetrics` fornece o avanço total em `+0x00`, usado pelo
medidor em `0x5ed1bc`. Não se deve colocar o avanço individual em `+0x20`, nem
deixar esse campo zerado para todas as letras.

A correção mantém todos os offsets e tamanhos. `positioning_x` recebe `pen_x`
antes do avanço de cada glifo; `positioning_y` permanece na linha horizontal.
`GlyphMetrics.v_advance` descreve escrita vertical e não deve mover essa linha.
FreeType retorna métricas em 26.6, já convertidas uma vez para pixels por
`fill_metrics`; a escrita não divide essas métricas por 64 novamente.

Espaços contam como passos. Excluí-los da contagem, mas mantê-los na lista
iterada, fazia o guest encerrar o loop antes das últimas letras. A correção
preserva a faixa completa, incluindo espaços e limite final inclusivo.

Referência pública de layout:
[shadPS4 font.h, commit e6c79a5](https://github.com/shadps4-emu/shadPS4/blob/e6c79a5fe0ea2f3d03fcb9c5c50cf61af063f3f5/src/core/libraries/font/font.h).
O consumo dos campos e a escolha da correção foram conferidos no guest.

`tests/test_knack_font.c` executa a implementação real com FreeType e a fonte
DejaVuSans redistribuída no projeto. Lê os campos pelos offsets consumidos pelo
guest, verifica toda a frase `Press Any Button`, espaços, linha horizontal e
escalas 32/64 pixels. `tests/test_font.py` compila e executa o teste no Windows.

Validação visual local: `out/knack2/font-gameplay/title-01.png`, decodificado sem
reescalonamento de `frames/output_001_1704x959.raw` (RGBA). O prompt completo
aparece legível; os glifos deixaram de compartilhar a mesma posição.

Este suporte continua limitado ao caminho horizontal observado. Shaping
complexo, bidi completo e escrita vertical não foram validados.
