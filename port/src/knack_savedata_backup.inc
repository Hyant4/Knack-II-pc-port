/* SPDX-License-Identifier: GPL-2.0-or-later
 * SaveData backup ABI: shadPS4 e6c79a5 savedata.cpp/save_backup.h,
 * Copyright 2024-2026 shadPS4 Emulator Project.
 * Backups complete synchronously. A manifest is published only after all files
 * and metadata have been copied; earlier generations remain recoverable.
 * This file is included by runtime_savedata.c and uses its module lock.
 */
typedef struct {
    uint32_t type; int32_t error,user; uint8_t pad[4];
    TitleId title; DirName dir; uint8_t reserved[40];
} SaveEvent;
_Static_assert(sizeof(SaveEvent)==104,"OrbisSaveDataEvent layout");
static SaveEvent save_events[64];
static unsigned event_head,event_count,backup_serial;

static int backup_is_regular(const char *path,int *directory) {
#ifdef _WIN32
    DWORD attributes=GetFileAttributesA(path);
    if (attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT)) return -1;
    *directory=(attributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
    return 0;
#else
    struct stat st;
    if (lstat(path,&st) || (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode))) return -1;
    *directory=S_ISDIR(st.st_mode);
    return 0;
#endif
}
static int backup_copy_tree(const char *source,const char *destination,unsigned depth) {
    int directory;
    if (depth>16 || backup_is_regular(source,&directory)) return -1;
    if (!directory) {
        FILE *in=fopen(source,"rb");
        if (!in) return -1;
        FILE *out=fopen(destination,"wb");
        if (!out) { fclose(in); return -1; }
        char data[65536]; size_t n; int failed=0;
        while ((n=fread(data,1,sizeof(data),in))) {
            if (fwrite(data,1,n,out)!=n) { failed=1; break; }
        }
        if (ferror(in) || fflush(out) || fsync(fileno(out))) failed=1;
        fclose(in);
        if (fclose(out)) failed=1;
        return failed ? -1 : 0;
    }
    if (mkdir(destination,0755)) return -1; /* Destination must be a new generation. */
    DIR *dir=opendir(source);
    if (!dir) return -1;
    struct dirent *entry; int failed=0;
    while ((entry=readdir(dir))) {
        if (!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
        char from[1024],to[1024];
        if (snprintf(from,sizeof(from),"%s/%s",source,entry->d_name)>=(int)sizeof(from) ||
            snprintf(to,sizeof(to),"%s/%s",destination,entry->d_name)>=(int)sizeof(to) ||
            backup_copy_tree(from,to,depth+1)) { failed=1; break; }
    }
    closedir(dir);
    return failed ? -1 : 0;
}
static int32_t save_backup_slot(int slot) {
    if (event_count==64) return (int32_t)0x809f0003; /* BUSY; retain the mount for retry. */
    char base[700],generation[64],folder[800],target[850],pending[850],manifest[800];
    if (snprintf(base,sizeof(base),"%s/savedata-backups/%d/%s/%s",runtime_file_user_dir(),
                 slot_identity[slot].user,slot_identity[slot].title.data,slot_identity[slot].dir.data)>=(int)sizeof(base))
        return ERR_INTERNAL;
    if (make_dirs(base)) return ERR_INTERNAL;
    for (;;) {
        snprintf(generation,sizeof(generation),"gen-%lld-%u",(long long)time(NULL),++backup_serial);
        snprintf(folder,sizeof(folder),"%s/%s",base,generation);
        if (!mkdir(folder,0755)) break;
        if (errno!=EEXIST) return ERR_INTERNAL;
    }
    snprintf(target,sizeof(target),"%s/data",folder);
    if (backup_copy_tree(slots[slot].host,target,0)) return ERR_INTERNAL;
    snprintf(target,sizeof(target),"%s/metadata",folder);
    if (backup_copy_tree(slots[slot].meta,target,0)) return ERR_INTERNAL;
    Param param;
    if (read_param(target,&param)) return ERR_BROKEN;
    snprintf(pending,sizeof(pending),"%s/current.tmp",folder);
    snprintf(manifest,sizeof(manifest),"%s/current.txt",base);
    FILE *file=fopen(pending,"wb");
    if (!file) return ERR_INTERNAL;
    int failed=fprintf(file,"%s\n",generation)<0;
    if (fflush(file) || fsync(fileno(file))) failed=1;
    if (fclose(file)) failed=1;
    if (failed) return ERR_INTERNAL;
#ifdef _WIN32
    if (!MoveFileExA(pending,manifest,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return ERR_INTERNAL;
#else
    if (rename(pending,manifest)) return ERR_INTERNAL;
#endif
    SaveEvent *event=&save_events[(event_head+event_count)%64];
    memset(event,0,sizeof(*event));
    event->type=1; event->user=slot_identity[slot].user;
    event->title=slot_identity[slot].title; event->dir=slot_identity[slot].dir;
    ++event_count;
    printf("SaveData: backup complete for %s/%s (%s)\n",event->title.data,event->dir.data,generation);
    return 0;
}
static ABI int32_t save_get_event(const void *param,SaveEvent *event) {
    (void)param;
    if (!initialized) return ERR_NOT_INITIALIZED;
    if (!event) return ERR_PARAMETER;
    pthread_mutex_lock(&lock);
    if (!event_count) { pthread_mutex_unlock(&lock); return ERR_NOT_FOUND; }
    *event=save_events[event_head];
    event_head=(event_head+1)%64; --event_count;
    pthread_mutex_unlock(&lock);
    return 0;
}
