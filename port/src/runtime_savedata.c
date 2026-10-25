/* libSceSaveData on host directories:
 *   <user>/savedata/<user id>/<title id>/<dir name>/        files the game writes
 *   <user>/savedata/<user id>/<title id>/<dir name>.sce_sys/ param.bin, icon0.png
 *   <user>/savedata/<user id>/<title id>.memory/memory.dat  SaveDataMemory
 * A mounted directory appears to the guest as /savedata0../savedata15.
 * Metadata lives beside the directory so the guest's own files are untouched. */
#define _GNU_SOURCE
#include "runtime.h"
#include "gpu/tlggpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#endif
#ifndef _WIN32
#include <ftw.h>
#endif

#define ERR_PARAMETER ((int32_t)0x809F0000)
#define ERR_NOT_INITIALIZED ((int32_t)0x809F0001)
#define ERR_NOT_MOUNTED ((int32_t)0x809F0004)
#define ERR_EXISTS ((int32_t)0x809F0007)
#define ERR_NOT_FOUND ((int32_t)0x809F0008)
#define ERR_INTERNAL ((int32_t)0x809F000B)
#define ERR_MOUNT_FULL ((int32_t)0x809F000C)
#define ERR_BAD_MOUNTED ((int32_t)0x809F000D)
#define ERR_BROKEN ((int32_t)0x809F000F)
#define ERR_INVALID_USER ((int32_t)0x809F0011)
#define ERR_MEMORY_NOT_READY ((int32_t)0x809F0012)
#define MODE_RDONLY 1
#define MODE_CREATE 4
#define MODE_COPY_ICON 16
#define MODE_CREATE2 32
#define SLOTS 16

typedef struct { char data[10]; char pad[6]; } TitleId;
typedef struct { char data[32]; } DirName;
typedef struct { char data[16]; } MountPoint;
typedef struct {
    char title[128], subtitle[128], detail[1024];
    uint32_t user_param; int32_t pad;
    int64_t mtime;
    uint8_t reserved[32];
} Param;
typedef struct { int32_t user; int32_t pad; const TitleId *title; const DirName *dir; const void *fingerprint;
                 uint64_t blocks; uint32_t mode; uint8_t reserved[32]; } Mount1;
typedef struct { int32_t user; int32_t pad; const DirName *dir; uint64_t blocks; uint32_t mode;
                 uint8_t reserved[32]; int32_t pad2; } Mount2;
typedef struct { MountPoint point; uint64_t required_blocks; uint32_t unused, status; uint8_t reserved[28]; int32_t pad; } MountResult;
typedef struct { int32_t user; int32_t pad; const TitleId *title; const DirName *dir; uint32_t unused;
                 uint8_t reserved[32]; int32_t pad2; } Delete;
typedef struct { int32_t user; int32_t pad; const TitleId *title; const DirName *dir; uint32_t key, order;
                 uint8_t reserved[32]; } SearchCond;
typedef struct { uint64_t blocks, free_blocks; uint8_t reserved[32]; } SearchInfo;
typedef struct { uint32_t hits; int32_t pad; DirName *names; uint32_t names_capacity, set_count;
                 Param *params; SearchInfo *infos; uint8_t reserved[12]; int32_t pad2; } SearchResult;
typedef struct { const void *buffer; uint64_t buffer_size, data_size; uint8_t reserved[32]; } Icon;
_Static_assert(sizeof(Param)==1328,"OrbisSaveDataParam layout");
_Static_assert(sizeof(Mount1)==80 && sizeof(Mount2)==64,"OrbisSaveDataMount layouts");
_Static_assert(sizeof(MountResult)==64,"OrbisSaveDataMountResult layout");
_Static_assert(sizeof(SearchCond)==64 && sizeof(SearchResult)==56,"dir name search layouts");

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static int initialized;
static char title_id[16]="UNKNOWN";
static struct { int used, saved; char host[700], meta[700]; } slots[SLOTS]; /* saved: param written */
/* Host bookkeeping; no guest structure layout changes. */
static struct { int32_t user; TitleId title; DirName dir; uint32_t mode; } slot_identity[SLOTS];
static char memory_path[720];
static uint64_t memory_size;
static size_t mounts_done, memory_writes;

void runtime_savedata_configure(const char *title) {
    if (title && *title) snprintf(title_id,sizeof(title_id),"%s",title);
}

static int make_dirs(const char *path) {
    char buffer[700];
    snprintf(buffer,sizeof(buffer),"%s",path);
    for (char *p=buffer+1;*p;++p) if (*p=='/') { *p=0; mkdir(buffer,0755); *p='/'; }
    return mkdir(buffer,0755) && errno!=EEXIST ? -1 : 0;
}
static void root(int32_t user, const char *title, char *out, size_t size) {
    snprintf(out,size,"%s/savedata/%d/%s",runtime_file_user_dir(),user,title && *title ? title : title_id);
}
static int valid_name(const char *name, size_t max) {
    size_t n=strnlen(name,max);
    if (!n || n==max) return 0;
    for (size_t i=0;i<n;++i) if (name[i]=='/' || name[i]=='\\' || (name[i]=='.' && (i==0 || name[i-1]=='.'))) return 0;
    return 1;
}
static void write_param(const char *meta, const Param *p) {
    char path[700]; snprintf(path,sizeof(path),"%s/param.bin",meta);
    FILE *f=fopen(path,"wb");
    if (f) { Param copy=*p; copy.mtime=time(NULL); fwrite(&copy,sizeof(copy),1,f); fclose(f); }
}
static int read_param(const char *meta, Param *p) {
    char path[700]; snprintf(path,sizeof(path),"%s/param.bin",meta);
    memset(p,0,sizeof(*p));
    FILE *f=fopen(path,"rb");
    if (!f) return -1;
    size_t n=fread(p,sizeof(*p),1,f); fclose(f);
    struct stat st;
    if (!stat(path,&st)) p->mtime=st.st_mtime;
    return n==1 ? 0 : -1;
}
#ifdef _WIN32
static void remove_tree(const char *path) { compat_remove_tree(path); }
#else
static int remove_entry(const char *path, const struct stat *st, int flag, struct FTW *ftw) {
    (void)st; (void)flag; (void)ftw; return remove(path);
}
static void remove_tree(const char *path) { nftw(path,remove_entry,16,FTW_DEPTH|FTW_PHYS); }
#endif

/* A save's param (title, subtitle, date) for the save dialog; 0 when it exists. */
int runtime_savedata_param(int32_t user, const char *dir, void *param) {
    if (!valid_name(dir,32)) return -1;
    char base[600], meta[680];
    root(user,NULL,base,sizeof(base));
    snprintf(meta,sizeof(meta),"%s/%s.sce_sys",base,dir);
    return read_param(meta,(Param *)param);
}
/* Host path of a save's icon0.png (the location picture in the save dialog); 0 when named. */
int runtime_savedata_icon(int32_t user, const char *dir, char *out, size_t size) {
    if (!valid_name(dir,32)) return -1;
    char base[600];
    root(user,NULL,base,sizeof(base));
    snprintf(out,size,"%s/%s.sce_sys/icon0.png",base,dir);
    return 0;
}
/* Bytes in a save's folder (the save dialog shows it); 0 when it cannot be read. */
static uint64_t tree_size(const char *path, int depth) {
    DIR *d=opendir(path);
    if (!d) return 0;
    uint64_t total=0; struct dirent *e;
    while ((e=readdir(d))) {
        if (!strcmp(e->d_name,".") || !strcmp(e->d_name,"..")) continue;
        char child[1024]; struct stat st;
        snprintf(child,sizeof(child),"%s/%s",path,e->d_name);
        if (stat(child,&st)) continue;
        if (S_ISDIR(st.st_mode)) { if (depth<8) total+=tree_size(child,depth+1); }
        else total+=(uint64_t)st.st_size;
    }
    closedir(d);
    return total;
}
uint64_t runtime_savedata_size(int32_t user, const char *dir) {
    if (!valid_name(dir,32)) return 0;
    char base[600], host[680];
    root(user,NULL,base,sizeof(base));
    snprintf(host,sizeof(host),"%s/%s",base,dir);
    return tree_size(host,0);
}
static ABI int32_t save_initialize(const void *param) { (void)param; initialized=1; return 0; }
static ABI int32_t save_terminate(void) {
    if (!initialized) return ERR_NOT_INITIALIZED;
    initialized=0; return 0;
}
static int32_t mount(int32_t user, const char *title, const DirName *dir, uint32_t mode, MountResult *result) {
    if (!initialized) return ERR_NOT_INITIALIZED;
    if (user<0) return ERR_INVALID_USER;
    if (!dir || !result || !valid_name(dir->data,sizeof(dir->data))) return ERR_PARAMETER;
    if (title && !valid_name(title,10)) return ERR_PARAMETER;
    char base[600], host[640], meta[680];
    root(user,title,base,sizeof(base));
    snprintf(host,sizeof(host),"%s/%s",base,dir->data);
    snprintf(meta,sizeof(meta),"%s/%s.sce_sys",base,dir->data);
    struct stat st;
    int exists=!stat(host,&st) && S_ISDIR(st.st_mode);
    if ((mode & MODE_CREATE) && exists) return ERR_EXISTS;
    if (!(mode & (MODE_CREATE|MODE_CREATE2)) && !exists) return ERR_NOT_FOUND;
    pthread_mutex_lock(&lock);
    int slot=-1;
    for (int i=0;i<SLOTS;++i) {
        if (slots[i].used && !strcmp(slots[i].host,host)) { pthread_mutex_unlock(&lock); return ERR_BAD_MOUNTED; }
        if (!slots[i].used && slot<0) slot=i;
    }
    if (slot<0) { pthread_mutex_unlock(&lock); return ERR_MOUNT_FULL; }
    if (!exists && (make_dirs(host) || make_dirs(meta))) { pthread_mutex_unlock(&lock); return ERR_INTERNAL; }
    if (!exists) { Param empty={0}; write_param(meta,&empty); }
    slots[slot].used=1;
    slots[slot].saved=0;
    memset(&slot_identity[slot],0,sizeof(slot_identity[slot]));
    slot_identity[slot].user=user;
    slot_identity[slot].mode=mode;
    snprintf(slot_identity[slot].title.data,sizeof(slot_identity[slot].title.data),"%.*s",
             (int)sizeof(slot_identity[slot].title.data)-1,title && *title ? title : title_id);
    slot_identity[slot].dir=*dir;
    snprintf(slots[slot].host,sizeof(slots[slot].host),"%s",host);
    snprintf(slots[slot].meta,sizeof(slots[slot].meta),"%s",meta);
    memset(result,0,sizeof(*result));
    char point[32];
    snprintf(point,sizeof(point),"/savedata%d",slot&15);
    memcpy(result->point.data,point,strlen(point)+1);
    result->status=exists ? 0 : 1; /* CREATED */
    runtime_file_mount(result->point.data,host);
    ++mounts_done;
    pthread_mutex_unlock(&lock);
    printf("Runtime: save data '%s' mounted at %s (%s%s)\n",dir->data,result->point.data,
           exists ? "existing" : "created",(mode & MODE_RDONLY) ? ", read-only" : "");
    return 0;
}
static ABI int32_t save_mount(const Mount1 *m, MountResult *result) {
    if (!m) return ERR_PARAMETER;
    return mount(m->user,m->title ? m->title->data : NULL,m->dir,m->mode,result);
}
static ABI int32_t save_mount2(const Mount2 *m, MountResult *result) {
    if (!m) return ERR_PARAMETER;
    return mount(m->user,NULL,m->dir,m->mode,result);
}
static int slot_of(const MountPoint *point) {
    if (!point || strnlen(point->data,sizeof(point->data))==sizeof(point->data) ||
        strncmp(point->data,"/savedata",9)) return -1;
    const char *number=point->data+9;
    if (*number<'0' || *number>'9') return -1;
    int slot=*number++-'0';
    if (*number) {
        if (slot!=1 || *number<'0' || *number>'5') return -1;
        slot=10+(*number++-'0');
    }
    if (*number) return -1;
    return slot>=0 && slot<SLOTS && slots[slot].used ? slot : -1;
}
#include "knack_savedata_backup.inc"

static int32_t save_umount_internal(const MountPoint *point,int backup) {
    if (!initialized) return ERR_NOT_INITIALIZED;
    pthread_mutex_lock(&lock);
    int slot=slot_of(point);
    if (slot>=0 && backup) {
        int32_t result=save_backup_slot(slot);
        if (result) { pthread_mutex_unlock(&lock); return result; }
    }
    int saved=slot>=0 && slots[slot].saved;
    if (slot>=0) { runtime_file_unmount(point->data); slots[slot].used=slots[slot].saved=0; }
    pthread_mutex_unlock(&lock);
    /* A save the game wrote (its param set) is done: "Game saved" in the corner.
     * TLG_SAVE_TOAST=0 turns it off. */
    static int toast=-1;
    if (toast<0) { const char *v=getenv("TLG_SAVE_TOAST"); toast=!(v && !strcmp(v,"0")); }
    if (saved && toast) tlggpu_save_toast(0);
    return slot>=0 ? 0 : ERR_NOT_FOUND;
}
static ABI int32_t save_umount(const MountPoint *point) { return save_umount_internal(point,0); }
static ABI int32_t save_umount_backup(const MountPoint *point) { return save_umount_internal(point,1); }
/* ABI and field semantics verified against shadPS4 e6c79a5 savedata.cpp
 * (Copyright 2024 shadPS4 Emulator Project, GPL-2.0-or-later).
 * Metadata reads never create or repair files, including on read-only mounts.
 */
static ABI int32_t save_get_param(const MountPoint *point, uint32_t type, void *buffer,
                                  uint64_t size, uint64_t *got_size) {
    if (!initialized) return ERR_NOT_INITIALIZED;
    if (!point || !buffer || type>5) return ERR_PARAMETER;
    uint64_t required=type==0 ? sizeof(Param) : type==4 ? sizeof(uint32_t) : type==5 ? sizeof(int64_t) : 1;
    if (size<required) return ERR_PARAMETER;
    pthread_mutex_lock(&lock);
    int slot=slot_of(point);
    if (slot<0) { pthread_mutex_unlock(&lock); return ERR_NOT_MOUNTED; }
    Param p;
    if (read_param(slots[slot].meta,&p)) { pthread_mutex_unlock(&lock); return ERR_BROKEN; }
    pthread_mutex_unlock(&lock);
    const void *source=&p;
    if (type>=1 && type<=3) {
        const char *text=type==1 ? p.title : type==2 ? p.subtitle : p.detail;
        size_t capacity=type==3 ? sizeof(p.detail) : sizeof(p.title);
        size_t length=strnlen(text,capacity);
        if (length>size-1) length=(size_t)(size-1);
        memcpy(buffer,text,length);
        ((char *)buffer)[length]=0;
        required=length+1;
    } else {
        if (type==4) source=&p.user_param;
        if (type==5) source=&p.mtime;
        memcpy(buffer,source,(size_t)required);
    }
    if (got_size) *got_size=required;
    if (runtime_verbose()) printf("SaveData: GetParam type=%u returned %llu bytes\n",type,(unsigned long long)required);
    return 0;
}
static ABI int32_t save_set_param(const MountPoint *point, uint32_t type, const void *buffer, uint64_t size) {
    if (!initialized) return ERR_NOT_INITIALIZED;
    if (!buffer) return ERR_PARAMETER;
    pthread_mutex_lock(&lock);
    int slot=slot_of(point);
    if (slot<0) { pthread_mutex_unlock(&lock); return ERR_NOT_MOUNTED; }
    Param p; read_param(slots[slot].meta,&p);
    if (slot_identity[slot].mode & MODE_RDONLY) { pthread_mutex_unlock(&lock); return ERR_BAD_MOUNTED; }
    switch (type) {
    case 0: if (size<sizeof(Param)) goto bad; memcpy(&p,buffer,sizeof(p)); break;     /* ALL */
    case 1: snprintf(p.title,sizeof(p.title),"%.*s",(int)size,(const char *)buffer); break;
    case 2: snprintf(p.subtitle,sizeof(p.subtitle),"%.*s",(int)size,(const char *)buffer); break;
    case 3: snprintf(p.detail,sizeof(p.detail),"%.*s",(int)size,(const char *)buffer); break;
    case 4: if (size<4) goto bad; memcpy(&p.user_param,buffer,4); break;
    default: goto bad;
    }
    write_param(slots[slot].meta,&p);
    slots[slot].saved=1;
    pthread_mutex_unlock(&lock);
    return 0;
bad:
    pthread_mutex_unlock(&lock);
    return ERR_PARAMETER;
}
static ABI int32_t save_icon(const MountPoint *point, const Icon *icon) {
    if (!initialized) return ERR_NOT_INITIALIZED;
    if (!icon || !icon->buffer) return ERR_PARAMETER;
    pthread_mutex_lock(&lock);
    int slot=slot_of(point);
    int32_t r=ERR_NOT_MOUNTED;
    if (slot>=0) {
        char path[700]; snprintf(path,sizeof(path),"%s/icon0.png",slots[slot].meta);
        FILE *f=fopen(path,"wb");
        r=f && fwrite(icon->buffer,1,icon->data_size,f)==icon->data_size ? 0 : ERR_INTERNAL;
        if (f) fclose(f);
    }
    pthread_mutex_unlock(&lock);
    return r;
}
static ABI int32_t save_delete(const Delete *d) {
    if (!initialized) return ERR_NOT_INITIALIZED;
    if (!d || !d->dir || !valid_name(d->dir->data,sizeof(d->dir->data))) return ERR_PARAMETER;
    char base[600], host[640], meta[680];
    root(d->user,d->title ? d->title->data : NULL,base,sizeof(base));
    snprintf(host,sizeof(host),"%s/%s",base,d->dir->data);
    snprintf(meta,sizeof(meta),"%s/%s.sce_sys",base,d->dir->data);
    struct stat st;
    if (stat(host,&st)) return ERR_NOT_FOUND;
    remove_tree(host);
    remove_tree(meta);
    printf("Runtime: save data '%s' deleted\n",d->dir->data);
    return 0;
}
/* SQL-LIKE pattern: % any run, _ one character. */
static int like(const char *s, const char *p) {
    if (!*p) return !*s;
    if (*p=='%') { for (;;++s) { if (like(s,p+1)) return 1; if (!*s) return 0; } }
    return *s && (*p=='_' || *p==*s) && like(s+1,p+1);
}
typedef struct { char name[32]; Param param; } Entry;
static uint32_t sort_key, sort_order;
static int compare(const void *a, const void *b) {
    const Entry *x=a, *y=b;
    int c = sort_key==1 ? (x->param.user_param>y->param.user_param)-(x->param.user_param<y->param.user_param)
          : sort_key==3 ? (x->param.mtime>y->param.mtime)-(x->param.mtime<y->param.mtime)
          : strcmp(x->name,y->name);
    return sort_order ? -c : c;
}
static ABI int32_t save_search(const SearchCond *cond, SearchResult *result) {
    if (!initialized) return ERR_NOT_INITIALIZED;
    if (!cond || !result) return ERR_PARAMETER;
    char base[600];
    root(cond->user,cond->title ? cond->title->data : NULL,base,sizeof(base));
    Entry *entries=NULL; size_t count=0, capacity=0;
    DIR *d=opendir(base);
    for (struct dirent *e; d && (e=readdir(d));) {
        size_t n=strlen(e->d_name);
        if (e->d_name[0]=='.' || n>=32 || (n>8 && !strcmp(e->d_name+n-8,".sce_sys"))) continue;
        if (cond->dir && cond->dir->data[0] && !like(e->d_name,cond->dir->data)) continue;
        if (count==capacity) { capacity=capacity ? capacity*2 : 16; entries=realloc(entries,capacity*sizeof(*entries)); }
        snprintf(entries[count].name,32,"%s",e->d_name);
        char meta[680]; snprintf(meta,sizeof(meta),"%s/%s.sce_sys",base,e->d_name);
        read_param(meta,&entries[count].param);
        ++count;
    }
    if (d) closedir(d);
    sort_key=cond->key; sort_order=cond->order;
    if (count) qsort(entries,count,sizeof(*entries),compare);
    result->hits=(uint32_t)count;
    uint32_t set=count<result->names_capacity ? (uint32_t)count : result->names_capacity;
    for (uint32_t i=0;i<set;++i) {
        if (result->names) memcpy(result->names[i].data,entries[i].name,32);
        if (result->params) result->params[i]=entries[i].param;
        if (result->infos) { memset(&result->infos[i],0,sizeof(SearchInfo)); result->infos[i].blocks=32768; result->infos[i].free_blocks=16384; }
    }
    result->set_count=set;
    free(entries);
    return 0;
}
/* SaveDataMemory: one fixed-size blob per user/title. */
static ABI int32_t memory_setup(int32_t user, uint64_t size, const Param *param) {
    if (!initialized) return ERR_NOT_INITIALIZED;
    if (!size) return ERR_PARAMETER;
    char base[600], dir[640];
    root(user,NULL,base,sizeof(base));
    snprintf(dir,sizeof(dir),"%s.memory",base);
    if (make_dirs(dir)) return ERR_INTERNAL;
    pthread_mutex_lock(&lock);
    snprintf(memory_path,sizeof(memory_path),"%s/memory.dat",dir);
    FILE *f=fopen(memory_path,"r+b");
    if (!f) f=fopen(memory_path,"w+b");
    int32_t r=ERR_INTERNAL;
    if (f) {
        fseek(f,0,SEEK_END);
        if ((uint64_t)ftell(f)<size && ftruncate(fileno(f),(off_t)size)) goto done;
        memory_size=size; r=0;
    done:
        fclose(f);
    }
    if (!r && param) write_param(dir,param);
    pthread_mutex_unlock(&lock);
    if (!r) printf("Runtime: save data memory ready (%llu bytes)\n",(unsigned long long)size);
    return r;
}
static int32_t memory_io(void *buffer, uint64_t size, int64_t offset, int write) {
    if (!initialized) return ERR_NOT_INITIALIZED;
    if (!buffer || offset<0) return ERR_PARAMETER;
    pthread_mutex_lock(&lock);
    if (!memory_size) { pthread_mutex_unlock(&lock); return ERR_MEMORY_NOT_READY; }
    if ((uint64_t)offset+size>memory_size) { pthread_mutex_unlock(&lock); return ERR_PARAMETER; }
    FILE *f=fopen(memory_path,"r+b");
    int32_t r=ERR_INTERNAL;
    if (f && !fseek(f,offset,SEEK_SET) &&
        (write ? fwrite(buffer,1,size,f) : fread(buffer,1,size,f))==size) r=0;
    if (f) fclose(f);
    if (!r && write) ++memory_writes;
    pthread_mutex_unlock(&lock);
    return r;
}
static ABI int32_t memory_get(int32_t user, void *buffer, uint64_t size, int64_t offset) { (void)user; return memory_io(buffer,size,offset,0); }
static ABI int32_t memory_set(int32_t user, void *buffer, uint64_t size, int64_t offset) { (void)user; return memory_io(buffer,size,offset,1); }

/* SaveDataMemory2 (The Last Guardian): same single blob, struct-based arguments and a slot id.
 * Only slot 0 is stored; the game uses one 1 MiB slot plus an icon. */
typedef struct { void *buffer; uint64_t size; int64_t offset; uint8_t reserved[40]; } MemoryData;
typedef struct { uint32_t option; int32_t user; uint64_t memory_size, icon_memory_size;
                 const Param *init_param; const Icon *init_icon; uint32_t slot; uint8_t reserved[20]; } MemorySetup2;
typedef struct { uint64_t existed_size; uint8_t reserved[16]; } MemorySetupResult;
typedef struct { int32_t user; uint8_t pad[4]; MemoryData *data; Param *param; Icon *icon;
                 uint32_t slot; uint8_t reserved[28]; } MemoryGet2;
typedef struct { int32_t user; uint8_t pad[4]; const MemoryData *data; const Param *param; const Icon *icon;
                 uint32_t count, slot; uint8_t reserved[24]; } MemorySet2;
typedef struct { int32_t user; uint32_t slot, option; uint8_t reserved[28]; } MemorySync;
_Static_assert(sizeof(MemoryData)==64 && sizeof(MemorySetup2)==64 && sizeof(MemorySetupResult)==24,"memory2 layouts");
_Static_assert(sizeof(MemoryGet2)==64 && sizeof(MemorySet2)==64 && sizeof(MemorySync)==40,"memory2 layouts");

static ABI int32_t memory_setup2(const MemorySetup2 *setup, MemorySetupResult *result) {
    if (!setup || setup->slot) return ERR_PARAMETER;
    char base[600], path[700];
    root(setup->user,NULL,base,sizeof(base));
    snprintf(path,sizeof(path),"%s.memory/memory.dat",base);
    struct stat st;
    uint64_t existed=stat(path,&st) ? 0 : (uint64_t)st.st_size;
    int32_t r=memory_setup(setup->user,setup->memory_size,existed ? NULL : setup->init_param);
    if (!r && result) { memset(result,0,sizeof(*result)); result->existed_size=existed; }
    return r;
}
static ABI int32_t memory_get2(const MemoryGet2 *get) {
    if (!get || get->slot) return ERR_PARAMETER;
    if (get->param) memset(get->param,0,sizeof(*get->param));
    if (get->icon && get->icon->buffer) ((Icon *)get->icon)->data_size=0;
    return get->data ? memory_io(get->data->buffer,get->data->size,get->data->offset,0) : 0;
}
static ABI int32_t memory_set2(const MemorySet2 *set) {
    if (!set || set->slot) return ERR_PARAMETER;
    for (uint32_t i=0;set->data && i<(set->count ? set->count : 1);++i) {
        int32_t r=memory_io(set->data[i].buffer,set->data[i].size,set->data[i].offset,1);
        if (r) return r;
    }
    return 0;
}
static ABI int32_t memory_sync(const MemorySync *sync) {
    (void)sync; /* every write is flushed when its FILE is closed */
    return memory_size ? 0 : ERR_MEMORY_NOT_READY;
}

static const RuntimeExport exports[]={
    {"sceSaveDataInitialize",save_initialize}, {"sceSaveDataInitialize2",save_initialize},
    {"sceSaveDataInitialize3",save_initialize}, {"sceSaveDataTerminate",save_terminate},
    {"sceSaveDataMount",save_mount}, {"sceSaveDataMount2",save_mount2}, {"sceSaveDataUmount",save_umount},
    {"sceSaveDataSetParam",save_set_param}, {"sceSaveDataSaveIcon",save_icon}, {"sceSaveDataDelete",save_delete},
    {"sceSaveDataGetParam",save_get_param},
    {"sceSaveDataUmountWithBackup",save_umount_backup}, {"sceSaveDataGetEventResult",save_get_event},
    {"sceSaveDataDirNameSearch",save_search},
    {"sceSaveDataSetupSaveDataMemory",memory_setup}, {"sceSaveDataGetSaveDataMemory",memory_get},
    {"sceSaveDataSetSaveDataMemory",memory_set},
    {"sceSaveDataSetupSaveDataMemory2",memory_setup2}, {"sceSaveDataGetSaveDataMemory2",memory_get2},
    {"sceSaveDataSetSaveDataMemory2",memory_set2}, {"sceSaveDataSyncSaveDataMemory",memory_sync},
};
uintptr_t runtime_savedata_resolve(const char *name) { return RUNTIME_LOOKUP(exports,name); }
void runtime_savedata_report(void) { printf("Runtime: save data mounts=%zu, memory writes=%zu\n",mounts_done,memory_writes); }

