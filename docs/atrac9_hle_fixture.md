# HLE-2.1B-FIXTURE: primeira decodificação ATRAC9 real

Data: 2026-10-18. Escopo: extração offline e decodificação isolada. Nenhum arquivo do jogo ou caminho de áudio de produção foi modificado.

## Correspondência comprovada

O evento `VOICE_SEGMENT` observado em `out/knack2-hle21a-segments/session-20261018-061927/01-game.elf.log:2914` informou:

- formato NGS2 `0x40`;
- quatro canais a 48000 Hz;
- `codecConfig = 0xFE7A17F0`;
- 1.447.680 bytes;
- FNV-1a de 64 bits sobre os primeiros 32 bytes: `3ee686a13d32bab6`.

O segmento corresponde ao recurso abaixo:

| Campo | Valor |
| --- | --- |
| Arquivo do dump | `data/0/90.0.apak` |
| Registro CPAK | 21 |
| Membro | `title_amb_sea.bsxd` |
| Offset do membro no CPAK | `0x01B062B0` |
| Tamanho do membro | `0x00161950` = 1.448.272 bytes |
| Chunk WAVE | membro `+0x140`; arquivo `0x01B063F0` |
| Entrada WAVE | membro `+0x154`; arquivo `0x01B06404` |
| Configuração no arquivo | membro `+0x194`; bytes `F0 17 7A FE` |
| Chunk DATA | membro `+0x240`; arquivo `0x01B064F0` |
| Payload ATRAC9 | membro `+0x250`; arquivo `0x01B06500` |
| Tamanho do payload | `0x00161700` = 1.447.680 bytes |

O valor little-endian `F0 17 7A FE` da entrada WAVE representa a configuração canônica `FE 7A 17 F0`. O tamanho do chunk DATA é `0x00161708`, exatamente oito bytes além do payload indicado pela entrada WAVE. O payload contém 1.885 superframes completos de 768 bytes.

O FNV-1a dos primeiros 32 bytes no offset físico `0x01B06500` é `3ee686a13d32bab6`. Esse valor coincide com a observação do endereço guest `0x102d5716c0`. A igualdade liga arquivo, membro SXDF, entrada WAVE, chunk DATA e ponteiro entregue ao NGS2 sem procurar offsets pelo resultado do decoder.

## Extração reproduzível

`scripts/extract_soundx_atrac9_fixture.py` lê a tabela CPAK, seleciona o membro pelo nome e valida:

1. limites do registro e do membro;
2. assinatura `SXDF`;
3. entrada WAVE `0x42`, quatro canais e 48000 Hz;
4. tamanho comprimido de 1.447.680 bytes;
5. configuração `FE7A17F0`;
6. tamanho correspondente do chunk DATA;
7. limites integrais do payload;
8. fingerprint observado no runtime;
9. divisibilidade por 768 bytes.

Comando usado, direcionando a saída para diretório temporário fora do Git:

```powershell
.\.venv\Scripts\python.exe scripts/extract_soundx_atrac9_fixture.py `
  --archive 'C:\CAMINHO_DO_DUMP\data\0\90.0.apak' `
  --member title_amb_sea.bsxd `
  --output "$env:TEMP\knack2-hle21b\title_amb_sea-superframe0.at9" `
  --config FE7A17F0 `
  --channels 4 `
  --segment-size 1447680 `
  --superframe-size 768 `
  --expected-fingerprint 3ee686a13d32bab6
```

O arquivo temporário tem 768 bytes. SHA-256 observado: `63b81e7fc267aee899720248be76cf69849a346b9ab1b4ad94d2e359d046bb2e`. Nenhum byte do payload foi impresso ou adicionado ao repositório.

## Resultado LibAtrac9

Configuração retornada por `Atrac9GetCodecInfo`:

| Campo | Valor |
| --- | ---: |
| Canais | 4 |
| Channel config index | 5 |
| Taxa | 48000 Hz |
| Bytes nominais por frame | 192 |
| Frames por superframe | 4 |
| Bytes por superframe | 768 |
| Amostras por frame e canal | 256 |
| Amostras por superframe e canal | 1024 |

Resultado da primeira decodificação real:

| Medida | Resultado |
| --- | ---: |
| Código do processo | 0 |
| Retorno dos quatro `Atrac9Decode` | 0, 0, 0, 0 |
| `bytesUsed` por frame | 195, 191, 191, 191 |
| Soma de `bytesUsed` | 768 |
| Frames decodificados | 4 |
| Amostras PCM S16 totais | 4096 |
| Amostras por canal | 1024 |
| RMS S16 agregado | 1080,972357 |
| RMS normalizado | 0,032988658 |
| Pico S16 | 3571 |
| Pico normalizado | 0,108978271 |
| Samples fora do intervalo S16 | 0 |

PCM é não nulo. O primeiro frame consumiu 195 bytes e os três seguintes consumiram 191 bytes cada. Portanto, o cursor interno deve seguir `bytesUsed`, mas o salto entre superframes deve respeitar o limite físico de 768 bytes. Não é correto avançar 192 bytes fixos por frame.

## Testes e limites

Testes sintéticos do extrator verificam extração exata e rejeição de payload que ultrapassa o membro CPAK. Testes do harness verificam as duas configurações capturadas, ordem de bytes e rejeição de superframe parcial.

Persistem antes da integração NGS2:

- confirmar comportamento em vários superframes consecutivos;
- validar trim de início/fim, loop e cursor exposto pelo SoundX;
- mapear canais para barramentos e ganhos;
- preservar estado LibAtrac9 por voz;
- proteger inicialização das tabelas globais em execução concorrente;
- validar lifetime e substituição de segmentos de streaming;
- testar configuração de oito canais separadamente.

Resultado do Gate: **GO para experimento de múltiplos superframes; NO-GO para integração ao `SystemRender` até revisão GPT-6 + Argon.**
