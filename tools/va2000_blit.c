#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>

#define REG(b,o) (*((volatile unsigned short *)((char *)(b)+(o))))
#define FB_OFFSET 0x10000UL

static void blit_wait(b) char *b; { while(REG(b,0x2a)!=0); }

static void blit_fill(base, pitch_w, x, y, w, h, color)
char *base; unsigned short pitch_w,x,y,w,h,color;
{
    unsigned long src;
    src = (unsigned long)y * pitch_w;
    blit_wait(base);
    REG(base,0x40) = (src>>16)&0xffff;
    REG(base,0x42) = src&0xffff;
    REG(base,0x1c) = pitch_w;
    REG(base,0x28) = color;
    REG(base,0x1e) = 1;
    REG(base,0x20) = x;
    REG(base,0x22) = y;
    REG(base,0x24) = x+w-1;
    REG(base,0x26) = y+h-1;
    REG(base,0x2a) = 1;
    blit_wait(base);
}

static void set_rtg_mode(base)
char *base;
{
    /* 800x600 40MHz */
    REG(base,0x70)=840; REG(base,0x72)=968; REG(base,0x74)=1056;
    REG(base,0x76)=601; REG(base,0x78)=605; REG(base,0x7a)=628;
    REG(base,0x7c)=1;
    /* 16bit 800px pitch */
    REG(base,0x0e)=1;
    REG(base,0x58)=800; REG(base,0x5c)=9;
    REG(base,0x06)=800; REG(base,0x08)=600;
    REG(base,0x04)=0;
    REG(base,0x14)=0x1e0;
    REG(base,0x18)=0x17;
    REG(base,0x1a)=0x1e0;
    /* Pan to framebuffer start */
    REG(base,0x38)=0; REG(base,0x3a)=0;
    /* Disable passthrough = RTG on */
    REG(base,0x4e)=0;
    /* Reset modeline to clean glitches */
    REG(base,0x70)=840; REG(base,0x72)=968; REG(base,0x74)=1056;
    REG(base,0x76)=601; REG(base,0x78)=605; REG(base,0x7a)=628;
}

static void restore_passthrough(base)
char *base;
{
    REG(base,0x38)=0xf8; REG(base,0x3a)=0;
    REG(base,0x70)=840; REG(base,0x72)=968; REG(base,0x74)=1056;
    REG(base,0x76)=601; REG(base,0x78)=605; REG(base,0x7a)=628;
    REG(base,0x7c)=1;
    REG(base,0x0e)=1; REG(base,0x58)=320; REG(base,0x5c)=9;
    REG(base,0x06)=640; REG(base,0x08)=480; REG(base,0x04)=0;
    REG(base,0x4e)=1;
    printf("Passthrough restored.\n");
}

int main()
{
    int fd;
    char *board;

    fd = open("/dev/va2000", O_RDWR);
    board = (char *)mmap((caddr_t)0, 0x400000UL,
        PROT_READ|PROT_WRITE, MAP_SHARED, fd, (off_t)0);

    /* Activate RTG mode */
    set_rtg_mode(board);
    printf("RTG mode activated.\n");

    /* Full screen RED via blitter */
    blit_fill(board, 800, 0, 0, 800, 600, 0xF800);
    printf("Red screen - press enter\n");
    getchar();

    /* Four quadrants */
    blit_fill(board, 800,   0,   0, 400, 300, 0xF800); /* red   */
    blit_fill(board, 800, 400,   0, 400, 300, 0x07E0); /* green */
    blit_fill(board, 800,   0, 300, 400, 300, 0x001F); /* blue  */
    blit_fill(board, 800, 400, 300, 400, 300, 0xFFFF); /* white */
    printf("Quadrants - press enter\n");
    getchar();

    restore_passthrough(board);
    munmap((caddr_t)board, 0x400000UL);
    close(fd);
    return 0;
}
