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
