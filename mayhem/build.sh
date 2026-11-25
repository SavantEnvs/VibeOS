#!/usr/bin/env bash
#
# mayhem/build.sh — host (x86_64) fuzz builds of VibeOS kernel subsystems that parse
# untrusted bytes: FAT32 disk images, the VFS on top of them, network frames, ELF programs,
# the boot device tree and TrueType fonts.
#
# VibeOS itself is a bare-metal aarch64 kernel (top-level Makefile, cross gcc, QEMU), which
# cannot host a fuzzer. Its subsystems are plain C with fixed-width types, so the harnesses
# compile the kernel sources unmodified for the host and stub only the device layer
# (block device, virtio-net, console). The kernel's own string/printf routines
# (kernel/string.c, kernel/printf.c) are linked in under vibe_* names (mayhem/harness/vibe_shim.h).
#
# Outputs: /mayhem/fuzz_<t> (libFuzzer) and /mayhem/fuzz_<t>-standalone for each target,
# plus /mayhem/vibeos_selftest (normal flags) which mayhem/test.sh runs.
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
H=mayhem/harness
OUT=build/mayhem        # gitignored by upstream's `build/`
mkdir -p "$OUT/fuzz" "$OUT/test"

# Kernel sources compiled as for the kernel (-DTARGET_QEMU, -fno-builtin), with -Ikernel only:
# kernel/libc shadows host libc headers and none of these files need it.
KFLAGS="-DTARGET_QEMU -fno-builtin -Ikernel -include $H/vibe_shim.h -Wno-unused-function"

build_set() {   # <outdir> <cflags...>
  local o="$1"; shift
  local cf="$*"
  # kernel string/printf runtime (printf itself stays a no-op from harness_stubs.c)
  $CC $cf $KFLAGS -c kernel/string.c -o "$o/k_string.o"
  $CC $cf $KFLAGS -Dvibe_printf=vibe_printf_kernel -c kernel/printf.c -o "$o/k_printf.o"
  $CC $cf -c "$H/harness_stubs.c" -o "$o/stubs.o"
  $CC $cf $KFLAGS -c kernel/elf.c -o "$o/k_elf.o"
  $CC $cf $KFLAGS -c kernel/dtb.c -o "$o/k_dtb.o"
}

# 1) Sanitized fuzz builds. The kernel code is compiled into each harness TU or into the
#    k_*.o objects with the same flags, so it is ASan/UBSan-instrumented and (for the libFuzzer
#    binaries) SanitizerCoverage-instrumented via -fsanitize=fuzzer-no-link. The standalone
#    reproducers get a second object set without coverage instrumentation.
#    UBSan relaxation: -fno-sanitize=shift-base. The kernel decodes little/big-endian fields as
#    `uint8 << 24` (fat32.c, net.c, dtb.c), which is signed-int overflow when the byte is >= 0x80
#    and fires on almost every disk image/frame; every other UBSan check stays on and halting.
FCF="$SANITIZER_FLAGS $DEBUG_FLAGS -fno-sanitize=shift-base"
COV="${LIB_FUZZING_ENGINE/-fsanitize=fuzzer/-fsanitize=fuzzer-no-link}"
mkdir -p "$OUT/standalone"
build_set "$OUT/fuzz" $FCF $COV
build_set "$OUT/standalone" $FCF
$CC $FCF -c "$H/lsan_off.c" -o "$OUT/fuzz/lsan_off.o"
$CC $FCF -c "$STANDALONE_FUZZ_MAIN" -o "$OUT/standalone/standalone_main.o"

for t in fat32 vfs net elf dtb ttf; do
  for v in fuzz standalone; do
    o="$OUT/$v"
    case "$t" in elf) extra="$o/k_elf.o" ;; dtb) extra="$o/k_dtb.o" ;; ttf) extra="-lm" ;; *) extra="" ;; esac
    objs="$o/k_string.o $o/k_printf.o $o/stubs.o $OUT/fuzz/lsan_off.o $extra"
    if [ "$v" = fuzz ]; then
      $CC $FCF $COV $KFLAGS -c "$H/fuzz_$t.c" -o "$o/fuzz_$t.o"
      $CC $FCF $LIB_FUZZING_ENGINE "$o/fuzz_$t.o" $objs -o "$SRC/fuzz_$t"
    else
      $CC $FCF $KFLAGS -c "$H/fuzz_$t.c" -o "$o/fuzz_$t.o"
      $CC $FCF "$o/standalone_main.o" "$o/fuzz_$t.o" $objs -o "$SRC/fuzz_$t-standalone"
    fi
  done
done

# 2) Functional self-test with normal flags (no sanitizers): known-answer checks of the same
#    subsystems on fixed inputs (mayhem/harness/selftest*.c, on the committed seeds).
TCF="-O1 $COVERAGE_FLAGS"
build_set "$OUT/test" $TCF
for f in selftest selftest_fs selftest_net; do
  $CC $TCF $KFLAGS -c "$H/$f.c" -o "$OUT/test/$f.o"
done
$CC $TCF "$OUT/test/selftest.o" "$OUT/test/selftest_fs.o" "$OUT/test/selftest_net.o" "$OUT/test/k_string.o" "$OUT/test/k_printf.o" "$OUT/test/stubs.o" \
    "$OUT/test/k_elf.o" "$OUT/test/k_dtb.o" -lm -o "$SRC/vibeos_selftest"
