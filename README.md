# Doom-HX86

Port of the original id Software DOOM engine (`linuxdoom-1.10`) to the
[Hashx86 OS](https://github.com/Hashx86-OS/Hashx86-OS) kernel.

## Status

Software renderer, keyboard + mouse input, single-player, and 8-channel sound
effects mixed at 22.05 kHz stereo through the Hashx86 audio stack. There is no
music playback yet. The app can download and install id Software's original
DOOM shareware WAD on first launch.

## Audio

DOOM's classic DMX mixer runs in the game process. Sound lumps from the WAD
(8-bit unsigned mono, 11025 Hz) are pre-cached at start-up; the mixer converts
them to 16-bit stereo at 22050 Hz and streams the result to the kernel through
`Hsys_audioOpen/Write/GetPos/Close` (Hsys 11-14). The kernel's AudioMixer feeds
the AC'97 driver, so the game needs no audio driver code of its own.

Menu music and level music (MUS) are not implemented.

## Prerequisites

- GCC/g++ with multilib support (for `-m32`)
- GNU `ld` (for `-melf_i386`)
- `make`
- A Hashx86-OS checkout for `libhx86` and `libmbedtls`

## Build

```bash
make KERNEL_DIR=/path/to/Hashx86-OS
```

Output: `build/Doom.bin`

The build defaults `KERNEL_DIR` to `../Hashx86-OS`.

## Game data

The application looks for `doom1.wad` in its working directory (the directory
that contains `Doom.bin`). If it is missing, the app shows a download dialog and
installs:

```text
<app directory>/doom1.wad
<app directory>/shareware/doom-1.8.wad.gz
```

The original DOOM v1.8 shareware WAD is downloaded as a gzip archive from the
idgames mirror at `gamers.org` (the same bytes as the primary mirror), verified
against a pinned SHA-256, inflated, verified again, and written next to the
executable. Hashx86 does not host or redistribute the game data.

Retail DOOM data is proprietary and must be supplied by the user from a legal
installation. Place a commercial IWAD (`doom.wad` or `doom2.wad`) in the app
directory to play the registered/commercial game.

## Install to Hashx86 OS

The App Store installs the binary as `/Apps/doom/Doom.bin`. To install manually,
copy `build/Doom.bin` to `/Apps/doom/Doom.bin` (or `/Hashx86/apps/`) on the disk
image. The installer and the engine both use the process working directory, so
`doom1.wad` ends up beside `Doom.bin`.

## Controls

| Input | Action |
|---|---|
| Arrow keys | Move / turn |
| `Ctrl` | Fire |
| `Space` | Use / open |
| `Shift` | Run |
| `Alt` | Strafe |
| `1`–`7` | Select weapon |
| `Tab` | Automap |
| `Esc` | Menu |
| Mouse | Turn / move; left button fires |

## Files

| Path | Description |
|---|---|
| `Doom/engine/` | Original `linuxdoom-1.10` engine sources plus Hashx86 platform files |
| `Doom/engine/i_main.c` | `DoomMain` glue used by the app entry point |
| `Doom/engine/i_system.c` | Timing and memory |
| `Doom/engine/i_video.c` | Framebuffer output and input |
| `Doom/engine/i_sound.c` | Silent sound stub |
| `Doom/engine/i_net.c` | Single-player network stub |
| `Doom/prog.cpp` | Entry point and shareware installer UI |
| `Doom/data/Archive.*` | Shareware WAD download and install |
| `Doom/data/HttpClient.*` | HTTPS client (mbedTLS) |
| `Doom/data/StoreUtil.*` | Small filesystem/string helpers |
| `Doom/compat.c` | C standard library stubs + syscall wrappers |
| `Doom/include/` | Freestanding libc headers for the engine |
| `third_party/miniz/` | DEFLATE/gzip decompressor |
| `Doom/linker.ld` | Linker script for Hashx86 OS binaries |
| `linuxdoom-1.10/` | Pristine upstream source snapshot |

## License

- **Engine code**: GPL v2 — Copyright (C) 1993-1996 Id Software, Inc.
- **Port code** (`Doom/prog.cpp`, `Doom/compat.c`, `Doom/engine/i_*.c`,
  `Doom/data/`, `Doom/include/`, `Makefile`, `linker.ld`): GPL v2
- **DOOM shareware data**: id Software's shareware distribution terms;
  downloaded by the app, not bundled
- **Retail game data**: Proprietary — not included
