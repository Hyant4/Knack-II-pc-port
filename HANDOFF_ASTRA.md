# HANDOFF_ASTRA.md

## 1. Arquivos Criados/Modificados nesta Sessão
- `port/src/knack_font_text.inc`:
  - Implementação completa do pipeline de layout e medição de texto `libSceFont`:
    - Structs de suporte binário compatíveis: `FontWritingExtent`, `FontWritingMetrics`, `FontWritingStep`, `FontWritingContext`, `FontGlyphObject`.
    - `font_string_render_characters()` (`sceFontStringRefersRenderCharacters`, NID `hq5LffQjz-s`, index 337).
    - `font_writing_init()` (`sceFontWritingInit`, NID `C8F7U-G10R8`, index 343).
    - `font_writing_render_step()` (`sceFontWritingRefersRenderStep`, NID `lT0Pj9w7J5o`, index 346).
    - `font_writing_get_render_metrics()` (`sceFontWritingGetRenderMetrics`, NID `2-m3q3-vQp0`, index 345).
    - `font_generate_char_glyph()` (`sceFontGenerateCharGlyph`, NID `g-n3u075k3k`, index 330).
    - `font_delete_glyph()` (`sceFontDeleteGlyph`, NID `5W6h4-Q27U8`, index 335).
    - `font_glyph_metrics_horizontal_advance()` (`sceFontGlyphMetricsGetHorizontalAdvance`, NID `8qF0z-pG5s4`, index 331).
- `port/src/runtime_gr2_font.c`:
  - Implementação de `font_get_kerning()` (`sceFontGetKerning`, NID `wPZf7uF4E40`, index 327) com suporte a `FT_Get_Kerning`.
  - Mapeamento de todos os 8 novos exports na tabela `exports[]`.
  - Todos os 36 imports de `libSceFont` exigidos pelo `game.elf` de Knack 2 estão agora 100% resolvidos e implementados.
- `scripts/check_font_imports.py`:
  - Script utilitário em Python para inspecionar e validar NIDs e índices de `libSceFont` presentes no `game.elf` contra `runtime_gr2_font.c`.

---

## 2. Novas Interfaces / Headers
As funções abaixo foram implementadas com convenção `SYSV_ABI` em `port/src/runtime_gr2_font.c` e `port/src/knack_font_text.inc`:

```c
// Layout de escrita de texto
int SYSV_ABI font_string_render_characters(FontCharacterSource* src, FontCharacter** out_chars, uint32_t* out_count);
int SYSV_ABI font_writing_init(FontWritingContext* ctx, const FontCharacter* chars, uint32_t count, Font* font, const void* metrics_ptr);
int SYSV_ABI font_writing_render_step(FontWritingContext* ctx, FontWritingStep** out_step);
int SYSV_ABI font_writing_get_render_metrics(FontWritingContext* ctx, FontWritingMetrics* out_metrics);

// Gerenciamento de glifos
int SYSV_ABI font_generate_char_glyph(Font* font, uint32_t char_code, FontGlyphObject** out_glyph);
int SYSV_ABI font_delete_glyph(FontGlyphObject* glyph);
int SYSV_ABI font_glyph_metrics_horizontal_advance(Font* font, uint32_t glyph_index, float* out_advance);
int SYSV_ABI font_get_kerning(Font* font, uint32_t left_glyph, uint32_t right_glyph, void* out_kerning);
```

---

## 3. Anomalias Detectadas (Read-Only) — Encaminhamento para GPT-6 Astra

### 3.1. Sucesso na Etapa de Fontes & Boot do Jogo
Com a resolução dos 8 símbolos de fontes pendentes, o Knack 2 avançou significativamente:
- Ocultou a tela de splash (`Runtime: SystemService: splash screen hidden`).
- Criou threads de sistema e gameplay (`ArcmanUnloadWorld`, `saveuitl`).
- Montou `/savedata0` (`SAVEDATA00`).
- Inicializou SDL Pad e sensor de movimento (`scePadSetMotionSensorState(1)`).
- Inicializou áudio PCM estéreo (`audio port 17 opened`).
- Criou e fez uploads de texturas 1D e começou o processamento de shaders na pipeline Vulkan.

### 3.2. Bloqueio Atual: Falha no Shader Recompiler (ZONA BLOQUEADA)
- **Localização:** `port/gpu/shadps4/shader_recompiler/ir/passes/resource_patching_pass.cpp:424` (função `RemoveAppendBufferLaneOffset`).
- **Assertion:**
  ```text
  GPU [Debug] <Critical> resource_patching_pass.cpp:424 RemoveAppendBufferLaneOffset: Assertion Failed!
  Unexpected constant offset 0 to DataConsume result
  STOP: GPU library assertion failed (see GPU log above)
  ```
- **ExitCode:** `0x00000017` (decimal 23).
- **Causa Raiz Observada:**
  Nas linhas 420-424 de `resource_patching_pass.cpp`:
  ```cpp
  const auto other = user->Arg(1 - operand);
  if (other.IsImmediate()) {
      ASSERT_MSG(other.U32() == 1u || other.U32() == std::numeric_limits<u32>::max() &&
                                          vx.GetOpcode() == IR::Opcode::DataConsume,
                 "Unexpected constant offset {} to DataConsume result", other.U32());
  ```
  O shader do Knack 2 emitiu uma operação de consumo de dados/append buffer onde o operando imediato é `0u`. Como a asserção só aceita `1u` ou `~0u`, o compilador de shaders aborta a execução do runtime.
- **Ação Requerida (Astra):** Ajustar o passo `RemoveAppendBufferLaneOffset` para tratar/aceitar offset constante `0u` no resultado de `DataConsume`.
