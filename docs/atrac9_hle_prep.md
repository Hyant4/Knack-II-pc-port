# HLE-2.1B-PREP: contrato ATRAC9 isolado

Data: 2026-10-18. Escopo: inspeção e harness independente; nenhum caminho de áudio de produção foi alterado.

## API reutilizada

O harness usa diretamente a API C já vendorizada em `port/third_party/LibAtrac9/C/src/libatrac9.h`:

1. `Atrac9GetHandle()` / `Atrac9ReleaseHandle()` gerenciam o decoder.
2. `Atrac9InitDecoder(handle, config[4])` inicializa a configuração.
3. `Atrac9GetCodecInfo(handle, &info)` fornece canais, taxa, tamanho do superframe, frames por superframe e amostras por frame.
4. `Atrac9Decode(handle, input, pcm, &bytesUsed)` decodifica um frame para PCM S16 intercalado.

`Atrac9Decode` não recebe o tamanho disponível. O chamador deve provar previamente que há pelo menos um superframe completo. O harness exige isso antes da primeira chamada e limita a execução a um superframe por padrão.

## Configuração de quatro bytes

LibAtrac9 lê os bits em ordem MSB-first. Os quatro bytes não devem ser passados por `memcpy` de um `uint32_t` host sem normalização de ordem:

| Bits | Campo |
| --- | --- |
| 0..7 | cabeçalho `0xFE` |
| 8..11 | índice da taxa de amostragem |
| 12..14 | índice da configuração de canais |
| 15 | bit de validação, deve ser zero |
| 16..26 | bytes por frame menos um |
| 27..28 | índice do superframe; frames = `1 << índice` |
| 29..31 | não consumidos pelo parser atual |

Configurações observadas no diagnóstico HLE-2.1A e verificadas pela API real:

| Bytes canônicos | Canais | Taxa | Bytes/frame | Frames/superframe | Bytes/superframe | Amostras/frame/canal |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `FE 7A 17 F0` | 4 | 48000 Hz | 192 | 4 | 768 | 256 |
| `FE 78 29 F0` | 8 | 48000 Hz | 336 | 4 | 1344 | 256 |

Cada superframe produz 1024 amostras por canal. Para o valor logado como `codecConfig = 0xFE7A17F0`, o array entregue à biblioteca deve ser `{0xFE, 0x7A, 0x17, 0xF0}`. A representação little-endian do inteiro (`F0 17 7A FE`) é inválida para o parser.

## Harness

`tests/atrac9_decode_harness.c` aceita bytes ATRAC9 crus, não RIFF e não bancos `.bsxd`. Sem `--input`, somente inicializa o decoder e imprime metadados. Com entrada válida, decodifica em memória e informa bytes avançados no fluxo, bytes consumidos pelo decoder, frames, amostras, RMS e pico. Não grava PCM e não reproduz áudio.

Exemplo após compilar o harness:

```text
atrac9-decode-harness.exe --config FE7A17F0 --input C:\temp\authorized.raw.at9 --max-superframes 1
```

`tests/test_atrac9_decode_harness.py` compila o harness e as fontes vendorizadas em diretório temporário. Nenhum `CMakeLists.txt`, `build.ps1`, archive versionado ou build global é alterado.

## Fixture pendente

O repositório não contém fixture ATRAC9 autorizada. O dump do jogo contém recursos empacotados, mas nenhum arquivo ATRAC9 cru foi identificado como fixture independente. Portanto, nenhuma decodificação foi declarada nesta etapa.

Dado mínimo necessário para o próximo ensaio: um superframe ATRAC9 cru, autorizado, de 768 bytes que corresponda exatamente a `FE7A17F0`; alternativamente, 1344 bytes para `FE7829F0`. A origem e o offset devem ser documentados fora do Git. Um recorte de recurso proprietário não deve ser adicionado ao repositório.

## Integração futura mínima

Após fixture e revisão GO, NGS2 deverá manter um decoder por voz, converter explicitamente o `codecConfig` para os quatro bytes canônicos, exigir superframe completo antes de chamar a biblioteca e conservar estado entre frames. PCM S16 intercalado poderá ser acumulado em fila por voz; mixer posterior deverá aplicar roteamento/ganho e converter ao layout de saída de `SystemRender`.

Pendências antes da integração:

- validar um superframe real e registrar erro/sucesso, bytes usados, amostras, RMS e pico;
- confirmar se `VOICE_SEGMENT` inicia sempre em limite de superframe;
- confirmar trim/loop e lifetime dos segmentos;
- mapear canais 4/8 para barramentos do mixer;
- definir cursor por bytes avançados no superframe, não somente por `bytesUsed` retornado por frame;
- manter `SystemRender` e pipeline de produção inalterados até revisão GPT-6 + Argon.
