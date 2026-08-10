# MNT VA2000 Device Driver for Amiga UNIX (AMIX)

## Overview

This is my AI driven hobby project to implement a device driver for the MNT VA2000
RTG graphics card running under Commodore Amiga UNIX (AMIX), System V Release 4.0.

This project is my first proper vibe-coding adventure. I started it after reading
Michael Ditton's Commodore DevCon documents and wondering the very (un)reasonable
question: *would it be possible to create a driver for a "modern" Amiga RTG card?*
....Without programming skills.

The motivation for this project is not world domination or achieving enlightenment
through driver development. This exists mostly out of curiosity, and fascination
to the forgotton and obscure Amiga Unix operating system. The goal is to explore,
and poke things with a stick, and see how far the combination of old
hardware, old Unix, modern AI tools can go.

This project builds on groundwork by Commodore Amix team, MNT Research, Klaus
Burckert and his X11R5 package from Gateway Volume 2 CD and Cirrus Logic
graphics card support.

---

## Disclaimer

This is a hobby project developed out of curiosity. Use it at your own risk.

Before installing the kernel driver, make a full backup of your AMIX system
disk. Patching kernel source files and rebuilding the kernel carries a real
risk of leaving your system unbootable. The install script creates `.orig`
backups of all modified files and the TROUBLESHOOTING.md file documents
recovery procedures, but these are not a substitute for a proper backup.

A few things worth knowing before you proceed:

- This has been tested on one specific machine (Amiga 3000, 68030, AMIX
  2.1p2a) with one specific VA2000 firmware version. Other configurations
  may behave differently.
- The kernel driver patches are applied to vanilla AMIX 2.1 source files.
  If your installation has already been modified — for example by Klaus
  Burkert's X11R5 package — the patch patterns may not match and manual
  intervention will be required.
- If the kernel fails to boot after installation, you will need an AMIX
  boot floppy to recover. Make sure you have one before you start.
- This project is not affiliated with MNT Research, Commodore, or any
  other organization. It is an independent experiment.

--- 

## Status (May 2026)

**Tested on:** Amiga 3000, 68030, AMIX SVR4 2.1p2a, MNT VA2000 fw1.9.0b2 (Zorro II mode)

### Working

- **Kernel driver installs and loads** in vanilla AMIX 2.1
- **Board detection** via `autocon()` at boot time
- **Register access** via `open()` and `read()`
- **Framebuffer mmap** — direct pixel access from user space
- **Display mode setting** — 800x600 16-bit, 1024x768, 1280x720 confirmed
- **Passthrough control** — switch between Amiga native and RTG mode
- **CPU pixel fill** — direct framebuffer writes
- **Hardware blitter** — fast rectangle fill confirmed working

### Known Issues

---

## Hardware Requirements

- Amiga with Zorro II slots (tested on Amiga 3000, 68030)
- MNT VA2000 graphics card, firmware 1.9.0 beta 2 or later
  - Zorro II mode, 4MB
  - Manufacturer ID: 0x6D6E, Product ID: 0x01
  - Board address: typically 0x600000
- Amiga UNIX (AMIX) System V Release 4.0, version 2.1p2a

### Tested Display Modes

| Mode | Clock | Status |
|------|-------|--------|
| 800x600 16-bit | 40MHz | Working |
| 1024x768 16-bit | 75MHz | Working |
| 1280x720 16-bit | 75MHz | Working |
| 640x480 | 40MHz | Monitor rejects signal |
| 1280x1024 | 100MHz | Monitor rejects signal |

---

## Files

```
src/
  va2000.c               Kernel device driver (major 68, /dev/va2000 c 68 0)
  va2000.h               Driver header, SVGA ioctl definitions

tools/
  va2000_test.c          Mode set and solid color fill test
  va2000_blit.c          Hardware blitter test — color bars, quadrants
  va2000_restore.c       Restore Amiga passthrough video
  amix_zp1_scan.c        Zorro bus scanner
  amix_zp3_mmap.c        Zorro mmap test

KERNEL_CHANGES.md        Required changes to AMIX kernel source files
install-va2000-driver.sh Experimental install script — copies driver and patches kernel sources
```

---

## Quick Start

### 1. Install the kernel driver

Run the install script from `/usr/sys`. The script copies `va2000.c` and
`va2000.h`, patches `amiga/driver/Makefile`, `scrdev.c`, `c0.c`, `screen.c`,
and `master.d/kernel.c`, then prints rebuild instructions.

```sh
cd /usr/sys
sh /path/to/install-va2000-driver.sh /path/to/va2000-amix
```

### 2. Create device node

This can be done before rebooting:

```sh
mknod /dev/va2000 c 68 0
chmod 666 /dev/va2000
```

### 3. Rebuild the kernel and reboot

```sh
cd /usr/sys
make install
sync; sync; sync
reboot
```

### 4. VA2000 tools

```sh
cc tools/va2000_blit.c -o va2000_blit
cc tools/va2000_xinit.c -o va2000_xinit
cc tools/va2000_restore.c -o va2000_restore
cc tools/va2000_spy.c -o va2000_spy
```

### 5. Test display

```sh
./va2000_blit
# Should show red screen, then color quadrants at 800x600
```

---



### mmap page calculation

Following Piccolo driver pattern:
```c
return (int)((va2000_boards[mindev] + offset) >> 11);
```
Not `phystopfn()` which was tried but did not improve behavior.

---

## License

MIT License. See LICENSE file.

-Antti Sokero 2026
