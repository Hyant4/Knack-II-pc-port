/* SPDX-License-Identifier: GPL-2.0-or-later
 * Text ABI and parser protocol adapted from shadPS4 e6c79a5 font.h/font.cpp.
 * Copyright 2024 shadPS4 Emulator Project.
 * Initial scope: horizontal Latin strings using the game's original parser.
 */
typedef struct FontCharacter {
    struct FontCharacter *prev,*next;
    void *order;
    Font *font;
    void *shape;
    uint32_t code,pad0;
    uint8_t span,index,kind;
    int8_t synthetic;
    uint32_t pad1;
    uint64_t flags;
    uint8_t reserved[136];
} FontCharacter;
_Static_assert(sizeof(FontCharacter)==200,"Font character ABI");
typedef struct {
    uint16_t magic; uint8_t type,detections; uint32_t options;
    Font *font; void *callback,*object;
} FontStringDetail;
typedef union { void *reserved[4]; struct { Font *font; uint32_t code; } result; } FontParseResult;
typedef int32_t (ABI *FontParser)(FontTextSource *,void **,FontParseResult *);
typedef struct {
    uint16_t magic;
    uint32_t form,count,terminate_code;
    void *terminate_order;
    FontCharacter *characters;
} FontString;
#define ERR_TEXT_SOURCE ((int32_t)0x80460008)
#define ERR_STRING ((int32_t)0x80460009)
static ABI int32_t font_string_create(const FontMemory *memory,FontTextSource *source,
                                      const FontStringDetail *detail,FontString **out) {
    if (!out) return ERR_INVALID_PARAMETER;
    *out=NULL;
    if (!memory || memory->kind!=MEM_MAGIC) return ERR_INVALID_MEMORY;
    if (!source || (source->system&0xffff)!=0xf04 || !source->parser) return ERR_TEXT_SOURCE;
    if (detail && (detail->magic!=0xfd4 || detail->type>1 || (detail->detections&~7) ||
                   (detail->options&~UINT32_C(0x80000007)))) return ERR_INVALID_PARAMETER;
    FontString *string=calloc(1,sizeof(*string));
    if (!string) return ERR_ALLOCATION_FAILED;
    string->magic=0xf05; string->form=(uint32_t)(source->system>>32);
    Font *fallback=source->font ? source->font : detail ? detail->font : NULL;
    int32_t error=ERR_TEXT_SOURCE;
    size_t capacity=0;
    for (size_t i=0;i<4096;++i) {
        FontParseResult result={0}; void *order=NULL;
        int32_t state=((FontParser)source->parser)(source,&order,&result);
        if (!state) {
            string->terminate_code=result.result.code; string->terminate_order=order;
            error=0; break;
        }
        if (state!=1) {
            if (state==-1 && (int32_t)result.result.code<0) error=(int32_t)result.result.code;
            break;
        }
        Font *font=result.result.font ? result.result.font : fallback;
        if (!font_of(font)) break;
        if (string->count==capacity) {
            capacity=capacity ? capacity*2 : 32;
            FontCharacter *characters=realloc(string->characters,capacity*sizeof(*characters));
            if (!characters) { error=ERR_ALLOCATION_FAILED; break; }
            string->characters=characters;
        }
        FontCharacter *c=&string->characters[string->count++];
        memset(c,0,sizeof(*c));
        c->order=order; c->font=font; c->code=result.result.code; c->span=1;
        if (c->code==32 || c->code==9 || c->code==10 || c->code==13 || c->code==0xa0) c->flags=0xe00;
    }
    if (error) { free(string->characters); free(string); return error; }
    for (uint32_t i=0;i<string->count;++i) {
        string->characters[i].prev=i ? &string->characters[i-1] : NULL;
        string->characters[i].next=i+1<string->count ? &string->characters[i+1] : NULL;
    }
    *out=string;
    once("sceFontCreateString: original guest parser, horizontal text layout");
    return FONT_OK;
}
static ABI int32_t font_string_destroy(FontString **pointer) {
    if (!pointer) return ERR_INVALID_PARAMETER;
    FontString *s=*pointer;
    if (!s || s->magic!=0xf05) { *pointer=NULL; return ERR_STRING; }
    free(s->characters); s->magic=0; free(s); *pointer=NULL;
    return FONT_OK;
}
static ABI FontCharacter *font_string_characters(FontString *string,uint32_t *count) {
    if (count) *count=string && string->magic==0xf05 ? string->count : 0;
    return string && string->magic==0xf05 && string->count ? string->characters : NULL;
}
static ABI FontCharacter *font_character_next(FontCharacter *character) {
    return character ? character->next : NULL;
}
static ABI FontCharacter *font_string_render_characters(FontString *string,
                                                       FontCharacter *start,
                                                       FontCharacter *last,
                                                       uint32_t *count) {
    once("sceFontStringRefersRenderCharacters");
    if (count) *count = 0;
    if (!count || !string || string->magic != 0xf05 || !string->count) return NULL;
    uintptr_t begin = (uintptr_t)string->characters;
    uintptr_t end = begin + string->count * sizeof(FontCharacter);
    uintptr_t first = start ? (uintptr_t)start : begin;
    uintptr_t final = last ? (uintptr_t)last : end - sizeof(FontCharacter);
    if (first < begin || first >= end || final < first || final >= end ||
        (first - begin) % sizeof(FontCharacter) || (final - begin) % sizeof(FontCharacter)) return NULL;
    /* Spaces still occupy a writing step. The guest iterates exactly this count;
     * filtering them here truncates the final letters while the iterator includes them. */
    *count = (uint32_t)((final - first) / sizeof(FontCharacter)) + 1;
    return (FontCharacter *)first;
}

/* Writing and glyph layout structures */
typedef struct {
    float top, bottom, left, right;
} FontWritingExtent;

typedef struct {
    float advance_x, advance_y;
    FontWritingExtent extent;
} FontWritingMetrics;

typedef struct {
    float x, y;
    float advance_x, advance_y;
    Font *font;
    uint32_t profile;
    uint32_t glyph_code;
    float positioning_x, positioning_y;
    GlyphMetrics glyph_metrics;
} FontWritingStep;
_Static_assert(sizeof(FontWritingStep) == 0x48, "SceFontWritingStep size");
_Static_assert(offsetof(FontWritingStep, advance_x) == 0x08, "Writing step advance X");
_Static_assert(offsetof(FontWritingStep, glyph_code) == 0x1c, "Writing step glyph code");
_Static_assert(offsetof(FontWritingStep, positioning_x) == 0x20, "Writing step anchor X");
_Static_assert(offsetof(FontWritingStep, positioning_y) == 0x24, "Writing step anchor Y");

typedef struct {
    uint16_t magic; /* 0x0F06 */
    uint16_t mask;
    FontString *string;
    FontCharacter *current;
    float pen_x, pen_y;
    FontWritingMetrics metrics;
    FontWritingStep step;
} FontWritingContext;
_Static_assert(sizeof(FontWritingContext) <= 256, "FontWritingContext fits inside SceFontWriting");

static ABI int32_t font_writing_init(void *writing_buf, FontString *string, FontCharacter *character) {
    once("sceFontWritingInit");
    if (!writing_buf) return ERR_INVALID_PARAMETER;
    memset(writing_buf, 0, 256);
    if (!string || string->magic != 0xf05) return ERR_INVALID_PARAMETER;
    FontWritingContext *ctx = (FontWritingContext *)writing_buf;
    ctx->magic = 0x0f06;
    ctx->string = string;
    ctx->current = character ? character : string->characters;
    ctx->pen_x = 0.0f;
    ctx->pen_y = 0.0f;
    return FONT_OK;
}

static ABI FontWritingStep *font_writing_render_step(void *writing_buf) {
    once("sceFontWritingRefersRenderStep");
    if (!writing_buf) return NULL;
    FontWritingContext *ctx = (FontWritingContext *)writing_buf;
    if (ctx->magic != 0x0f06 || !ctx->current) return NULL;

    while (ctx->current) {
        FontCharacter *c = ctx->current;
        ctx->current = c->next;

        memset(&ctx->step, 0, sizeof(ctx->step));
        ctx->step.x = ctx->pen_x;
        ctx->step.y = ctx->pen_y;
        /* Knack's raster loop at game.elf+0x5f1dc8/+0x5f1e58 consumes
         * Positioning at +0x20/+0x24, not the x/y fields at +0/+4.
         * These are cumulative baseline coordinates in pixels; FreeType's
         * 26.6 metrics have already been converted by fill_metrics(). */
        ctx->step.positioning_x = ctx->pen_x;
        ctx->step.positioning_y = ctx->pen_y;
        ctx->step.font = c->font;
        ctx->step.profile = 1;
        ctx->step.glyph_code = c->code;

        if (c->font) {
            GlyphMetrics gm = {0};
            if (font_get_char_glyph_metrics(c->font, c->code, &gm) == FONT_OK) {
                ctx->step.glyph_metrics = gm;
                ctx->step.advance_x = gm.h_advance;
                /* Vertical glyph metrics do not move a horizontal baseline. */
                ctx->step.advance_y = 0.0f;
            } else {
                ctx->step.advance_x = c->font->scale_w * 0.5f;
            }
        } else {
            ctx->step.advance_x = 16.0f;
        }

        ctx->pen_x += ctx->step.advance_x;
        ctx->pen_y += ctx->step.advance_y;

        ctx->metrics.advance_x = ctx->pen_x;
        ctx->metrics.advance_y = ctx->pen_y;
        if (ctx->pen_x > ctx->metrics.extent.right) ctx->metrics.extent.right = ctx->pen_x;

        return &ctx->step;
    }
    return NULL;
}

static ABI int32_t font_writing_get_render_metrics(void *writing_buf, FontWritingMetrics *metrics) {
    once("sceFontWritingGetRenderMetrics");
    if (!writing_buf || !metrics) return ERR_INVALID_PARAMETER;
    FontWritingContext *ctx = (FontWritingContext *)writing_buf;
    if (ctx->magic != 0x0f06) return ERR_INVALID_PARAMETER;
    *metrics = ctx->metrics;
    return FONT_OK;
}

/* Glyph object structures */
typedef struct {
    uint16_t magic; /* 0x0F03 */
    uint16_t pad;
    uint32_t code;
    Font *font;
    GlyphMetrics metrics;
    float horizontal_advance;
} FontGlyphObject;

static ABI int32_t font_generate_char_glyph(Font *handle, uint32_t code, const void *detail, FontGlyphObject **out) {
    (void)detail;
    once("sceFontGenerateCharGlyph");
    if (!out) return ERR_INVALID_PARAMETER;
    *out = NULL;
    Font *font = font_of(handle);
    if (!font) return ERR_INVALID_FONT_HANDLE;
    FontGlyphObject *obj = calloc(1, sizeof(*obj));
    if (!obj) return ERR_ALLOCATION_FAILED;
    obj->magic = 0x0f03;
    obj->code = code;
    obj->font = font;
    font_get_char_glyph_metrics(font, code, &obj->metrics);
    obj->horizontal_advance = obj->metrics.h_advance;
    *out = obj;
    return FONT_OK;
}

static ABI int32_t font_delete_glyph(const FontMemory *memory, FontGlyphObject **pointer) {
    (void)memory;
    once("sceFontDeleteGlyph");
    if (!pointer) return ERR_INVALID_PARAMETER;
    FontGlyphObject *obj = *pointer;
    if (obj && obj->magic == 0x0f03) {
        obj->magic = 0;
        free(obj);
    }
    *pointer = NULL;
    return FONT_OK;
}

static ABI float *font_glyph_metrics_horizontal_advance(FontGlyphObject *glyph) {
    once("sceFontGlyphRefersMetricsHorizontalAdvance");
    return glyph && glyph->magic == 0x0f03 ? &glyph->horizontal_advance : NULL;
}


