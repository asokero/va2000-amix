# Troubleshooting — VA2000 AMIX Driver Installation

Known problems encountered when installing the VA2000 driver on AMIX SVR4.

---

## 1. `grep: illegal option -- q`

**Problem:** The install script fails with `grep: illegal option -- q` and all
patches report FAILED.

**Cause:** AMIX ships an old SVR4 grep that does not support the `-q` (quiet)
flag. The flag was added in POSIX.2 (1992) and is not present in older System V
grep implementations.

**Solution:** Use output redirection to suppress output instead of `-q`:

```sh
# Wrong (fails on AMIX):
grep -q "pattern" file

# Correct:
grep "pattern" file > /dev/null 2>&1
```

The current `install-va2000-driver.sh` already uses the correct form.

---

## 2. `grep: syntax error` with `\|` (OR alternation)

**Problem:** grep exits with a syntax error when the pattern contains `\|`.

**Cause:** AMIX grep uses Basic Regular Expressions (BRE). The `\|` alternation
operator is a GNU grep extension and is not part of BRE or POSIX ERE as
supported by old SVR4 grep.

**Solution:** Split into separate grep calls:

```sh
# Wrong (fails on AMIX):
grep "foo\|bar" file > /dev/null 2>&1

# Correct:
grep "foo" file > /dev/null 2>&1 || grep "bar" file > /dev/null 2>&1
```

---

## 3. `$()` command substitution not recognized

**Problem:** The shell exits with a syntax error at lines using `$(...)`.

**Cause:** AMIX `/bin/sh` is a Bourne shell that predates POSIX and does not
support `$(...)` command substitution. Only backtick syntax is supported.

**Solution:** Replace all `$(...)` with backticks:

```sh
# Wrong (fails on AMIX):
DIR=$(dirname "$0")

# Correct:
DIR=`dirname "$0"`
```

---

## 4. `kernel.c:200: 'va2000init' undeclared, outside of functions`

**Problem:** Kernel compilation fails with an `undeclared` error for `va2000init`
at the `io_init[]` array definition.

**Cause:** The `extern` declaration for `va2000init` was inserted after the
`extern scropen()` line (~line 248), but `io_init[]` appears at line 200 —
before that block. The C compiler processes the file top to bottom, so it
encounters `va2000init` in `io_init[]` before seeing its `extern` declaration.

**Solution:** The `extern` declarations must be placed immediately after
`extern void parinit();` at line 197, which is the line directly before
`io_init[]`. The current `install-va2000-driver.sh` uses this anchor and is
already correct. If you patched manually using the wrong anchor, restore
`master.d/kernel.c` from its `.orig` backup and re-run the install script:

```sh
cp master.d/kernel.c.orig master.d/kernel.c
sh /path/to/install-va2000-driver.sh /path/to/va2000-amix
```

---

## 5. Driver does not load — kernel compiles but `/dev/va2000` fails to open

**Problem:** The kernel boots and compiles without errors, but opening
`/dev/va2000` returns `ENXIO` (no such device). The va2000 driver appears to
not be registered at all.

**Cause:** `master.d/kernel.c` was not patched. The kernel needs three changes
in this file to register the driver: the `extern` function declarations, the
`cdevsw[]` entry at slot 68, and `va2000init` in `io_init[]`. Without these,
the driver object file (`va2000.o`) is compiled but never linked into the kernel
dispatch table or called at init time.

**Solution:** Re-run `install-va2000-driver.sh` from `/usr/sys` — the script
patches `master.d/kernel.c` automatically. Then rebuild:

```sh
rm -f amiga/config/unix.o
rm -f master.d/exp
rm -f unix
make install
sync && sync && sync && reboot
```

If the script was already run once without this patch, restore `kernel.c` from
its `.orig` backup first:

```sh
cp master.d/kernel.c.orig master.d/kernel.c
sh /path/to/install-va2000-driver.sh /path/to/va2000-amix
```

---

## 6. Kernel boots but `va2000_boards` is an unresolved symbol

**Problem:** `make install` succeeds but the new kernel panics on boot with an
unresolved symbol error for `va2000_boards`.

**Cause:** The old `unix` binary (the kernel) was not removed before
`make install`. The AMIX build system links the final kernel into a file called
`unix`. If `unix` already exists and is newer than the object files, `make` may
skip the link step entirely, leaving the old kernel in place. Even if the link
runs, a stale `amiga/driver/exp` export table can cause the linker to miss
newly added symbols.

**Solution:** Remove the old kernel and export table before rebuilding:

```sh
rm -f amiga/config/unix.o
rm -f master.d/exp
rm -f unix
make install
sync && sync && sync && reboot
```

---

## 7. Install script run twice — duplicate code inserted

**Problem:** The install script was run more than once on the same source tree.
The kernel sources now contain duplicate `case 2:` blocks or duplicate
`SVGAChkGroup` definitions, which cause compile errors.

**Cause:** Running the script a second time after it has already been applied.
The idempotency checks (marker greps) should prevent this, but if the first run
was interrupted or partially applied, re-running can duplicate some patches.

**Solution:** Restore the original files from the `.orig` backups the script
created, then run the install script once from a clean state:

```sh
cp amiga/driver/va2000.c.orig  amiga/driver/va2000.c    # if it exists
cp amiga/console/scrdev.c.orig amiga/console/scrdev.c
cp amiga/console/c0.c.orig     amiga/console/c0.c
cp amiga/console/screen.c.orig amiga/console/screen.c
cp master.d/kernel.c.orig      master.d/kernel.c
sh /path/to/install-va2000-driver.sh /path/to/va2000-amix
```

If no `.orig` backups exist, restore from the AMIX installation media or from
the `vanilla/` directory in this repository.

---

## 8. Kernel does not boot after installation

**Problem:** The system hangs or panics after rebooting with the new kernel.

**Cause:** A bug in the patched kernel sources, a failed link, or an
incompatible `va2000.c` for this kernel version.

**Solution:** Boot from the AMIX installation floppy, then restore the original
kernel sources and rebuild:

```sh
# From /usr/sys after booting from floppy:
cp amiga/driver/va2000.c.orig  amiga/driver/va2000.c
cp amiga/console/scrdev.c.orig amiga/console/scrdev.c
cp amiga/console/c0.c.orig     amiga/console/c0.c
cp amiga/console/screen.c.orig amiga/console/screen.c
cp master.d/kernel.c.orig      master.d/kernel.c
rm -f amiga/config/unix.o master.d/exp unix
make install
sync && sync && sync && reboot
```

This restores the vanilla kernel. If the vanilla kernel also does not boot,
the problem is unrelated to this driver.

---

## 9. Driver missing from `io_init[]` — board not initialized at boot

**Problem:** The kernel compiles and boots, `/dev/va2000` exists and can be
opened, but the driver never finds the board. `autocon()` returns 0 even
though the VA2000 is physically present.

**Cause:** `va2000init` was not added to `io_init[]` in `master.d/kernel.c`.
`io_init[]` is the list of functions the kernel calls at boot time to detect
and initialize hardware. Without an entry here, `va2000init()` is never called,
`va2000_boards[]` stays zero, and every call to `open()` returns `ENXIO`.

The install script uses an awk pattern to insert `va2000init` before the null
terminator. Earlier versions used `/^\t0\};/` which fails silently on old SVR4
awk (AMIX) because `\t` in regex patterns is not recognized — the pattern
never matches, awk exits successfully, and the file is rewritten without the
insertion. The current script uses `index($0, "0};")` instead.

**Diagnosis:** Check whether `va2000init` is in the io_init block:

```sh
grep -n "io_init\|va2000" /usr/sys/master.d/kernel.c
```

Expected output after correct patching:
```
197: extern void va2000init();
198: void  (*io_init[])() = {
199:     parinit,
200:     va2000init,
201:     0};
```

**Solution:** Restore `kernel.c` from its backup and re-run:

```sh
cp master.d/kernel.c.orig master.d/kernel.c
sh /path/to/install-va2000-driver.sh /path/to/va2000-amix
rm -f amiga/config/unix.o master.d/exp unix
make install
sync && sync && sync && reboot
```

---

## 10. Function name mismatch — `cdevsw` calls unresolved symbols

**Problem:** The kernel compiles but panics on boot with unresolved symbol
errors for `va2open`, `va2close`, `va2read`, `va2write`, `va2ioctl`,
`va2mmap`, or similar short names.

**Cause:** An old version of `va2000.c` used short function names (`va2open`,
`va2close`, etc.) instead of the full names (`va2000open`, `va2000close`, etc.)
expected by the `cdevsw[]` entry that `install-va2000-driver.sh` inserts.
The linker cannot resolve the names and the kernel fails to boot.

**Solution:** Verify that `va2000.c` uses the full names:

```sh
grep "^va2" /usr/sys/amiga/driver/va2000.c
```

Expected output (all `va2000` prefix, no bare `va2` prefix):
```
va2000init()
va2000open(devp, mode, type, cr)
va2000close(dev, mode, type, cr)
va2000read(dev, uiop, cr)
va2000write(dev, uiop, cr)
va2000mmap(dev, offset, maxprot)
va2000ioctl(dev, cmd, arg, mode, cr, rvalp)
```

If bare `va2` names are present, copy the corrected `src/va2000.c` from the
repository and rebuild:

```sh
cp /path/to/va2000-amix/src/va2000.c /usr/sys/amiga/driver/va2000.c
rm -f amiga/config/unix.o master.d/exp unix
make install
sync && sync && sync && reboot
```
