/*
 * va2000_restest.c - Resolution and color depth test for MNT VA2000
 *
 * Tests display resolutions at 16bpp and 8bpp (palette/CLUT).
 * Each mode is shown with color bars for visual confirmation.
 *
 * Compile: cc va2000_restest.c -o va2000_restest
 * Run:     ./va2000_restest
 *
 * For each mode:
 *   Enter = OK (image visible and correct)
 *   f     = FAIL (no signal, garbage, or wrong colors)
 *   q     = quit
 *
 * 8bpp note: CPU fills are used (blitter is unreliable in 8bpp mode).
 *            Drawing is slower — the bars appear left to right.
 */

#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>

#define REG(b,o) (*((volatile unsigned short *)((char *)(b)+(o))))
#define FB_OFFSET 0x10000UL

/* Pixel clock register values */
#define CLK_75MHZ  0
#define CLK_40MHZ  1
#define CLK_100MHZ 3

/* Color mode register values */
#define CM_8BIT  0
#define CM_16BIT 1

/* RGB565 colors for 16bpp test bars */
#define C_WHITE   0xFFFF
#define C_YELLOW  0xFFE0
#define C_CYAN    0x07FF
#define C_GREEN   0x07E0
#define C_MAGENTA 0xF81F
#define C_RED     0xF800
#define C_BLUE    0x001F
#define C_BLACK   0x0000

struct resmode {
    char          *name;
    unsigned short w;       /* logical pixel width */
    unsigned short h;       /* logical pixel height */
    unsigned short hss;
    unsigned short hse;
    unsigned short hmax;
    unsigned short vss;
    unsigned short vse;
    unsigned short vmax;
    unsigned short clk;
    unsigned short bpp;     /* 8 or 16 */
    unsigned short scale;   /* scalemode register: 0=1:1, 5=2x both axes */
    int            result;  /* 0=untested 1=ok 2=fail */
};

/* Palette for 8bpp bars — 8 entries, indices 0-7.
 * Channel mapping: R data -> 0x200, G -> 0x400, B -> 0x600 (P96 convention). */
static unsigned char pal_r[8] = {255, 255,   0,   0, 255, 255,   0,   0};
static unsigned char pal_g[8] = {255, 255, 255, 255,   0,   0,   0,   0};
static unsigned char pal_b[8] = {255,   0, 255,   0, 255,   0, 255,   0};
/* indices:                        0     1     2     3     4     5     6     7  */
/* colors:                       whi   yel   cyn   grn   mag   red   blu   blk */

static struct resmode modes[] = {
    /* Standard resolutions, 16bpp */
    /* 640x480: VA2000 has no native 640x480 timing; uses 800x600 sync with smaller display window */
    /*  name                w     h    hss   hse   hmax  vss   vse  vmax  clk        bpp sc res */
    { "640x480  16bpp",   640,  480,  840,  968, 1056,  601,  605,  628, CLK_40MHZ,  16, 0, 0 },
    { "800x600  16bpp",   800,  600,  840,  968, 1056,  601,  605,  628, CLK_40MHZ,  16, 0, 0 },
    { "1024x768 16bpp",  1024,  768, 1048, 1184, 1328,  771,  777,  806, CLK_75MHZ,  16, 0, 0 },
    { "1280x720 16bpp",  1280,  720, 1390, 1430, 1650,  725,  730,  750, CLK_75MHZ,  16, 0, 0 },
    { "1280x1024 16bpp", 1280, 1024, 1328, 1440, 1600, 1025, 1028, 1066, CLK_100MHZ, 16, 0, 0 },
    { "1920x1080 16bpp", 1920, 1080, 1992, 2000, 2287, 1083, 1088, 1109, CLK_75MHZ,  16, 0, 0 },
    /* 8bpp palette modes */
    { "640x480  8bpp",    640,  480,  840,  968, 1056,  601,  605,  628, CLK_40MHZ,   8, 0, 0 },
    { "800x600  8bpp",    800,  600,  840,  968, 1056,  601,  605,  628, CLK_40MHZ,   8, 0, 0 },
    /* 320x200 8bpp: 2x h+v scaling, physical output 640x400 in 800x600 frame */
    { "320x200  8bpp 2x", 320,  200,  840,  968, 1056,  601,  605,  628, CLK_40MHZ,   8, 5, 0 },
    { (char *)0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

static void
blit_wait(base)
char *base;
{
    while (REG(base, 0x2a) != 0)
        ;
}

/* 16bpp hardware blitter fill */
static void
blit_fill(base, pitch_w, x, y, w, h, color)
char *base;
unsigned short pitch_w, x, y, w, h, color;
{
    unsigned long src;
    src = (unsigned long)y * pitch_w;
    blit_wait(base);
    REG(base, 0x40) = (unsigned short)((src >> 16) & 0xffff);
    REG(base, 0x42) = (unsigned short)(src & 0xffff);
    REG(base, 0x1c) = pitch_w;
    REG(base, 0x28) = color;
    REG(base, 0x1e) = CM_16BIT;
    REG(base, 0x20) = x;
    REG(base, 0x22) = y;
    REG(base, 0x24) = (unsigned short)(x + w - 1);
    REG(base, 0x26) = (unsigned short)(y + h - 1);
    REG(base, 0x2a) = 1;
    blit_wait(base);
}

/* Write first 8 palette entries for color bar test.
 * R->0x200, G->0x400, B->0x600 per P96 channel mapping. */
static void
set_palette(base)
char *base;
{
    volatile unsigned short *pr;
    volatile unsigned short *pg;
    volatile unsigned short *pb;
    int i;

    for (i = 0; i < 8; i++) {
        pr  = (volatile unsigned short *)(base + 0x200 + i * 2);
        pg  = (volatile unsigned short *)(base + 0x400 + i * 2);
        pb  = (volatile unsigned short *)(base + 0x600 + i * 2);
        *pr = (unsigned short)pal_r[i];
        *pg = (unsigned short)pal_g[i];
        *pb = (unsigned short)pal_b[i];
    }
}

/* CPU fill for 8bpp (1 byte per pixel) */
static void
cpu_fill_8(fb, pitch, x, y, w, h, color)
unsigned char *fb;
unsigned short pitch;  /* bytes per row */
unsigned short x, y, w, h;
unsigned char color;
{
    unsigned short row, col;
    unsigned char *p;

    for (row = 0; row < h; row++) {
        p = fb + (unsigned long)(y + row) * pitch + x;
        for (col = 0; col < w; col++) {
            *p++ = color;
        }
    }
}

static void
set_mode(base, m)
char *base;
struct resmode *m;
{
    unsigned short pitch_w;
    unsigned short screen_w;
    unsigned short screen_h;
    unsigned short colormode;

    if (m->bpp == 8) {
        colormode = CM_8BIT;
        pitch_w   = m->w / 2;  /* 2 pixels per 16-bit word */
        /* refresh_cool_max >= 0x0a required to avoid flicker in 8bpp */
        REG(base, 0x16) = 0x0a;
    } else {
        colormode = CM_16BIT;
        pitch_w   = m->w;
    }

    if (m->scale) {
        screen_w = (unsigned short)(m->w * 2);
        screen_h = (unsigned short)(m->h * 2);
        if (screen_h < 480) screen_h = 480;
    } else {
        screen_w = m->w;
        screen_h = m->h;
    }

    REG(base, 0x70) = m->hss;
    REG(base, 0x72) = m->hse;
    REG(base, 0x74) = m->hmax;
    REG(base, 0x76) = m->vss;
    REG(base, 0x78) = m->vse;
    REG(base, 0x7a) = m->vmax;
    REG(base, 0x7c) = m->clk;
    REG(base, 0x0e) = colormode;
    REG(base, 0x58) = pitch_w;
    REG(base, 0x5c) = 9;
    REG(base, 0x06) = screen_w;
    REG(base, 0x08) = screen_h;
    REG(base, 0x04) = m->scale;
    REG(base, 0x14) = 0x1e0;
    REG(base, 0x18) = 0x17;
    REG(base, 0x1a) = 0x1e0;
    REG(base, 0x38) = 0;
    REG(base, 0x3a) = 0;
    REG(base, 0x4e) = 0;        /* RTG mode on */
    /* Repeat modeline to clear glitches */
    REG(base, 0x70) = m->hss;
    REG(base, 0x72) = m->hse;
    REG(base, 0x74) = m->hmax;
    REG(base, 0x76) = m->vss;
    REG(base, 0x78) = m->vse;
    REG(base, 0x7a) = m->vmax;
}

static void
draw_test_pattern(base, m)
char *base;
struct resmode *m;
{
    unsigned short bar_w;
    unsigned short colors16[8];
    unsigned char  colors8[8];
    unsigned short pitch_w;
    unsigned short pitch_b;
    unsigned char *fb;
    int i;

    bar_w = m->w / 8;

    colors16[0] = C_WHITE;   colors16[1] = C_YELLOW;
    colors16[2] = C_CYAN;    colors16[3] = C_GREEN;
    colors16[4] = C_MAGENTA; colors16[5] = C_RED;
    colors16[6] = C_BLUE;    colors16[7] = C_BLACK;

    colors8[0] = 0; colors8[1] = 1;  /* white, yellow  */
    colors8[2] = 2; colors8[3] = 3;  /* cyan,  green   */
    colors8[4] = 4; colors8[5] = 5;  /* magenta, red   */
    colors8[6] = 6; colors8[7] = 7;  /* blue,  black   */

    if (m->bpp == 8) {
        pitch_w = m->w / 2;
        pitch_b = (unsigned short)(pitch_w * 2);  /* = m->w bytes per row */
        fb = (unsigned char *)(base + FB_OFFSET);
        set_palette(base);
        for (i = 0; i < 8; i++) {
            cpu_fill_8(fb, pitch_b,
                       (unsigned short)(i * bar_w), 0,
                       bar_w, m->h,
                       colors8[i]);
        }
    } else {
        pitch_w = m->w;
        for (i = 0; i < 8; i++) {
            blit_fill(base, pitch_w,
                      (unsigned short)(i * bar_w), 0,
                      bar_w, m->h,
                      colors16[i]);
        }
    }
}

static void
restore_passthrough(base)
char *base;
{
    REG(base, 0x38) = 0xf8;
    REG(base, 0x3a) = 0;
    REG(base, 0x70) = 840;
    REG(base, 0x72) = 968;
    REG(base, 0x74) = 1056;
    REG(base, 0x76) = 601;
    REG(base, 0x78) = 605;
    REG(base, 0x7a) = 628;
    REG(base, 0x7c) = 1;
    REG(base, 0x0e) = 1;
    REG(base, 0x58) = 320;
    REG(base, 0x5c) = 9;
    REG(base, 0x06) = 640;
    REG(base, 0x08) = 480;
    REG(base, 0x04) = 0;
    REG(base, 0x4e) = 1;
}

int main()
{
    int fd;
    char *board;
    int i;
    int c;
    int tested;
    int passed;

    printf("VA2000 Resolution + Color Depth Test\n");
    printf("=====================================\n\n");
    printf("For each mode:\n");
    printf("  Enter = OK (image visible, colors correct)\n");
    printf("  f     = FAIL (no signal, garbage, or wrong colors)\n");
    printf("  q     = quit\n\n");
    printf("8bpp modes: bars drawn by CPU, appear left to right.\n");
    printf("            White/Yellow/Cyan/Green/Magenta/Red/Blue/Black\n\n");
    printf("If monitor goes dark, wait a moment then press f+Enter.\n\n");

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

    for (i = 0; modes[i].name != (char *)0; i++) {
        struct resmode *m = &modes[i];

        printf("Testing: %s (%dx%d, %dbpp",
               m->name, m->w, m->h, m->bpp);
        if (m->scale)
            printf(", 2x scaled");
        printf(")...\n");

        set_mode(board, m);
        draw_test_pattern(board, m);

        printf("  Visible + correct? Enter=OK  f=FAIL  q=quit: ");
        fflush(stdout);

        c = getchar();
        while (c != '\n' && c != EOF) {
            int ch = c;
            c = getchar();
            c = ch;
            break;
        }
        while (getchar() != '\n')
            ;

        if (c == 'q' || c == EOF) {
            m->result = 2;
            printf("  Aborted.\n\n");
            break;
        } else if (c == 'f') {
            m->result = 2;
            printf("  FAILED\n\n");
        } else {
            m->result = 1;
            printf("  OK\n\n");
        }
    }

    restore_passthrough(board);
    printf("Passthrough restored.\n\n");

    printf("=== RESULTS ===\n");
    tested = 0;
    passed = 0;
    for (i = 0; modes[i].name != (char *)0; i++) {
        struct resmode *m = &modes[i];
        if (m->result == 0) continue;
        tested++;
        if (m->result == 1) {
            passed++;
            printf("  OK   : %s\n", m->name);
        } else {
            printf("  FAIL : %s\n", m->name);
        }
    }
    printf("\n%d/%d modes working.\n", passed, tested);

    munmap((caddr_t)board, 0x400000UL);
    close(fd);
    return 0;
}
