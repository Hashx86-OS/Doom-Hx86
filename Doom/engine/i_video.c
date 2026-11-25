// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Video and input for the Hashx86 port.
//
//-----------------------------------------------------------------------------

#include <Hx86/Hx86.h>

#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "v_video.h"
#include "d_event.h"
#include "i_video.h"
#include "i_system.h"

extern void D_PostEvent(event_t* ev);

static uint32_t* framebuffer;
static int fb_width;
static int fb_height;

static uint32_t palette32[256];

/* Destination rectangle for the 320x200 image, letterboxed to 4:3. */
static int dst_x;
static int dst_y;
static int dst_width;
static int dst_height;
static int src_x[4096];
static int src_y[4096];

/* ------------------------------------------------------------------------- */
/* Input                                                                      */
/* ------------------------------------------------------------------------- */

/* Scancode set 1 -> DOOM key code. Values below 0 mean "not mapped". */
static int scancode_to_doom[128];

static void BuildKeyMap(void) {
    for (int i = 0; i < 128; i++) scancode_to_doom[i] = -1;

    scancode_to_doom[0x01] = KEY_ESCAPE;
    for (int i = 0; i < 10; i++) {
        // 0x02 is '1' ... 0x0A is '9', 0x0B is '0'.
        scancode_to_doom[0x02 + i] = (i == 9) ? '0' : ('1' + i);
    }
    scancode_to_doom[0x0C] = KEY_MINUS;
    scancode_to_doom[0x0D] = KEY_EQUALS;
    scancode_to_doom[0x0E] = KEY_BACKSPACE;
    scancode_to_doom[0x0F] = KEY_TAB;

    scancode_to_doom[0x10] = 'q';
    scancode_to_doom[0x11] = 'w';
    scancode_to_doom[0x12] = 'e';
    scancode_to_doom[0x13] = 'r';
    scancode_to_doom[0x14] = 't';
    scancode_to_doom[0x15] = 'y';
    scancode_to_doom[0x16] = 'u';
    scancode_to_doom[0x17] = 'i';
    scancode_to_doom[0x18] = 'o';
    scancode_to_doom[0x19] = 'p';
    scancode_to_doom[0x1A] = '[';
    scancode_to_doom[0x1B] = ']';
    scancode_to_doom[0x1C] = KEY_ENTER;
    scancode_to_doom[0x1D] = KEY_RCTRL;

    scancode_to_doom[0x1E] = 'a';
    scancode_to_doom[0x1F] = 's';
    scancode_to_doom[0x20] = 'd';
    scancode_to_doom[0x21] = 'f';
    scancode_to_doom[0x22] = 'g';
    scancode_to_doom[0x23] = 'h';
    scancode_to_doom[0x24] = 'j';
    scancode_to_doom[0x25] = 'k';
    scancode_to_doom[0x26] = 'l';
    scancode_to_doom[0x27] = ';';
    scancode_to_doom[0x28] = '\'';
    scancode_to_doom[0x29] = '`';
    scancode_to_doom[0x2A] = KEY_RSHIFT;
    scancode_to_doom[0x2B] = '\\';

    scancode_to_doom[0x2C] = 'z';
    scancode_to_doom[0x2D] = 'x';
    scancode_to_doom[0x2E] = 'c';
    scancode_to_doom[0x2F] = 'v';
    scancode_to_doom[0x30] = 'b';
    scancode_to_doom[0x31] = 'n';
    scancode_to_doom[0x32] = 'm';
    scancode_to_doom[0x33] = ',';
    scancode_to_doom[0x34] = '.';
    scancode_to_doom[0x35] = '/';
    scancode_to_doom[0x36] = KEY_RSHIFT;
    scancode_to_doom[0x37] = '*';
    scancode_to_doom[0x38] = KEY_RALT;
    scancode_to_doom[0x39] = ' ';
    scancode_to_doom[0x3A] = -1;  // caps lock

    scancode_to_doom[0x3B] = KEY_F1;
    scancode_to_doom[0x3C] = KEY_F2;
    scancode_to_doom[0x3D] = KEY_F3;
    scancode_to_doom[0x3E] = KEY_F4;
    scancode_to_doom[0x3F] = KEY_F5;
    scancode_to_doom[0x40] = KEY_F6;
    scancode_to_doom[0x41] = KEY_F7;
    scancode_to_doom[0x42] = KEY_F8;
    scancode_to_doom[0x43] = KEY_F9;
    scancode_to_doom[0x44] = KEY_F10;
    scancode_to_doom[0x57] = KEY_F11;
    scancode_to_doom[0x58] = KEY_F12;

    // Keypad digits and operators.
    scancode_to_doom[0x47] = '7';
    scancode_to_doom[0x49] = '9';
    scancode_to_doom[0x4A] = KEY_MINUS;
    scancode_to_doom[0x4C] = '5';
    scancode_to_doom[0x4E] = KEY_EQUALS;
    scancode_to_doom[0x4F] = '1';
    scancode_to_doom[0x51] = '3';
    scancode_to_doom[0x52] = '0';
    scancode_to_doom[0x53] = '.';

    // Hashx86 reports the arrow cluster using the plain set-1 codes; the
    // matching keypad digits are not available separately.
    scancode_to_doom[0x48] = KEY_UPARROW;
    scancode_to_doom[0x4B] = KEY_LEFTARROW;
    scancode_to_doom[0x4D] = KEY_RIGHTARROW;
    scancode_to_doom[0x50] = KEY_DOWNARROW;
}

static InputState input_state;
static unsigned char previous_keys[128];
static unsigned char previous_buttons;
static int input_primed;

static void I_PollInput(void) {
    syscall_get_input(&input_state);

    if (!input_primed) {
        memcpy(previous_keys, input_state.keyStates, sizeof(previous_keys));
        previous_buttons = (unsigned char)(input_state.mouseButtons & 7);
        input_primed = 1;
        return;
    }

    for (int i = 0; i < 128; i++) {
        unsigned char now = input_state.keyStates[i];
        unsigned char before = previous_keys[i];
        if (now == before) continue;
        int key = scancode_to_doom[i];
        if (key < 0) continue;
        event_t event;
        event.type = now ? ev_keydown : ev_keyup;
        event.data1 = key;
        event.data2 = 0;
        event.data3 = 0;
        D_PostEvent(&event);
    }
    memcpy(previous_keys, input_state.keyStates, sizeof(previous_keys));

    unsigned char buttons = (unsigned char)(input_state.mouseButtons & 7);
    int dx = input_state.mouseDX;
    int dy = input_state.mouseDY;
    if (dx != 0 || dy != 0 || buttons != previous_buttons) {
        event_t event;
        event.type = ev_mouse;
        event.data1 = buttons;
        event.data2 = dx;
        event.data3 = dy;
        D_PostEvent(&event);
    }
    previous_buttons = buttons;
}

/* ------------------------------------------------------------------------- */
/* Graphics                                                                   */
/* ------------------------------------------------------------------------- */

static void BuildDestination(void) {
    int wanted_width = (fb_height * 4) / 3;
    if (wanted_width > fb_width) wanted_width = fb_width;

    dst_width = wanted_width;
    dst_height = (wanted_width * 3) / 4;
    if (dst_height > fb_height) {
        dst_height = fb_height;
        dst_width = (fb_height * 4) / 3;
        if (dst_width > fb_width) dst_width = fb_width;
    }
    if (dst_width < 1) dst_width = 1;
    if (dst_height < 1) dst_height = 1;
    if (dst_width > 4096) dst_width = 4096;
    if (dst_height > 4096) dst_height = 4096;

    dst_x = (fb_width - dst_width) / 2;
    dst_y = (fb_height - dst_height) / 2;

    for (int x = 0; x < dst_width; x++)
        src_x[x] = (x * SCREENWIDTH) / dst_width;
    for (int y = 0; y < dst_height; y++)
        src_y[y] = (y * SCREENHEIGHT) / dst_height;
}

void I_InitGraphics(void) {
    FramebufferInfo fb = syscall_get_framebuffer();
    framebuffer = (uint32_t*)fb.buffer;
    fb_width = (int)fb.width;
    fb_height = (int)fb.height;

    printf("[DOOM] Framebuffer: %dx%d @ 0x%x\n", fb_width, fb_height, (unsigned int)fb.buffer);

    BuildKeyMap();
    BuildDestination();

    // Clear the letterbox once; the per-frame blit only touches the 4:3 area.
    if (framebuffer) {
        for (int i = 0; i < fb_width * fb_height; i++) framebuffer[i] = 0xFF000000u;
    }
}

void I_ShutdownGraphics(void) {}

void I_SetPalette(byte* palette) {
    if (!palette) return;
    for (int i = 0; i < 256; i++) {
        unsigned char r = palette[i * 3 + 0];
        unsigned char g = palette[i * 3 + 1];
        unsigned char b = palette[i * 3 + 2];
        palette32[i] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
}

void I_UpdateNoBlit(void) {}

void I_FinishUpdate(void) {
    if (!framebuffer || !screens[0]) return;

    byte* source = screens[0];
    for (int y = 0; y < dst_height; y++) {
        byte* row = source + src_y[y] * SCREENWIDTH;
        uint32_t* dest = framebuffer + (dst_y + y) * fb_width + dst_x;
        for (int x = 0; x < dst_width; x++) dest[x] = palette32[row[src_x[x]]];
    }
}

void I_WaitVBL(int count) {
    if (count <= 0) return;
    syscall_sleep((unsigned int)count * 1000u / 70u);
}

void I_ReadScreen(byte* scr) {
    if (screens[0]) memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}

void I_StartFrame(void) { I_PollInput(); }

void I_StartTic(void) { I_PollInput(); }
