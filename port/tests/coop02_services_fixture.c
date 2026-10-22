/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Test-only UI/filesystem boundaries; real UserService and real runtime lookup. */
int coop02_file_calls, coop02_save_calls;
/* System services contracts: dialogs (progress bars run until closed), PlayGo, trophies. */
#undef NDEBUG /* the checks are asserts; release builds define NDEBUG */
#include "../src/runtime_services.c"
#include <assert.h>


int tlggpu_text_input_begin(const char *text, const char *prompt) { (void)text; (void)prompt; return 0; }
int tlggpu_text_input_poll(char *text, uint64_t size) { (void)text; (void)size; return 0; }
int64_t runtime_file_open(const char *path, int flags, int mode) { (void)path; (void)flags; (void)mode; ++coop02_file_calls; return -1; }
int64_t runtime_file_read(int fd, void *buffer, uint64_t size) { (void)fd; (void)buffer; (void)size; ++coop02_file_calls; return -1; }
int64_t runtime_file_close(int fd) { (void)fd; ++coop02_file_calls; return 0; }
static int choice_next=-1, message_next=-1, message_code=0, progress_shown=0, toasts=0;
int tlggpu_save_list_begin(int kind, const TlgSaveEntry *entries, int count, int focus) { (void)kind; (void)entries; (void)focus; return count>0; }
int tlggpu_save_list_poll(void) { return choice_next; }
int tlggpu_save_message_begin(int kind, int code, uint64_t value, const char *text, int buttons, const TlgSaveEntry *entry) {
    (void)kind; (void)value; (void)text; (void)buttons; (void)entry; message_code=code; return 1;
}
int tlggpu_save_message_poll(void) { return message_next; }
void tlggpu_save_dialog_close(void) {}
void tlggpu_save_progress(int kind, int active, int percent, const char *text) { (void)kind; (void)percent; (void)text; progress_shown=active; }
void tlggpu_save_toast(int kind) { (void)kind; ++toasts; }
int runtime_savedata_param(int32_t user, const char *dir, void *param) { (void)user; (void)dir; (void)param; ++coop02_save_calls; return -1; }
int runtime_savedata_icon(int32_t user, const char *dir, char *out, size_t size) { (void)user; (void)dir; (void)out; (void)size; ++coop02_save_calls; return -1; }
uint64_t runtime_savedata_size(int32_t user, const char *dir) { (void)user; (void)dir; ++coop02_save_calls; return 0; }


static const struct { const char *nid, *symbol; } import_names[]={
#include "../src/import_names.inc"
#include "../src/knack_import_names.inc"
};
/* Symbol name for a scoped NID, or NULL when this eboot/libc never imports it. */
const char *runtime_symbol(const char *nid) {
    for (size_t i=0;i<sizeof(import_names)/sizeof(*import_names);++i)
        if (!strcmp(nid,import_names[i].nid)) return import_names[i].symbol;
    return NULL;
}
/* Same symbol name means same contract whether imported from libkernel or
 * libScePosix, so newer modules resolve by name through runtime_symbol. */
uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *nid) {
    const char *symbol=runtime_symbol(nid);
    if (!symbol) return 0;
    for (size_t i=0;i<count;++i) if (!strcmp(symbol,table[i].name)) return (uintptr_t)table[i].function;
    return 0;
}

int coop02_fixture_ui_state(void) { return message_code+progress_shown+toasts; }
