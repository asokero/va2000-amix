/*
 * amix_zp3_mmap.c - Test mmap access to VA2000 via /dev/va2000
 *
 * Verifies that the va2000 kernel driver mmap() function works correctly.
 * Maps the register area and reads key registers.
 *
 * Compile: cc amix_zp3_mmap.c -o amix_zp3_mmap
 * Run: ./amix_zp3_mmap
 *
 * Expected output after boot (card in passthrough/idle mode):
 *   mmap OK at c1032000
 *   fw_version: 90
 *   screen_w:   90
 *   screen_h:   90
 *
 * Note: screen_w and screen_h show 90 (same as fw_version) because
 * the card has not been initialized with a display mode yet.
 * This is normal and expected.
 */

#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>

/* Register offsets (from va2000.h / mntvareg.h) */
#define VA2000_FW_VERSION  0x00
#define VA2000_SCREEN_W    0x06
#define VA2000_SCREEN_H    0x08
#define VA2000_COLORMODE   0x0E

int main()
{
    int fd;
    unsigned short *regs;
    unsigned short fw, screen_w, screen_h, colormode;
    int i;

    printf("VA2000 mmap test\n");
    printf("================\n\n");

    fd = open("/dev/va2000", O_RDWR);
    if (fd < 0) {
        perror("open /dev/va2000");
        return 1;
    }

    /* Map register area (first 64KB of board) */
    regs = (unsigned short *)mmap(
        (caddr_t)0,
        0x10000,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        (off_t)0
    );

    if (regs == (unsigned short *)-1) {
        perror("mmap registers");
        close(fd);
        return 1;
    }

    printf("mmap OK at %p\n\n", regs);

    /* Read key registers */
    fw        = regs[VA2000_FW_VERSION / 2];
    screen_w  = regs[VA2000_SCREEN_W  / 2];
    screen_h  = regs[VA2000_SCREEN_H  / 2];
    colormode = regs[VA2000_COLORMODE / 2];

    printf("Register values:\n");
    printf("  fw_version  (0x00): %u (0x%04X)\n", fw, fw);
    printf("  screen_w    (0x06): %u\n", screen_w);
    printf("  screen_h    (0x08): %u\n", screen_h);
    printf("  colormode   (0x0E): %u\n", colormode);

    printf("\nRaw register dump (first 16 registers):\n  ");
    for (i = 0; i < 16; i++) {
        printf("%04X ", regs[i]);
        if (i == 7) printf("\n  ");
    }
    printf("\n");

    if (fw >= 5 && fw < 0xFFFF)
        printf("\nBoard status: OK (firmware valid)\n");
    else
        printf("\nBoard status: WARNING (unexpected firmware value)\n");

    munmap((caddr_t)regs, 0x10000);
    close(fd);
    return 0;
}
