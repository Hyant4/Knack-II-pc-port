/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Unmodified COOP-01 Pad + real COOP-02 UserService; SDL3 virtual devices, no game/GPU. */
#define _GNU_SOURCE
#undef NDEBUG
#include <assert.h>
#include <unistd.h>
#include <SDL3/SDL.h>
/* Deterministic keyboard state; do not mutate SDL's const internal array. */
static bool keyboard[SDL_SCANCODE_COUNT];
static const bool *test_keyboard_state(int *count) {
    if (count) *count=SDL_SCANCODE_COUNT;
    return keyboard;
}
#define SDL_GetKeyboardState test_keyboard_state
#include "../src/runtime_pad.c"
#undef SDL_GetKeyboardState
#include "coop02_api.h"
extern int coop02_file_calls, coop02_save_calls;
#ifdef _WIN32
#define setenv(name, value, overwrite) _putenv_s(name, value)
#endif

static TlgGpuMouse mouse_input;
static int mouse_takes, capture;
static TlgGpuInputSettings settings={.mouse_enabled=1,.sensitivity=1,.smoothing=0};
void tlggpu_mouse_take(TlgGpuMouse *out) {
    *out=mouse_input;
    mouse_input.dx=mouse_input.dy=0;
    mouse_input.pressed=0;
    ++mouse_takes;
}
void tlggpu_mouse_pad_hud(int a,float x,float y,int t,int c) { (void)a;(void)x;(void)y;(void)t;(void)c; }
void tlggpu_input_settings(TlgGpuInputSettings *out) { *out=settings; }
int tlggpu_overlay_captures_input(void) { return capture; }
void tlggpu_overlay_notice(const char *text,float seconds) { (void)text;(void)seconds; }
void tlggpu_graffiti_guide(int force) { (void)force; }
float tlggpu_sensor_aim_scale(void) { return 1; }
typedef struct { SDL_JoystickID id; SDL_Joystick *joystick; int calls; Uint16 low,high; } Device;
static bool SDLCALL rumble(void *userdata,Uint16 low,Uint16 high) {
    Device *d=userdata;
    ++d->calls; d->low=low; d->high=high;
    return true;
}
static void pump(void) { SDL_UpdateJoysticks(); SDL_UpdateGamepads(); }
static void attach(Device *d,const char *name) {
    SDL_VirtualJoystickTouchpadDesc touch={.nfingers=2};
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes=SDL_GAMEPAD_AXIS_COUNT; desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
    desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;
    desc.vendor_id=0x1d50; desc.product_id=0x6189;
    desc.name=name; desc.userdata=d; desc.Rumble=rumble;
    desc.ntouchpads=1; desc.touchpads=&touch;
    d->id=SDL_AttachVirtualJoystick(&desc); assert(d->id);
    d->joystick=SDL_OpenJoystick(d->id); assert(d->joystick);
    pump();
}
static void detach(Device *d) {
    SDL_CloseJoystick(d->joystick); d->joystick=NULL;
    assert(SDL_DetachVirtualJoystick(d->id)); pump();
}
static void button(Device *d,SDL_GamepadButton b,bool down) {
    assert(SDL_SetJoystickVirtualButton(d->joystick,b,down)); pump();
}
static PadData read_player(int handle) {
    PadData data;
    assert(pad_read_state(handle,&data)==0);
    return data;
}
static PadData await_connection(int handle,int connected) {
    PadData data={0};
    for (int n=0;n<140;++n) {
        pump(); data=read_player(handle);
        if (data.connected==connected) return data;
        SDL_Delay(10);
    }
    assert(data.connected==connected);
    return data;
}
static void neutral(const PadData *d) {
    assert(!d->buttons && !d->l2 && !d->r2 && !d->touch_count);
    assert(d->left_x==128 && d->left_y==128 && d->right_x==128 && d->right_y==128);
}
static void pass(const char *text) { printf("PASS: %s\n",text); }

int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    assert(argc==2);
    if (!strcmp(argv[1],"--single")) {
        Coop02Users users=coop02_users();
        assert(users.initialize(NULL)==0);
        int32_t ids[4];
        assert(users.list(ids)==0 && ids[0]==1 && ids[1]==-1 && ids[2]==-1 && ids[3]==-1);
        SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0x1d50/0x6189");
        assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));
        assert(pad_init()==0 && pad_open(1,0,0,NULL)==1);
        assert(pad_open(4099,0,0,NULL)==ERR_INVALID_ARG && pad_open(2,0,0,NULL)==ERR_INVALID_ARG);
        PadData data=read_player(1); neutral(&data); assert(data.connected);
        Device device={0}; attach(&device,"COOP single player");
        button(&device,SDL_GAMEPAD_BUTTON_EAST,true);
        for (int i=0;i<140;++i) {
            data=read_player(1);
            if (data.buttons==BTN_CIRCLE) break;
            SDL_Delay(10);
        }
        assert(data.buttons==BTN_CIRCLE);
        assert(pad_read_state(2,&data)==ERR_INVALID_HANDLE);
        assert(users.list(ids)==0 && ids[0]==1 && ids[1]==-1);
        int32_t event[2];
        assert(users.event(event)==0 && event[0]==0 && event[1]==1);
        assert(users.event(event)==COOP02_NO_EVENT);
        assert(pad_close(1)==0 && !coop02_file_calls && !coop02_save_calls);
        detach(&device);
        if (players[0].gamepad) SDL_CloseGamepad(players[0].gamepad);
        SDL_Quit();
        pass("real UserService + COOP-01 default keeps one user, one handle, keyboard fallback and physical-device input");
        return 0;
    }
    const int32_t p1=1, p2=(int32_t)strtol(argv[1],NULL,10);
    Coop02Users users=coop02_users();
    assert(users.initialize(NULL)==0);
    int32_t ids[4];
    assert(users.list(ids)==0 && ids[0]==p1 && ids[1]==p2 && ids[2]==-1 && ids[3]==-1);
    setenv("SDL_VIDEODRIVER","dummy",1);
    setenv("SDL_AUDIODRIVER","dummy",1);
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0x1d50/0x6189");
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));
    PadData data[2]; ControllerInfo info;
    assert(pad_open(p1,0,0,NULL)==ERR_NOT_INITIALIZED);
    assert(pad_init()==0 && pad_init()==0);
    if (p2!=2) assert(pad_open(2,0,0,NULL)==ERR_INVALID_ARG);
    assert(pad_open(999,0,0,NULL)==ERR_INVALID_ARG);
    assert(pad_open(-1,0,0,NULL)==ERR_INVALID_ARG);
    assert(pad_open(p1,1,0,NULL)==ERR_INVALID_ARG);
    assert(pad_open(p1,0,1,NULL)==ERR_INVALID_ARG);
    assert(pad_open(p1,0,0,NULL)==1);
    assert(pad_open(p1,0,0,NULL)==ERR_ALREADY_OPENED);
    assert(pad_read_state(2,&data[0])==ERR_INVALID_HANDLE);
    data[0]=read_player(1); assert(data[0].connected); neutral(&data[0]);
    pass("registered P2 has no open handle until scePadOpen; P1 keyboard fallback remains connected");

    /* Real UserService already owns both opaque IDs. Pad must leave its LOGIN queue intact. */
    assert(pad_open(p2,2,0,NULL)==2);
    assert(pad_open(p2,0,0,NULL)==ERR_ALREADY_OPENED);
    assert(pad_open(999,0,0,NULL)==ERR_INVALID_ARG);
    assert(pad_read_state(0,&data[0])==ERR_INVALID_HANDLE);
    assert(pad_read_state(3,&data[0])==ERR_INVALID_HANDLE);
    assert(pad_read_state(2,NULL)==ERR_INVALID_ARG);
    assert(pad_read(2,data,0)==ERR_INVALID_ARG && pad_read(2,data,65)==ERR_INVALID_ARG);
    assert(pad_read(3,NULL,0)==ERR_INVALID_HANDLE);
    assert(pad_read(2,data,64)==1 && !data[0].connected);
    assert(pad_info(2,&info)==0 && !info.connected && !info.connected_count);
    int32_t event[2]={-1,-1};
    assert(users.event(event)==0 && event[0]==0 && event[1]==p1);
    assert(users.initialize2(0)==0);
    assert(users.event(event)==0 && event[0]==0 && event[1]==p2);
    assert(users.event(event)==COOP02_NO_EVENT);
    pass("real UserService IDs, two Pad handles, errors and Pad queries preserve both LOGIN events");

    Device a={0},b={0},c={0};
    attach(&a,"COOP test A");
    /* Initial P1 can remain virtually connected; wait for its physical input. */
    button(&a,SDL_GAMEPAD_BUTTON_EAST,true);
    for (int n=0;n<140;++n) {
        data[0]=read_player(1);
        if (data[0].buttons & BTN_CIRCLE) break;
        SDL_Delay(10);
    }
    assert(data[0].buttons==BTN_CIRCLE);
    button(&a,SDL_GAMEPAD_BUTTON_EAST,false);
    data[0]=read_player(1); neutral(&data[0]);
    pass("one SDL controller still drives P1");

    attach(&b,"COOP test B");
    data[1]=await_connection(2,1); neutral(&data[1]);
    assert(pad_info(1,&info)==0 && info.connected && info.connected_count==1);
    assert(pad_info(2,&info)==0 && info.connected && info.connected_count==1);
    button(&b,SDL_GAMEPAD_BUTTON_SOUTH,true);
    data[1]=read_player(2); assert(data[1].buttons==BTN_CROSS);
    data[0]=read_player(1); assert(!(data[0].buttons & BTN_CROSS));
    button(&a,SDL_GAMEPAD_BUTTON_WEST,true);
    button(&b,SDL_GAMEPAD_BUTTON_START,true);
    data[0]=read_player(1); data[1]=read_player(2);
    assert(data[0].buttons==BTN_SQUARE && data[1].buttons==(BTN_CROSS|BTN_OPTIONS));
    button(&a,SDL_GAMEPAD_BUTTON_WEST,false);
    button(&b,SDL_GAMEPAD_BUTTON_SOUTH,false); button(&b,SDL_GAMEPAD_BUTTON_START,false);
    pass("P2 Cross never reaches P1; simultaneous face buttons and Options are distinct");

    for (int axis_index=0;axis_index<4;++axis_index) {
        assert(SDL_SetJoystickVirtualAxis(a.joystick,axis_index,32767));
        assert(SDL_SetJoystickVirtualAxis(b.joystick,axis_index,-32768));
    }
    assert(SDL_SetJoystickVirtualAxis(a.joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,32767));
    assert(SDL_SetJoystickVirtualAxis(b.joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,32767)); pump();
    data[0]=read_player(1); data[1]=read_player(2);
    assert(data[0].left_x==255 && data[0].left_y==255 && data[0].right_x==255 && data[0].right_y==255);
    assert(data[1].left_x==0 && data[1].left_y==0 && data[1].right_x==0 && data[1].right_y==0);
    assert(data[0].l2==255 && !data[0].r2 && data[0].buttons==BTN_L2);
    assert(data[1].r2==255 && !data[1].l2 && data[1].buttons==BTN_R2);
    for (int axis_index=0;axis_index<SDL_GAMEPAD_AXIS_COUNT;++axis_index) {
        assert(SDL_SetJoystickVirtualAxis(a.joystick,axis_index,axis_index>=SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? -32768 : 0));
        assert(SDL_SetJoystickVirtualAxis(b.joystick,axis_index,axis_index>=SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? -32768 : 0));
    }
    pump();
    pass("four axes and both triggers remain independent");

    uint8_t vibration_a[]={31,63},vibration_b[]={127,255};
    assert(pad_vibration(1,vibration_a)==0);
    assert(a.calls==1 && !b.calls && a.low==31*257 && a.high==63*257);
    assert(pad_vibration(2,vibration_b)==0);
    assert(a.calls==1 && b.calls==1 && b.low==127*257 && b.high==65535);
    assert(pad_vibration(3,vibration_a)==ERR_INVALID_HANDLE && pad_vibration(2,NULL)==ERR_INVALID_ARG);
    pass("rumble callbacks target the handle's SDL device");

    assert(SDL_SetJoystickVirtualTouchpad(b.joystick,0,0,true,0.75f,0.5f,1));
    button(&b,SDL_GAMEPAD_BUTTON_TOUCHPAD,true);
    data[1]=read_player(2); data[0]=read_player(1);
    assert(data[1].touch_count==1 && data[1].touches[0].x==1439 && (data[1].buttons & BTN_TOUCHPAD));
    assert(!data[0].touch_count && !(data[0].buttons & BTN_TOUCHPAD));
    assert(SDL_SetJoystickVirtualTouchpad(b.joystick,0,0,false,0,0,0));
    button(&b,SDL_GAMEPAD_BUTTON_TOUCHPAD,false);
    button(&b,SDL_GAMEPAD_BUTTON_BACK,true); button(&b,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true);
    data[1]=read_player(2); data[0]=read_player(1);
    assert(data[1].touch_count==2 && !data[0].touch_count);
    button(&b,SDL_GAMEPAD_BUTTON_BACK,false); button(&b,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,false);
    SDL_Delay(160); read_player(2); SDL_Delay(160); read_player(2);
    button(&b,SDL_GAMEPAD_BUTTON_MISC1,true);
    read_player(2); SDL_Delay(30);
    data[1]=read_player(2); data[0]=read_player(1);
    assert(fabsf(data[1].orientation[2])>0.01f && data[0].orientation[2]==0);
    button(&b,SDL_GAMEPAD_BUTTON_MISC1,false); read_player(2);
    button(&b,SDL_GAMEPAD_BUTTON_MISC1,true); read_player(2);
    button(&b,SDL_GAMEPAD_BUTTON_MISC1,false); read_player(2);
    pass("physical/staged touch and simulated motion cannot cross player boundaries");

    /* Read P2 first: it must not consume P1's pending mouse delta or button. */
    mouse_input=(TlgGpuMouse){.captured=1,.dx=60,.held=1,.pressed=1};
    int takes_before=mouse_takes;
    data[1]=read_player(2);
    assert(mouse_takes==takes_before && !(data[1].buttons & BTN_R2));
    data[0]=read_player(1);
    assert(mouse_takes==takes_before+1 && (data[0].buttons & BTN_R2));
    mouse_input=(TlgGpuMouse){0};
    keyboard[SDL_SCANCODE_SPACE]=true; keyboard[SDL_SCANCODE_A]=true;
    data[1]=read_player(2); data[0]=read_player(1);
    assert(!(data[1].buttons & BTN_CROSS) && data[1].left_x==128);
    assert((data[0].buttons & BTN_CROSS) && data[0].left_x==0);
    keyboard[SDL_SCANCODE_SPACE]=false; keyboard[SDL_SCANCODE_A]=false;
    injected.buttons=BTN_TRIANGLE; injected.stick[0]=17; injected_motion=1;
    data[1]=read_player(2); data[0]=read_player(1);
    assert(!(data[1].buttons & BTN_TRIANGLE) && data[1].left_x==128);
    assert((data[0].buttons & BTN_TRIANGLE) && data[0].left_x==17);
    injected.buttons=0; injected.stick[0]=-1; injected_motion=0;
    pass("keyboard, mouse consumption and synthetic input remain P1-only");

    capture=1;
    data[0]=read_player(1); data[1]=read_player(2);
    neutral(&data[0]); neutral(&data[1]);
    capture=0; read_player(1); read_player(2);
    pass("overlay capture neutralizes both players");

    SDL_JoystickID old_b=b.id;
    button(&b,SDL_GAMEPAD_BUTTON_BACK,true);
    button(&b,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true);
    assert(read_player(2).touch_count==2);
    detach(&b);
    data[1]=await_connection(2,0); neutral(&data[1]);
    assert(data[1].orientation[3]==1 && pad_info(2,&info)==0 && !info.connected);
    int a_calls=a.calls;
    assert(pad_vibration(2,vibration_b)==0 && a.calls==a_calls);
    button(&a,SDL_GAMEPAD_BUTTON_EAST,true);
    data[0]=read_player(1); assert(data[0].buttons & BTN_CIRCLE);
    attach(&b,"COOP test B reconnected");
    assert(b.id!=old_b);
    data[1]=await_connection(2,1); neutral(&data[1]);
    assert(data[1].orientation[3]==1 && data[1].connected_count==2);
    data[0]=read_player(1); assert(data[0].buttons & BTN_CIRCLE);
    button(&a,SDL_GAMEPAD_BUTTON_EAST,false);
    pass("P2 unplug/replug yields neutral input, new ID, same handle, no stale motion or rumble routing");

    detach(&a); read_player(2);
    button(&b,SDL_GAMEPAD_BUTTON_SOUTH,true);
    data[1]=read_player(2); data[0]=read_player(1);
    assert(data[1].buttons==BTN_CROSS && !(data[0].buttons & BTN_CROSS));
    attach(&a,"COOP test A reconnected");
    button(&a,SDL_GAMEPAD_BUTTON_NORTH,true);
    for (int n=0;n<140;++n) {
        data[0]=read_player(1);
        if (data[0].buttons & BTN_TRIANGLE) break;
        SDL_Delay(10);
    }
    assert(data[0].buttons==BTN_TRIANGLE && read_player(2).buttons==BTN_CROSS);
    pass("P1 unplug/replug never promotes surviving P2");

    attach(&c,"COOP extra controller"); button(&c,SDL_GAMEPAD_BUTTON_EAST,true);
    SDL_Delay(1050);
    assert(read_player(1).buttons==BTN_TRIANGLE && read_player(2).buttons==BTN_CROSS);
    pass("third device cannot steal occupied player slots");

    assert(pad_vibration(2,vibration_b)==0);
    int a_calls_before_close=a.calls;
    assert(pad_close(2)==0 && pad_close(2)==ERR_INVALID_HANDLE);
    assert(!b.low && !b.high && a.calls==a_calls_before_close);
    assert(pad_read_state(2,data)==ERR_INVALID_HANDLE && pad_vibration(2,vibration_b)==ERR_INVALID_HANDLE);
    assert(pad_open(p2,0,0,NULL)==2 && read_player(2).buttons==BTN_CROSS);
    assert(pad_motion_state(2,1)==0 && pad_motion_state(3,1)==ERR_INVALID_HANDLE);
    assert(pad_ok_handle(1)==0 && pad_ok_handle(2)==0 && pad_ok_handle(3)==ERR_INVALID_HANDLE);
    assert(pad_light_bar(2,NULL)==0 && pad_info(2,NULL)==ERR_INVALID_ARG);
    assert(pad_close(1)==0 && pad_close(2)==0);
    assert(users.terminate()==0 && users.initialize(NULL)==0);
    assert(users.list(ids)==0 && ids[0]==p1 && ids[1]==p2 && ids[2]==-1 && ids[3]==-1);
    assert(users.event(event)==COOP02_NO_EVENT);
    int32_t initial=-1;
    assert(users.initial(&initial)==0 && initial==p1);
    assert(!coop02_file_calls && !coop02_save_calls);
    pass("close/reopen and hotplug do not log out users, duplicate LOGIN or access saves/files");
    detach(&a); detach(&b); detach(&c);
    for (int i=0;i<PAD_PLAYERS;++i) if (players[i].gamepad) SDL_CloseGamepad(players[i].gamepad);
    SDL_Quit();
    printf("PASS: COOP-01 + COOP-02 real HLE integration (initial user %d, second user %d)\n",p1,p2);
    return 0;
}
