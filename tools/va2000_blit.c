/*
 * va2000_blit.c - Hardware blitter functions for MNT VA2000 on Amiga UNIX
 *
 * Provides fast hardware-accelerated rectangle fill using the VA2000
 * built-in blitter. Much faster than CPU pixel-by-pixel writes for
 * large areas.
 *
 * Key finding: blitter src_offset is RELATIVE to framebuffer start,
 * not an absolute address. For row y: src_offset = y * pitch_words.
 * src=0 means start of framebuffer (board offset 0x10000).
 *
 * Compile test: cc va2000_blit.c -o va2000_blit
 * Run:          ./va2000_blit
 */

#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>

#define REG(b,o) (*((volatile unsigned short *)((char *)(b)+(o))))

/* Blitter registers */
#define R_BLITTER_ROW_PITCH  0x1c
#define R_BLITTER_COLORMODE  0x1e
#define R_BLITTER_X1         0x20
#define R_BLITTER_Y1         0x22
#define R_BLITTER_X2         0x24
#define R_BLITTER_Y2         0x26
#define R_BLITTER_RGB16      0x28
#define R_BLITTER_ENABLE     0x2a
#define R_BLITTER_SRC_HI     0x40
#define R_BLITTER_SRC_LO     0x42

/* Display registers */
#define R_SCALEMODE     0x04
#define R_SCREEN_W      0x06
#define R_SCREEN_H      0x08
#define R_COLORMODE     0x0e
#define R_SAFE_X2       0x14
#define R_RAM_FETCH     0x18
#define R_FETCH_PREROLL 0x1a
#define R_PAN_PTR_HI    0x38
#define R_PAN_PTR_LO    0x3a
#define R_CAPTURE_MODE  0x4e
#define R_ROW_PITCH     0x58
#define R_ROW_PITCH_SHF 0x5c
#define R_H_SYNC_START  0x70
#define R_H_SYNC_END    0x72
#define R_H_MAX         0x74
#define R_V_SYNC_START  0x76
#define R_V_SYNC_END    0x78
#define R_V_MAX         0x7a
#define R_PIX_CLK       0x7c

#define MNTVA_COLOR_16BIT565 1
#define FB_OFFSET 0x10000UL

/* RGB565 color values */
#define COLOR_WHITE   0xFFFF
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_GREEN   0x07E0
#define COLOR_MAGENTA 0xF81F
#define COLOR_RED     0xF800
#define COLOR_BLUE    0x001F
#define COLOR_BLACK   0x0000

/*
 * blitter_wait() - Wait for blitter to finish current operation.
 */
static void
blitter_wait(base)
char *base;
{
    while (REG(base, R_BLITTER_ENABLE) != 0)
        ;
}

/*
 * blitter_fill_rect() - Fill rectangle using VA2000 hardware blitter.
 *
 * Parameters:
 *   base    = board base from mmap(/dev/va2000)
 *   pitch_w = row pitch in words (= screen_width for 16-bit)
 *   x, y    = top-left corner of rectangle
 *   w, h    = width and height in pixels
 *   color   = RGB565 fill color
 *
 * Note on src_offset: blitter uses offset RELATIVE to framebuffer,
 * measured in 16-bit words. For row y: src = y * pitch_w.
 * This was determined experimentally - src=0 fills from fb start.
 */
static void
blitter_fill_rect(base, pitch_w, x, y, w, h, color)
char *base;
unsigned short pitch_w;
unsigned short x;
unsigned short y;
unsigned short w;
unsigned short h;
unsigned short color;
{
    unsigned long src;

    blitter_wait(base);

    /* Source = start of row y, relative to framebuffer, in words */
    src = (unsigned long)y * pitch_w;
    REG(base, R_BLITTER_SRC_HI) = (unsigned short)((src >> 16) & 0xffff);
    REG(base, R_BLITTER_SRC_LO) = (unsigned short)(src & 0xffff);

    REG(base, R_BLITTER_ROW_PITCH) = pitch_w;
    REG(base, R_BLITTER_RGB16)     = color;
    REG(base, R_BLITTER_COLORMODE) = MNTVA_COLOR_16BIT565;

    /* x2/y2 are inclusive end coordinates */
    REG(base, R_BLITTER_X1) = x;
    REG(base, R_BLITTER_Y1) = y;
    REG(base, R_BLITTER_X2) = x + w - 1;
    REG(base, R_BLITTER_Y2) = y + h - 1;

    REG(base, R_BLITTER_ENABLE) = 1;
    blitter_wait(base);
}

/*
 * set_rtg_mode() - Switch VA2000 to RTG (own framebuffer) mode.
 * Modeline: 800x600, 40MHz clock.
 */
static void
set_rtg_mode(base)
char *base;
{
    REG(base, R_H_SYNC_START) = 840;
    REG(base, R_H_SYNC_END)   = 968;
    REG(base, R_H_MAX)        = 1056;
    REG(base, R_V_SYNC_START) = 601;
    REG(base, R_V_SYNC_END)   = 605;
    REG(base, R_V_MAX)        = 628;
    REG(base, R_PIX_CLK)      = 1;       /* 40MHz */
    REG(base, R_COLORMODE)    = MNTVA_COLOR_16BIT565;
    REG(base, R_ROW_PITCH)    = 800;
    REG(base, R_ROW_PITCH_SHF)= 9;
    REG(base, R_SCREEN_W)     = 800;
    REG(base, R_SCREEN_H)     = 600;
    REG(base, R_SCALEMODE)    = 0;
    REG(base, R_SAFE_X2)      = 0x1e0;
    REG(base, R_FETCH_PREROLL)= 0x1e0;
    REG(base, R_RAM_FETCH)    = 0x17;
    REG(base, R_PAN_PTR_HI)   = 0;
    REG(base, R_PAN_PTR_LO)   = 0;
    REG(base, R_CAPTURE_MODE) = 0;       /* disable passthrough */
    /* Reset modeline to clean up glitches */
    REG(base, R_H_SYNC_START) = 840;
    REG(base, R_H_SYNC_END)   = 968;
    REG(base, R_H_MAX)        = 1056;
    REG(base, R_V_SYNC_START) = 601;
    REG(base, R_V_SYNC_END)   = 605;
    REG(base, R_V_MAX)        = 628;
}

/*
 * restore_passthrough() - Return VA2000 to Amiga native video passthrough.
 * Required sequence: pan_ptr + modeline + colormode + capture_mode=1.
 */
static void
restore_passthrough(base)
char *base;
{
    REG(base, R_PAN_PTR_HI)   = 0xf8;
    REG(base, R_PAN_PTR_LO)   = 0;
    REG(base, R_H_SYNC_START) = 840;
    REG(base, R_H_SYNC_END)   = 968;
    REG(base, R_H_MAX)        = 1056;
    REG(base, R_V_SYNC_START) = 601;
    REG(base, R_V_SYNC_END)   = 605;
    REG(base, R_V_MAX)        = 628;
    REG(base, R_PIX_CLK)      = 1;
    REG(base, R_COLORMODE)    = MNTVA_COLOR_16BIT565;
    REG(base, R_ROW_PITCH)    = 320;
    REG(base, R_ROW_PITCH_SHF)= 9;
    REG(base, R_SCREEN_W)     = 640;
    REG(base, R_SCREEN_H)     = 480;
    REG(base, R_SCALEMODE)    = 0;
    REG(base, R_CAPTURE_MODE) = 1;
    printf("Passthrough restored.\n");
}

int main()
{
    int fd;
    char *board;
    int c;
    int i;

    printf("VA2000 blitter test\n");
    printf("===================\n");
    printf("Commands:\n");
    printf("  1 = full screen RED (blitter)\n");
    printf("  2 = four colored quadrants (blitter)\n");
    printf("  3 = eight color bars (blitter)\n");
    printf("  4 = checkerboard pattern (blitter)\n");
    printf("  q = quit and restore passthrough\n\n");

    fd = open("/dev/va2000", O_RDWR);
    if (fd < 0) {
        perror("open /dev/va2000");
        return 1;
    }

    board = (char *)mmap(
        (caddr_t)0, 0x400000UL,
        PROT_READ | PROT_WRITE, MAP_SHARED,
        fd, (off_t)0);

    if (board == (char *)-1) {
        perror("mmap");
        close(fd);
        return 1;
    }

    set_rtg_mode(board);
    printf("RTG mode active. 800x600 16bit.\n\n");

    /* Initial display: color bars */
    blitter_fill_rect(board, 800,   0, 0, 100, 600, COLOR_WHITE);
    blitter_fill_rect(board, 800, 100, 0, 100, 600, COLOR_YELLOW);
    blitter_fill_rect(board, 800, 200, 0, 100, 600, COLOR_CYAN);
    blitter_fill_rect(board, 800, 300, 0, 100, 600, COLOR_GREEN);
    blitter_fill_rect(board, 800, 400, 0, 100, 600, COLOR_MAGENTA);
    blitter_fill_rect(board, 800, 500, 0, 100, 600, COLOR_RED);
    blitter_fill_rect(board, 800, 600, 0, 100, 600, COLOR_BLUE);
    blitter_fill_rect(board, 800, 700, 0, 100, 600, COLOR_BLACK);
    printf("Color bars drawn.\n");

    /* Flush stdin */
    while ((c = getchar()) == '\n')
        ;

    do {
        if (c == '1') {
            blitter_fill_rect(board, 800, 0, 0, 800, 600, COLOR_RED);
            printf("RED\n");

        } else if (c == '2') {
            blitter_fill_rect(board, 800,   0,   0, 400, 300, COLOR_RED);
            blitter_fill_rect(board, 800, 400,   0, 400, 300, COLOR_GREEN);
            blitter_fill_rect(board, 800,   0, 300, 400, 300, COLOR_BLUE);
            blitter_fill_rect(board, 800, 400, 300, 400, 300, COLOR_WHITE);
            printf("QUADRANTS\n");

        } else if (c == '3') {
            blitter_fill_rect(board, 800,   0, 0, 100, 600, COLOR_WHITE);
            blitter_fill_rect(board, 800, 100, 0, 100, 600, COLOR_YELLOW);
            blitter_fill_rect(board, 800, 200, 0, 100, 600, COLOR_CYAN);
            blitter_fill_rect(board, 800, 300, 0, 100, 600, COLOR_GREEN);
            blitter_fill_rect(board, 800, 400, 0, 100, 600, COLOR_MAGENTA);
            blitter_fill_rect(board, 800, 500, 0, 100, 600, COLOR_RED);
            blitter_fill_rect(board, 800, 600, 0, 100, 600, COLOR_BLUE);
            blitter_fill_rect(board, 800, 700, 0, 100, 600, COLOR_BLACK);
            printf("COLOR BARS\n");

        } else if (c == '4') {
            /* Checkerboard - 40x30 cells of 20x20 pixels */
            unsigned short col;
            unsigned short row;
            unsigned short colors[2];
            colors[0] = COLOR_WHITE;
            colors[1] = COLOR_BLACK;
            for (row = 0; row < 30; row++) {
                for (col = 0; col < 40; col++) {
                    blitter_fill_rect(board, 800,
                        col * 20, row * 20,
                        20, 20,
                        colors[(col + row) & 1]);
                }
            }
            printf("CHECKERBOARD\n");
        }

    } while ((c = getchar()) != 'q' && c != EOF);

    restore_passthrough(board);
    munmap((caddr_t)board, 0x400000UL);
    close(fd);
    return 0;
}
