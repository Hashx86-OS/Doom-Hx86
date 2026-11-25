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

static unsigned int rdtsc_low(void) {
    unsigned int lo;
    __asm__ volatile("rdtsc" : "=a"(lo) : : "edx");
    return lo;
}

static unsigned int tsc_base;
static unsigned int tsc_per_ms;
static int timer_ready;

static void I_CalibrateTimer(void) {
    struct timespec req;
    req.tv_sec = 0;
    req.tv_nsec = 50 * 1000 * 1000;  // 50 ms

    unsigned int start = rdtsc_low();
    syscall_nanosleep(&req, NULL);
    unsigned int end = rdtsc_low();

    // The kernel may service the sleep slightly late; the ratio is still
    // close enough for a game clock.
    tsc_per_ms = (end - start) / 50;
    if (tsc_per_ms == 0) tsc_per_ms = 1;
    tsc_base = start;
    timer_ready = 1;
}

//
// I_GetTime
// returns time in 1/35th second tics (TICRATE).
//
int I_GetTime(void) {
    if (!timer_ready) I_CalibrateTimer();

    unsigned int delta = rdtsc_low() - tsc_base;
    unsigned int msec = delta / tsc_per_ms;

    // Avoid overflowing msec * TICRATE on long sessions.
    return (int)((msec / 1000) * TICRATE + ((msec % 1000) * TICRATE) / 1000);
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
