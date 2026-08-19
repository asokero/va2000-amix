/*
 * va2000.c - MNT VA2000 device driver for Amiga UNIX (AMIX SVR4)
 *
 * Copyright (c) 2025-2026 Antti Sokero
 * SPDX-License-Identifier: MIT
 * See LICENSE file in the repository root for details.
 *
 * Kernel character device: /dev/va2000 (major 68, minor 0)
 * Card: MNT VA2000, Zorro II or Zorro III, manufacturer 0x6D6E product 0x01
 *
 * VA2000 memory map, relative to the AutoConfig base, in BOTH bus modes:
 *   +0x000000 - +0x000FFF : registers (4 KiB)
 *   +0x010000 - +size     : framebuffer
 * The layout is the same because the firmware assigns it the same way in its
 * CONFIGURED state; only the aperture SIZE differs -- 4 MB in Zorro II, 32 MB
 * in Zorro III -- and it is read from AutoConfig, not assumed.
 *
 * ADDRESS AGNOSTIC (2026-08-19).  cd_BoardAddr is a PHYSICAL address, and on
 * this machine only Zorro II space is transparently translated for the kernel;
 * a Zorro III board at 0x40000000+ is inside the kernel's own virtual region,
 * so dereferencing it directly does not fault -- it silently hits kernel
 * memory.  So this driver keeps two bases: va2000_boards[] stays PHYSICAL
 * because d_mmap must return a page frame number, and va2000_regs[] is what
 * the CPU dereferences.  With -DVA2000_KVA (the 68040/68060 port) the latter
 * is a real kernel mapping with an explicit noncacheable-serialised class;
 * without it, on the vanilla 68030 kernel, the two are the same value and this
 * driver behaves exactly as it always has.
 *
 * Registers are write-only from kernel context; reads return the
 * firmware version byte (0x5a for fw98).  RTG activation
 * (capture_mode=0) must be performed from userspace via mmap.
 * Framebuffer starts at mmap offset 0x10000.
 *
 * ioctl interface is compatible with svgaioctl.h (Xsvga / Xrtg).
 * SVGAIOCSetScreenMode: use HDisplayEnd/VDisplayEnd, not Width/Height.
 *
 * Based on:
 *   tiga.c  - ULowell TIGA driver by Michael Ditto, Commodore-Amiga
 *   aen.c   - A2065 Ethernet driver by Keith Gabryelski, Commodore
 *
 * autocon() usage (from aen.c):
 *   autocon(mannum, which, &base_address, &memory_size)
 *   mannum = (manufacturer << 16) | product = 0x6D6E0001
 */

#include "sys/types.h"
#include "sys/immu.h"
#include "sys/param.h"
#include "sys/uio.h"
#include "sys/sysmacros.h"
#include "sys/inline.h"
#include "sys/errno.h"
#include "va2000.h"

#define VA2000_PRODUCT   0x6D6E0001UL
#define VA2000_HWLEN     0x00400000UL   /* Zorro II aperture -- FALLBACK ONLY, used
                                         * when AutoConfig reports an unusable size */
#define VA2000_FB_OFFSET 0x00010000UL
#define VA2000_FB_SIZE   (VA2000_HWLEN - VA2000_FB_OFFSET)
#define VA2000_REGLEN    0x00001000UL   /* register window: 4 KiB in both bus modes.
                                         * Every register this driver touches is below
                                         * 0x800 (palette 0x600 + 255*2 = 0x7fe), so
                                         * one page covers all of them. */
#define VA2000_WINLEN    0x00010000UL   /* temporary window for read()/write(); see
                                         * va2000rdwr() for why it is not the aperture */
#define VA2000_CM_NCS    0x40           /* noncacheable, serialised: control registers */
#define VA2000_FW_MIN    5
#define VA2000_MAXBOARDS 2

#define VA2REG(base, offset) \
    (*((volatile unsigned short *)((char *)(base) + (offset))))

#define VA2_FW_VERSION   0x00
#define VA2_SCALEMODE    0x04
#define VA2_SCREEN_W     0x06
#define VA2_SCREEN_H     0x08
#define VA2_COLORMODE    0x0E
#define VA2_SAFE_X2      0x14
#define VA2_RAM_FETCH    0x18
#define VA2_FETCH_PRE    0x1A
#define VA2_BLIT_PITCH   0x1C
#define VA2_BLIT_CMODE   0x1E
#define VA2_BLIT_X1      0x20
#define VA2_BLIT_Y1      0x22
#define VA2_BLIT_X2      0x24
#define VA2_BLIT_Y2      0x26
#define VA2_BLIT_RGB16   0x28
#define VA2_BLIT_ENABLE  0x2A
#define VA2_PAN_HI       0x38
#define VA2_PAN_LO       0x3A
#define VA2_BLIT_SRC_HI  0x40
#define VA2_BLIT_SRC_LO  0x42
#define VA2_CAPTURE_MODE 0x4E
#define VA2_ROW_PITCH    0x58
#define VA2_PITCH_SHIFT  0x5C
#define VA2_H_SS         0x70
#define VA2_H_SE         0x72
#define VA2_H_MAX        0x74
#define VA2_V_SS         0x76
#define VA2_V_SE         0x78
#define VA2_V_MAX        0x7A
#define VA2_PIX_CLK      0x7C

#define VA2CLR_8BIT   0
#define VA2CLR_16BIT  1
#define VA2CLR_32BIT  2

#define VA2CLK_75MHZ  0
#define VA2CLK_40MHZ  1
#define VA2CLK_100MHZ 3

/* extern so scrdev.c can access va2000_boards[0].  PHYSICAL base: d_mmap
 * converts it to a page frame number, so it must NOT become a kernel VA. */
long va2000_boards[VA2000_MAXBOARDS];
/* Aperture size as reported by AutoConfig (4 MB Zorro II / 32 MB Zorro III). */
long va2000_size[VA2000_MAXBOARDS];
/* What the CPU dereferences for register access.  See the header comment. */
static long va2000_regs[VA2000_MAXBOARDS];
static int va2000_open_count[VA2000_MAXBOARDS];
static unsigned short va2000_cur_w[VA2000_MAXBOARDS];
static unsigned short va2000_cur_h[VA2000_MAXBOARDS];
static unsigned short va2000_cur_bpp[VA2000_MAXBOARDS];
static unsigned short va2000_cur_pitch[VA2000_MAXBOARDS];
static unsigned short va2000_monitor_switch[VA2000_MAXBOARDS];


#ifdef VA2000_KVA
/*
 * Provided by the 68040/68060 port (src/devkvmap040.s), not by AMIX:
 *   char *dev_kvmap(phys, nbytes, cm, flags)   -- map MMIO with an explicit
 *                                                 cache class; NULL on failure
 *   void  dev_kvunmap(kva, nbytes)             -- undo one dev_kvmap
 * flags bit 0 = NOSLEEP.
 */
extern char *dev_kvmap();
extern void dev_kvunmap();
#endif

/*
 * va2000_map_regs() -- establish the CPU-usable register base for one board.
 *
 * Idempotent, and called before the first register access on every path that
 * can be the first one (init, and open for a board discovered late).  The
 * mapping is deliberately never released: a mapping can outlive the file
 * descriptor that caused it, and one 4 KiB slot out of the kernel's virtual
 * arena is not worth the lifetime problem that releasing it would create.
 *
 * Returns 1 if va2000_regs[dev] is usable afterwards, 0 otherwise.
 */
static int
va2000_map_regs(dev)
int dev;
{
    if (va2000_regs[dev]) return 1;
    if (!va2000_boards[dev]) return 0;
#ifdef VA2000_KVA
    /* NOSLEEP: this runs from the kernel's io_init walker at boot. */
    va2000_regs[dev] = (long)dev_kvmap(va2000_boards[dev], VA2000_REGLEN,
                                       VA2000_CM_NCS, 1);
    if (!va2000_regs[dev])
        printf("va2000: cannot map registers at 0x%lx\n", va2000_boards[dev]);
#else
    /* Vanilla 68030 kernel: the board is in transparently translated space. */
    va2000_regs[dev] = va2000_boards[dev];
#endif
    return va2000_regs[dev] ? 1 : 0;
}

static int
va2000_present(base)
long base;
{
    unsigned short fw;
    if (!base) return 0;
    fw = VA2REG(base, VA2_FW_VERSION);
    if (fw == 0x0000 || fw == 0xFFFF) return 0;
    if (fw < VA2000_FW_MIN) return 0;
    return 1;
}

static unsigned short
svga_pixclock_to_va2(hz)
unsigned long hz;
{
    if (hz <= 40000000UL) return VA2CLK_40MHZ;
    if (hz <= 75000000UL) return VA2CLK_75MHZ;
    return VA2CLK_100MHZ;
}

/*
 * va2_set_mode() - Write timing and color registers.
 *
 * NOTE: VA2000 register writes from kernel are write-only.
 * capture_mode=0 is written here but RTG activation requires
 * a userspace mmap write first (blit_test3, va2000_preinit).
 *
 * K&R: each parameter on its own line.
 */
static void
va2_set_mode(base, w, h, hss, hse, hmax, vss, vse, vmax, clk, colormode, pitch_w, scalemode)
long base;
unsigned short w;
unsigned short h;
unsigned short hss;
unsigned short hse;
unsigned short hmax;
unsigned short vss;
unsigned short vse;
unsigned short vmax;
unsigned short clk;
unsigned short colormode;
unsigned short pitch_w;
unsigned short scalemode;  /* (vscale<<2)|hscale; 0=1:1, 5=2x both */
{
    VA2REG(base, VA2_H_SS)         = hss;
    VA2REG(base, VA2_H_SE)         = hse;
    VA2REG(base, VA2_H_MAX)        = hmax;
    VA2REG(base, VA2_V_SS)         = vss;
    VA2REG(base, VA2_V_SE)         = vse;
    VA2REG(base, VA2_V_MAX)        = vmax;
    VA2REG(base, VA2_PIX_CLK)      = clk;
    VA2REG(base, VA2_COLORMODE)    = colormode;
    VA2REG(base, VA2_ROW_PITCH)    = pitch_w;
    VA2REG(base, VA2_PITCH_SHIFT)  = 9;
    VA2REG(base, VA2_SCREEN_W)     = w;
    VA2REG(base, VA2_SCREEN_H)     = h;
    VA2REG(base, VA2_SCALEMODE)    = scalemode;
    VA2REG(base, VA2_SAFE_X2)      = 0x1e0;
    VA2REG(base, VA2_FETCH_PRE)    = 0x1e0;
    VA2REG(base, VA2_RAM_FETCH)    = 0x17;
    VA2REG(base, VA2_PAN_HI)       = 0;
    VA2REG(base, VA2_PAN_LO)       = 0;
    VA2REG(base, VA2_CAPTURE_MODE) = 0;

    /* Repeat modeline to clean glitches */
    VA2REG(base, VA2_H_SS)         = hss;
    VA2REG(base, VA2_H_SE)         = hse;
    VA2REG(base, VA2_H_MAX)        = hmax;
    VA2REG(base, VA2_V_SS)         = vss;
    VA2REG(base, VA2_V_SE)         = vse;
    VA2REG(base, VA2_V_MAX)        = vmax;
}

static void
va2_restore_passthrough(base)
long base;
{
    VA2REG(base, VA2_PAN_HI)       = 0xf8;
    VA2REG(base, VA2_PAN_LO)       = 0;
    VA2REG(base, VA2_H_SS)         = 840;
    VA2REG(base, VA2_H_SE)         = 968;
    VA2REG(base, VA2_H_MAX)        = 1056;
    VA2REG(base, VA2_V_SS)         = 601;
    VA2REG(base, VA2_V_SE)         = 605;
    VA2REG(base, VA2_V_MAX)        = 628;
    VA2REG(base, VA2_PIX_CLK)      = VA2CLK_40MHZ;
    VA2REG(base, VA2_COLORMODE)    = VA2CLR_16BIT;
    VA2REG(base, VA2_ROW_PITCH)    = 320;
    VA2REG(base, VA2_PITCH_SHIFT)  = 9;
    VA2REG(base, VA2_SCREEN_W)     = 640;
    VA2REG(base, VA2_SCREEN_H)     = 480;
    VA2REG(base, VA2_SCALEMODE)    = 0;
    VA2REG(base, VA2_CAPTURE_MODE) = 1;
}

static int
va2_blit_wait(base)
long base;
{
    int timeout;
    timeout = 200000;
    while (VA2REG(base, VA2_BLIT_ENABLE) != 0) {
        if (--timeout <= 0) return -1;
    }
    return 0;
}

void
va2000init()
{
    int i;
    long size;
    unsigned short fw;

    for (i = 0; i < VA2000_MAXBOARDS; i++) {
        va2000_boards[i]         = 0;
        va2000_size[i]           = 0;
        va2000_regs[i]           = 0;
        va2000_open_count[i]     = 0;
        va2000_cur_w[i]          = 0;
        va2000_cur_h[i]          = 0;
        va2000_cur_bpp[i]        = 16;
        va2000_cur_pitch[i]      = 0;
        va2000_monitor_switch[i] = SVGAMONITORSWITCH_Amiga;
    }

    size = 0;
    if (!autocon(VA2000_PRODUCT, 0, &va2000_boards[0], &size)) {
        printf("va2000: no board found\n");
        return;
    }

    /* The aperture size comes from AutoConfig so that the same binary serves a
     * 4 MB Zorro II board and a 32 MB Zorro III one.  Anything smaller than the
     * register window plus one framebuffer page is not a size we can use, so
     * fall back rather than compute a negative framebuffer. */
    if (size < (long)(VA2000_FB_OFFSET + VA2000_REGLEN))
        size = (long)VA2000_HWLEN;
    va2000_size[0] = size;

    printf("va2000: board found at 0x%lx, aperture %ld KB\n",
           va2000_boards[0], (long)(size >> 10));

    if (!va2000_map_regs(0)) {
        va2000_boards[0] = 0;
        return;
    }

    if (!va2000_present(va2000_regs[0])) {
        printf("va2000: board not responding or firmware too old\n");
        va2000_boards[0] = 0;
        return;
    }

    fw = VA2REG(va2000_regs[0], VA2_FW_VERSION);
    printf("va2000: firmware version %d, ready\n", (int)fw);
}

int
va2000open(devp, mode, type, cr)
dev_t *devp;
int mode;
int type;
struct cred *cr;
{
    unsigned int dev;
    long size;

    dev = getminor(*devp);
    if (dev >= VA2000_MAXBOARDS) return ENXIO;
    if (!va2000_boards[dev]) {
        size = 0;
        if (!autocon(VA2000_PRODUCT, dev, &va2000_boards[dev], &size))
            return ENXIO;
        if (size < (long)(VA2000_FB_OFFSET + VA2000_REGLEN))
            size = (long)VA2000_HWLEN;
        va2000_size[dev] = size;
    }
    if (!va2000_map_regs(dev)) return ENXIO;
    if (!va2000_present(va2000_regs[dev])) return ENXIO;
    va2000_open_count[dev]++;
    return 0;
}

int
va2000close(dev, mode, type, cr)
dev_t dev;
int mode;
int type;
struct cred *cr;
{
    unsigned int mindev;
    mindev = getminor(dev);
    if (mindev >= VA2000_MAXBOARDS) return ENXIO;
    if (va2000_open_count[mindev] > 0) {
        va2000_open_count[mindev]--;
        if (va2000_open_count[mindev] == 0 && va2000_regs[mindev]) {
            va2_restore_passthrough(va2000_regs[mindev]);
            va2000_monitor_switch[mindev] = SVGAMONITORSWITCH_Amiga;
        }
    }
    return 0;
}

/*
 * va2000rdwr() -- character read()/write() across the whole aperture.
 *
 * This is a compatibility path: X and the RTG clients use mmap(), and nothing
 * in the tools tree reads or writes the device this way.  It is kept working
 * rather than quietly dropped, because removing it would be a behaviour change
 * nobody asked for.
 *
 * It must NOT be pointed at the permanent register mapping: that covers one
 * 4 KiB page, and this path addresses the entire aperture -- up to 32 MB in
 * Zorro III, which cannot be mapped permanently (the kernel's virtual arena is
 * 4 MB in total, shared with the rest of the kernel).  So under -DVA2000_KVA it
 * maps a bounded temporary window per chunk and releases it again.
 *
 * The window is mapped noncacheable-SERIALISED, which is the conservative
 * class: correct everywhere, and it deliberately does not depend on the
 * framebuffer cache-class work.  Revisit only with a measurement.
 */
static int
va2000rdwr(dev, direction, uiop)
int dev;
uio_rw_t direction;
struct uio *uiop;
{
    unsigned long n;
    unsigned long count;
    unsigned long off;
    int error;
#ifdef VA2000_KVA
    char *kva;
#endif

    if (!va2000_boards[dev]) return ENXIO;
    if ((count = uiop->uio_resid) == 0) return 0;
    if (uiop->uio_offset < 0 ||
        uiop->uio_offset >= (long)va2000_size[dev]) return ENXIO;
    off = (unsigned long)uiop->uio_offset;
    n = off + count;
    if (n > (unsigned long)va2000_size[dev])
        count = (unsigned long)va2000_size[dev] - off;
    if (direction == UIO_WRITE && count == 0) return ENXIO;
#ifdef VA2000_KVA
    while (count > 0) {
        n = count;
        if (n > VA2000_WINLEN) n = VA2000_WINLEN;
        /* sleep is allowed here: this is syscall context, not io_init */
        kva = dev_kvmap(va2000_boards[dev] + off, n, VA2000_CM_NCS, 0);
        if (!kva) return ENOMEM;
        error = uiomove(kva, n, direction, uiop);
        dev_kvunmap(kva, n);
        if (error) return error;
        off += n;
        count -= n;
    }
    return 0;
#else
    if (error = uiomove(
            (char *)(va2000_boards[dev]) + off,
            count, direction, uiop))
        return error;
    return 0;
#endif
}

int
va2000read(dev, uiop, cr)
dev_t dev;
struct uio *uiop;
struct cred *cr;
{ return va2000rdwr(getminor(dev), UIO_READ, uiop); }

int
va2000write(dev, uiop, cr)
dev_t dev;
struct uio *uiop;
struct cred *cr;
{ return va2000rdwr(getminor(dev), UIO_WRITE, uiop); }

int
va2000mmap(dev, offset, maxprot)
dev_t dev;
off_t offset;
int maxprot;
{
    unsigned int mindev;
    mindev = getminor(dev);
    if (mindev >= VA2000_MAXBOARDS) return -1;
    if (!va2000_boards[mindev]) return -1;
    /* PHYSICAL on purpose: the VM builds the user mapping from this page frame
     * number.  Bounded by the AutoConfig aperture, not by a compile-time 4 MB. */
    if (offset >= 0 && offset < (off_t)va2000_size[mindev])
        return (int)((va2000_boards[mindev] + offset) >> 11);
    return -1;
}

int
va2000ioctl(dev, cmd, arg, mode, cr, rvalp)
dev_t dev;
int cmd;
int arg;
int mode;
struct cred *cr;
int *rvalp;
{
    unsigned int mindev;
    long base;
    struct SVGABoardData  bd;
    struct SVGAScreenMode sm;
    struct SVGAScreenData sd;
    struct SVGAPanPos     pp;
    unsigned short sw;
    unsigned short va2clr;
    unsigned short va2clk;
    unsigned short va2scale;
    unsigned short pitch_w;
    unsigned short w;
    unsigned short h;

    mindev = getminor(dev);
    if (mindev >= VA2000_MAXBOARDS || !va2000_regs[mindev])
        return ENXIO;
    base = va2000_regs[mindev];    /* register access only -- never for mmap */

    switch (cmd) {

    case VA2IOC_GETFW:
        *rvalp = (int)VA2REG(base, VA2_FW_VERSION);
        break;

    case VA2IOC_GETSCREENW:
        *rvalp = (int)va2000_cur_w[mindev];
        break;

    case VA2IOC_GETSCREENH:
        *rvalp = (int)va2000_cur_h[mindev];
        break;

    case VA2IOC_GETCOLORMODE:
        *rvalp = (int)VA2REG(base, VA2_COLORMODE);
        break;

    case SVGAIOCGetBoardData:
        bd.SVGABD_CardID       = SVGACARDID_VA2000;
        bd.SVGABD_FrameBufSize = va2000_size[mindev] - (long)VA2000_FB_OFFSET;
        bd.SVGABD_MaxPixClk    = 100;
        bd.SVGABD_HasBlitter   = 1;
        bd.SVGABD_HasPanning   = 1;
        bd.SVGABD_Colors       = 0x0008;  /* hicolor only */
        bd.SVGABD_HasHWCursor  = 0;
        bd.SVGABD_reserved1    = 0;
        bd.SVGABD_reserved2[0] = 0;
        bd.SVGABD_reserved2[1] = 0;
        bd.SVGABD_reserved2[2] = 0;
        bd.SVGABD_reserved2[3] = 0;
        if (copyout((caddr_t)&bd, (caddr_t)arg, sizeof(bd)))
            return EFAULT;
        break;

    case SVGAIOCGetFBufSize:
        *rvalp = (int)(va2000_size[mindev] - (long)VA2000_FB_OFFSET);
        break;

    case SVGAIOCGetMonitorSwitch:
        sw = va2000_monitor_switch[mindev];
        if (copyout((caddr_t)&sw, (caddr_t)arg, sizeof(sw)))
            return EFAULT;
        break;

    case SVGAIOCSetMonitorSwitch:
        if (copyin((caddr_t)arg, (caddr_t)&sw, sizeof(sw)))
            return EFAULT;
        va2000_monitor_switch[mindev] = sw;
        /* RTG activation handled by userspace - kernel writes
         * do not activate VA2000 hardware */
        break;

    case SVGAIOCSetScreenMode:
        if (copyin((caddr_t)arg, (caddr_t)&sm, sizeof(sm)))
            return EFAULT;

        va2clk = svga_pixclock_to_va2(sm.SVGASM_PixelClock);

        if (sm.SVGASM_ColorMode == SVGACM_8BIT) {
            va2clr  = VA2CLR_8BIT;
            pitch_w = sm.SVGASM_HDisplayEnd / 2;  /* 2 pixels per 16-bit word */
        } else {
            va2clr  = VA2CLR_16BIT;
            pitch_w = sm.SVGASM_HDisplayEnd;
        }

        /* apply 2x h+v pixel doubling for small modes (<=640x360) */
        if (sm.SVGASM_VDisplayEnd <= 360 && sm.SVGASM_HDisplayEnd <= 640) {
            w        = (unsigned short)(sm.SVGASM_HDisplayEnd * 2);
            h        = (unsigned short)(sm.SVGASM_VDisplayEnd * 2);
            if (h < 480) h = 480;
            va2scale = 5;  /* (1<<2)|1: vscale=1, hscale=1 */
        } else {
            w        = (unsigned short)sm.SVGASM_HDisplayEnd;
            h        = (unsigned short)sm.SVGASM_VDisplayEnd;
            va2scale = 0;
        }

        va2_set_mode(base, w, h,
                     (unsigned short)sm.SVGASM_HSyncStart,
                     (unsigned short)sm.SVGASM_HSyncEnd,
                     (unsigned short)sm.SVGASM_HTotal,
                     (unsigned short)sm.SVGASM_VSyncStart,
                     (unsigned short)sm.SVGASM_VSyncEnd,
                     (unsigned short)sm.SVGASM_VTotal,
                     (unsigned short)va2clk,
                     (unsigned short)va2clr,
                     (unsigned short)pitch_w,
                     (unsigned short)va2scale);

        va2000_cur_w[mindev]     = sm.SVGASM_HDisplayEnd;
        va2000_cur_h[mindev]     = sm.SVGASM_VDisplayEnd;
        va2000_cur_pitch[mindev] = pitch_w * 2;
        va2000_cur_bpp[mindev]   = (sm.SVGASM_ColorMode == SVGACM_8BIT) ? 8 : 16;
        va2000_monitor_switch[mindev] = SVGAMONITORSWITCH_SVGA;
        break;

    case SVGAIOCGetScreenData:
        sd.SVGASD_VisibleWidth  = va2000_cur_w[mindev];
        sd.SVGASD_VisibleHeight = va2000_cur_h[mindev];
        sd.SVGASD_VirtualWidth  = va2000_cur_w[mindev];
        sd.SVGASD_VirtualHeight = va2000_cur_h[mindev];
        sd.SVGASD_HFreq         = 0;
        sd.SVGASD_VFreq         = 0;
        sd.SVGASD_BytesPerRow   = va2000_cur_pitch[mindev];
        sd.SVGASD_BitsPerPixel  = va2000_cur_bpp[mindev];
        sd.SVGASD_padding1      = 0;
        sd.SVGASD_padding2      = 0;
        sd.SVGASD_Color         = (unsigned long)((va2000_cur_bpp[mindev] == 8) ? SVGACM_8BIT : SVGACM_16BIT);
        sd.SVGASD_reserved2[0]  = 0;
        sd.SVGASD_reserved2[1]  = 0;
        sd.SVGASD_reserved2[2]  = 0;
        sd.SVGASD_reserved2[3]  = 0;
        sd.SVGASD_reserved2[4]  = 0;
        sd.SVGASD_reserved2[5]  = 0;
        sd.SVGASD_reserved2[6]  = 0;
        sd.SVGASD_reserved2[7]  = 0;
        sd.SVGASD_reserved2[8]  = 0;
        sd.SVGASD_reserved2[9]  = 0;
        if (copyout((caddr_t)&sd, (caddr_t)arg, sizeof(sd)))
            return EFAULT;
        break;

    case SVGAIOCGetPanPos:
        pp.SVGAPP_XPos = 0;
        pp.SVGAPP_YPos = 0;
        if (copyout((caddr_t)&pp, (caddr_t)arg, sizeof(pp)))
            return EFAULT;
        break;

    case SVGAIOCSetPanPos:
    case SVGAIOCSetPanXPos:
    case SVGAIOCSetPanYPos:
        break;

    case SVGAIOCClearBoardMem:
        {
            unsigned short pw2;
            unsigned short h2;
            unsigned long src;
            pw2 = va2000_cur_w[mindev];
            h2  = va2000_cur_h[mindev];
            if (pw2 == 0 || h2 == 0) break;
            src = 0;
            if (va2_blit_wait(base) != 0) break;
            VA2REG(base, VA2_BLIT_SRC_HI) = (src>>16)&0xffff;
            VA2REG(base, VA2_BLIT_SRC_LO) = src&0xffff;
            VA2REG(base, VA2_BLIT_PITCH)   = pw2;
            VA2REG(base, VA2_BLIT_RGB16)   = 0x0000;
            VA2REG(base, VA2_BLIT_CMODE)   = VA2CLR_16BIT;
            VA2REG(base, VA2_BLIT_X1)      = 0;
            VA2REG(base, VA2_BLIT_Y1)      = 0;
            VA2REG(base, VA2_BLIT_X2)      = pw2 - 1;
            VA2REG(base, VA2_BLIT_Y2)      = h2 - 1;
            VA2REG(base, VA2_BLIT_ENABLE)  = 1;
            va2_blit_wait(base);
        }
        break;

    case SVGAIOCBlankScreen:
        {
            unsigned short onoff;
            if (copyin((caddr_t)arg, (caddr_t)&onoff, sizeof(onoff)))
                return EFAULT;
            VA2REG(base, VA2_CAPTURE_MODE) = (onoff == 0) ? 1 : 0;
        }
        break;

    case SVGAIOCStartBlit:
    case SVGAIOCEndBlit:
        break;

    case SVGAIOCGetRGB:
        {
            unsigned long buf[4];
            if (copyin((caddr_t)arg, (caddr_t)buf, sizeof(buf)))
                return EFAULT;
            buf[1] = buf[0];
            buf[2] = buf[0];
            buf[3] = buf[0];
            if (copyout((caddr_t)buf, (caddr_t)arg, sizeof(buf)))
                return EFAULT;
        }
        break;

    case SVGAIOCSetRGB:
        {
            unsigned long buf[4];
            unsigned long idx;
            volatile unsigned short *preg;
            if (copyin((caddr_t)arg, (caddr_t)buf, sizeof(buf)))
                return EFAULT;
            idx = buf[0];
            if (idx < 256) {
                /* R→0x200, G→0x400, B→0x600 (P96 channel mapping) */
                preg  = (volatile unsigned short *)((char *)base + 0x200 + idx * 2);
                *preg = (unsigned short)(buf[1] & 0xff);
                preg  = (volatile unsigned short *)((char *)base + 0x400 + idx * 2);
                *preg = (unsigned short)(buf[2] & 0xff);
                preg  = (volatile unsigned short *)((char *)base + 0x600 + idx * 2);
                *preg = (unsigned short)(buf[3] & 0xff);
            }
        }
        break;

    case SVGAIOCGetBorderColor:
        {
            unsigned long col;
            col = 0;
            if (copyout((caddr_t)&col, (caddr_t)arg, sizeof(col)))
                return EFAULT;
        }
        break;

    case SVGAIOCSetBorderColor:
        break;

    case SVGAIOCDisableScrGrp:
    case SVGAIOCEnableScrGrp:
        break;

    default:
        return EINVAL;
    }

    return 0;
}
