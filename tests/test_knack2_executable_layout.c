/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "knack_executable_layout.h"
#include "knack_native_resolution.h"
#include "knack_ultrawide_patch.h"
#include "knack_dialogue_gain.h"

int main(int argc,char **argv) {
    const char *serial=NULL,*version=NULL;
    assert(knack_code_profile("CUSA08014","01.01",NULL,&serial,&version));
    assert(!strcmp(serial,"CUSA08014") && !strcmp(version,"01.01"));
    assert(knack_code_profile("CUSA08014","01.00",NULL,&serial,&version));
    assert(!strcmp(serial,"CUSA08014") && !strcmp(version,"01.00"));
    assert(!knack_code_profile("CUSA08014","01.00","elf-0b7843d3",&serial,&version));
    assert(!knack_code_profile("CUSA07670","01.01","elf-80ccc228",&serial,&version));
    assert(!knack_code_profile("CUSA08014","01.01","unknown",&serial,&version));
    assert(!knack_code_profile("CUSA08014","01.01","",&serial,&version));
    assert(!knack_code_profile("CUSA99999","01.01",NULL,&serial,&version));
    assert(knack_code_profile("CUSA08014","01.01","elf-0b7843d3",&serial,&version));
    assert(!strcmp(serial,"CUSA07670") && !strcmp(version,"01.01"));
    assert(knack_native_profile(serial,version)==&knack_native_eu);
    assert(knack_wide_profile(serial,version)==knack_wide_eu);
    assert(!dialogue_profile_matches(serial,version));
    if (argc==2) {
        FILE *file=fopen(argv[1],"rb"); assert(file);
        assert(!fseek(file,0,SEEK_END)); long length=ftell(file);assert(length>0);rewind(file);
        unsigned char *image=malloc((size_t)length);assert(image);
        assert(fread(image,1,(size_t)length,file)==(size_t)length);fclose(file);
        assert(knack_native_matches(image,(uint64_t)length,knack_native_profile(serial,version)));
        assert(knack_wide_image_matches(image,(uint64_t)length,knack_wide_profile(serial,version)));
        assert(!knack_native_matches(image,(uint64_t)length,&knack_native_us));
        assert(!knack_wide_image_matches(image,(uint64_t)length,knack_wide_us));
        free(image);
        puts("Real private image: shared layout selected; US signatures rejected.");
    }
    puts("Executable layout selector PASS; no guest executed.");
    return 0;
}
