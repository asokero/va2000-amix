# MNT VA2000 Device Driver for Amiga UNIX (AMIX)

## Overview

This is the first known device driver for the MNT VA2000 RTG graphics card
running under Commodore Amiga UNIX (AMIX), System V Release 4.0.

This project is my first proper vibe-coding adventure: a VA2000 driver experiment for Amiga Unix, started after reading Michael Ditton’s Commodore DevCon documents and wondering the very reasonable question: *would it be possible to create drives for a "modern" Amiga RTG card?

The motivation is not world domination, enterprise readiness, or achieving perfect enlightenment through driver development. This exists mostly out of curiosity, research, and a long-standing fascination with Amiga Unix. The goal is to explore, learn, poke things with a stick, and see how far the combination of old hardware, old Unix, modern tools, and questionable confidence can be pushed.

At least in its early stages, this project builds on the groundwork laid by Klaus Burckert, especially the X11R5 package from Gateway Volume 2 and his Cirrus Logic based graphics card support package. In other words, this is not an attempt to reinvent every wheel from scratch, but rather to stand on the shoulders of previous Amiga Unix explorers and carefully attach a VA2000-shaped backpack.

This is very much an experimental project. Things may work, things may break, and some things may work only after staring at them long enough. And if something does not work on your setup, I most likely wont able to fix it :D


### What works

- **Board detection** via `autocon()` at boot time
- **Register access** via `open()` and `read()`
- **Framebuffer mmap** — direct pixel access from user space
- **Display mode setting** — 800x600 16-bit confirmed working
- **Passthrough control** — switch between Amiga native video and RTG mode
- **CPU pixel fill** — direct framebuffer writes
- **Hardware blitter** — fast rectangle fill via VA2000 blitter engine

### Status

Confirmed working on real hardware, May 2026:
- Amiga 3000, 68030, AMIX 2.1p2a
- MNT VA2000 firmware 1.9.0 beta 2, Zorro II 4MB mode
- 800x600 16-bit color output via HDMI
- Hardware blitter: color bars, quadrants, checkerboard patterns confirmed

---

## Hardware Requirements

- Amiga with Zorro II slots (tested on Amiga 3000)
- MNT VA2000 graphics card, firmware 1.9.0 beta 2 or later
  - Zorro II mode, 4MB
  - Manufacturer ID: 0x6D6E, Product ID: 0x01
  - Board address: 0x600000
- Amiga UNIX (AMIX) System V Release 4.0, version 2.1p2a

---

## Files in This repository

```
va2000.c              - Kernel driver source code
va2000.h              - Kernel driver header (ioctl definitions)
va2000_test.c         - Interactive display test (CPU fills, color bars)
va2000_blit.c         - Hardware blitter test (fast rect fill)
va2000_restore.c      - Restore passthrough mode utility
amix_zp1_scan.c       - Utility: scan for Zorro boards via /dev/mem
amix_zp3_mmap.c       - Utility: test mmap access to VA2000
README.md             - This file
```

---

## VA2000 Memory Map

```
Offset 0x000000 - 0x00FFFF : registers
Offset 0x010000 - 0x3FFFFF : framebuffer RAM (~4MB)
```

## Key Registers

```
0x00  fw_version          firmware version (90 = fw 1.9.0b2)
0x04  scalemode           0=1x, 1=2x
0x06  screen_w            screen width
0x08  screen_h            screen height
0x0E  colormode           0=8bit, 1=16bit565, 2=32bit
0x14  safe_x2             safe area right
0x18  ram_fetch_delay2_max
0x1a  fetch_preroll
0x1c  blitter_row_pitch   blitter pitch in words
0x1e  blitter_colormode   blitter color mode
0x20  blitter_x1          rect left
0x22  blitter_y1          rect top
0x24  blitter_x2          rect right (inclusive)
0x26  blitter_y2          rect bottom (inclusive)
0x28  blitter_rgb16       fill color RGB565
0x2a  blitter_enable      write 1=start, read 0=done
0x38  pan_ptr_hi          pan pointer high
0x3a  pan_ptr_lo          pan pointer low
0x40  blitter_src_hi      source offset high (relative to fb, in words)
0x42  blitter_src_lo      source offset low
0x4e  capture_mode        0=RTG, 1=passthrough
0x58  row_pitch           display pitch in words
0x5c  row_pitch_shift     pitch as shift value
0x70  h_sync_start
0x72  h_sync_end
0x74  h_max
0x76  v_sync_start
0x78  v_sync_end
0x7a  v_max
0x7c  pixel_clk_sel       0=75MHz, 1=40MHz, 3=100MHz
```

---

## Passthrough vs RTG Mode

**Passthrough** (`capture_mode=1`): Shows Amiga native video via VA2000CX.
Default at boot. Restore sequence requires ALL of:
pan_ptr=0xf8:0x0000 + modeline + colormode + pitch + screen_w/h + capture_mode=1.
Setting only capture_mode=1 is NOT sufficient.

**RTG** (`capture_mode=0`): Shows VA2000 framebuffer.
Activation sequence: modeline + colormode + pitch + screen_w/h +
pan_ptr=0:0 + capture_mode=0 + modeline again (to clean glitches).

## Hardware Blitter

The VA2000 blitter does fast hardware rectangle fills.

**Critical finding:** `blitter_src` is relative to framebuffer start,
in 16-bit words. For row y: `src = y * pitch_words`. NOT an absolute address.
Determined experimentally by filling framebuffer with 0xBEEF and finding
where changed pixels appear after blit with src=0.

**Fill sequence:**
```
1. Wait blitter_enable == 0
2. blitter_src = y * pitch_words
3. blitter_row_pitch = pitch_words
4. blitter_rgb16 = color
5. blitter_colormode = 1
6. blitter_x1/y1 = top-left
7. blitter_x2/y2 = bottom-right (inclusive)
8. blitter_enable = 1
9. Wait blitter_enable == 0
```

## Known Modeline: 800x600 40MHz

```
h_sync_start=840  h_sync_end=968  h_max=1056
v_sync_start=601  v_sync_end=605  v_max=628
pixel_clk_sel=1 (40MHz)
row_pitch=800  row_pitch_shift=9  screen_w=800  screen_h=600
```

## RGB565 Colors

```
0xF800=Red   0x07E0=Green  0x001F=Blue   0xFFFF=White
0x0000=Black 0xFFE0=Yellow 0x07FF=Cyan   0xF81F=Magenta
```

---

## Installation

### 1. Copy files to kernel source

```sh
cp va2000.c va2000.h /usr/sys/amiga/driver/
```

### 2. Edit /usr/sys/amiga/driver/Makefile

Add `va2000.o\` to OBJ list before `tiga.o`.

### 3. Edit /usr/sys/master.d/kernel.c

Backup: `cp kernel.c kernel.c.orig`

**Add extern** (after tiopen line):
```c
extern va2open(),va2close(),va2read(),va2write(),va2ioctl(),va2mmap();
```

**Add to io_init[]**:
```c
extern void va2init();
void    (*io_init[])() = {
        parinit,
        va2init,
        0};
```

**Add to cdevsw[] at slot 68**:
```c
va2open,va2close,va2read,va2write,va2ioctl,
        va2mmap,ND,ND,ND,ND,notty,nostr,nullflag,  /*68=va2000*/
```

### 4. Build and install

```sh
cd /usr/sys && make install
```

### 5. Create device node

```sh
mknod /dev/va2000 c 68 0
chmod 666 /dev/va2000
```

### 6. Reboot

Boot messages:
```
va2000: board found at 0x600000
va2000: firmware version 90, ready
```

---

## Verification

```sh
dd if=/dev/va2000 bs=2 count=1 | od -x   # expect: 005a

cc amix_zp3_mmap.c -o t && ./t            # mmap test

cc va2000_test.c -o t && ./t              # CPU fill test
# commands: r=red g=green b=blue w=white c=bars q=quit

cc va2000_blit.c -o t && ./t              # blitter test
# commands: 1=red 2=quadrants 3=bars 4=checkerboard q=quit
```

---

## Recovery

Display stuck in RTG: `cc va2000_restore.c -o r && ./r`

---

## TODO

- [ ] rect_copy blitter (source to destination)
- [ ] More resolutions (1024x768, 1280x720, 1920x1080)
- [ ] 8-bit palette mode
- [ ] 32-bit color mode
- [ ] X11 server support
- [ ] showboards utility
- [ ] ioctl for mode setting
- [ ] Blitter vs CPU speed benchmark

---

## Credits

- MNT VA2000 hardware/firmware: Lukas F. Hartmann, MNT Media and Technology
- AMIX driver framework: Michael Ditto, Commodore-Amiga Inc.
- X11R5 installation and RTG drivers by Klaus Burckert (Gateway CD vol2)
- autocon() reference: Keith Gabryelski, Commodore (aen.c)
- Driver development: Amiga enthusiast + Claude (Anthropic), May 2026

## References

- https://github.com/mntmn/amiga2000-gfxcard
- https://shop.mntre.com/products/va2000-last-stock
- https://man.netbsd.org/NetBSD-8.0/amiga/mntva.4
- https://www.amigaunix.com
