// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Entry point used by prog.cpp after the game data dialog.
//
//-----------------------------------------------------------------------------

#include "doomdef.h"
#include "m_argv.h"
#include "d_main.h"

extern "C" void DoomMain(int argc, char** argv) {
    myargc = argc;
    myargv = argv;

    D_DoomMain();
}
