/*
 * va2000.c - MNT VA2000 device driver for Amiga UNIX (AMIX SVR4)
 *
 * Copyright (c) 2025-2026 Antti Sokero
 * SPDX-License-Identifier: MIT
 * See LICENSE file in the repository root for details.
 *
 * Kernel character device: /dev/va2000 (major 68, minor 0)
 * Card: MNT VA2000, Zorro II, manufacturer 0x6D6E product 0x01
 *
 * VA2000 memory map:
 *   0x000000 - 0x00FFFF : registers (write-only from kernel context)
 *   0x010000 - 0x3FFFFF : framebuffer
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
#define VA2000_HWLEN     0x00400000UL
#define VA2000_FB_OFFSET 0x00010000UL
#define VA2000_FB_SIZE   (VA2000_HWLEN - VA2000_FB_OFFSET)
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

/* extern so scrdev.c can access va2000_boards[0] */
long va2000_boards[VA2000_MAXBOARDS];
static int va2000_open_count[VA2000_MAXBOARDS];
static unsigned short va2000_cur_w[VA2000_MAXBOARDS];
static unsigned short va2000_cur_h[VA2000_MAXBOARDS];
static unsigned short va2000_cur_bpp[VA2000_MAXBOARDS];
static unsigned short va2000_cur_pitch[VA2000_MAXBOARDS];
static unsigned short va2000_monitor_switch[VA2000_MAXBOARDS];


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
va2_set_mode(base, w, h, hss, hse, hmax, vss, vse, vmax, clk, colormode, pitch_w)
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
    VA2REG(base, VA2_SCALEMODE)    = 0;
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
    long dummy;
    unsigned short fw;

    for (i = 0; i < VA2000_MAXBOARDS; i++) {
        va2000_boards[i]         = 0;
        va2000_open_count[i]     = 0;
        va2000_cur_w[i]          = 0;
        va2000_cur_h[i]          = 0;
        va2000_cur_bpp[i]        = 16;
        va2000_cur_pitch[i]      = 0;
        va2000_monitor_switch[i] = SVGAMONITORSWITCH_Amiga;
    }

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

    fw = VA2REG(va2000_boards[0], VA2_FW_VERSION);
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
    long dummy;

    dev = getminor(*devp);
    if (dev >= VA2000_MAXBOARDS) return ENXIO;
    if (!va2000_boards[dev] &&
        !autocon(VA2000_PRODUCT, dev, &va2000_boards[dev], &dummy))
        return ENXIO;
    if (!va2000_present(va2000_boards[dev])) return ENXIO;
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
        if (va2000_open_count[mindev] == 0 && va2000_boards[mindev]) {
            va2_restore_passthrough(va2000_boards[mindev]);
            va2000_monitor_switch[mindev] = SVGAMONITORSWITCH_Amiga;
        }
    }
    return 0;
}

static int
va2000rdwr(dev, direction, uiop)
int dev;
uio_rw_t direction;
struct uio *uiop;
{
    unsigned long n;
    unsigned long count;
    int error;

    if (!va2000_boards[dev]) return ENXIO;
    if ((count = uiop->uio_resid) == 0) return 0;
    if (uiop->uio_offset < 0 ||
        uiop->uio_offset >= (long)VA2000_HWLEN) return ENXIO;
    n = uiop->uio_offset + count;
    if (n > VA2000_HWLEN) count = VA2000_HWLEN - uiop->uio_offset;
    if (direction == UIO_WRITE && count == 0) return ENXIO;
    if (error = uiomove(
            (char *)(va2000_boards[dev]) + uiop->uio_offset,
            count, direction, uiop))
        return error;
    return 0;
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
    if (offset >= 0 && offset < (off_t)VA2000_HWLEN)
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
    unsigned short pitch_w;
    unsigned short w;
    unsigned short h;

    mindev = getminor(dev);
    if (mindev >= VA2000_MAXBOARDS || !va2000_boards[mindev])
        return ENXIO;
    base = va2000_boards[mindev];

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
        bd.SVGABD_FrameBufSize = (long)VA2000_FB_SIZE;
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
        *rvalp = (int)VA2000_FB_SIZE;
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

        /* This driver implements 16-bit only; hardware supports 8/16/32-bit */
        va2clr  = VA2CLR_16BIT;
        va2clk  = svga_pixclock_to_va2(sm.SVGASM_PixelClock);
        pitch_w = sm.SVGASM_HDisplayEnd;

        va2_set_mode(base,
                     (unsigned short)sm.SVGASM_HDisplayEnd,
                     (unsigned short)sm.SVGASM_VDisplayEnd,
                     (unsigned short)sm.SVGASM_HSyncStart,
                     (unsigned short)sm.SVGASM_HSyncEnd,
                     (unsigned short)sm.SVGASM_HTotal,
                     (unsigned short)sm.SVGASM_VSyncStart,
                     (unsigned short)sm.SVGASM_VSyncEnd,
                     (unsigned short)sm.SVGASM_VTotal,
                     (unsigned short)va2clk,
                     (unsigned short)va2clr,
                     (unsigned short)pitch_w);

        va2000_cur_w[mindev]     = sm.SVGASM_HDisplayEnd;
        va2000_cur_h[mindev]     = sm.SVGASM_VDisplayEnd;
        va2000_cur_pitch[mindev] = pitch_w * 2;
        va2000_cur_bpp[mindev]   = 16;
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
        sd.SVGASD_Color         = (unsigned long)SVGACM_16BIT;
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
