/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Real copied UserService, ABI entry points and runtime NID lookup; no guest/game/files. */
#undef NDEBUG
#include "coop02_api.h"
#include <pthread.h>
#include <string.h>
#ifdef _WIN32
#define setenv(name,value,overwrite) _putenv_s(name,value)
#endif
extern int coop02_file_calls, coop02_save_calls;

typedef struct { uint32_t before; int32_t ids[4]; uint32_t after; } ListGuard;
typedef struct { uint32_t before; int32_t data[2]; uint32_t after; } EventGuard;
static ListGuard list_buffer(void) {
    return (ListGuard){.before=0x12345678,.ids={91,92,93,94},.after=0x87654321};
}
static EventGuard event_buffer(void) {
    return (EventGuard){.before=0x12345678,.data={91,92},.after=0x87654321};
}
static void verify_guards(uint32_t before,uint32_t after) {
    assert(before==0x12345678 && after==0x87654321);
}
static void verify_list(Coop02Users *u,int32_t second) {
    ListGuard b=list_buffer();
    assert(u->list(b.ids)==0);
    verify_guards(b.before,b.after);
    assert(b.ids[0]==1 && b.ids[1]==second && b.ids[2]==-1 && b.ids[3]==-1);
}
static void verify_name(Coop02Users *u,int32_t id,const char *expected) {
    char b[128]; memset(b,0x5a,sizeof(b));
    const size_t needed=strlen(expected)+1;
    assert(needed<sizeof(b));
    assert(u->name(id,b,needed-1)==COOP02_SHORT);
    for (size_t i=0;i<sizeof(b);++i) assert(b[i]==0x5a);
    assert(u->name(id,b,needed)==0 && !strcmp(b,expected));
    assert(b[needed]==0x5a);
    assert(u->name(id,b,0)==COOP02_SHORT);
}
typedef struct { Coop02Users users; int32_t result; EventGuard event; } Consumer;
static void *consume(void *data) {
    Consumer *c=data;
    assert(c->users.initialize(NULL)==0 && c->users.initialize2(-1)==0);
    c->result=c->users.event(c->event.data);
    verify_guards(c->event.before,c->event.after);
    return NULL;
}
int main(int argc,char **argv) {
    assert(argc==3);
    setvbuf(stdout,NULL,_IONBF,0);
    const char *scenario=argv[1];
    const int32_t second=(int32_t)strtol(argv[2],NULL,10);
    const int invalid=!strcmp(scenario,"invalid");
    Coop02Users u=coop02_users();
    /* Invalid pointers cannot consume LOGIN or freeze the configuration. */
    assert(u.initial(NULL)==COOP02_INVALID && u.list(NULL)==COOP02_INVALID);
    assert(u.event(NULL)==COOP02_INVALID && u.name(1,NULL,64)==COOP02_INVALID);
    if (!strcmp(scenario,"null-does-not-freeze")) {
        setenv("TLG_LOCAL_USERS","2",1);
        setenv("TLG_LOCAL_P2_USER_ID",argv[2],1);
    }
    if (invalid) {
        assert(u.initialize(NULL)==COOP02_INVALID && u.initialize2(0)==COOP02_INVALID);
        int32_t id=99;
        ListGuard b=list_buffer(); EventGuard e=event_buffer(); char name[16]; memset(name,0x5a,sizeof(name));
        assert(u.initial(&id)==COOP02_INVALID && id==99);
        assert(u.list(b.ids)==COOP02_INVALID && b.ids[0]==91 && b.ids[3]==94);
        assert(u.event(e.data)==COOP02_INVALID && e.data[0]==91 && e.data[1]==92);
        assert(u.name(1,name,sizeof(name))==COOP02_INVALID && name[0]==0x5a);
        verify_guards(b.before,b.after); verify_guards(e.before,e.after);
        assert(u.terminate()==0);
        setenv("TLG_LOCAL_USERS","1",1);
        assert(u.initialize(NULL)==COOP02_INVALID); /* Configuration error is also frozen. */
        assert(!coop02_file_calls && !coop02_save_calls);
        puts("PASS: invalid configuration fails explicitly, preserves buffers, creates no sessions/events, no retry duplication");
        return 0;
    }
    if (!strcmp(scenario,"prequery")) {
        verify_list(&u,second); /* Original HLE also permits queries before initialize. */
        EventGuard e=event_buffer();
        assert(u.event(e.data)==0 && e.data[0]==0 && e.data[1]==1);
        assert(u.initialize(NULL)==0 && u.initialize2(0)==0);
        if (second!=-1) assert(u.event(e.data)==0 && e.data[0]==0 && e.data[1]==second);
        assert(u.event(e.data)==COOP02_NO_EVENT && !coop02_file_calls && !coop02_save_calls);
        puts("PASS: queries before initialize preserve original availability and never requeue consumed LOGIN");
        return 0;
    }
    if (!strcmp(scenario,"threads")) {
        Consumer consumers[8]; pthread_t threads[8];
        for (int i=0;i<8;++i) {
            consumers[i]=(Consumer){.users=u,.event=event_buffer()};
            assert(pthread_create(&threads[i],NULL,consume,&consumers[i])==0);
        }
        int initial_events=0,second_events=0,empty=0;
        for (int i=0;i<8;++i) {
            assert(pthread_join(threads[i],NULL)==0);
            Consumer *c=&consumers[i];
            if (c->result==COOP02_NO_EVENT) { ++empty; assert(c->event.data[0]==91 && c->event.data[1]==92); }
            else {
                assert(c->result==0 && c->event.data[0]==0);
                if (c->event.data[1]==1) ++initial_events;
                else { assert(c->event.data[1]==second); ++second_events; }
            }
        }
        assert(initial_events==1 && second_events==1 && empty==6);
        verify_list(&u,second);
        assert(!coop02_file_calls && !coop02_save_calls);
        puts("PASS: concurrent initialize/event consumers deliver exactly one LOGIN per real local user");
        return 0;
    }
    assert(u.initialize(NULL)==0 && u.initialize2(0)==0);
    int32_t initial=99;
    assert(u.initial(&initial)==0 && initial==1);
    verify_list(&u,second);
    const char *p1=getenv("TLG_USER_NAME"); if (!p1) p1="Hunter";
    verify_name(&u,1,p1);
    if (second!=-1) verify_name(&u,second,strcmp(p1,"Player 2") ? "Player 2" : "Local Player 2");
    char name[32]; memset(name,0x5a,sizeof(name));
    assert(u.name(-1,name,sizeof(name))==COOP02_INVALID && name[0]==0x5a);
    assert(u.name(0,name,sizeof(name))==COOP02_INVALID);
    assert(u.name(second==99 ? 98 : 99,name,sizeof(name))==COOP02_INVALID);
    if (second==-1) assert(u.name(2,name,sizeof(name))==COOP02_INVALID);

    EventGuard e=event_buffer();
    assert(u.event(e.data)==0 && e.data[0]==0 && e.data[1]==1);
    verify_guards(e.before,e.after);
    assert(u.initialize(NULL)==0 && u.initialize2(16)==0 && u.event(NULL)==COOP02_INVALID);
    assert(u.terminate()==0 && u.initialize(NULL)==0); /* Pending P2 LOGIN must survive too. */
    if (second!=-1) {
        e=event_buffer();
        assert(u.event(e.data)==0 && e.data[0]==0 && e.data[1]==second);
        verify_guards(e.before,e.after);
    }
    e=event_buffer();
    assert(u.event(e.data)==COOP02_NO_EVENT && e.data[0]==91 && e.data[1]==92);
    assert(u.terminate()==0 && u.terminate()==0 && u.initialize(NULL)==0);
    assert(u.event(e.data)==COOP02_NO_EVENT); /* Same process/session, no synthetic logout/login. */
    verify_list(&u,second);
    setenv("TLG_LOCAL_USERS",second==-1 ? "2" : "1",1);
    setenv("TLG_LOCAL_P2_USER_ID","444",1);
    assert(u.initialize2(0)==0);
    verify_list(&u,second); /* Environment edits cannot replace users in an existing session. */
    assert(u.event(e.data)==COOP02_NO_EVENT && !coop02_file_calls && !coop02_save_calls);
    printf("PASS: UserService %s initial=1 second=%d, names, four-ID list, LOGIN, reinitialize, errors, zero save/file calls\n",scenario,second);
    return 0;
}
