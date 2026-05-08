/*
 * va2000.h - MNT VA2000 device driver definitions for Amiga UNIX (AMIX)
 *
 * Copyright (c) 2026, see README.md for credits.
 *
 * Driver for MNT VA2000 RTG graphics card under AMIX SVR4.
 * First known working VA2000 driver for Amiga UNIX.
 *
 * Hardware: MNT Media and Technology VA2000
 *   Zorro II, 4MB, Manufacturer 0x6D6E, Product 0x01
 */

#ifndef VA2000_H
#define VA2000_H

/* ioctl command base - 'V' << 8 */
#define VA2IOC          ('V' << 8)

/* ioctl commands */
#define VA2IOC_GETFW        (VA2IOC | 1)  /* get firmware version number  */
#define VA2IOC_GETSCREENW   (VA2IOC | 2)  /* get current screen width     */
#define VA2IOC_GETSCREENH   (VA2IOC | 3)  /* get current screen height    */
#define VA2IOC_GETCOLORMODE (VA2IOC | 4)  /* get color mode               */

/* Color mode values (from mntvareg.h) */
#define VA2000_COLORMODE_8BIT   0
#define VA2000_COLORMODE_16BIT  1
#define VA2000_COLORMODE_32BIT  2

#endif /* VA2000_H */
