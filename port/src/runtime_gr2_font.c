/* Gravity Rush 2: libSceFont / libSceFontFt (glyph rendering with FreeType).
 *
 * The game opens its own font files from memory (sceFontOpenFontMemory), measures glyphs
 * with sceFontGetCharGlyphMetrics and rasterizes them one by one into an 8-bit coverage
 * atlas with sceFontRenderCharGlyphImageHorizontal. Everything goes through one FreeType
 * library behind one lock: FreeType faces are not thread-safe and glyph rendering is rare
 * (the game caches glyphs in its atlas).
 *
 * Units: scales are pixels per em (FreeType char size at 72 dpi); all metrics are float
 * pixels, y up for bearings; render coordinates are surface pixels, y down, (x, y) being the
 * pen position on the baseline. Layouts follow the PS4 SDK as reconstructed by shadPS4
 * (src/core/libraries/font) and checked against the game's call sites.
 */
#define _GNU_SOURCE
#include "runtime.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#define FONT_OK 0
#define ERR_FATAL ((int32_t)0x80460001)
#define ERR_INVALID_PARAMETER ((int32_t)0x80460002)
#define ERR_INVALID_MEMORY ((int32_t)0x80460003)
#define ERR_INVALID_LIBRARY ((int32_t)0x80460004)
#define ERR_INVALID_FONT_HANDLE ((int32_t)0x80460005)
#define ERR_INVALID_RENDERER ((int32_t)0x80460007)
#define ERR_ALLOCATION_FAILED ((int32_t)0x80460010)
#define ERR_NO_SUPPORT_LIBRARY ((int32_t)0x80460018)
#define ERR_FONT_OPEN_FAILED ((int32_t)0x80460036)
#define ERR_NO_SUPPORT_CODE ((int32_t)0x80460041)
#define ERR_NO_SUPPORT_GLYPH ((int32_t)0x80460042)
#define ERR_NO_SUPPORT_SURFACE ((int32_t)0x80460050)
#define ERR_ALREADY_BOUND_RENDERER ((int32_t)0x80460060)
#define ERR_NOT_BOUND_RENDERER ((int32_t)0x80460061)

#define MEM_MAGIC 0x0F00
#define LIBRARY_MAGIC 0x0F01
#define FONT_MAGIC 0x0F02
#define RENDERER_MAGIC 0x0F07

/* ---- Guest structures ---- */
/* SceFontMemory (sceFontMemoryInit fills it; the game allocates 64 bytes). */
typedef struct {
    uint16_t kind, attributes;
    uint32_t region_size;
    void *region, *mspace;
    const void *interface;
    void *destroy_callback, *destroy_object;
    void *reserved, *parent;
} FontMemory;
typedef struct { float width, height, h_bearing_x, h_bearing_y, h_advance, v_bearing_x, v_bearing_y, v_advance; } GlyphMetrics;
typedef struct { float baseline_offset, line_advance, decoration_extent; } HorizontalLayout;
typedef struct { float offset_x, offset_y, position_x, position_y; } Kerning;
typedef struct {
    void *buffer;
    int32_t width_byte;
    int8_t pixel_size_byte;
    uint8_t pad0, style_flag, pad2;
    int32_t width, height;
    uint32_t scissor_x0, scissor_y0, scissor_x1, scissor_y1;
    uint64_t reserved[11];
} RenderSurface;
typedef struct {
    const void *stage;
    struct { uint8_t *address; uint32_t width_byte; uint8_t pixel_size_byte, pixel_format; uint16_t pad; } image;
    struct { uint32_t x, y, w, h; } update;
    struct { float bearing_x, bearing_y, advance, stride; uint32_t width, height; } metrics;
} RenderOutput;
typedef struct { uint16_t tag, pad; uint32_t flags, subfont_index; int32_t unique_id; const void *reserved[2]; } OpenParams;
/* What sceFontSelectLibraryFt / sceFontSelectRendererFt return: opaque driver tables the
 * game hands back to the Create functions unchanged. */
typedef struct { uint32_t magic, reserved; void *reserved_ptr; uintptr_t functions[4]; } LibrarySelection;
typedef struct { uint32_t magic, size; uintptr_t functions[3]; } RendererSelection;
_Static_assert(sizeof(FontMemory)==64,"SceFontMemory");
_Static_assert(sizeof(GlyphMetrics)==32,"SceFontGlyphMetrics");
_Static_assert(sizeof(HorizontalLayout)==12,"SceFontHorizontalLayout");
_Static_assert(sizeof(Kerning)==16,"SceFontKerning");
_Static_assert(sizeof(RenderSurface)==128,"SceFontRenderSurface");
_Static_assert(sizeof(RenderOutput)==64,"SceFontRenderOutput");
_Static_assert(sizeof(OpenParams)==32,"SceFontOpenDetail");

/* ---- Host objects behind the guest handles ---- */
typedef struct { uint16_t magic; const FontMemory *memory; uint64_t edition; int fonts; uint32_t open_mode; int system_fonts; uint32_t cache_size; } Library;
typedef struct { uint16_t magic; const FontMemory *memory; uint64_t edition; } Renderer;
typedef struct {
    uint16_t magic;
    Library *library;
    Renderer *renderer;
    FT_Face face;
    void *owned_data;               /* host font replacement; NULL for guest-owned memory */
    float scale_w, scale_h;          /* sceFontSetScalePixel: metrics and layout */
    float render_w, render_h;        /* sceFontSetupRenderScalePixel: rendering */
    FT_F26Dot6 size_w, size_h;       /* char size currently set on the face */
    uint8_t guest_reserved[256];     /* the PS4 handle is a 256-byte object */
} Font;

static const LibrarySelection library_ft={0x53464C46u /* "FLFS" */,0,NULL,{0,0,0,0}};
static const RendererSelection renderer_ft={0x53524646u /* "FFRS" */,sizeof(RendererSelection),{0,0,0}};
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static FT_Library freetype; /* created with the first library, kept for the process lifetime */

static void once(const char *what) {
    static const char *seen[48];
    static int count;
    static pthread_mutex_t seen_lock=PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&seen_lock);
    for (int i=0;i<count;++i) if (seen[i]==what) { pthread_mutex_unlock(&seen_lock); return; }
    if (count<48) seen[count++]=what;
    pthread_mutex_unlock(&seen_lock);
    printf("Font: %s\n",what);
}

static Font *font_of(void *handle) {
    Font *font=handle;
    return font && font->magic==FONT_MAGIC && font->face ? font : NULL;
}

/* Char size in 26.6 at 72 dpi, so one em is `w` x `h` pixels. Called with the lock held. */
static int set_size(Font *font, float w, float h) {
    if (!(w>0.0f) || !(h>0.0f) || w>4096.0f || h>4096.0f) return -1;
    FT_F26Dot6 cw=(FT_F26Dot6)lrintf(w*64.0f), ch=(FT_F26Dot6)lrintf(h*64.0f);
    if (cw==font->size_w && ch==font->size_h) return 0;
    if (FT_Set_Char_Size(font->face,cw,ch,72,72)) { font->size_w=font->size_h=0; return -1; }
    font->size_w=cw; font->size_h=ch;
    return 0;
}

static int load_glyph(Font *font, uint32_t code, int32_t *error) {
    FT_UInt index=FT_Get_Char_Index(font->face,code);
    if (!index) { *error=ERR_NO_SUPPORT_GLYPH; return -1; }
    if (FT_Load_Glyph(font->face,index,FT_LOAD_NO_HINTING|FT_LOAD_NO_BITMAP)) { *error=ERR_NO_SUPPORT_GLYPH; return -1; }
    return 0;
}

static void fill_metrics(const FT_Glyph_Metrics *m, GlyphMetrics *out) {
    out->width=m->width/64.0f; out->height=m->height/64.0f;
    out->h_bearing_x=m->horiBearingX/64.0f; out->h_bearing_y=m->horiBearingY/64.0f; out->h_advance=m->horiAdvance/64.0f;
    out->v_bearing_x=m->vertBearingX/64.0f; out->v_bearing_y=m->vertBearingY/64.0f; out->v_advance=m->vertAdvance/64.0f;
}

static int64_t floor_div64(int64_t v) { return v>=0 ? v/64 : -((-v+63)/64); }

/* ---- Memory, library, renderer ---- */
static ABI int32_t font_memory_init(FontMemory *memory, void *region, uint32_t size, const void *interface,
                                    void *mspace, void *destroy_callback, void *destroy_object) {
    once("sceFontMemoryInit");
    if (!memory) return ERR_INVALID_PARAMETER;
    if (!interface && (!region || !size)) { memory->kind=0; return ERR_INVALID_PARAMETER; }
    memory->kind=MEM_MAGIC; memory->attributes=0; memory->region_size=size; memory->region=region;
    memory->mspace=mspace; memory->interface=interface; memory->destroy_callback=destroy_callback;
    memory->destroy_object=destroy_object; memory->reserved=NULL; memory->parent=mspace;
    return FONT_OK;
}

static ABI int32_t font_memory_term(FontMemory *memory) {
    once("sceFontMemoryTerm");
    if (!memory) return ERR_INVALID_PARAMETER;
    if (memory->kind!=MEM_MAGIC) return ERR_INVALID_MEMORY;
    if (memory->destroy_callback) {
        typedef void (ABI *Destroy)(FontMemory *, void *, void *);
        Destroy destroy=(Destroy)memory->destroy_callback;
        memory->kind=0;
        destroy(memory,memory->mspace,memory->destroy_object);
        return FONT_OK;
    }
    memset(memory,0,sizeof(*memory));
    return FONT_OK;
}

static ABI const LibrarySelection *font_select_library_ft(int32_t value) {
    (void)value; once("sceFontSelectLibraryFt"); return &library_ft;
}
static ABI const RendererSelection *font_select_renderer_ft(int32_t value) {
    (void)value; once("sceFontSelectRendererFt"); return &renderer_ft;
}

static ABI int32_t font_create_library(const FontMemory *memory, const void *selection, uint64_t edition, Library **out) {
    once("sceFontCreateLibraryWithEdition");
    if (!out) return ERR_INVALID_PARAMETER;
    *out=NULL;
    if (!memory || memory->kind!=MEM_MAGIC) return ERR_INVALID_MEMORY;
    if (!selection) return ERR_NO_SUPPORT_LIBRARY;
    Library *library=calloc(1,sizeof(*library));
    if (!library) return ERR_ALLOCATION_FAILED;
    pthread_mutex_lock(&lock);
    if (!freetype && FT_Init_FreeType(&freetype)) {
        freetype=NULL;
        pthread_mutex_unlock(&lock); free(library);
        fprintf(stderr,"Font: FreeType initialization failed\n");
        return ERR_FATAL;
    }
    pthread_mutex_unlock(&lock);
    library->magic=LIBRARY_MAGIC; library->memory=memory; library->edition=edition;
    *out=library;
    int major=0,minor=0,patch=0;
    FT_Library_Version(freetype,&major,&minor,&patch);
    printf("Font: library created (edition %#llx, FreeType %d.%d.%d)\n",(unsigned long long)edition,major,minor,patch);
    return FONT_OK;
}

static ABI int32_t font_destroy_library(Library **pointer) {
    once("sceFontDestroyLibrary");
    if (!pointer) return ERR_INVALID_PARAMETER;
    Library *library=*pointer;
    if (!library || library->magic!=LIBRARY_MAGIC) return ERR_INVALID_LIBRARY;
    pthread_mutex_lock(&lock);
    library->magic=0;
    /* Fonts still open keep using it; the last sceFontCloseFont frees it. */
    int release=!library->fonts;
    pthread_mutex_unlock(&lock);
    if (release) free(library);
    *pointer=NULL;
    return FONT_OK;
}

static ABI int32_t font_support_external_fonts(Library *library, uint32_t max_fonts, uint32_t formats) {
    once("sceFontSupportExternalFonts");
    if (!library || library->magic!=LIBRARY_MAGIC) return ERR_INVALID_LIBRARY;
    printf("Font: external fonts supported (max %u, formats %#x)\n",max_fonts,formats);
    return FONT_OK;
}

static ABI int32_t font_create_renderer(const FontMemory *memory, const void *selection, uint64_t edition, Renderer **out) {
    once("sceFontCreateRendererWithEdition");
    if (!out) return ERR_INVALID_PARAMETER;
    *out=NULL;
    if (!memory || memory->kind!=MEM_MAGIC) return ERR_INVALID_MEMORY;
    if (!selection) return ERR_NO_SUPPORT_LIBRARY;
    Renderer *renderer=calloc(1,sizeof(*renderer));
    if (!renderer) return ERR_ALLOCATION_FAILED;
    renderer->magic=RENDERER_MAGIC; renderer->memory=memory; renderer->edition=edition;
    *out=renderer;
    return FONT_OK;
}

static ABI int32_t font_destroy_renderer(Renderer **pointer) {
    once("sceFontDestroyRenderer");
    if (!pointer) return ERR_INVALID_PARAMETER;
    Renderer *renderer=*pointer;
    if (!renderer || renderer->magic!=RENDERER_MAGIC) return ERR_INVALID_RENDERER;
    renderer->magic=0;
    /* Fonts may still point at it (PS4 leaves that to the caller); keep the block alive. */
    *pointer=NULL;
    return FONT_OK;
}

/* Probably (library, uint32_t *resolution); the game passes an 8-byte global it never reads. */
static ABI int32_t font_get_pixel_resolution(Library *library, uint32_t *resolution) {
    (void)library; once("sceFontGetPixelResolution");
    if (resolution) *resolution=72;
    return FONT_OK;
}

/* ---- Fonts ---- */
static ABI int32_t font_open_memory(Library *library, const void *data, uint32_t size, const OpenParams *params, Font **out) {
    once("sceFontOpenFontMemory");
    if (!library || library->magic!=LIBRARY_MAGIC) { if (out) *out=NULL; return ERR_INVALID_LIBRARY; }
    if (!data || !size || !out) { if (out) *out=NULL; return ERR_INVALID_PARAMETER; }
    *out=NULL;
    Font *font=calloc(1,sizeof(*font));
    if (!font) return ERR_ALLOCATION_FAILED;
    FT_Long index=params ? (FT_Long)params->subfont_index : 0;
    pthread_mutex_lock(&lock);
    /* The data stays owned by the guest, which keeps it mapped while the font is open. */
    FT_Error error=FT_New_Memory_Face(freetype,data,size,index,&font->face);
    if (error) {
        pthread_mutex_unlock(&lock); free(font);
        fprintf(stderr,"Font: open failed (%u bytes at %p, FreeType error %d)\n",size,data,error);
        return ERR_FONT_OPEN_FAILED;
    }
    if (!font->face->charmap) FT_Select_Charmap(font->face,FT_ENCODING_UNICODE);
    float scale=font->face->units_per_EM ? font->face->units_per_EM/64.0f : 16.0f;
    font->magic=FONT_MAGIC; font->library=library;
    font->scale_w=font->scale_h=font->render_w=font->render_h=scale;
    ++library->fonts;
    printf("Font: opened %s %s (%u bytes, %ld glyphs, %u units/em)\n",
           font->face->family_name ? font->face->family_name : "?",
           font->face->style_name ? font->face->style_name : "",
           size,(long)font->face->num_glyphs,font->face->units_per_EM);
    pthread_mutex_unlock(&lock);
    *out=font;
    return FONT_OK;
}

static ABI int32_t font_close(Font *handle) {
    once("sceFontCloseFont");
    pthread_mutex_lock(&lock);
    Font *font=font_of(handle);
    if (!font) { pthread_mutex_unlock(&lock); return ERR_INVALID_FONT_HANDLE; }
    FT_Done_Face(font->face);
    free(font->owned_data);
    font->face=NULL; font->magic=0;
    Library *library=font->library;
    int release=0;
    if (library && --library->fonts==0 && library->magic!=LIBRARY_MAGIC) release=1;
    pthread_mutex_unlock(&lock);
    if (release) free(library);
    free(font);
    return FONT_OK;
}

static ABI int32_t font_bind_renderer(Font *handle, Renderer *renderer) {
    once("sceFontBindRenderer");
    pthread_mutex_lock(&lock);
    Font *font=font_of(handle);
    int32_t result=FONT_OK;
    if (!font) result=ERR_INVALID_FONT_HANDLE;
    else if (font->renderer) result=ERR_ALREADY_BOUND_RENDERER;
    else if (!renderer || renderer->magic!=RENDERER_MAGIC) result=ERR_INVALID_RENDERER;
    else {
        /* Binding starts the render style from the font's current one. */
        font->renderer=renderer; font->render_w=font->scale_w; font->render_h=font->scale_h;
    }
    pthread_mutex_unlock(&lock);
    return result;
}

static ABI int32_t font_unbind_renderer(Font *handle) {
    once("sceFontUnbindRenderer");
    pthread_mutex_lock(&lock);
    Font *font=font_of(handle);
    int32_t result=!font ? ERR_INVALID_FONT_HANDLE : !font->renderer ? ERR_NOT_BOUND_RENDERER : FONT_OK;
    if (result==FONT_OK) font->renderer=NULL;
    pthread_mutex_unlock(&lock);
    return result;
}

static ABI int32_t font_set_scale_pixel(Font *handle, float w, float h) {
    once("sceFontSetScalePixel");
    pthread_mutex_lock(&lock);
    Font *font=font_of(handle);
    if (font) { font->scale_w=w; font->scale_h=h; }
    pthread_mutex_unlock(&lock);
    return font ? FONT_OK : ERR_INVALID_FONT_HANDLE;
}

static ABI int32_t font_setup_render_scale_pixel(Font *handle, float w, float h) {
    once("sceFontSetupRenderScalePixel");
    pthread_mutex_lock(&lock);
    Font *font=font_of(handle);
    int32_t result=!font ? ERR_INVALID_FONT_HANDLE : !font->renderer ? ERR_NOT_BOUND_RENDERER : FONT_OK;
    if (result==FONT_OK) { font->render_w=w; font->render_h=h; }
    pthread_mutex_unlock(&lock);
    return result;
}

/* Baseline and line height from the font bounding box (head.yMax / yMin), at the font scale.
 * decorationExtent only grows with weight effects, which the game never sets. */
static ABI int32_t font_get_horizontal_layout(Font *handle, HorizontalLayout *layout) {
    once("sceFontGetHorizontalLayout");
    pthread_mutex_lock(&lock);
    Font *font=font_of(handle);
    int32_t result=FONT_OK;
    if (!font) result=ERR_INVALID_FONT_HANDLE;
    else if (!layout) result=ERR_INVALID_PARAMETER;
    else if (set_size(font,font->scale_w,font->scale_h)) result=ERR_INVALID_PARAMETER;
    else {
        FT_Fixed y_scale=font->face->size->metrics.y_scale;
        FT_Long top=FT_MulFix(font->face->bbox.yMax,y_scale), bottom=FT_MulFix(font->face->bbox.yMin,y_scale);
        if (!FT_IS_SCALABLE(font->face)) {
            top=font->face->size->metrics.ascender; bottom=font->face->size->metrics.descender;
        }
        layout->baseline_offset=top/64.0f;
        layout->line_advance=(top-bottom)/64.0f;
        layout->decoration_extent=0.0f;
    }
    if (result!=FONT_OK && layout) memset(layout,0,sizeof(*layout));
    pthread_mutex_unlock(&lock);
    return result;
}

static ABI int32_t font_get_char_glyph_metrics(Font *handle, uint32_t code, GlyphMetrics *metrics) {
    once("sceFontGetCharGlyphMetrics");
    if (!metrics) return ERR_INVALID_PARAMETER;
    memset(metrics,0,sizeof(*metrics));
    if (!code) return ERR_NO_SUPPORT_CODE;
    pthread_mutex_lock(&lock);
    Font *font=font_of(handle);
    int32_t result=FONT_OK;
    if (!font) result=ERR_INVALID_FONT_HANDLE;
    else if (set_size(font,font->scale_w,font->scale_h)) result=ERR_INVALID_PARAMETER;
    else if (!load_glyph(font,code,&result)) fill_metrics(&font->face->glyph->metrics,metrics);
    pthread_mutex_unlock(&lock);
    return result;
}

/* (font, previous code, code, kerning) at the render scale; zero without a 'kern' pair. */
static ABI int32_t font_get_render_scaled_kerning(Font *handle, uint32_t previous, uint32_t code, Kerning *kerning) {
    once("sceFontGetRenderScaledKerning");
    if (!kerning) return ERR_INVALID_PARAMETER;
    memset(kerning,0,sizeof(*kerning));
    pthread_mutex_lock(&lock);
    Font *font=font_of(handle);
    int32_t result=FONT_OK;
    if (!font) result=ERR_INVALID_FONT_HANDLE;
    else if (!font->renderer) result=ERR_NOT_BOUND_RENDERER;
    else if (previous && code && FT_HAS_KERNING(font->face) && !set_size(font,font->render_w,font->render_h)) {
        FT_UInt left=FT_Get_Char_Index(font->face,previous), right=FT_Get_Char_Index(font->face,code);
        FT_Vector delta={0,0};
        if (left && right && !FT_Get_Kerning(font->face,left,right,FT_KERNING_UNFITTED,&delta)) {
            kerning->offset_x=delta.x/64.0f;
            kerning->offset_y=delta.y/64.0f;
        }
    }
    pthread_mutex_unlock(&lock);
    return result;
}

static ABI int32_t font_get_kerning(Font *handle, uint32_t previous, uint32_t code, Kerning *kerning) {
    once("sceFontGetKerning");
    if (!kerning) return ERR_INVALID_PARAMETER;
    memset(kerning,0,sizeof(*kerning));
    pthread_mutex_lock(&lock);
    Font *font=font_of(handle);
    int32_t result=FONT_OK;
    if (!font) result=ERR_INVALID_FONT_HANDLE;
    else if (previous && code && FT_HAS_KERNING(font->face) && !set_size(font,font->scale_w,font->scale_h)) {
        FT_UInt left=FT_Get_Char_Index(font->face,previous), right=FT_Get_Char_Index(font->face,code);
        FT_Vector delta={0,0};
        if (left && right && !FT_Get_Kerning(font->face,left,right,FT_KERNING_UNFITTED,&delta)) {
            kerning->offset_x=delta.x/64.0f;
            kerning->offset_y=delta.y/64.0f;
        }
    }
    pthread_mutex_unlock(&lock);
    return result;
}

static ABI void font_render_surface_init(RenderSurface *surface, void *buffer, int32_t width_byte,
                                         int32_t pixel_size_byte, int32_t width, int32_t height) {
    once("sceFontRenderSurfaceInit");
    if (!surface) return;
    if (width<0) width=0;
    if (height<0) height=0;
    memset(surface,0,sizeof(*surface));
    surface->buffer=buffer; surface->width_byte=width_byte; surface->pixel_size_byte=(int8_t)pixel_size_byte;
    surface->width=width; surface->height=height;
    surface->scissor_x1=(uint32_t)width; surface->scissor_y1=(uint32_t)height;
}

static int ceil_int(float v) { return (int)ceilf(v); }
static int floor_int(float v) { return (int)floorf(v); }

/* Rasterizes `code` with its pen at (x, y) (surface pixels, y down, baseline), writing the
 * glyph's coverage over its bitmap rectangle clipped to the scissor. Subpixel pen positions
 * are kept, so a glyph placed at (cell_x - bearingX, cell_y + bearingY) starts exactly at the
 * cell corner and spans ceil(width) x ceil(height) pixels, as the game's atlas expects. */
static ABI int32_t font_render_char_glyph_image_horizontal(Font *handle, uint32_t code, RenderSurface *surface,
                                                           float x, float y, GlyphMetrics *metrics, RenderOutput *output) {
    once("sceFontRenderCharGlyphImageHorizontal");
    if (metrics) memset(metrics,0,sizeof(*metrics));
    if (output) memset(output,0,sizeof(*output));
    if (!code) return ERR_NO_SUPPORT_CODE;
    if (!surface || !metrics || !output) return ERR_INVALID_PARAMETER;
    pthread_mutex_lock(&lock);
    Font *font=font_of(handle);
    int32_t result=FONT_OK;
    int bpp=surface->pixel_size_byte;
    if (!font) result=ERR_INVALID_FONT_HANDLE;
    else if (!font->renderer) result=ERR_NOT_BOUND_RENDERER;
    else if (!surface->buffer || surface->width<=0 || surface->height<=0 || surface->width_byte<=0 || (bpp!=1 && bpp!=4))
        result=ERR_NO_SUPPORT_SURFACE;
    else if (!isfinite(x) || !isfinite(y) || fabsf(x)>1e6f || fabsf(y)>1e6f) result=ERR_INVALID_PARAMETER;
    else if (set_size(font,font->render_w,font->render_h)) result=ERR_INVALID_PARAMETER;
    else if (!load_glyph(font,code,&result)) {
        FT_GlyphSlot slot=font->face->glyph;
        fill_metrics(&slot->metrics,metrics);
        int64_t pen_x=llrintf(x*64.0f), pen_y=llrintf(y*64.0f);
        int64_t cell_x=floor_div64(pen_x), cell_y=floor_div64(pen_y);
        if (slot->format==FT_GLYPH_FORMAT_OUTLINE)
            FT_Outline_Translate(&slot->outline,(FT_Pos)(pen_x-cell_x*64),(FT_Pos)-(pen_y-cell_y*64));
        int update_x=0, update_y=0, update_w=0, update_h=0;
        if (FT_Render_Glyph(slot,FT_RENDER_MODE_NORMAL)) result=ERR_NO_SUPPORT_GLYPH;
        else {
            const FT_Bitmap *bitmap=&slot->bitmap;
            int glyph_w=(int)bitmap->width, glyph_h=(int)bitmap->rows;
            int64_t dest_x=cell_x+slot->bitmap_left, dest_y=cell_y-slot->bitmap_top;
            int64_t clip_x0=surface->scissor_x0, clip_y0=surface->scissor_y0;
            int64_t clip_x1=surface->scissor_x1<(uint32_t)surface->width ? surface->scissor_x1 : (uint32_t)surface->width;
            int64_t clip_y1=surface->scissor_y1<(uint32_t)surface->height ? surface->scissor_y1 : (uint32_t)surface->height;
            if ((int64_t)surface->width_byte<clip_x1*bpp) clip_x1=surface->width_byte/bpp;
            int64_t x0=dest_x>clip_x0 ? dest_x : clip_x0, y0=dest_y>clip_y0 ? dest_y : clip_y0;
            int64_t x1=dest_x+glyph_w<clip_x1 ? dest_x+glyph_w : clip_x1, y1=dest_y+glyph_h<clip_y1 ? dest_y+glyph_h : clip_y1;
            if (x1>x0 && y1>y0 && (bitmap->pixel_mode==FT_PIXEL_MODE_GRAY || bitmap->pixel_mode==FT_PIXEL_MODE_MONO)) {
                uint8_t *base=surface->buffer;
                for (int64_t row=y0;row<y1;++row) {
                    const uint8_t *source=bitmap->buffer+(row-dest_y)*bitmap->pitch;
                    uint8_t *target=base+row*surface->width_byte;
                    for (int64_t column=x0;column<x1;++column) {
                        int64_t sx=column-dest_x;
                        uint8_t value=bitmap->pixel_mode==FT_PIXEL_MODE_GRAY
                            ? (bitmap->num_grays==256 || bitmap->num_grays<2 ? source[sx] : (uint8_t)(source[sx]*255/(bitmap->num_grays-1)))
                            : ((source[sx>>3]>>(7-(sx&7)))&1 ? 255 : 0);
                        if (bpp==1) target[column]=value;
                        else memset(target+column*4,value,4);
                    }
                }
                update_x=(int)x0; update_y=(int)y0; update_w=(int)(x1-x0); update_h=(int)(y1-y0);
            } else {
                update_x=(int)(x0<clip_x1 ? x0 : clip_x1); update_y=(int)(y0<clip_y1 ? y0 : clip_y1);
                if (update_x<0) update_x=0;
                if (update_y<0) update_y=0;
            }
        }
        if (result==FONT_OK) {
            output->stage=NULL;
            output->image.address=(uint8_t *)surface->buffer+(size_t)update_y*(size_t)surface->width_byte+(size_t)update_x*(size_t)bpp;
            output->image.width_byte=(uint32_t)surface->width_byte;
            output->image.pixel_size_byte=(uint8_t)bpp;
            output->update.x=(uint32_t)update_x; output->update.y=(uint32_t)update_y;
            output->update.w=(uint32_t)update_w; output->update.h=(uint32_t)update_h;
            /* Image metrics: integer pixel box of the glyph relative to the pen position. */
            float left=x+metrics->h_bearing_x, top=y-metrics->h_bearing_y;
            float right=left+metrics->width, bottom=top+metrics->height;
            int left_i=floor_int(left), top_i=floor_int(top), right_i=ceil_int(right), bottom_i=ceil_int(bottom);
            int advance_i=ceil_int(x+metrics->h_advance);
            output->metrics.bearing_x=(float)left_i-x;
            output->metrics.bearing_y=y-(float)top_i;
            output->metrics.advance=(float)advance_i-x;
            output->metrics.stride=(float)(advance_i>right_i ? advance_i : right_i)-x;
            output->metrics.width=right_i>left_i ? (uint32_t)(right_i-left_i) : 0;
            output->metrics.height=bottom_i>top_i ? (uint32_t)(bottom_i-top_i) : 0;
        }
    }
    if (result!=FONT_OK) { memset(metrics,0,sizeof(*metrics)); memset(output,0,sizeof(*output)); }
    pthread_mutex_unlock(&lock);
    return result;
}

/* ABI and validation: shadPS4 e6c79a5 font.cpp (GPL-2.0-or-later).
 * The memory-backed FreeType provider retains a face until close in every mode.
 */
static ABI int32_t font_set_open_mode(Library *library,uint32_t mode) {
    if (!library || library->magic!=LIBRARY_MAGIC) return ERR_INVALID_LIBRARY;
    if (mode>2) return ERR_INVALID_PARAMETER;
    pthread_mutex_lock(&lock);
    library->open_mode=mode;
    pthread_mutex_unlock(&lock);
    return FONT_OK;
}
static ABI int32_t font_support_system(Library *library) {
    if (!library || library->magic!=LIBRARY_MAGIC) return ERR_INVALID_LIBRARY;
    if (!getenv("KNACK2_SYSTEM_FONT")) return ERR_FONT_OPEN_FAILED;
    pthread_mutex_lock(&lock);
    int32_t result=library->system_fonts ? (int32_t)0x80460021 : 0;
    library->system_fonts=1;
    pthread_mutex_unlock(&lock);
    return result;
}
static ABI int32_t font_attach_cache(Library *library,void *buffer,uint32_t size) {
    (void)buffer; /* FreeType keeps its own CPU cache; guest cache memory stays guest-owned. */
    if (!library || library->magic!=LIBRARY_MAGIC) return ERR_INVALID_LIBRARY;
    if (size<0x2000) return ERR_INVALID_PARAMETER;
    pthread_mutex_lock(&lock);
    int32_t result=library->cache_size ? (int32_t)0x80460022 : 0;
    if (!result) library->cache_size=size;
    pthread_mutex_unlock(&lock);
    once("device-cache request uses FreeType host cache");
    return result;
}
static ABI int32_t font_open_set(Library *library,uint32_t type,uint32_t mode,const OpenParams *params,Font **out) {
    if (!out) return ERR_INVALID_PARAMETER;
    *out=NULL;
    if (!library || library->magic!=LIBRARY_MAGIC) return ERR_INVALID_LIBRARY;
    if (mode>2) return ERR_INVALID_PARAMETER;
    const char *path=getenv("KNACK2_SYSTEM_FONT");
    if (!library->system_fonts || !path) return ERR_FONT_OPEN_FAILED;
    FILE *file=fopen(path,"rb");
    if (!file) return ERR_FONT_OPEN_FAILED;
    if (fseek(file,0,SEEK_END)) { fclose(file); return ERR_FONT_OPEN_FAILED; }
    long size=ftell(file);
    if (size<=0 || size>64*1024*1024 || fseek(file,0,SEEK_SET)) { fclose(file); return ERR_FONT_OPEN_FAILED; }
    void *data=malloc((size_t)size);
    if (!data) { fclose(file); return ERR_ALLOCATION_FAILED; }
    size_t count=fread(data,1,(size_t)size,file);
    fclose(file);
    if (count!=(size_t)size) { free(data); return ERR_FONT_OPEN_FAILED; }
    /* The configured host file is a standalone substitute, not a PS4 collection. */
    OpenParams selected={0};
    if (params) selected=*params;
    selected.subfont_index=0;
    int32_t result=font_open_memory(library,data,(uint32_t)size,&selected,out);
    if (result) free(data);
    else {
        (*out)->owned_data=data;
        printf("Font: system set %#x uses explicit local substitute %s\n",type,path);
    }
    return result;
}

/* Text source layout and initialization adapted from shadPS4 e6c79a5 font.h/font.cpp.
 * Copyright 2024 shadPS4 Emulator Project; GPL-2.0-or-later.
 */
typedef struct {
    uint64_t system;
    const void *start,*end,*current;
    void *parser,*object,*font,*saved[5];
} FontTextSource;
_Static_assert(sizeof(FontTextSource)==0x60,"Font text source ABI");
static ABI int32_t font_text_init(FontTextSource *source,const void *text,uint32_t size,void *parser,void *object) {
    if (!source) return ERR_INVALID_PARAMETER;
    memset(source,0,sizeof(*source));
    if (!parser || (size && (uintptr_t)text>UINTPTR_MAX-size)) return ERR_INVALID_PARAMETER;
    source->system=(UINT64_C(0x10)<<32)|0x0f04;
    source->start=source->current=text;
    source->end=size ? (const void *)((uintptr_t)text+size) : NULL;
    source->parser=parser; source->object=object;
    source->saved[0]=(void *)text; source->saved[1]=(void *)source->end;
    source->saved[2]=parser; source->saved[3]=object;
    return FONT_OK;
}
static ABI int32_t font_text_default(FontTextSource *source,Font *font) {
    if (!source || (source->system&0xffff)!=0x0f04) return ERR_INVALID_PARAMETER;
    source->font=source->saved[4]=font;
    return FONT_OK;
}
static ABI int32_t font_text_form(FontTextSource *source,int32_t form) {
    if (!source || (source->system&0xffff)!=0x0f04 || form<0x10 || form>0x12) return ERR_INVALID_PARAMETER;
    source->system=(source->system&UINT32_MAX)|((uint64_t)(uint32_t)form<<32);
    return FONT_OK;
}

#include "knack_font_text.inc"

static const RuntimeExport exports[]={
    {"sceFontCreateString",font_string_create},
    {"sceFontDestroyString",font_string_destroy},
    {"sceFontStringRefersTextCharacters",font_string_characters},
    {"sceFontStringRefersRenderCharacters",font_string_render_characters},
    {"sceFontCharacterRefersTextNext",font_character_next},
    {"sceFontTextSourceInit",font_text_init},
    {"sceFontTextSourceSetDefaultFont",font_text_default},
    {"sceFontTextSourceSetWritingForm",font_text_form},
    {"sceFontSetFontsOpenMode",font_set_open_mode},
    {"sceFontAttachDeviceCacheBuffer",font_attach_cache},
    {"sceFontSupportSystemFonts",font_support_system},
    {"sceFontOpenFontSet",font_open_set},
    {"sceFontMemoryInit",font_memory_init},
    {"sceFontMemoryTerm",font_memory_term},
    {"sceFontSelectLibraryFt",font_select_library_ft},
    {"sceFontSelectRendererFt",font_select_renderer_ft},
    {"sceFontCreateLibraryWithEdition",font_create_library},
    {"sceFontDestroyLibrary",font_destroy_library},
    {"sceFontSupportExternalFonts",font_support_external_fonts},
    {"sceFontCreateRendererWithEdition",font_create_renderer},
    {"sceFontDestroyRenderer",font_destroy_renderer},
    {"sceFontGetPixelResolution",font_get_pixel_resolution},
    {"sceFontOpenFontMemory",font_open_memory},
    {"sceFontCloseFont",font_close},
    {"sceFontBindRenderer",font_bind_renderer},
    {"sceFontUnbindRenderer",font_unbind_renderer},
    {"sceFontSetScalePixel",font_set_scale_pixel},
    {"sceFontSetupRenderScalePixel",font_setup_render_scale_pixel},
    {"sceFontGetHorizontalLayout",font_get_horizontal_layout},
    {"sceFontGetCharGlyphMetrics",font_get_char_glyph_metrics},
    {"sceFontGetRenderScaledKerning",font_get_render_scaled_kerning},
    {"sceFontGetKerning",font_get_kerning},
    {"sceFontWritingInit",font_writing_init},
    {"sceFontWritingRefersRenderStep",font_writing_render_step},
    {"sceFontWritingGetRenderMetrics",font_writing_get_render_metrics},
    {"sceFontGenerateCharGlyph",font_generate_char_glyph},
    {"sceFontDeleteGlyph",font_delete_glyph},
    {"sceFontGlyphRefersMetricsHorizontalAdvance",font_glyph_metrics_horizontal_advance},
    {"sceFontRenderSurfaceInit",font_render_surface_init},
    {"sceFontRenderCharGlyphImageHorizontal",font_render_char_glyph_image_horizontal},
};
uintptr_t runtime_gr2_font_resolve(const char *name) {
    return RUNTIME_LOOKUP(exports,name);
}
