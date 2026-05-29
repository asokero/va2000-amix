# Kernel Changes Required for VA2000 X11 Support

These changes to AMIX kernel source files are required for installing the VA2000 driver.

## Overview

AMIX uses a screen subsystem (`/dev/screen`) that Xsvga uses to allocate
framebuffers. By default it only supports Amiga native bitplane modes.
VA2000 support requires adding `type=2` (chunky RTG) to three files.

## Files to Modify

### 1. `/usr/sys/amiga/driver/Makefile`

**Purpose:** Include `va2000.o` in the `amiga/driver/exp` link so the driver
is actually compiled and linked into the kernel. Without this change the
linker reports all va2000 entry points as unresolved symbols even though
`va2000.c` has been copied to the directory.

Add `va2000.o` to the `ld -r -o exp ...` line, after `tiga.o`:

```
# Before:
ld -r -o exp ... tiga.o  audio.o  aen/exp

# After:
ld -r -o exp ... tiga.o va2000.o  audio.o  aen/exp
```

---

### 3. `/usr/sys/amiga/console/scrdev.c`

**Purpose:** Add VA2000 as `type=2` in AMIX screen subsystem.

**Change 1:** Add `va2000_base` field to `scrdev` struct:
```c
/* Find: */
    struct pollhead pollist;
} scrdev[NSCRDEV];

/* Replace with: */
    struct pollhead pollist;
    unsigned long va2000_base;  /* VA2000 board base address (type=2) */
} scrdev[NSCRDEV];
```

**Change 2:** Add VA2000 case to `allocbmap()`:
```c
/* Add before "default:" in allocbmap() switch: */
    case 2:
        /* VA2000 chunky framebuffer - board memory, nothing to allocate */
        bp->bpl[0] = (unsigned char *)(dp->va2000_base + 0x10000UL);
        bp->depth = 1;
        break;
```

**Change 3:** Add VA2000 case to `freebmap()`:
```c
/* Add after case 1: in freebmap() switch: */
    case 2:
        /* VA2000: board memory, nothing to free */
        bp->bpl[0] = (unsigned char *)0;
        break;
```

**Change 4:** Add VA2000 mapping to `scrmmap()`:
```c
/* Add before existing "if (offset < ...)" in scrmmap(): */
    if (dp->type == 2)
    {
        /* VA2000 chunky 16-bit: width * height * 2 bytes */
        if (offset < (off_t)(bp->width * bp->height * 2))
            return phystopfn(bp->bpl[0] + offset);
        return -1;
    }
```

**Change 5:** In `SIOCSETTYPE` handler, skip copper list for VA2000:
```c
/* After dp->type = scrtype.type; add: */
    if (dp->type == 2)
    {
        extern long va2000_boards[];
        dp->va2000_base = va2000_boards[0];
        if (!dp->va2000_base)
            return ENXIO;
        dp->copsize = 0;
        dp->copbuf[0] = (unsigned short *)0;
        dp->copbuf[1] = (unsigned short *)0;
        break;  /* skip copper list allocation */
    }
```

---

### 4. `/usr/sys/amiga/console/c0.c`

**Purpose:** Allow type=2 to pass `check_scrtype()` validation.

```c
/* Add in check_scrtype() switch, before "default:": */
    case 2:
        /* VA2000 chunky RTG - bypass Amiga mode checks */
        return 0;
```

---

### 5. `/usr/sys/amiga/console/screen.c`

**Purpose:** Add `SVGAChkGroup()` stub (called when screen group activates).

```c
/* Add near top of file, before first function: */
void
SVGAChkGroup(groupnum)
int groupnum;
{
    /* Stub: VA2000 display switching handled by va2000 driver */
}
```

---

### 6. `/usr/sys/master.d/kernel.c`

**Purpose:** Register va2000 in the character device switch table and call
`va2000init` at boot time.

**Change 1:** Add `extern` declarations for cdevsw entry points immediately
after `extern scropen()`. The block goes after the other driver extern
declarations:
```c
extern va2000open(),va2000close(),va2000read(),va2000write(),
	va2000ioctl(),va2000mmap();
```

**Change 2:** Add `extern` declaration for `va2000init` immediately after
`extern void parinit();` (the line directly before `io_init[]`). This
placement is required because `io_init[]` uses `va2000init` on the next
line — the declaration must appear first:
```c
extern void va2000init();
```

**Change 3:** Replace the empty slot 68 in `cdevsw[]`:
```c
/* Replace the /*68*/ line with: */
va2000open,va2000close,va2000read,va2000write,va2000ioctl,
	va2000mmap,ND,ND,ND,ND,notty,nostr,nullflag,		/*68=va2000*/
```

**Change 4:** Add `va2000init` to `io_init[]` before the null terminator:
```c
void	(*io_init[])() = {
	parinit,
	va2000init,     /* add this line */
	0};
```

---

## Rebuild Kernel

Create the device node first (can be done before rebooting):

```sh
mknod /dev/va2000 c 68 0
chmod 666 /dev/va2000
```

Then rebuild and reboot:

```sh
cd /usr/sys
make install
sync; sync; sync
reboot
```

---

## XsvgaConfig Changes

Set `ScreenMode "800x600_60"` and `Device /dev/va2000`:

```
ScreenMode "800x600_60"
Device /dev/va2000

ModeDB
"800x600_60"  40  800 840 968 1056  600 601 605 628  H+V+
"1024x768_75" 75 1024 1048 1184 1328  768 771 777 806
"1280x720_75" 75 1280 1390 1430 1650  720 725 730 750
# end of mode-database
```

Note: The H-sync timings differ slightly from Piccolo defaults.
