/* SPDX-License-Identifier: GPL-2.0-or-later
 * Synthetic disk-backed SaveData tests. The caller supplies an empty temporary
 * directory; no game dump or real user save is accessed.
 */
#define _GNU_SOURCE
#include <assert.h>
#include "../port/src/runtime_savedata.c"

static const char *test_root;
const char *runtime_file_user_dir(void) { return test_root; }
int runtime_file_mount(const char *guest,const char *host) { (void)guest; (void)host; return 0; }
void runtime_file_unmount(const char *guest) { (void)guest; }
void tlggpu_save_toast(int kind) { (void)kind; }
int compat_remove_tree(const char *path) { (void)path; assert(!"Unexpected deletion in SaveData test"); return -1; }
uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *name) {
    for (size_t i=0;i<count;++i) if (!strcmp(name,table[i].name)) return (uintptr_t)table[i].function;
    return 0;
}
static void write_bytes(const char *path,const void *data,size_t size) {
    FILE *file=fopen(path,"wb"); assert(file);
    assert(fwrite(data,1,size,file)==size); assert(!fclose(file));
}
int main(int argc,char **argv) {
    assert(argc==2); test_root=argv[1];
    runtime_savedata_configure("CUSA08014");
    assert(runtime_savedata_resolve("sceSaveDataGetParam"));
    assert(runtime_savedata_resolve("sceSaveDataUmountWithBackup"));
    assert(runtime_savedata_resolve("sceSaveDataGetEventResult"));
    Param p={0},out={0}; uint64_t got=1234;
    MountPoint point={"/savedata0"};
    assert(save_get_param(&point,0,&out,sizeof(out),&got)==ERR_NOT_INITIALIZED);
    assert(!save_initialize(NULL));
    assert(save_get_param(&point,0,&out,sizeof(out),&got)==ERR_NOT_MOUNTED);
    DirName name={"SAVEDATA00"}; MountResult mounted;
    Mount2 request={0}; request.user=1; request.dir=&name; request.mode=MODE_CREATE;
    assert(!save_mount2(&request,&mounted));
    strcpy(p.title,"Knack save title"); strcpy(p.subtitle,"Subtitle"); strcpy(p.detail,"Details");
    p.user_param=0x12345678;
    assert(!save_set_param(&mounted.point,0,&p,sizeof(p)));
    assert(!save_umount(&mounted.point));
    request.mode=MODE_RDONLY;
    assert(!save_mount2(&request,&mounted));
    assert(save_set_param(&mounted.point,0,&p,sizeof(p))==ERR_BAD_MOUNTED);
    assert(!save_get_param(&mounted.point,0,&out,sizeof(out),&got));
    assert(got==sizeof(Param) && !strcmp(out.title,p.title) && out.user_param==p.user_param && out.mtime>0);
    char path[1024]; snprintf(path,sizeof(path),"%s/param.bin",slots[0].meta);
    FILE *file=fopen(path,"rb"); assert(file); Param disk; assert(fread(&disk,sizeof(disk),1,file)==1); fclose(file);
    assert(!memcmp(&disk,&p,offsetof(Param,mtime)));
    for (uint32_t type=1;type<=3;++type) {
        struct { unsigned char before; char text[5]; unsigned char after; } guard={0xa5,{0},0x5a};
        assert(!save_get_param(&mounted.point,type,guard.text,sizeof(guard.text),&got));
        assert(guard.before==0xa5 && guard.after==0x5a && got==5 && guard.text[4]==0);
        assert(!save_get_param(&mounted.point,type,guard.text,1,NULL) && guard.text[0]==0);
        assert(save_get_param(&mounted.point,type,guard.text,0,&got)==ERR_PARAMETER);
    }
    uint32_t user=0; int64_t modified=0;
    assert(!save_get_param(&mounted.point,4,&user,sizeof(user),&got) && user==p.user_param && got==4);
    assert(!save_get_param(&mounted.point,5,&modified,sizeof(modified),&got) && modified>0 && got==8);
    const uint32_t types[]={0,4,5}; const size_t sizes[]={sizeof(Param),4,8};
    for (size_t i=0;i<3;++i) {
        memset(&out,0xa5,sizeof(out)); got=1234;
        assert(save_get_param(&mounted.point,types[i],&out,sizes[i]-1,&got)==ERR_PARAMETER);
        assert(((unsigned char *)&out)[0]==0xa5 && got==1234);
    }
    assert(save_get_param(NULL,0,&out,sizeof(out),NULL)==ERR_PARAMETER);
    assert(save_get_param(&mounted.point,6,&out,sizeof(out),NULL)==ERR_PARAMETER);
    assert(save_get_param(&mounted.point,0,NULL,sizeof(out),NULL)==ERR_PARAMETER);
    const char *invalid[]={"/savedata","/savedataX","/savedata00","/savedata0/x","/savedata16"};
    for (size_t i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
        MountPoint bad={0}; strcpy(bad.data,invalid[i]);
        assert(save_get_param(&bad,0,&out,sizeof(out),NULL)==ERR_NOT_MOUNTED);
    }
    MountPoint unterminated; memset(&unterminated,'x',sizeof(unterminated));
    assert(save_get_param(&unterminated,0,&out,sizeof(out),NULL)==ERR_NOT_MOUNTED);
    write_bytes(path,"short",5);
    assert(save_get_param(&mounted.point,0,&out,sizeof(out),NULL)==ERR_BROKEN);
    assert(save_umount_backup(&mounted.point)==ERR_BROKEN && slots[0].used);
    SaveEvent event;
    assert(save_get_event(NULL,&event)==ERR_NOT_FOUND);
    write_bytes(path,&disk,sizeof(disk));
    snprintf(path,sizeof(path),"%s/payload.dat",slots[0].host);
    write_bytes(path,"known save payload",18);
    assert(!save_umount_backup(&mounted.point) && !slots[0].used);
    assert(!save_get_event(NULL,&event));
    assert(event.type==1 && event.error==0 && event.user==1);
    assert(!strcmp(event.title.data,"CUSA08014") && !strcmp(event.dir.data,"SAVEDATA00"));
    assert(save_get_event(NULL,&event)==ERR_NOT_FOUND);
    assert(save_get_param(&mounted.point,0,&out,sizeof(out),NULL)==ERR_NOT_MOUNTED);
    request.mode=0;
    assert(!save_mount2(&request,&mounted));
    snprintf(path,sizeof(path),"%s/payload.dat",slots[0].host);
    write_bytes(path,"updated save payload",20);
    assert(!save_umount_backup(&mounted.point));
    assert(!save_get_event(NULL,&event) && event.type==1 && !event.error);
    assert(save_get_event(NULL,&event)==ERR_NOT_FOUND);
    assert(!save_terminate());
    assert(save_get_event(NULL,&event)==ERR_NOT_INITIALIZED);
    puts("PASS: SaveData fields, buffer boundaries, read-only metadata, corruption, backup and event lifecycle");
    return 0;
}
