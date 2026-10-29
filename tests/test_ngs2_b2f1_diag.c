/* B2-F1 passive accounting reconciliation test. */
#define main ngs2_overflow_existing_tests_main
#include "test_ngs2_overflow.c"
#undef main

int main(void) {
    Ngs2System *sys=calloc(1,sizeof(*sys));
    Ngs2Rack *rack=calloc(1,sizeof(*rack));
    Ngs2Voice *voices=calloc(2,sizeof(*voices));
    Ngs2Atrac9Voice *a=calloc(1,sizeof(*a));
    Ngs2Atrac9Voice *h=calloc(1,sizeof(*h));
    assert(sys && rack && voices && a && h);

    ngs2_systems=sys;
    sys->racks=rack;
    rack->system=sys;
    rack->rack_id=0x1000;
    rack->instance=3;
    rack->capacity=2;
    rack->voices=voices;

    voices[0].rack=rack;
    voices[0].index=0;
    voices[0].atrac9=h;
    voices[0].internal_state=NGS2_VOICE_PLAYING;
    voices[0].waveform=(Ngs2Waveform){
        .format=NGS2_WAVEFORM_HEVAG,.channels=1,
        .sample_rate=48000,.valid=1
    };

    voices[1].rack=rack;
    voices[1].index=1;
    voices[1].atrac9=a;
    voices[1].internal_state=NGS2_VOICE_PAUSED;
    voices[1].waveform=(Ngs2Waveform){
        .format=NGS2_WAVEFORM_ATRAC9,.channels=2,
        .sample_rate=48000,.codec_config=1,.valid=1
    };

    h->queued_compressed_bytes=768;
    h->segment_count=1;
    h->segments[0].byte_count=512;
    h->segment_reservations=1;
    h->generation=7;

    a->queued_compressed_bytes=768;
    a->active_valid=1;
    a->active.byte_count=768;
    a->generation=9;
    a->decoded_bytes=128;

    sys->compressed_bytes=1536;
    ngs2_compressed_bytes=1536;

    char line[768]={0},top[1600]={0};
    ngs2_b2f1_snapshot_locked(sys,line,sizeof(line),top,sizeof(top));

    assert(strstr(line,
        "global=1536 all_system=1536 system=1536 sum_voice=1536"));
    assert(strstr(line,"codec_h/a/o=768/768/0"));
    assert(strstr(line,
        "state_stop/play/pause/release/other=0/768/768/0/0"));
    assert(strstr(line,
        "queued=512 active=768 unpublished=256 underaccount=0"));
    assert(strstr(line,
        "reservations=1 queue_events=1 active_events=1"));
    assert(strstr(top,"rack=0x1000/3"));
    assert(strstr(top,"gen=7"));
    assert(strstr(top,"gen=9"));

    /* A snapshot must not change the audio accounting or the queue. */
    assert(sys->compressed_bytes==1536 && ngs2_compressed_bytes==1536);
    assert(h->segment_count==1 && h->segment_reservations==1);
    assert(a->active_valid==1 && a->decoded_bytes==128);

    /* Deliberate inconsistency must remain visible, not corrected. */
    sys->compressed_bytes=1535;
    ngs2_b2f1_snapshot_locked(sys,line,sizeof(line),top,sizeof(top));
    assert(strstr(line,
        "global=1536 all_system=1535 system=1535 sum_voice=1536"));

    ngs2_systems=NULL;
    ngs2_compressed_bytes=0;
    free(h);free(a);free(voices);free(rack);free(sys);
    puts("ALL HLE-2.2D-B2-F1 DIAGNOSTIC TESTS PASSED");
    return 0;
}
