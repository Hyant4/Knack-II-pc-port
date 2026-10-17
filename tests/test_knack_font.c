/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include "../port/src/runtime_gr2_font.c"

uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    for (size_t i=0;i<count;++i) if (!strcmp(table[i].name,name)) return (uintptr_t)table[i].function;
    return 0;
}
static ABI int32_t parse_ascii(FontTextSource *source,void **order,FontParseResult *result) {
    const unsigned char *p=source->current;
    *order=(void *)p;
    result->result.font=source->font;
    result->result.code=*p;
    if (!*p) return 0;
    source->current=p+1;
    return 1;
}
static float check_phrase(FontMemory *memory,Font *font,float scale) {
    const char *phrase="Press Any Button";
    assert(!font_set_scale_pixel(font,scale,scale));
    FontTextSource source;
    assert(!font_text_init(&source,phrase,0,(void *)parse_ascii,NULL));
    assert(!font_text_default(&source,font));
    assert(!font_text_form(&source,0x12));
    FontString *string=NULL;
    assert(!font_string_create(memory,&source,NULL,&string));
    uint32_t count=0;
    FontCharacter *first=font_string_render_characters(string,NULL,NULL,&count);
    assert(first && count==strlen(phrase)); /* Spaces cannot shorten the iteration. */
    _Alignas(16) unsigned char writing[256];
    assert(!font_writing_init(writing,string,first));
    float expected_x=0;
    for (uint32_t i=0;i<count;++i) {
        FontWritingStep *step=font_writing_render_step(writing);
        assert(step && step->glyph_code==(uint32_t)phrase[i]);
        /* Mirror the guest's byte offsets, independently of C field names. */
        float x,y,advance;
        memcpy(&x,(const char *)step+0x20,4);
        memcpy(&y,(const char *)step+0x24,4);
        memcpy(&advance,(const char *)step+0x08,4);
        assert(fabsf(x-expected_x)<0.001f && y==0 && step->advance_y==0);
        assert(advance>0 && advance<scale*2);
        if (i) assert(x>0);
        expected_x+=advance;
    }
    assert(!font_writing_render_step(writing));
    FontWritingMetrics metrics;
    assert(!font_writing_get_render_metrics(writing,&metrics));
    assert(fabsf(metrics.advance_x-expected_x)<0.001f && metrics.advance_y==0);
    assert(metrics.advance_x>scale*5); /* Reject accidental second 26.6 division. */
    assert(font_string_render_characters(string,first+1,first+3,&count)==first+1 && count==3);
    assert(!font_string_render_characters(string,first+3,first+1,&count) && count==0);
    assert(!font_string_destroy(&string));
    return expected_x;
}
int main(int argc,char **argv) {
    assert(argc==2);
    FILE *file=fopen(argv[1],"rb"); assert(file);
    assert(!fseek(file,0,SEEK_END)); long size=ftell(file); assert(size>0);
    rewind(file); void *data=malloc((size_t)size); assert(data);
    assert(fread(data,1,(size_t)size,file)==(size_t)size); fclose(file);
    FontMemory memory={0}; memory.kind=MEM_MAGIC;
    Library *library=NULL; Font *font=NULL;
    assert(!font_create_library(&memory,&library_ft,0,&library));
    assert(!font_open_memory(library,data,(uint32_t)size,NULL,&font));
    float width32=check_phrase(&memory,font,32),width64=check_phrase(&memory,font,64);
    assert(fabsf(width64-width32*2)<1.0f);
    assert(!font_close(font));
    assert(!font_destroy_library(&library)); free(data);
    puts("PASS: guest positioning offsets, horizontal baseline, spaces, full phrase and pixel scaling");
    return 0;
}
