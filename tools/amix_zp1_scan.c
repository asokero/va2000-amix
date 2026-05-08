/*
 * amix_zp1_scan.c - Scan Zorro II memory area for known boards
 *
 * Phase 1 utility: probes known board addresses via /dev/mem.
 * Uses addresses from AmigaOS ShowConfig output.
 *
 * Compile: cc amix_zp1_scan.c -o amix_zp1_scan
 * Run as root: ./amix_zp1_scan
 */

#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>

struct board_info {
    unsigned long  base;
    unsigned long  size;
    unsigned short manuf;
    unsigned char  prod;
    char          *name;
    char          *type;
    int            has_regs;
};

static struct board_info boards[] = {
    { 0x600000UL, 0x10000UL, 0x6D6E, 0x01,
      "MNT VA2000",          "RTG Card",        1 },
    { 0x200000UL, 0x10000UL, 0x0893, 0x05,
      "IB Helfrich Piccolo", "GFX Memory",      0 },
    { 0xEB0000UL, 0x10000UL, 0x0893, 0x06,
      "IB Helfrich Piccolo", "GFX Registers",   1 },
    { 0xE90000UL, 0x10000UL, 0x0202, 0x70,
      "Commodore A2065",     "Ethernet",         1 },
    { 0UL, 0UL, 0, 0, (char *)0, (char *)0,     0 }
};

static int
all_same(mem, len, val)
unsigned char *mem;
int len;
unsigned char val;
{
    int i;
    for (i = 0; i < len; i++)
        if (mem[i] != val)
            return 0;
    return 1;
}

static unsigned short
read16(mem, offset)
unsigned char *mem;
int offset;
{
    return ((unsigned short)mem[offset] << 8) | mem[offset + 1];
}

static void
probe_board(fd, b)
int fd;
struct board_info *b;
{
    unsigned char *mem;
    unsigned short fw;
    int i;

    printf("Board: %s (%s)\n", b->name, b->type);
    printf("  Address: 0x%06lX  Manuf: 0x%04X  Prod: 0x%02X\n",
           b->base, b->manuf, b->prod);

    mem = (unsigned char *)mmap(
        (caddr_t)0, b->size, PROT_READ, MAP_SHARED,
        fd, (off_t)b->base);

    if (mem == (unsigned char *)-1) {
        perror("  mmap");
        printf("  Status: NOT ACCESSIBLE via /dev/mem\n\n");
        return;
    }

    printf("  Status: ACCESSIBLE\n");

    if (all_same(mem, 16, 0x00)) {
        printf("  Response: all zeros\n");
    } else if (all_same(mem, 16, 0xFF)) {
        printf("  Response: all 0xFF (no board?)\n");
    } else {
        printf("  Response: board responding\n");
        if (b->has_regs && b->manuf == 0x6D6E) {
            fw = read16(mem, 0x00);
            printf("  VA2000 fw_version: 0x%04X (%u)\n", fw, fw);
        }
    }

    printf("  Hex dump: ");
    for (i = 0; i < 16; i++)
        printf("%02X ", mem[i]);
    printf("\n\n");

    munmap((caddr_t)mem, b->size);
}

int main()
{
    int fd;
    int i;

    printf("AMIX Zorro Board Scanner - Phase 1\n");
    printf("===================================\n\n");

    fd = open("/dev/mem", O_RDONLY);
    if (fd < 0) {
        perror("open /dev/mem");
        return 1;
    }

    for (i = 0; boards[i].base != 0UL; i++)
        probe_board(fd, &boards[i]);

    close(fd);
    return 0;
}
