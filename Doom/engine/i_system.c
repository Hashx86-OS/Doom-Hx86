// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	System interface for the Hashx86 port.
//
//-----------------------------------------------------------------------------

#include <Hx86/Hx86.h>

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "doomdef.h"
#include "m_misc.h"
#include "i_video.h"
#include "i_sound.h"

#include "d_net.h"
#include "g_game.h"

#include "i_system.h"

int mb_used = 8;

extern boolean demorecording;

void I_Tactile(int on, int off, int total) {
    // UNUSED.
    on = off = total = 0;
}

ticcmd_t emptycmd;

ticcmd_t* I_BaseTiccmd(void) { return &emptycmd; }

int I_GetHeapSize(void) { return mb_used * 1024 * 1024; }

byte* I_ZoneBase(int* size) {
    *size = mb_used * 1024 * 1024;
    byte* base = (byte*)malloc(*size);
    if (!base) I_Error("I_ZoneBase: failed to allocate %d bytes", *size);
    return base;
}

/* ------------------------------------------------------------------------- */
/* Timing                                                                     */
/* ------------------------------------------------------------------------- */

// Kernel PIT-based millisecond clock (Hsys 15). Calibrating the TSC in user
// space is unreliable: nanosleep used to return before the thread actually
// blocked, so the measured interval collapsed and the game clock ran fast.
// The kernel clock is independent of the CPU frequency.
#define HX86_HSYS_GET_TIME_MS 15

static int32_t HxTimeMs(void) {
    int32_t result = 0;
    __asm__ __volatile__("int $0x80"
                         : "=a"(result)
                         : "a"(sys_Hcall), "b"(HX86_HSYS_GET_TIME_MS)
                         : "memory");
    return result;
}

//
// I_GetTime
// returns time in 1/35th second tics (TICRATE).
//
int I_GetTime(void) {
    int32_t msec = HxTimeMs();
    if (msec < 0) msec = 0;
    return (int)(((uint64_t)(uint32_t)msec * TICRATE) / 1000u);
}

/* ------------------------------------------------------------------------- */
/* Lifecycle                                                                  */
/* ------------------------------------------------------------------------- */

void I_Init(void) {
    I_InitSound();
}

void I_Quit(void) {
    D_QuitNetGame();
    I_ShutdownSound();
    I_ShutdownMusic();
    M_SaveDefaults();
    I_ShutdownGraphics();
    // Exit the whole process: the GUI event-handler thread created for the
    // download dialog would otherwise keep the process (and its fullscreen
    // screen lock) alive after this thread exits.
    syscall_exit_group(0);
}

void I_BeginRead(void) {}

void I_EndRead(void) {}

byte* I_AllocLow(int length) {
    byte* mem = (byte*)malloc(length);
    if (!mem) I_Error("I_AllocLow: out of memory (%d bytes)", length);
    memset(mem, 0, length);
    return mem;
}

//
// I_Error
//
void I_Error(char* error, ...) {
    va_list argptr;

    va_start(argptr, error);
    fprintf(stderr, "Error: ");
    vfprintf(stderr, error, argptr);
    fprintf(stderr, "\n");
    va_end(argptr);

    fflush(stderr);

    if (demorecording) G_CheckDemoStatus();

    D_QuitNetGame();
    I_ShutdownGraphics();

    syscall_exit_group(1);
}
