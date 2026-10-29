# Gate HLE-2.1C — decodificação ATRAC9 contínua

Data: 2026-10-18

Executor: GPT-5.6 Sol

Branch/HEAD inicial: `knack2-windows` / `5664a07`

## Escopo

Validação offline. Nenhum arquivo em `port/src/**`, `port/third_party/**`, build system, jogo ou save foi modificado. Fixture proprietária permaneceu em diretório temporário fora do Git; somente hashes e métricas são registrados.

## Origem e extração

- Arquivo lógico: `CUSA08014/data/0/90.0.apak`.
- CPAK record: 21.
- Membro: `title_amb_sea.bsxd`, offset `0x1B062B0`, tamanho `1.448.272` bytes.
- WAVE: `0x1B063F0`; entrada 0: `0x1B06404`.
- Configuração: campo `0x1B06444`, bytes armazenados `F0 17 7A FE`, ordem canônica `FE7A17F0`.
- DATA: `0x1B064F0`; payload ATRAC9: `0x1B06500`.
- Segmento: `1.447.680` bytes = 1.885 superframes de 768 bytes.
- Fingerprint FNV-1a dos primeiros 32 bytes: `3ee686a13d32bab6`.
- Extração deste gate: primeiros 8 superframes completos, 6.144 bytes, SHA-256 `ba5385f8a3a3676d49dccf87eda048c5966ba39f978729349d0bb54d6b9f1516`.

Comando reproduzível, usando dump legítimo local:

```powershell
.\.venv\Scripts\python.exe scripts\extract_soundx_atrac9_fixture.py `
  --archive <OWN_GAME_ARCHIVE> --member title_amb_sea.bsxd `
  --output <TEMP>\title_amb_sea-8sf.at9 `
  --config FE7A17F0 --channels 4 --segment-size 1447680 `
  --superframe-size 768 --superframes 8 `
  --expected-fingerprint 3ee686a13d32bab6
```

Extrator valida CPAK, membro SXDF, WAVE, formato `0x42`, canais, 48 kHz, tamanho comprimido, configuração, DATA, fingerprint e todos os limites antes de escrever. Saída existente não é sobrescrita.

## Configuração confirmada pela LibAtrac9

- 4 canais.
- 48.000 Hz.
- 4 frames por superframe.
- 256 amostras por canal por frame.
- 1.024 amostras por canal por superframe.
- 768 bytes por superframe.
- `768 / 4 = 192` é somente média. Consumo real observado por superframe: `195, 191, 191, 191` bytes. Integração não deve usar stride fixo de 192 bytes.

## Resultados reais

Cada linha abaixo usou um único handle mantido entre todas as chamadas, exceto linhas `reset`. Todos os retornos de `Atrac9Decode` foram zero. Cada superframe consumiu exatamente 768 bytes; nenhum valor S16 ficou fora do tipo representável.

| Modo | Superframes | Frames | Bytes | Amostras S16 totais | Amostras/canal | RMS S16 | Pico S16 | FNV-1a PCM |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| contínuo | 2 | 8 | 1.536 | 8.192 | 2.048 | 1161,414095 | 3813 | `2c10d0e52a0c4fa6` |
| reset por superframe | 2 | 8 | 1.536 | 8.192 | 2.048 | 1117,617521 | 3813 | `0acef43e8919ebe8` |
| contínuo | 4 | 16 | 3.072 | 16.384 | 4.096 | 1342,613737 | 4116 | `4e641f80a958e28c` |
| reset por superframe | 4 | 16 | 3.072 | 16.384 | 4.096 | 1209,917082 | 3813 | `c588c899118c107f` |
| contínuo | 8 | 32 | 6.144 | 32.768 | 8.192 | 1287,705937 | 4116 | `a79abed856a64db5` |
| reset por superframe | 8 | 32 | 6.144 | 32.768 | 8.192 | 1166,251071 | 3813 | `12e9689348a12f70` |

No caso contínuo de 8 superframes:

- 32/32 frames: retorno zero, limite válido, 1.024 samples S16 intercalados por chamada e `s16_out_of_range=0`.
- RMS por frame: 51,110672 a 1975,077246. Pico por frame: 235 a 4116.
- Posições acumuladas: `195, 386, 577, 768, 963, 1154, 1345, 1536, 1731, 1922, 2113, 2304, 2499, 2690, 2881, 3072, 3267, 3458, 3649, 3840, 4035, 4226, 4417, 4608, 4803, 4994, 5185, 5376, 5571, 5762, 5953, 6144`.
- Soma por superframe: `768, 768, 768, 768, 768, 768, 768, 768`.

## Estado e fronteiras

Diferença entre última amostra do superframe anterior e primeira do próximo, medida por canal:

| Modo, 8 superframes | Máximo absoluto entre fronteiras | Maior RMS de fronteira |
|---|---:|---:|
| contínuo | 79 | 50,094910 |
| reset por superframe | 2812 | 1845,607149 |

Reset começa a divergir no segundo superframe: hash, RMS, pico e saltos de fronteira mudam. Evidência confirma estado histórico necessário para saída contínua correta. Métrica de uma amostra por canal é detector simples de salto; não prova sozinha ausência de artefato audível.

Sequência contínua de 8 superframes repetida em novo processo produziu novamente `a79abed856a64db5`. Saída determinística no caso medido.

Dois handles foram inicializados antes da decodificação e alimentados alternadamente com mesmos 8 superframes. Retorno, `bytesUsed` e todo PCM compararam iguais frame a frame (`dual_handle_match=1`). Isso comprova independência serial dos handles; execução concorrente em threads não foi testada.

## Buffer parcial e alocações

Arquivo temporário de 1.000 bytes continha um superframe completo e 232 bytes seguintes. Harness decodificou somente primeiro superframe, depois rejeitou `232 != 768` com exit 6, sem chamar LibAtrac9 para região parcial. Resultado: 4 frames, 768 bytes usados e avançados. Isto valida proteção do harness, não comportamento interno do decoder diante de entrada truncada.

Harness contínuo aloca buffer comprimido uma vez antes do loop e usa buffers PCM de tamanho fixo na stack. LibAtrac9 aloca somente `Atrac9Handle` em `Atrac9GetHandle` e libera em `Atrac9ReleaseHandle`; `Atrac9Decode` não contém alocação dinâmica. Modo `reset` realoca handle por desenho experimental; modo contínuo não.

## Testes automatizados

```text
.\.venv\Scripts\python.exe -m unittest tests.test_atrac9_decode_harness tests.test_extract_soundx_atrac9_fixture -v

9 tests; OK
```

Cobertura: configurações capturadas de 4/8 canais, ordem dos bytes, buffer truncado, dois handles, flags incompatíveis, extração de um/dois superframes e limite de membro truncado. Dados sintéticos exercitam apenas parsing/limites; resultados PCM vêm exclusivamente da fixture real temporária.

## Conclusão e riscos

**GO para planejamento da integração NGS2 ATRAC9; NO-GO para integrar automaticamente.** Decodificação sequencial, estado, consumo, determinismo, truncamento e handles independentes possuem evidência suficiente para desenhar fila e cursor.

Riscos restantes:

- Lifetime e concorrência de buffers guest ainda exigem contrato de cópia/pinning.
- Thread safety simultânea da LibAtrac9 não foi exercitada.
- 8 superframes de um único recurso não cobrem outras configurações, corrupção, seek, loop ou streaming.
- Estado NGS2 por voz deve possuir handle próprio; reset por segmento quebra continuidade.
- Cursor deve avançar por `bytesUsed` de cada frame e validar fechamento exato no limite do superframe; nunca assumir 192 bytes.
- Pacing, mixagem multicanal, ganho, clipping, FINISHED e descarte de voz permanecem fora deste gate.
