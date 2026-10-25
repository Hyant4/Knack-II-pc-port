/* Positive Cmd5 storage contracts. No bus injection or gain semantics assumed. */
#define main previous_mixer_tests_main
#include "test_ngs2_mixer.c"
#undef main
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"CMD5_CONTRACT_FAILED case=%s line=%d: %s\n",name,__LINE__,#x); return 1; } } while(0)
typedef struct {Ngs2Handle *sys,*foreign,*src_rack,*bus_rack,*foreign_rack,*v[4],*bus,*fv;} Fixture;
static void open_fixture(Fixture *f) {
    memset(f,0,sizeof(*f));unsigned char so[0x40],ro[0xa8];system_option(so);
    assert(!ngs2_system_create(so,NULL,&f->sys));assert(!ngs2_system_create(so,NULL,&f->foreign));
    rack_option(ro,4,"Sampler");assert(!ngs2_rack_create(f->sys,0x1000,ro,NULL,&f->src_rack));
    for(unsigned i=0;i<4;++i)assert(!ngs2_voice_handle(f->src_rack,i,&f->v[i]));
    rack_option(ro,1,"PreMaster");assert(!ngs2_rack_create(f->sys,0x2000,ro,NULL,&f->bus_rack));
    assert(!ngs2_voice_handle(f->bus_rack,0,&f->bus));
    assert(!ngs2_rack_create(f->foreign,0x2000,ro,NULL,&f->foreign_rack));
    assert(!ngs2_voice_handle(f->foreign_rack,0,&f->fv));
}
static int32_t connect_voice(Ngs2Handle *from,Ngs2Handle *to,uint32_t input,uint32_t output) {
    unsigned char cmd[0x18];command(cmd,sizeof(cmd),5);put_u32(cmd,8,input);put_u32(cmd,12,output);
    put_uptr(cmd,16,(uintptr_t)to);return ngs2_voice_control(from,cmd);
}
static int unchanged_connection(Ngs2Handle *from,Ngs2Handle *to,Ngs2Handle *rack) {
    Ngs2Voice *v=ngs2_voice_locked(from);
    return v && v->connection==to && v->connection_rack==ngs2_rack_locked(rack) &&
           v->connection_input==2 && v->connection_output==6;
}
static int run_case(const char *name) {
    Fixture f;open_fixture(&f);Ngs2Voice *v=ngs2_voice_locked(f.v[0]);
    if(!strcmp(name,"rack")) {
        REQUIRE(connect_voice(f.v[0],f.bus_rack,2,6)==0);
        REQUIRE(unchanged_connection(f.v[0],f.bus_rack,f.bus_rack));
    } else if(!strcmp(name,"voice")) {
        REQUIRE(connect_voice(f.v[0],f.bus,2,6)==0);
        REQUIRE(unchanged_connection(f.v[0],f.bus,f.bus_rack));
    } else if(!strcmp(name,"disconnect")) {
        REQUIRE(connect_voice(f.v[0],f.bus,2,6)==0);
        REQUIRE(connect_voice(f.v[0],NULL,9,11)==0);
        REQUIRE(!v->connection && !v->connection_rack && !v->connection_input && !v->connection_output);
    } else if(!strcmp(name,"invalid") || !strcmp(name,"foreign_voice") || !strcmp(name,"foreign_rack") || !strcmp(name,"self")) {
        REQUIRE(connect_voice(f.v[0],f.bus,2,6)==0);
        Ngs2Handle *target=!strcmp(name,"invalid")?(Ngs2Handle *)(uintptr_t)0xdeadbeef:
                          !strcmp(name,"foreign_voice")?f.fv:
                          !strcmp(name,"foreign_rack")?f.foreign_rack:f.v[0];
        REQUIRE(connect_voice(f.v[0],target,3,7)==NGS2_INVALID);
        REQUIRE(unchanged_connection(f.v[0],f.bus,f.bus_rack));
    } else if(!strcmp(name,"cycle2")) {
        REQUIRE(connect_voice(f.v[0],f.v[1],2,6)==0);
        REQUIRE(connect_voice(f.v[1],f.v[0],3,7)==NGS2_INVALID);
        REQUIRE(!ngs2_voice_locked(f.v[1])->connection);
        REQUIRE(unchanged_connection(f.v[0],f.v[1],f.src_rack));
    } else if(!strcmp(name,"chain_cycle")) {
        REQUIRE(connect_voice(f.v[0],f.v[1],2,6)==0);
        REQUIRE(connect_voice(f.v[1],f.v[2],0,0)==0);
        REQUIRE(connect_voice(f.v[2],f.v[3],0,0)==0);
        REQUIRE(connect_voice(f.v[3],f.bus,0,0)==0);
        REQUIRE(connect_voice(f.bus,f.v[0],0,0)==NGS2_INVALID);
        REQUIRE(!ngs2_voice_locked(f.bus)->connection);
    } else if(!strcmp(name,"rack_endpoint")) {
        REQUIRE(connect_voice(f.v[1],f.bus_rack,0,0)==0);
        REQUIRE(connect_voice(f.v[0],f.v[1],2,6)==0);
        REQUIRE(unchanged_connection(f.v[0],f.v[1],f.src_rack));
    } else if(!strcmp(name,"existing_cycle")) {
        /* Robustness against corrupt/legacy voice edges, not an API-reachable candidate state. */
        pthread_mutex_lock(&ngs2_lock);Ngs2Voice *a=ngs2_voice_locked(f.v[1]),*z=ngs2_voice_locked(f.v[2]);
        a->connection=f.v[2];a->connection_rack=z->rack;
        z->connection=f.v[1];z->connection_rack=a->rack;pthread_mutex_unlock(&ngs2_lock);
        REQUIRE(connect_voice(f.v[0],f.v[1],2,6)==NGS2_INVALID);REQUIRE(!v->connection);
    } else if(!strcmp(name,"destroy_rack") || !strcmp(name,"destroy_voice_target")) {
        Ngs2Handle *target=!strcmp(name,"destroy_rack")?f.bus_rack:f.bus;
        REQUIRE(connect_voice(f.v[0],target,2,6)==0);
        REQUIRE(!ngs2_rack_destroy(f.bus_rack,NULL));f.bus_rack=NULL;
        REQUIRE(ngs2_voice_locked(f.v[0])==v);
        REQUIRE(!v->connection && !v->connection_rack && !v->connection_input && !v->connection_output);
    } else if(!strcmp(name,"short_command")) {
        REQUIRE(connect_voice(f.v[0],f.bus,2,6)==0);
        unsigned char cmd[0x10];command(cmd,sizeof(cmd),5);
        REQUIRE(ngs2_voice_control(f.v[0],cmd)==NGS2_INVALID);
        REQUIRE(unchanged_connection(f.v[0],f.bus,f.bus_rack));
    } else if(!strcmp(name,"pcm_preserved")) {
        setup_waveform(f.v[0],4);set_voice_state(f.v[0],0);float matrix[32]={0};matrix[0]=1.0f;
        set_voice_matrix(f.v[0],matrix,32);pthread_mutex_lock(&ngs2_lock);push_synthetic_grain(v,16384);
        uint64_t generation=v->atrac9->generation;pthread_mutex_unlock(&ngs2_lock);
        REQUIRE(connect_voice(f.v[0],f.bus_rack,2,6)==0);
        REQUIRE(v->atrac9->generation==generation && v->atrac9->pcm_count==1);
        float out[256*8],aux[256*8];Ngs2RenderBuffer buffers[2]={{out,sizeof(out),0x18,8},{aux,sizeof(aux),0x18,8}};
        REQUIRE(!test_render_retry(f.sys,buffers,2));REQUIRE(fabsf(out[0]-0.125f)<1e-6f);
        REQUIRE(v->atrac9->pcm_count==0 && v->atrac9->generation==generation);
    } else if(!strcmp(name,"trace_rack")) {
        REQUIRE(connect_voice(f.v[0],f.bus_rack,2,6)==0);atomic_store(&ngs2_route_diag_setting,1);
        unsigned char cmd[0x18];command(cmd,sizeof(cmd),5);char line[512]={0},expected[80];
        pthread_mutex_lock(&ngs2_lock);ngs2_route_diag_locked(v,cmd,sizeof(cmd),5,line,sizeof(line));
        snprintf(expected,sizeof(expected),"target_rack=0x2000/%u",ngs2_rack_locked(f.bus_rack)->instance);
        pthread_mutex_unlock(&ngs2_lock);REQUIRE(strstr(line,expected));REQUIRE(strstr(line,"target_voice=-1"));
    } else {REQUIRE(!"unknown case");}
    REQUIRE(!ngs2_system_destroy(f.sys,NULL));REQUIRE(!ngs2_system_destroy(f.foreign,NULL));
    printf("PASS_CMD5_CASE %s\n",name);return 0;
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;setvbuf(stdout,NULL,_IONBF,0);
    ngs2_atrac9_setting=1;ngs2_render_mix_setting=1;
    ngs2_b2f2a_setting=0;ngs2_b2f2c_setting=0;
    atomic_store(&ngs2_audio_diag_setting,0);atomic_store(&ngs2_route_diag_setting,0);
    return run_case(argv[1]);
}
