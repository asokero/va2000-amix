/*
 * va2000.h - MNT VA2000 device driver definitions for Amiga UNIX (AMIX)
 *
 * Copyright (c) 2025-2026 Antti Sokero
 * SPDX-License-Identifier: MIT
 * See LICENSE file in the repository root for details.
 *
 * ioctl definitions for /dev/va2000 (major 68, Zorro II/III).
 * Native VA2000 ioctls and svgaioctl.h-compatible interface
 * for use with Xsvga and Xrtg.
 */

#ifndef VA2000_H
#define VA2000_H

/* ------------------------------------------------------------------ */
/* Native VA2000 ioctls                                                */
/* ------------------------------------------------------------------ */

#define VA2IOC          ('V' << 8)

#define VA2IOC_GETFW        (VA2IOC | 1)
#define VA2IOC_GETSCREENW   (VA2IOC | 2)
#define VA2IOC_GETSCREENH   (VA2IOC | 3)
#define VA2IOC_GETCOLORMODE (VA2IOC | 4)

/* ------------------------------------------------------------------ */
/* svgaioctl.h-compatible interface (Xsvga / Xrtg)                    */
/* Interface definitions based on svgaioctl.h by Georg Hessmann        */
/* and Klaus Burckert                                                   */
/* ------------------------------------------------------------------ */

#define SVGAIOC                 (0xe300)

#define SVGAIOCClearBoardMem    (SVGAIOC|0x01)
#define SVGAIOCGetBoardData     (SVGAIOC|0x02)
#define SVGAIOCGetFBufSize      (SVGAIOC|0x04)
#define SVGAIOCGetMonitorSwitch (SVGAIOC|0x06)
#define SVGAIOCSetMonitorSwitch (SVGAIOC|0x08)  /* note: 0x08 not 0x07 */
#define SVGAIOCSetScreenMode    (SVGAIOC|0x0B)
#define SVGAIOCGetScreenData    (SVGAIOC|0x0C)
#define SVGAIOCGetPanPos        (SVGAIOC|0x0E)
#define SVGAIOCSetPanPos        (SVGAIOC|0x0F)
#define SVGAIOCSetPanXPos       (SVGAIOC|0x11)
#define SVGAIOCSetPanYPos       (SVGAIOC|0x13)
#define SVGAIOCGetRGB           (SVGAIOC|0x14)
#define SVGAIOCSetRGB           (SVGAIOC|0x15)
#define SVGAIOCGetBorderColor   (SVGAIOC|0x16)
#define SVGAIOCSetBorderColor   (SVGAIOC|0x17)
#define SVGAIOCDisableScrGrp    (SVGAIOC|0x19)
#define SVGAIOCEnableScrGrp     (SVGAIOC|0x21)
#define SVGAIOCBlankScreen      (SVGAIOC|0x23)
#define SVGAIOCStartBlit        (SVGAIOC|0x25)
#define SVGAIOCEndBlit          (SVGAIOC|0x27)

/* Card ID for VA2000 */
#define SVGACARDID_VA2000       16

/* Monitor switch values */
#define SVGAMONITORSWITCH_Amiga 1
#define SVGAMONITORSWITCH_SVGA  2

/* Color modes */
#define SVGACM_8BIT     2
#define SVGACM_16BIT    4
#define SVGACM_24BIT    5

/* SVGABoardData - returned by SVGAIOCGetBoardData */
struct SVGABoardData {
    long  SVGABD_CardID;
    long  SVGABD_FrameBufSize;
    short SVGABD_MaxPixClk;
    short SVGABD_HasBlitter;
    short SVGABD_HasPanning;
    short SVGABD_Colors;
    short SVGABD_HasHWCursor;
    short SVGABD_reserved1;
    long  SVGABD_reserved2[4];
};

/* SVGAScreenMode - used by SVGAIOCSetScreenMode */
struct SVGAScreenMode {
    unsigned long  SVGASM_PixelClock;
    unsigned short SVGASM_HDisplayEnd;
    unsigned short SVGASM_HSyncStart;
    unsigned short SVGASM_HSyncEnd;
    unsigned short SVGASM_HTotal;
    unsigned short SVGASM_VDisplayEnd;
    unsigned short SVGASM_VSyncStart;
    unsigned short SVGASM_VSyncEnd;
    unsigned short SVGASM_VTotal;
    unsigned short SVGASM_ColorMode;
    unsigned short SVGASM_Flags;
    unsigned short SVGASM_Width;
    unsigned short SVGASM_Height;
    unsigned short SVGASM_HPosition;
    unsigned short SVGASM_VPosition;
};

/* SVGAScreenData - returned by SVGAIOCGetScreenData */
struct SVGAScreenData {
    unsigned short SVGASD_VisibleWidth;
    unsigned short SVGASD_VisibleHeight;
    unsigned short SVGASD_VirtualWidth;
    unsigned short SVGASD_VirtualHeight;
    unsigned short SVGASD_HFreq;
    unsigned short SVGASD_VFreq;
    unsigned short SVGASD_BytesPerRow;
    unsigned short SVGASD_BitsPerPixel;
    unsigned short SVGASD_padding1;
    unsigned short SVGASD_padding2;
    unsigned long  SVGASD_Color;
    long           SVGASD_reserved2[10];
};

/* SVGAPanPos - used by SVGAIOCGetPanPos / SVGAIOCSetPanPos */
struct SVGAPanPos {
    unsigned short SVGAPP_XPos;
    unsigned short SVGAPP_YPos;
};

#endif /* VA2000_H */
