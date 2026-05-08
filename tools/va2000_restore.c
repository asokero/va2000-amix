/*
 * va2000_restore.c - Restore VA2000 passthrough mode on Amiga UNIX
 *
 * Use this if the display is stuck in RTG mode after a crash or
 * if va2000_test exits without restoring passthrough.
 *
 * Compile: cc va2000_restore.c -o va2000_restore
 * Run:     ./va2000_restore
 */

#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>

#define REG(base, off) (*((volatile unsigned short *)((char *)(base) + (off))))

int main()
{
    int fd;
    char *board;

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

    /* Pan pointer to capture area at end of video memory */
    REG(board, 0x38) = 0xf8;
    REG(board, 0x3a) = 0;

    /* 640x480 modeline, 40MHz */
    REG(board, 0x70) = 840;
    REG(board, 0x72) = 968;
    REG(board, 0x74) = 1056;
    REG(board, 0x76) = 601;
    REG(board, 0x78) = 605;
    REG(board, 0x7a) = 628;
    REG(board, 0x7c) = 1;

    /* 16-bit color, 640px pitch */
    REG(board, 0x0e) = 1;
    REG(board, 0x58) = 320;
    REG(board, 0x5c) = 9;
    REG(board, 0x06) = 640;
    REG(board, 0x08) = 480;
    REG(board, 0x04) = 0;

    /* Enable passthrough capture mode */
    REG(board, 0x4e) = 1;

    printf("VA2000 passthrough restored.\n");

    munmap((caddr_t)board, 0x400000UL);
    close(fd);
    return 0;
}
