// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Single-player network interface for the Hashx86 port.
//
//-----------------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "d_net.h"
#include "i_system.h"
#include "m_argv.h"

void I_InitNetwork(void) {
    doomcom = (doomcom_t*)malloc(sizeof(*doomcom));
    if (!doomcom) I_Error("I_InitNetwork: out of memory");
    memset(doomcom, 0, sizeof(*doomcom));

    doomcom->ticdup = 1;
    doomcom->extratics = 0;

    // Single player only for this port.
    netgame = false;
    doomcom->id = DOOMCOM_ID;
    doomcom->numplayers = doomcom->numnodes = 1;
    doomcom->deathmatch = false;
    doomcom->consoleplayer = 0;
}

void I_NetCmd(void) {
    doomcom->remotenode = -1;
    doomcom->datalength = 0;
}
