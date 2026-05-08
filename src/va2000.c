/*
 * va2000.c - Device driver for MNT VA2000 graphics card on Amiga UNIX
 *
 * First known working VA2000 driver for AMIX SVR4.
 * Developed May 2026.
 *
 * Based on:
 *   tiga.c  - ULowell TIGA board driver by Michael Ditto, Commodore-Amiga
 *   aen.c   - A2065 Ethernet driver by Keith Gabryelski, Commodore
 *
 * VA2000 hardware (MNT Media and Technology):
 *   Zorro II, 4MB total address space
 *   Manufacturer ID: 0x6D6E  Product ID: 0x01
 *   0x000000 - 0x00FFFF : registers (MNTVARegs layout, see va2000.h)
 *   0x010000 - 0x3FFFFF : framebuffer RAM
 *
 * autocon() signature (documented in aen.c):
 *   autocon(mannum, which, base_address, memory)
 *   Returns nonzero on success.
 *   base_address and memory are pointers to long.
 *
 * To add to kernel:
 *   1. Copy va2000.c and va2000.h to /usr/sys/amiga/driver/
 *   2. Add va2000.o to OBJ list in /usr/sys/amiga/driver/Makefile
 *   3. Edit /usr/sys/master.d/kernel.c (see README.md for details)
 *   4. cd /usr/sys && make install
 *   5. mknod /dev/va2000 c 68 0 && chmod 666 /dev/va2000
 *   6. reboot
 */

#include "sys/types.h"
#include "sys/immu.h"
#include "sys/param.h"
#include "sys/uio.h"
#include "sys/sysmacros.h"
#include "sys/inline.h"
#include "sys/errno.h"
#include "va2000.h"

/*
 * Packed Zorro product ID for autocon():
 *   upper 16 bits = manufacturer ID (0x6D6E = "MNT")
 *   lower 16 bits = product ID (0x01)
 * Same format as AEN_BOARD 0x02020070 in aen.h.
 */
#define VA2000_PRODUCT   0x6D6E0001UL

/* Total board size: 4MB */
#define VA2000_HWLEN     0x00400000UL

/*
 * Minimum firmware version.
 * From P96 AmigaOS driver source: if (fwrev < 5) return 0;
 * Firmware 1.9.0 beta 2 reports version 90.
 */
#define VA2000_FW_MIN    5

/* Maximum number of VA2000 boards supported */
#define VA2000_MAXBOARDS 2

/*
 * Register access macro.
 * VA2000 registers are 16-bit, accessed at 2-byte intervals.
 * base is a long (physical address), cast to char* for offset arithmetic.
 */
#define VA2000_REG(base, offset) \
    (*((volatile unsigned short *)((char *)(base) + (offset))))

/*
 * Register offsets (from va2000.h by Lukas F. Hartmann, MNT
 * and mntvareg.h from NetBSD mntva driver by Radoslaw Kujawa)
 */
#define VA2000_FW_VERSION  0x00  /* firmware version, must be >= VA2000_FW_MIN */
#define VA2000_SCREEN_W    0x06  /* screen width in pixels  */
#define VA2000_SCREEN_H    0x08  /* screen height in pixels */
#define VA2000_COLORMODE   0x0E  /* color depth mode        */

/*
 * Board base addresses.
 * Type long, same as aen_autoconfig_t.address in aen.h.
 * Filled in by autocon() at init time.
 * Index = minor device number (0 = first VA2000 found).
 */
static long va2000_boards[VA2000_MAXBOARDS];

/* Open reference count per board */
static int  va2000_open_count[VA2000_MAXBOARDS];


/*
 * va2000_present() - verify board is responding after autocon() found it.
 *
 * Reads fw_version register and checks it is a sane value.
 * Returns 1 if board looks alive, 0 otherwise.
 *
 * Note: before mode initialization all registers read 0x005A.
 * This is normal - the board is in passthrough/idle mode.
 * The fw_version register however should always reflect the actual
 * firmware version, not 0x005A, once the board is powered up.
 */
static int
va2000_present(base)
long base;
{
    unsigned short fw;

    if (!base)
        return 0;

    fw = VA2000_REG(base, VA2000_FW_VERSION);

    /* 0x0000 = board not responding at this address */
    /* 0xFFFF = no board mapped here (open bus) */
    if (fw == 0x0000 || fw == 0xFFFF)
        return 0;

    /* Firmware too old - P96 driver also rejects fw < 5 */
    if (fw < VA2000_FW_MIN)
        return 0;

    return 1;
}


/*
 * va2init() - called once at boot from init_tbl[] in kernel.c.
 *
 * Uses autocon() to locate VA2000 board on the Zorro bus.
 * Prints detection result to console - this is what you see at boot:
 *   "va2000: board found at 0x600000"
 *   "va2000: firmware version 90, ready"
 *
 * Same pattern as aenautoconfig() in aen.c.
 */
void
va2init()
{
    int i;
    long dummy;
    unsigned short fw;

    for (i = 0; i < VA2000_MAXBOARDS; i++) {
        va2000_boards[i] = 0;
        va2000_open_count[i] = 0;
    }

    /*
     * autocon(mannum, which, &base_address, &memory_size)
     * Scans Zorro bus for board matching VA2000_PRODUCT.
     * On success, fills va2000_boards[0] with board base address.
     */
    if (!autocon(VA2000_PRODUCT, 0, &va2000_boards[0], &dummy)) {
        printf("va2000: no board found\n");
        return;
    }

    printf("va2000: board found at 0x%lx\n", va2000_boards[0]);

    if (!va2000_present(va2000_boards[0])) {
        printf("va2000: board not responding or firmware too old\n");
        va2000_boards[0] = 0;
        return;
    }

    fw = VA2000_REG(va2000_boards[0], VA2000_FW_VERSION);
    printf("va2000: firmware version %d, ready\n", (int)fw);
}


/*
 * va2open() - called when /dev/va2000 is opened.
 *
 * Verifies board exists and is responding.
 * Tries autocon() again if va2init() did not find the board.
 * Mirrors tiopen() in tiga.c.
 */
int
va2open(devp, mode, type, cr)
dev_t *devp;
int mode;
int type;
struct cred *cr;
{
    unsigned int dev;
    long dummy;

    dev = getminor(*devp);

    if (dev >= VA2000_MAXBOARDS)
        return ENXIO;

    if (!va2000_boards[dev] &&
        !autocon(VA2000_PRODUCT, dev, &va2000_boards[dev], &dummy))
        return ENXIO;

    if (!va2000_present(va2000_boards[dev]))
        return ENXIO;

    va2000_open_count[dev]++;
    return 0;
}


/*
 * va2close() - called when /dev/va2000 is closed.
 */
int
va2close(dev, mode, type, cr)
dev_t dev;
int mode;
int type;
struct cred *cr;
{
    unsigned int mindev;

    mindev = getminor(dev);

    if (mindev >= VA2000_MAXBOARDS)
        return ENXIO;

    if (va2000_open_count[mindev] > 0)
        va2000_open_count[mindev]--;

    return 0;
}


/*
 * va2rdwr() - internal read/write handler.
 *
 * Provides sequential access to board address space.
 * offset 0x000000 = registers, offset 0x010000 = framebuffer.
 * Modeled after amrdwr() in amiga.c.
 */
static int
va2rdwr(dev, direction, uiop)
int dev;
uio_rw_t direction;
struct uio *uiop;
{
    unsigned long n, count;
    int error;

    if (!va2000_boards[dev])
        return ENXIO;

    if ((count = uiop->uio_resid) == 0)
        return 0;

    if (uiop->uio_offset < 0 ||
        uiop->uio_offset >= (long)VA2000_HWLEN)
        return ENXIO;

    /* Clamp transfer to board size */
    n = uiop->uio_offset + count;
    if (n > VA2000_HWLEN)
        count = VA2000_HWLEN - uiop->uio_offset;

    if (direction == UIO_WRITE && count == 0)
        return ENXIO;

    if (error = uiomove(
            (char *)(va2000_boards[dev]) + uiop->uio_offset,
            count, direction, uiop))
        return error;

    return 0;
}

int
va2read(dev, uiop, cr)
dev_t dev;
struct uio *uiop;
struct cred *cr;
{
    return va2rdwr(getminor(dev), UIO_READ, uiop);
}

int
va2write(dev, uiop, cr)
dev_t dev;
struct uio *uiop;
struct cred *cr;
{
    return va2rdwr(getminor(dev), UIO_WRITE, uiop);
}


/*
 * va2mmap() - map VA2000 board memory into calling process address space.
 *
 * This is the key function for X11 support. It allows an X server or
 * other graphics program to map the framebuffer directly into its address
 * space and write pixels without going through the kernel on every access.
 *
 * Memory layout accessible via mmap():
 *   offset 0x000000 - 0x00FFFF : register area (MNTVARegs)
 *   offset 0x010000 - 0x3FFFFF : framebuffer RAM
 *
 * Returns physical page frame number via phystopfn(), or -1 on error.
 * The kernel uses the page frame number to build the process page table.
 *
 * Identical in structure to timmap() in tiga.c.
 */
int
va2mmap(dev, offset, maxprot)
dev_t dev;
off_t offset;
int maxprot;
{
    unsigned int mindev;

    mindev = getminor(dev);

    if (mindev >= VA2000_MAXBOARDS)
        return -1;

    if (!va2000_boards[mindev])
        return -1;

    if (offset >= 0 && offset < (off_t)VA2000_HWLEN)
        return phystopfn((char *)(va2000_boards[mindev]) + offset);

    return -1;
}


/*
 * va2ioctl() - device control operations.
 *
 * Currently provides read-only access to key board status registers.
 * Future: mode setting, palette control, blitter operations.
 */
int
va2ioctl(dev, cmd, arg, mode, cr, rvalp)
dev_t dev;
int cmd;
int arg;
int mode;
struct cred *cr;
int *rvalp;
{
    unsigned int mindev;
    long base;

    mindev = getminor(dev);

    if (mindev >= VA2000_MAXBOARDS || !va2000_boards[mindev])
        return ENXIO;

    base = va2000_boards[mindev];

    switch (cmd) {
    case VA2IOC_GETFW:
        /* Return firmware version number */
        *rvalp = (int)VA2000_REG(base, VA2000_FW_VERSION);
        break;
    case VA2IOC_GETSCREENW:
        /* Return current screen width (valid after mode set) */
        *rvalp = (int)VA2000_REG(base, VA2000_SCREEN_W);
        break;
    case VA2IOC_GETSCREENH:
        /* Return current screen height (valid after mode set) */
        *rvalp = (int)VA2000_REG(base, VA2000_SCREEN_H);
        break;
    case VA2IOC_GETCOLORMODE:
        /* Return color mode (0=8bit, 1=16bit565, 2=32bit) */
        *rvalp = (int)VA2000_REG(base, VA2000_COLORMODE);
        break;
    default:
        return EINVAL;
    }

    return 0;
}
