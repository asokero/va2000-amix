/*
 * va2000_test.c - Interactive display test for MNT VA2000 on Amiga UNIX
 *
 * Sets 800x600 16-bit mode and provides interactive color tests.
 * Press q to quit and restore passthrough mode.
 *
 * Compile: cc va2000_test.c -o va2000_test
 * Run:     ./va2000_test
 *
 * Commands:
 *   r = red screen
 *   g = green screen
 *   b = blue screen
 *   w = white screen
 *   c = color bars (8 colors)
 *   q = quit and restore passthrough
 */

#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>

#define REG(base, off) (*((volatile unsigned short *)((char *)(base) + (off))))

/* Register offsets */
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

/* Color modes */
#define MNTVA_COLOR_16BIT565 1

/* Framebuffer offset within board */
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

static void
set_modeline(base, hss, hse, hmax, vss, vse, vmax, clk)
char *base;
unsigned short hss, hse, hmax, vss, vse, vmax, clk;
{
    REG(base, R_H_SYNC_START) = hss;
    REG(base, R_H_SYNC_END)   = hse;
    REG(base, R_H_MAX)        = hmax;
    REG(base, R_V_SYNC_START) = vss;
    REG(base, R_V_SYNC_END)   = vse;
    REG(base, R_V_MAX)        = vmax;
    REG(base, R_PIX_CLK)      = clk;
}

static void
restore_passthrough(base)
char *base;
{
    REG(base, R_PAN_PTR_HI)   = 0xf8;
    REG(base, R_PAN_PTR_LO)   = 0;
    set_modeline(base, 840, 968, 1056, 601, 605, 628, 1);
    REG(base, R_COLORMODE)    = MNTVA_COLOR_16BIT565;
    REG(base, R_ROW_PITCH)    = 320;
    REG(base, R_ROW_PITCH_SHF)= 9;
    REG(base, R_SCREEN_W)     = 640;
    REG(base, R_SCREEN_H)     = 480;
    REG(base, R_SCALEMODE)    = 0;
    REG(base, R_CAPTURE_MODE) = 1;
    printf("Passthrough restored.\n");
}

static void
fill_screen(fb, color, w, h)
unsigned short *fb;
unsigned short color;
unsigned short w;
unsigned short h;
{
    unsigned long i;
    unsigned long total;
    total = (unsigned long)w * h;
    for (i = 0; i < total; i++)
        fb[i] = color;
}

static void
draw_colorbars(fb, w, h)
unsigned short *fb;
unsigned short w;
unsigned short h;
{
    unsigned short colors[8];
    unsigned short bar_w;
    unsigned short x;
    unsigned short y;
    unsigned short bar;

    colors[0] = COLOR_WHITE;
    colors[1] = COLOR_YELLOW;
    colors[2] = COLOR_CYAN;
    colors[3] = COLOR_GREEN;
    colors[4] = COLOR_MAGENTA;
    colors[5] = COLOR_RED;
    colors[6] = COLOR_BLUE;
    colors[7] = COLOR_BLACK;

    bar_w = w / 8;

    for (y = 0; y < h; y++) {
        for (bar = 0; bar < 8; bar++) {
            for (x = 0; x < bar_w; x++) {
                fb[y * w + bar * bar_w + x] = colors[bar];
            }
        }
    }
}

int main()
{
    int fd;
    char *board;
    unsigned short *fb;
    int c;
    unsigned short W;
    unsigned short H;

    W = 800;
    H = 600;

    printf("VA2000 display test - 800x600 16bit\n");
    printf("Commands: r=red g=green b=blue w=white c=bars q=quit\n\n");

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

    fb = (unsigned short *)(board + FB_OFFSET);

    /* Set 800x600 mode */
    set_modeline(board, 840, 968, 1056, 601, 605, 628, 1);
    REG(board, R_COLORMODE)    = MNTVA_COLOR_16BIT565;
    REG(board, R_ROW_PITCH)    = W;
    REG(board, R_ROW_PITCH_SHF)= 9;
    REG(board, R_SCREEN_W)     = W;
    REG(board, R_SCREEN_H)     = H;
    REG(board, R_SCALEMODE)    = 0;
    REG(board, R_SAFE_X2)      = 0x1e0;
    REG(board, R_FETCH_PREROLL)= 0x1e0;
    REG(board, R_RAM_FETCH)    = 0x17;
    REG(board, R_PAN_PTR_HI)   = 0;
    REG(board, R_PAN_PTR_LO)   = 0;

    /* Disable passthrough */
    REG(board, R_CAPTURE_MODE) = 0;
    set_modeline(board, 840, 968, 1056, 601, 605, 628, 1);

    /* Draw color bars as initial display */
    draw_colorbars(fb, W, H);
    printf("Color bars displayed.\n");

    /* Flush any pending input */
    while ((c = getchar()) == '\n')
        ;

    do {
        switch (c) {
        case 'r':
            fill_screen(fb, COLOR_RED, W, H);
            printf("RED\n");
            break;
        case 'g':
            fill_screen(fb, COLOR_GREEN, W, H);
            printf("GREEN\n");
            break;
        case 'b':
            fill_screen(fb, COLOR_BLUE, W, H);
            printf("BLUE\n");
            break;
        case 'w':
            fill_screen(fb, COLOR_WHITE, W, H);
            printf("WHITE\n");
            break;
        case 'c':
            draw_colorbars(fb, W, H);
            printf("COLOR BARS\n");
            break;
        case 'q':
            break;
        default:
            break;
        }
    } while ((c = getchar()) != 'q' && c != EOF);

    restore_passthrough(board);
    munmap((caddr_t)board, 0x400000UL);
    close(fd);
    return 0;
}
