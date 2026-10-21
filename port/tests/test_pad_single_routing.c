/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Reuse the existing SDL virtual-device harness; replace only its entry point.
 * COOP11_HOST selects the unmodified COOP-02 real-provider harness when requested. */
#ifndef COOP11_HOST
#define COOP11_HOST "test_pad_coop.c"
#endif
#define main coop11_reference_suite_main
#include COOP11_HOST
#undef main

static void release_buttons(Device *d) {
    for (int b=0;b<SDL_GAMEPAD_BUTTON_COUNT;++b)
        assert(SDL_SetJoystickVirtualButton(d->joystick,b,false));
    pump();
}
static void shutdown_devices(Device *a,Device *b,Device *c) {
    if (a->joystick) detach(a);
    if (b->joystick) detach(b);
    if (c->joystick) detach(c);
    SDL_Quit(); /* SDL owns remaining open gamepad references until process exit. */
}
int main(int argc,char **argv) {
    assert(argc>=2);
    setvbuf(stdout,NULL,_IONBF,0);
    const int coop=!strcmp(argv[1],"--coop-closed-handle");
    int32_t first=1,second=7001;
#ifdef COOP11_REAL_USERS
    Coop02Users users=coop02_users();
    int32_t ids[4];
    assert(users.initialize(NULL)==0 && users.initial(&first)==0 && users.list(ids)==0);
    second=ids[1];
    assert(ids[0]==first && ids[2]==-1 && ids[3]==-1);
    assert(coop ? second!=-1 : second==-1);
#else
    if (argc>2 && !strcmp(argv[2],"--nonstandard")) first=41;
    pad_test_initial=first;
    pad_test_users[0]=first; pad_test_users[1]=coop ? second : -1;
#endif
    setenv("SDL_VIDEODRIVER","dummy",1);
    setenv("SDL_AUDIODRIVER","dummy",1);
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0x1d50/0x6189");
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));
    assert(pad_init()==0 && pad_open(first,0,0,NULL)==1);
    Device a={0},b={0},c={0};
#ifndef COOP11_REAL_USERS
    if (coop) pad_test_service_error=1; /* Fail before the first sample, after validated open. */
#endif
    attach(&a,"COOP11 A"); attach(&b,"COOP11 B");
    PadData data=read_player(1); neutral(&data);
    if (!strcmp(argv[1],"--probe-unplug")) {
        button(&a,SDL_GAMEPAD_BUTTON_EAST,true);
        assert(read_player(1).buttons==BTN_CIRCLE);
        detach(&a);
    }
    button(&b,SDL_GAMEPAD_BUTTON_SOUTH,true);
    data=read_player(1);
#ifndef COOP11_REAL_USERS
    pad_test_service_error=0;
#endif
    if (!coop && !(data.buttons & BTN_CROSS)) {
        printf("REGRESSION: single-user P1 ignores surviving/active second device; buttons=0x%x\n",data.buttons);
        shutdown_devices(&a,&b,&c);
        return !strcmp(argv[1],"--probe-unplug") ? 43 : 42;
    }
    if (!coop && (!strcmp(argv[1],"--probe-switch") || !strcmp(argv[1],"--probe-unplug"))) {
        pass("single-user active/surviving second device controls P1");
        shutdown_devices(&a,&b,&c);
        return 0;
    }
    if (coop) {
        assert(!data.buttons); /* P2 exists in UserService, even though handle 2 is closed. */
        assert(pad_open(second,0,0,NULL)==2 && read_player(2).buttons==BTN_CROSS);
#ifndef COOP11_REAL_USERS
        pad_test_service_error=1;
        assert(!read_player(1).buttons && read_player(2).buttons==BTN_CROSS);
        pad_test_service_error=0; /* Failed snapshot cannot collapse an established session to P1. */
#endif
        assert(pad_close(2)==0 && !read_player(1).buttons);
        assert(pad_open(second,0,0,NULL)==2 && read_player(2).buttons==BTN_CROSS);
        attach(&c,"COOP11 C"); button(&c,SDL_GAMEPAD_BUTTON_NORTH,true); SDL_Delay(1050);
        assert(!read_player(1).buttons && read_player(2).buttons==BTN_CROSS);
        release_buttons(&c);
        detach(&a);
        assert(!read_player(1).buttons && read_player(2).buttons==BTN_CROSS);
        assert(pad_close(1)==0 && pad_close(2)==0);
        pass("two registered users keep fixed bindings with P2 handle closed/reopened and third-device activity");
    } else {
        assert(data.buttons==BTN_CROSS);
        release_buttons(&b); button(&a,SDL_GAMEPAD_BUTTON_WEST,true);
        assert(read_player(1).buttons==BTN_SQUARE);
        release_buttons(&a);
        assert(SDL_SetJoystickVirtualAxis(b.joystick,SDL_GAMEPAD_AXIS_LEFTX,32767)); pump();
        assert(read_player(1).left_x==255);
        assert(SDL_SetJoystickVirtualAxis(b.joystick,SDL_GAMEPAD_AXIS_LEFTX,0)); pump();
        button(&a,SDL_GAMEPAD_BUTTON_EAST,true); assert(read_player(1).buttons==BTN_CIRCLE);
        release_buttons(&a);
        assert(SDL_SetJoystickVirtualAxis(b.joystick,SDL_GAMEPAD_AXIS_LEFTX,16000)); pump();
        assert(read_player(1).left_x==128); /* Exact legacy activity threshold: >16000. */
        assert(SDL_SetJoystickVirtualAxis(b.joystick,SDL_GAMEPAD_AXIS_LEFTX,16001)); pump();
        assert(read_player(1).left_x>128);
        assert(SDL_SetJoystickVirtualAxis(b.joystick,SDL_GAMEPAD_AXIS_LEFTX,0)); pump();
        uint8_t vibration[]={73,109};
        assert(pad_vibration(1,vibration)==0 && b.low==73*257 && b.high==109*257 && !a.low);

        /* A's staged touch and simulated orientation must not survive a handoff to B. */
        button(&a,SDL_GAMEPAD_BUTTON_BACK,true); data=read_player(1); assert(data.touch_count==1);
        release_buttons(&a); button(&b,SDL_GAMEPAD_BUTTON_SOUTH,true);
        data=read_player(1); assert(data.buttons==BTN_CROSS && !data.touch_count);
        release_buttons(&b); button(&a,SDL_GAMEPAD_BUTTON_MISC1,true);
        data=read_player(1); assert(fabsf(data.orientation[2])>0.01f);
        release_buttons(&a); button(&b,SDL_GAMEPAD_BUTTON_SOUTH,true);
        data=read_player(1); assert(data.buttons==BTN_CROSS && data.orientation[3]==1 && !data.touch_count);
        release_buttons(&b); button(&a,SDL_GAMEPAD_BUTTON_EAST,true);
        assert(read_player(1).buttons==BTN_CIRCLE);
        detach(&a); button(&b,SDL_GAMEPAD_BUTTON_SOUTH,true);
        data=read_player(1); assert(data.buttons==BTN_CROSS);
        assert(pad_close(1)==0 && pad_open(first,0,0,NULL)==1 && read_player(1).buttons==BTN_CROSS);
        release_buttons(&b);
        attach(&a,"COOP11 A reconnected"); button(&a,SDL_GAMEPAD_BUTTON_WEST,true); SDL_Delay(1050);
        assert(read_player(1).buttons==BTN_SQUARE);
        release_buttons(&a); attach(&c,"COOP11 third"); button(&c,SDL_GAMEPAD_BUTTON_NORTH,true); SDL_Delay(1050);
        assert(read_player(1).buttons==BTN_TRIANGLE); /* Default mode still accepts a third controller. */
        assert(pad_close(1)==0);
        pass("single-user activity, axes, rumble, handoff reset, unplug, reconnect, reopen and third device");
    }
#ifdef COOP11_REAL_USERS
    int32_t event[2];
    assert(users.event(event)==0 && event[0]==0 && event[1]==first);
    if (coop) assert(users.event(event)==0 && event[0]==0 && event[1]==second);
    assert(users.event(event)==COOP02_NO_EVENT);
    assert(!coop02_file_calls && !coop02_save_calls);
    pass("routing queries leave real UserService LOGIN queue and saves untouched");
#endif
    shutdown_devices(&a,&b,&c);
    return 0;
}
