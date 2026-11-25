// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Sound interface for Hashx86.
//
//	This is the classic Doom 8-channel DMX mixer, writing 16-bit stereo
//	output into the kernel's audio ring through Hsys 11-14 instead of
//	/dev/dsp. Sound lumps in the WAD are raw 8-bit unsigned mono samples
//	at 11025 Hz behind an 8-byte header; they are pre-cached at start-up
//	and mixed at 22050 Hz.
//
//-----------------------------------------------------------------------------

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <Hx86/Hsyscalls/syscalls.h>

#include "doomdef.h"
#include "doomstat.h"
#include "sounds.h"
#include "i_sound.h"
#include "i_system.h"
#include "w_wad.h"
#include "z_zone.h"

// Hx86 audio Hsyscalls (Hsys 11-14). Keep these local so release builds work
// against the published libhx86 without requiring a newer kernel checkout.
#define HX86_HSYS_AUDIO_OPEN 11
#define HX86_HSYS_AUDIO_WRITE 12
#define HX86_HSYS_AUDIO_GETPOS 13
#define HX86_HSYS_AUDIO_CLOSE 14

#define SAMPLECOUNT 512
#define NUM_CHANNELS 8
#define BUFMUL 4  // Stereo 16-bit: 4 bytes per frame.
#define MIXBYTES (SAMPLECOUNT * BUFMUL)
#define MIXBUFFERSIZE (MIXBYTES / 2)

#define SAMPLERATE 11025  // Rate of the WAD sound lumps.
#define OUTPUTRATE 22050  // Rate we mix and submit at.
#define TARGET_BUFFERED (8 * 1024)

// SNDSERV references in m_misc.c.
FILE* sndserver = 0;
char* sndserver_filename = (char*)"sndserver";

static int32_t audio_stream = -1;
static uint32_t audio_written;
static uint32_t audio_played;

// The actual lengths of all sound effects (padded, excluding the header).
static int lengths[NUMSFX];

// The global mixing buffer: alternating left/right signed 16-bit samples.
static signed short mixbuffer[MIXBUFFERSIZE];

// Channel step amount and its 0.16 fixed-point remainder.
static unsigned int channelstep[NUM_CHANNELS];
static unsigned int channelstepremainder[NUM_CHANNELS];

// Channel data pointers, start and end.
static unsigned char* channels[NUM_CHANNELS];
static unsigned char* channelsend[NUM_CHANNELS];

// Time/gametic that the channel started playing; used for oldest-channel
// replacement when more sounds than channels are active.
static int channelstart[NUM_CHANNELS];

// Handle and SFX id of each playing channel.
static int channelhandles[NUM_CHANNELS];
static int channelids[NUM_CHANNELS];

// Pitch to stepping lookup.
static int steptable[256];

// Volume lookups; also turn unsigned samples into signed.
static int vol_lookup[128 * 256];

static int* channelleftvol_lookup[NUM_CHANNELS];
static int* channelrightvol_lookup[NUM_CHANNELS];

static int next_handle;
static int tables_ready;

/**
 * HxAudioCall() - Invoke one of the kernel's audio Hsyscalls.
 */
static int32_t HxAudioCall(uint32_t call, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    int32_t result = -1;
    __asm__ __volatile__("int $0x80"
                         : "=a"(result)
                         : "a"(sys_Hcall), "b"(call), "c"(arg1), "d"(arg2), "S"(arg3)
                         : "memory");
    return result;
}

static int32_t HxAudioOpen(uint32_t rate, uint32_t chans, uint32_t bits) {
    return HxAudioCall(HX86_HSYS_AUDIO_OPEN, rate, chans, bits);
}

static int32_t HxAudioWrite(int32_t stream, const void* data, uint32_t length) {
    return HxAudioCall(HX86_HSYS_AUDIO_WRITE, (uint32_t)stream, (uint32_t)data, length);
}

static int32_t HxAudioGetPosition(int32_t stream) {
    return HxAudioCall(HX86_HSYS_AUDIO_GETPOS, (uint32_t)stream, 0, 0);
}

static int32_t HxAudioClose(int32_t stream) {
    return HxAudioCall(HX86_HSYS_AUDIO_CLOSE, (uint32_t)stream, 0, 0);
}

/**
 * InitTables() - Build the pitch and volume lookup tables (idempotent).
 *
 * steptable[pitch] is the sample advance in 0.16 fixed point measured at
 * SAMPLERATE. The octave curve 2^((pitch-128)/64) is built with integer
 * recurrences so the engine does not need libm.
 */
static void InitTables(void) {
    int i;
    int j;

    if (tables_ready) return;
    tables_ready = 1;

    steptable[128] = 65536;
    {
        long long value = 65536;
        // 2^(1/64) in 16.16 fixed point is about 66250.
        for (i = 129; i < 256; i++) {
            value = (value * 66250 + 32768) >> 16;
            steptable[i] = (int)value;
        }
        value = 65536;
        for (i = 127; i >= 0; i--) {
            value = (value * 65536 + 33125) / 66250;
            steptable[i] = (int)value;
        }
    }

    for (i = 0; i < 128; i++)
        for (j = 0; j < 256; j++) vol_lookup[i * 256 + j] = (i * (j - 128) * 256) / 127;
}

/**
 * ReadSfx() - Load and pad one sound lump.
 *
 * Returns a pointer past the 8-byte DMX header and stores the padded sample
 * length. Missing sounds (DOOM II names in the shareware WAD) fall back to
 * the pistol, matching the original engine.
 */
static void* ReadSfx(char* sfxname, int* len) {
    unsigned char* sfx;
    unsigned char* padded;
    int size;
    int bodySize;
    int paddedSize;
    int copySize;
    int i;
    int lump;
    char name[24];

    sprintf(name, "ds%s", sfxname);

    lump = W_CheckNumForName(name);
    if (lump == -1) lump = W_GetNumForName("dspistol");

    size = W_LumpLength(lump);
    sfx = (unsigned char*)W_CacheLumpNum(lump, PU_STATIC);

    bodySize = size - 8;
    if (bodySize < 0) bodySize = 0;

    paddedSize = ((bodySize + (SAMPLECOUNT - 1)) / SAMPLECOUNT) * SAMPLECOUNT;

    padded = (unsigned char*)Z_Malloc(paddedSize + 8, PU_STATIC, 0);
    copySize = size;
    if (copySize > paddedSize + 8) copySize = paddedSize + 8;
    memcpy(padded, sfx, copySize);
    for (i = copySize; i < paddedSize + 8; i++) padded[i] = 128;

    // Free through the WAD cache so the lump slot is cleared too.
    W_FreeLump(lump);

    *len = paddedSize;
    return (void*)(padded + 8);
}

/**
 * MixBlock() - Mix the next SAMPLECOUNT stereo frames into mixbuffer.
 */
static void MixBlock(void) {
    unsigned int sample;
    int dl;
    int dr;
    signed short* leftout;
    signed short* rightout;
    signed short* leftend;
    int chan;

    leftout = mixbuffer;
    rightout = mixbuffer + 1;
    leftend = mixbuffer + SAMPLECOUNT * 2;

    while (leftout != leftend) {
        dl = 0;
        dr = 0;

        for (chan = 0; chan < NUM_CHANNELS; chan++) {
            if (channels[chan]) {
                sample = *channels[chan];
                dl += channelleftvol_lookup[chan][sample];
                dr += channelrightvol_lookup[chan][sample];

                channelstepremainder[chan] += channelstep[chan];
                channels[chan] += channelstepremainder[chan] >> 16;
                channelstepremainder[chan] &= 65536 - 1;

                if (channels[chan] >= channelsend[chan]) channels[chan] = 0;
            }
        }

        if (dl > 0x7fff)
            *leftout = 0x7fff;
        else if (dl < -0x8000)
            *leftout = -0x8000;
        else
            *leftout = (signed short)dl;

        if (dr > 0x7fff)
            *rightout = 0x7fff;
        else if (dr < -0x8000)
            *rightout = -0x8000;
        else
            *rightout = (signed short)dr;

        leftout += 2;
        rightout += 2;
    }
}

/**
 * PumpAudio() - Keep the kernel's audio ring filled with mixed samples.
 *
 * The kernel mixer's read position is the amount played; the difference from
 * the amount submitted is what is still buffered. Mixing stops once the ring
 * holds TARGET_BUFFERED bytes, so this is safe to call every frame.
 */
static void PumpAudio(void) {
    int32_t position;

    if (audio_stream < 0) return;

    position = HxAudioGetPosition(audio_stream);
    if (position >= 0) audio_played = (uint32_t)position;

    while ((uint32_t)(audio_written - audio_played) < TARGET_BUFFERED) {
        int32_t accepted;

        MixBlock();
        accepted = HxAudioWrite(audio_stream, mixbuffer, MIXBYTES);
        if (accepted <= 0) break;

        audio_written += (uint32_t)accepted;
        if (accepted < (int32_t)MIXBYTES) break;

        position = HxAudioGetPosition(audio_stream);
        if (position >= 0) audio_played = (uint32_t)position;
    }
}

/**
 * I_InitSound() - Open the audio stream, build tables and pre-cache sounds.
 */
void I_InitSound(void) {
    int i;

    InitTables();
    memset(mixbuffer, 0, sizeof(mixbuffer));
    memset(channels, 0, sizeof(channels));
    memset(channelhandles, 0, sizeof(channelhandles));
    memset(channelids, 0, sizeof(channelids));
    next_handle = 1;

    for (i = 1; i < NUMSFX; i++) {
        if (!S_sfx[i].link) {
            S_sfx[i].data = ReadSfx(S_sfx[i].name, &lengths[i]);
        } else {
            S_sfx[i].data = S_sfx[i].link->data;
            lengths[i] = lengths[S_sfx[i].link - S_sfx];
        }
    }

    audio_stream = HxAudioOpen(OUTPUTRATE, 2, 16);
    audio_written = 0;
    audio_played = 0;

    if (audio_stream >= 0)
        printf("[SND] Doom audio: %d Hz stereo, %d channels\n", OUTPUTRATE, NUM_CHANNELS);
    else
        printf("[SND] Doom audio: no audio stream, sound disabled\n");
}

/**
 * I_UpdateSound() - Mix and submit as much audio as the ring can take.
 */
void I_UpdateSound(void) {
    PumpAudio();
}

/**
 * I_SubmitSound() - Output is submitted by I_UpdateSound(); kept for the
 * engine's synchronous interface.
 */
void I_SubmitSound(void) {}

/**
 * I_ShutdownSound() - Close the audio stream.
 */
void I_ShutdownSound(void) {
    if (audio_stream >= 0) {
        HxAudioClose(audio_stream);
        audio_stream = -1;
    }
    memset(channels, 0, sizeof(channels));
}

/**
 * I_SetChannels() - Initialize the internal mixing lookups.
 *
 * Called by S_Init after I_InitSound, so only reset the mixer state.
 */
void I_SetChannels(void) {
    InitTables();
    memset(channels, 0, sizeof(channels));
}

void I_SetSfxVolume(int volume) {
    snd_SfxVolume = volume;
}

void I_SetMusicVolume(int volume) {
    snd_MusicVolume = volume;
}

/**
 * I_GetSfxLumpNum() - Raw data lump index for a sound descriptor.
 */
int I_GetSfxLumpNum(sfxinfo_t* sfx) {
    char namebuf[16];
    sprintf(namebuf, "ds%s", sfx->name);
    return W_GetNumForName(namebuf);
}

/**
 * StartChannel() - Add one sound to the internal channel list.
 */
static int StartChannel(int sfxid, int volume, int sep, int step) {
    static const int chainSfx[] = {sfx_sawup, sfx_sawidl, sfx_sawful, sfx_sawhit, sfx_stnmov,
                                   sfx_pistol};
    int i;
    int oldest;
    int oldestnum;
    int slot;
    int leftvol;
    int rightvol;
    int handle;
    unsigned int k;

    if (sfxid <= 0 || sfxid >= NUMSFX) return -1;
    if (!S_sfx[sfxid].data || lengths[sfxid] <= 0) return -1;
    if (volume < 0) volume = 0;
    if (volume > 127) volume = 127;
    if (sep < 0) sep = 0;
    if (sep > 255) sep = 255;

    // Chainsaw and similar sounds play only one at a time.
    for (k = 0; k < sizeof(chainSfx) / sizeof(chainSfx[0]); k++) {
        if (sfxid == chainSfx[k]) {
            for (i = 0; i < NUM_CHANNELS; i++) {
                if (channels[i] && channelids[i] == sfxid) {
                    channels[i] = 0;
                    break;
                }
            }
        }
    }

    // Find a free channel, or the oldest one when all are busy.
    oldest = gametic;
    oldestnum = 0;
    for (i = 0; i < NUM_CHANNELS && channels[i]; i++) {
        if (channelstart[i] < oldest) {
            oldestnum = i;
            oldest = channelstart[i];
        }
    }
    slot = (i == NUM_CHANNELS) ? oldestnum : i;

    channels[slot] = (unsigned char*)S_sfx[sfxid].data;
    channelsend[slot] = channels[slot] + lengths[sfxid];
    channelstep[slot] = (unsigned int)step;
    channelstepremainder[slot] = 0;
    channelstart[slot] = gametic;
    channelids[slot] = sfxid;

    // Separation is 1..256; x^2 panning keeps equal-power-ish balance.
    sep += 1;
    leftvol = volume - ((volume * sep * sep) >> 16);
    sep -= 257;
    rightvol = volume - ((volume * sep * sep) >> 16);

    if (leftvol < 0) leftvol = 0;
    if (leftvol > 127) leftvol = 127;
    if (rightvol < 0) rightvol = 0;
    if (rightvol > 127) rightvol = 127;

    channelleftvol_lookup[slot] = &vol_lookup[leftvol * 256];
    channelrightvol_lookup[slot] = &vol_lookup[rightvol * 256];

    handle = next_handle++;
    if (next_handle <= 0) next_handle = 1;
    channelhandles[slot] = handle;

    return handle;
}

/**
 * I_StartSound() - Start a sound effect on a mixer channel.
 */
int I_StartSound(int id, int vol, int sep, int pitch, int priority) {
    int step;

    (void)priority;

    if (pitch < 0) pitch = 0;
    if (pitch > 255) pitch = 255;

    // steptable is relative to SAMPLERATE; scale it to the output rate.
    step = (int)(((long long)steptable[pitch] * OUTPUTRATE) / SAMPLERATE);

    return StartChannel(id, vol, sep, step);
}

/**
 * I_StopSound() - Stop the channel playing the given handle.
 */
void I_StopSound(int handle) {
    int i;
    if (handle == 0) return;
    for (i = 0; i < NUM_CHANNELS; i++) {
        if (channels[i] && channelhandles[i] == handle) {
            channels[i] = 0;
            return;
        }
    }
}

/**
 * I_SoundIsPlaying() - Whether the given handle is still mixing.
 */
int I_SoundIsPlaying(int handle) {
    int i;
    if (handle == 0) return 0;
    for (i = 0; i < NUM_CHANNELS; i++) {
        if (channels[i] && channelhandles[i] == handle) return 1;
    }
    return 0;
}

/**
 * I_UpdateSoundParams() - Change volume/separation of a playing sound.
 */
void I_UpdateSoundParams(int handle, int vol, int sep, int pitch) {
    int i;
    int leftvol;
    int rightvol;

    (void)pitch;
    if (handle == 0) return;

    if (vol < 0) vol = 0;
    if (vol > 127) vol = 127;
    if (sep < 0) sep = 0;
    if (sep > 255) sep = 255;

    sep += 1;
    leftvol = vol - ((vol * sep * sep) >> 16);
    sep -= 257;
    rightvol = vol - ((vol * sep * sep) >> 16);

    if (leftvol < 0) leftvol = 0;
    if (leftvol > 127) leftvol = 127;
    if (rightvol < 0) rightvol = 0;
    if (rightvol > 127) rightvol = 127;

    for (i = 0; i < NUM_CHANNELS; i++) {
        if (channels[i] && channelhandles[i] == handle) {
            channelleftvol_lookup[i] = &vol_lookup[leftvol * 256];
            channelrightvol_lookup[i] = &vol_lookup[rightvol * 256];
            return;
        }
    }
}

//
// MUSIC API - no music playback yet.
//
void I_InitMusic(void) {}
void I_ShutdownMusic(void) {}

void I_PauseSong(int handle) { (void)handle; }
void I_ResumeSong(int handle) { (void)handle; }

int I_RegisterSong(void* data) {
    (void)data;
    return 1;
}

void I_PlaySong(int handle, int looping) {
    (void)handle;
    (void)looping;
}

void I_StopSong(int handle) { (void)handle; }
void I_UnRegisterSong(int handle) { (void)handle; }
