#!/bin/sh
# install-va2000-driver.sh — Install MNT VA2000 kernel driver into AMIX source tree
#
# Usage: run from /usr/sys
#   sh /path/to/install-va2000-driver.sh /path/to/va2000-amix
#
# $1: repository root (where src/ lives). Defaults to the script's own directory.
#
# What this script does:
#   1. Copies va2000.c and va2000.h to /usr/sys/amiga/driver/
#   2. Patches /usr/sys/amiga/console/scrdev.c  (5 changes)
#   3. Patches /usr/sys/amiga/console/c0.c      (1 change)
#   4. Patches /usr/sys/amiga/console/screen.c  (1 change - SVGAChkGroup stub)
#   5. Prints kernel rebuild commands
#
# Creates .orig backups before modifying any file.
# See KERNEL_CHANGES.md for the full description of each patch.
#
# awk patterns verified against vanilla AMIX 2.1 kernel sources.

set -e

# --------------------------------------------------------------------------
# Sanity checks
# --------------------------------------------------------------------------

if [ ! -d amiga/console ] || [ ! -d amiga/driver ]; then
    echo "ERROR: Run this script from /usr/sys." >&2
    echo "       Current directory: `pwd`" >&2
    exit 1
fi

SCRIPT_DIR=`dirname "$0"`
SRCDIR="${1:-$SCRIPT_DIR}"

if [ ! -f "$SRCDIR/src/va2000.c" ]; then
    echo "ERROR: Source files not found in: $SRCDIR/src/" >&2
    echo "       Pass the repository root as argument: sh install-va2000-driver.sh /path" >&2
    exit 1
fi

SCRDEV=amiga/console/scrdev.c
C0=amiga/console/c0.c
SCREEN=amiga/console/screen.c
KERNELC=master.d/kernel.c
DRVMAKE=amiga/driver/Makefile

for F in "$SCRDEV" "$C0" "$SCREEN" "$KERNELC" "$DRVMAKE"; do
    if [ ! -f "$F" ]; then
        echo "ERROR: Required kernel source file not found: $F" >&2
        exit 1
    fi
done

echo "Source directory: $SRCDIR"
echo "Kernel tree:      `pwd`"
echo ""

TMPFILE=/tmp/va2000_patch_$$
AWKFILE=/tmp/va2000_awk_$$

# --------------------------------------------------------------------------
# Helpers
# --------------------------------------------------------------------------

backup() {
    if [ ! -f "$1.orig" ]; then
        cp "$1" "$1.orig"
        echo "  backup:  $1.orig"
    else
        echo "  backup already exists: $1.orig  (not overwritten)"
    fi
}

# apply_awk_patch FILE MARKER SEARCH DESC AWKFILE
#   MARKER: string present only after patch is applied (grep check)
#   SEARCH: string that must exist in FILE (pre-check, diagnostic)
apply_awk_patch() {
    FILE="$1"
    MARKER="$2"
    SEARCH="$3"
    DESC="$4"
    AWKF="$5"

    if grep "$MARKER" "$FILE" > /dev/null 2>&1; then
        echo "  already applied: $DESC"
        return 0
    fi

    if grep "$SEARCH" "$FILE" > /dev/null 2>&1; then
        : # found, proceed
    else
        echo "  FAILED:  $DESC" >&2
        echo "           Search pattern not found: '$SEARCH'" >&2
        echo "           Apply manually — see KERNEL_CHANGES.md" >&2
        return 0
    fi

    if awk -f "$AWKF" "$FILE" > "$TMPFILE"; then
        if [ -s "$TMPFILE" ]; then
            mv "$TMPFILE" "$FILE"
        else
            echo "  FAILED:  $DESC (awk produced empty output)" >&2
            rm -f "$TMPFILE"
            return 0
        fi
    else
        echo "  FAILED:  $DESC (awk error)" >&2
        rm -f "$TMPFILE"
        return 0
    fi

    if grep "$MARKER" "$FILE" > /dev/null 2>&1; then
        echo "  patched: $DESC"
    else
        echo "  FAILED:  $DESC (awk ran but marker not found)" >&2
        echo "           Apply manually — see KERNEL_CHANGES.md" >&2
    fi
}

# --------------------------------------------------------------------------
# 1. Copy driver source files
# --------------------------------------------------------------------------

echo "=== Copying driver source files ==="

if [ -f amiga/driver/va2000.c ]; then backup amiga/driver/va2000.c; fi
if [ -f amiga/driver/va2000.h ]; then backup amiga/driver/va2000.h; fi

cp "$SRCDIR/src/va2000.c" amiga/driver/va2000.c
cp "$SRCDIR/src/va2000.h" amiga/driver/va2000.h
echo "  copied: amiga/driver/va2000.c"
echo "  copied: amiga/driver/va2000.h"
echo ""

# --------------------------------------------------------------------------
# 2. Patch amiga/driver/Makefile — add va2000.o to the driver object list
# --------------------------------------------------------------------------

echo "=== Patching $DRVMAKE ==="
backup "$DRVMAKE"

# Add va2000.o to the ld line that builds amiga/driver/exp.
# tiga.o is the nearest analogous RTG driver entry; insert va2000.o after it.
# sub() replaces only the first occurrence, so the clean target is unaffected.
cat > "$AWKFILE" << 'EOF'
/tiga\.o/ && !done {
    sub(/tiga\.o/, "tiga.o va2000.o")
    done = 1
}
{ print }
EOF
apply_awk_patch "$DRVMAKE" \
    "va2000\.o" \
    "tiga\.o" \
    "amiga/driver/Makefile: add va2000.o to driver list" \
    "$AWKFILE"

echo ""

# --------------------------------------------------------------------------
# 4. Patch scrdev.c (5 changes)
# --------------------------------------------------------------------------

echo "=== Patching $SCRDEV ==="
backup "$SCRDEV"

# scrdev change 1:
# Add va2000_base field to scrdev struct before "} scrdev[NSCRDEV];"
cat > "$AWKFILE" << 'EOF'
/} scrdev\[NSCRDEV\];/ {
    print "    unsigned long va2000_base;  /* VA2000 board base address (type=2) */"
}
{ print }
EOF
apply_awk_patch "$SCRDEV" \
    "va2000_base" \
    "} scrdev" \
    "scrdev.c: va2000_base field in struct" \
    "$AWKFILE"

# scrdev change 2:
# Add case 2 in allocbmap() before the default: label (4-space indent).
# State machine: activate on allocbmap, fire on first "    default:" after it.
cat > "$AWKFILE" << 'EOF'
/allocbmap/ { in_alloc = 1 }
in_alloc && !done_alloc && /^    default:/ {
    print "    case 2:"
    print "\t/* VA2000 chunky framebuffer - board memory, nothing to allocate */"
    print "\tbp->bpl[0] = (unsigned char *)(dp->va2000_base + 0x10000UL);"
    print "\tbp->depth = 1;"
    print "\tbreak;"
    done_alloc = 1
}
{ print }
EOF
apply_awk_patch "$SCRDEV" \
    "VA2000 chunky framebuffer" \
    "allocbmap" \
    "scrdev.c: case 2 in allocbmap()" \
    "$AWKFILE"

# scrdev change 3:
# Add case 2 in freebmap() after case 1's break.
# Uses index() to match "break;" — avoids \t in regex (not safe on old SVR4 awk).
cat > "$AWKFILE" << 'EOF'
/freebmap/ { in_free = 1 }
in_free && !done_free && index($0, "break;") > 0 {
    print
    print "    case 2:"
    print "\t/* VA2000: board memory, nothing to free */"
    print "\tbp->bpl[0] = (unsigned char *)0;"
    print "\tbreak;"
    done_free = 1
    next
}
{ print }
EOF
apply_awk_patch "$SCRDEV" \
    "VA2000: board memory, nothing to free" \
    "freebmap" \
    "scrdev.c: case 2 in freebmap()" \
    "$AWKFILE"

# scrdev change 4:
# Add VA2000 mmap handling in scrmmap() before the existing
# "if (offset < (off_t)(bp->width..." line.
cat > "$AWKFILE" << 'EOF'
/scrmmap/ { in_mmap = 1 }
in_mmap && !done_mmap && /    if \(offset < / {
    print "    if (dp->type == 2)"
    print "    {"
    print "\t/* VA2000 chunky 16-bit: width * height * 2 bytes */"
    print "\tif (offset < (off_t)(bp->width * bp->height * 2))"
    print "\t    return phystopfn(bp->bpl[0] + offset);"
    print "\treturn -1;"
    print "    }"
    done_mmap = 1
}
{ print }
EOF
apply_awk_patch "$SCRDEV" \
    "VA2000 chunky 16-bit" \
    "scrmmap" \
    "scrdev.c: VA2000 mmap in scrmmap()" \
    "$AWKFILE"

# scrdev change 5:
# In SIOCSETTYPE handler, insert VA2000 early-exit block after
# "dp->type = scrtype.type;" so type=2 skips copper list allocation.
cat > "$AWKFILE" << 'EOF'
/SIOCSETTYPE/ { in_stype = 1 }
in_stype && !done_stype && /dp->type = scrtype\.type;/ {
    print
    print "\t    if (dp->type == 2)"
    print "\t    {"
    print "\t\textern long va2000_boards[];"
    print "\t\tdp->va2000_base = va2000_boards[0];"
    print "\t\tif (!dp->va2000_base)"
    print "\t\t    return ENXIO;"
    print "\t\tdp->copsize = 0;"
    print "\t\tdp->copbuf[0] = (unsigned short *)0;"
    print "\t\tdp->copbuf[1] = (unsigned short *)0;"
    print "\t\tbreak;  /* skip copper list allocation */"
    print "\t    }"
    done_stype = 1
    next
}
{ print }
EOF
apply_awk_patch "$SCRDEV" \
    "dp->va2000_base = va2000_boards" \
    "SIOCSETTYPE" \
    "scrdev.c: SIOCSETTYPE VA2000 early-exit" \
    "$AWKFILE"

echo ""

# --------------------------------------------------------------------------
# 5. Patch c0.c (1 change)
# --------------------------------------------------------------------------

echo "=== Patching $C0 ==="
backup "$C0"

# Add case 2 in check_scrtype() before the default: label.
# State machine: activate on check_scrtype, fire on first "    default:" after it.
cat > "$AWKFILE" << 'EOF'
/check_scrtype/ { in_fn = 1 }
in_fn && !done && /^    default:/ {
    print "    case 2:"
    print "\t/* VA2000 chunky RTG - bypass Amiga mode checks */"
    print "\treturn 0;"
    done = 1
}
{ print }
EOF
apply_awk_patch "$C0" \
    "VA2000 chunky RTG" \
    "check_scrtype" \
    "c0.c: case 2 in check_scrtype()" \
    "$AWKFILE"

echo ""

# --------------------------------------------------------------------------
# 6. Patch screen.c (1 change)
# --------------------------------------------------------------------------

echo "=== Patching $SCREEN ==="
backup "$SCREEN"

# Append SVGAChkGroup() stub at end of file.
# This function is called when a screen group activates.
# Linker position within a translation unit does not matter.

if grep "SVGAChkGroup" "$SCREEN" > /dev/null 2>&1; then
    echo "  already applied: screen.c: SVGAChkGroup() stub"
else
    cat >> "$SCREEN" << 'EOF'

void
SVGAChkGroup(groupnum)
int groupnum;
{
    /* Stub: VA2000 display switching handled by va2000 driver */
}
EOF
    if grep "SVGAChkGroup" "$SCREEN" > /dev/null 2>&1; then
        echo "  patched: screen.c: SVGAChkGroup() stub"
    else
        echo "  FAILED:  screen.c: SVGAChkGroup() stub" >&2
        echo "           Apply manually — see KERNEL_CHANGES.md" >&2
    fi
fi

echo ""

# --------------------------------------------------------------------------
# 7. Patch master.d/kernel.c (4 changes)
# --------------------------------------------------------------------------

echo "=== Patching $KERNELC (4 changes) ==="
backup "$KERNELC"

# kernel.c change 1:
# Add extern declarations for the cdevsw entry point functions (va2000open etc.)
# after the other driver extern declarations near extern scropen.
cat > "$AWKFILE" << 'EOF'
/extern scropen/ {
    print
    print "extern va2000open(),va2000close(),va2000read(),va2000write(),"
    print "\tva2000ioctl(),va2000mmap();"
    next
}
{ print }
EOF
apply_awk_patch "$KERNELC" \
    "extern va2000open" \
    "extern scropen" \
    "kernel.c: extern declarations for va2000 cdevsw functions" \
    "$AWKFILE"

# kernel.c change 2:
# Add extern declaration for va2000init specifically, placed immediately after
# "extern void parinit();" which is the line directly before io_init[].
# This patch uses its own marker (extern void va2000init) so it applies
# correctly even on a file that was previously patched with the wrong-location
# extern block — in that case change 1 is skipped but this patch still fires.
cat > "$AWKFILE" << 'EOF'
/extern void parinit/ {
    print
    print "extern void va2000init();"
    next
}
{ print }
EOF
apply_awk_patch "$KERNELC" \
    "extern void va2000init" \
    "extern void parinit" \
    "kernel.c: extern declaration for va2000init before io_init[]" \
    "$AWKFILE"

# kernel.c change 3:
# Replace the empty slot 68 in cdevsw[] with the va2000 entry.
# Uses index() for a literal string match on "/*68*/" — no regex,
# works on all awk versions including old SVR4 awk.
cat > "$AWKFILE" << 'EOF'
index($0, "/*68*/") > 0 {
    print "va2000open,va2000close,va2000read,va2000write,va2000ioctl,"
    print "\tva2000mmap,ND,ND,ND,ND,notty,nostr,nullflag,\t\t/*68=va2000*/"
    next
}
{ print }
EOF
apply_awk_patch "$KERNELC" \
    "va2000open,va2000close" \
    "cdevsw" \
    "kernel.c: cdevsw slot 68 = va2000" \
    "$AWKFILE"

# kernel.c change 4:
# Add va2000init to io_init[] before the null terminator.
# Uses index() to match "0};" — avoids \t in regex (not safe on old SVR4 awk).
cat > "$AWKFILE" << 'EOF'
/io_init/ { in_init = 1 }
in_init && !done_init && index($0, "0};") > 0 {
    print "\tva2000init,"
    done_init = 1
}
{ print }
EOF
apply_awk_patch "$KERNELC" \
    "va2000init," \
    "io_init" \
    "kernel.c: va2000init in io_init[]" \
    "$AWKFILE"

echo ""

# --------------------------------------------------------------------------
# Cleanup
# --------------------------------------------------------------------------

rm -f "$TMPFILE" "$AWKFILE"

# --------------------------------------------------------------------------
# Done — print rebuild instructions
# --------------------------------------------------------------------------

echo "=== Driver installed ==="
echo ""
echo "Create the device node (can be done now, before rebooting):"
echo ""
echo "  mknod /dev/va2000 c 68 0"
echo "  chmod 666 /dev/va2000"
echo ""
echo "Rebuild the kernel and reboot:"
echo ""
echo "  cd /usr/sys"
echo "  make install"
echo "  sync; sync; sync"
echo "  reboot"
echo ""
echo "If the kernel fails to boot, boot from floppy and restore originals:"
echo ""
echo "  cp amiga/driver/Makefile.orig  amiga/driver/Makefile"
echo "  cp amiga/driver/va2000.c.orig  amiga/driver/va2000.c"
echo "  cp amiga/console/scrdev.c.orig amiga/console/scrdev.c"
echo "  cp amiga/console/c0.c.orig     amiga/console/c0.c"
echo "  cp amiga/console/screen.c.orig amiga/console/screen.c"
echo "  cp master.d/kernel.c.orig      master.d/kernel.c"
echo "  make install"
echo "  sync; sync; sync"
echo "  reboot"
