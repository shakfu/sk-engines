#!/usr/bin/env bash
#
# Fetch + cross-build ChucK's core (the language/VM/UGens) into thirdparty/chuck, producing the
# libchuck.a that the ENGINE=chuck build links and the headers it includes. thirdparty/chuck is
# gitignored (~115 MB of source), so this reproduces it from upstream instead of vendoring it.
#
# Unlike Csound, ChucK has NO upstream Daisy/bare-metal port: its core assumes a hosted POSIX/Windows
# userland (dirent.h, netinet/in.h, unistd.h, gettimeofday, dlopen, libsndfile). WebChucK only builds
# because emscripten's sysroot stubs those headers. So this script supplies a small **shim sysroot**
# (Daisy/shim/*), a force-included **prelude** declaring the handful of POSIX functions ChucK assumes
# are transitively present, and a **stubs TU** that resolves the disabled-feature symbols (chugin
# dlopen, directory scan, tty, BSD random, libsndfile sf_*). The compiled source subset + feature
# defines are exactly WebChucK's (src/makefile `EMSCRIPTENSRCS` + the emscripten `-D__DISABLE_*`),
# which is the closest upstream config to "no OS". See docs/dev/chuck-impl.md (M0).
#
# Produces:
#   thirdparty/chuck/Daisy/lib/libchuck.a       <- linked by the Makefile chuck branch
#   thirdparty/chuck/Daisy/shim/                 <- shim headers + ck_prelude.h (an -I + -include path)
#   (headers come straight from thirdparty/chuck/src/core)
#
# Usage:
#   scripts/fetch_chuck.sh             # fetch the source + build libchuck.a
#   scripts/fetch_chuck.sh --no-build  # just fetch (build later)
#
# Env overrides:
#   CHUCK_REPO  upstream repo          (default: https://github.com/ccrma/chuck)
#   CHUCK_REF   tag / branch / commit  (default: chuck-1.5.5.8 - a pinned release known to build here;
#                                       bump it as newer releases are validated)
#   JOBS        parallel compile jobs  (default: CPU count)
#
# Prerequisites: the arm-none-eabi GCC toolchain, and either curl+tar or git.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tp="$repo_root/thirdparty/chuck"
core="$tp/src/core"
out="$tp/Daisy"
shim="$out/shim"

CHUCK_REPO="${CHUCK_REPO:-https://github.com/ccrma/chuck}"
CHUCK_REF="${CHUCK_REF:-chuck-1.5.5.8}"
JOBS="${JOBS:-$( (command -v nproc >/dev/null && nproc) || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

do_build=1
[ "${1:-}" = "--no-build" ] && do_build=0

die() { echo "error: $*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

CXX=arm-none-eabi-g++
CC=arm-none-eabi-gcc
AR=arm-none-eabi-ar

# --- prerequisites -----------------------------------------------------------------------------
if [ "$do_build" = 1 ]; then
    have "$CXX" || die "$CXX not found - install the ARM toolchain (Daisy toolchain or arm-none-eabi)"
fi
{ have curl && have tar; } || have git || die "need curl+tar or git to fetch ChucK"

# --- 1) source ---------------------------------------------------------------------------------
download_chuck() {
    if have curl && have tar; then
        local tmp p; tmp="$(mktemp -d)"
        for p in "refs/tags/$CHUCK_REF" "refs/heads/$CHUCK_REF" "$CHUCK_REF"; do
            echo "    trying $CHUCK_REPO/archive/$p.tar.gz"
            if curl -fsSL "$CHUCK_REPO/archive/$p.tar.gz" -o "$tmp/ck.tgz"; then
                tar -xzf "$tmp/ck.tgz" -C "$tmp"
                local d; d="$(find "$tmp" -maxdepth 1 -type d -name 'chuck-*' | head -1)"
                if [ -n "$d" ]; then mv "$d" "$tp"; rm -rf "$tmp"; return 0; fi
            fi
        done
        rm -rf "$tmp"
    fi
    if have git; then
        echo "    tarball unavailable; cloning with git"
        git clone --depth 1 --single-branch --branch "$CHUCK_REF" "$CHUCK_REPO" "$tp" 2>/dev/null && return 0
        git clone --filter=blob:none "$CHUCK_REPO" "$tp" && git -C "$tp" checkout "$CHUCK_REF" && return 0
    fi
    return 1
}

if [ -f "$core/chuck.h" ]; then
    echo "==> thirdparty/chuck already present (skipping fetch; 'rm -rf $tp' to re-fetch)"
else
    echo "==> fetching chuck $CHUCK_REF from $CHUCK_REPO"
    mkdir -p "$(dirname "$tp")"
    download_chuck || die "could not fetch chuck $CHUCK_REF"
fi
[ -f "$core/chuck.h" ] || die "$core/chuck.h missing - $tp does not look like a ChucK source tree"

# --- 1b) Daisy MIDI patch ----------------------------------------------------------------------
# Re-introduce ChucK's MidiIn/MidiOut device classes over the UART: strip midiio_rtmidi's RtMidi
# backend (no OS MIDI API / callback thread on bare metal; rtmidi.cpp is not compiled) and add
# MidiInManager::inject() so the host feeds bytes into the VM (a shred's `min => now` wakes via the
# existing per-VM event buffer). Pairs with dropping __DISABLE_MIDI__ from DEFS above. Idempotent:
# detect the already-applied marker so re-runs (source kept) don't double-apply. See
# docs/dev/chuck-midi-in-porting.md. Pinned to CHUCK_REF; a ref bump may require refreshing the patch.
midi_patch="$repo_root/scripts/patches/midi_daisy.patch"
[ -f "$midi_patch" ] || die "missing $midi_patch (needed to re-enable ChucK MidiIn on the Daisy build)"
if grep -q "MidiInManager::inject" "$core/midiio_rtmidi.cpp" 2>/dev/null; then
    echo "==> Daisy MIDI patch already applied (skipping)"
else
    echo "==> applying Daisy MIDI patch -> $core"
    patch -p1 -d "$core" < "$midi_patch" \
        || die "failed to apply $midi_patch (CHUCK_REF=$CHUCK_REF may have drifted from the patch)"
fi

# --- 2) shim sysroot + prelude (the bare-metal port surface) -----------------------------------
# ChucK's core #includes a hosted POSIX userland that newlib lacks. We satisfy the *includes* with
# minimal shim headers and the *symbols* with the stubs TU below. The prelude is force-included
# (-include) ahead of every ChucK TU to declare the POSIX functions ChucK assumes without including
# their headers. These are the exact gaps surfaced by compiling the WebChucK source subset for
# cortex-m7 (docs/dev/chuck-impl.md, M0).
echo "==> writing shim sysroot -> $shim"
mkdir -p "$shim/sys" "$shim/netinet" "$shim/arpa"

cat > "$shim/ck_prelude.h" <<'EOF'
// Force-included ahead of every ChucK core TU (-include). Declares the POSIX userland functions
// ChucK uses but assumes are transitively present from hosted headers. Stubbed in chuck_posix_stubs.c.
#pragma once
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#ifdef __cplusplus
extern "C" {
#endif
int    fileno(FILE*);
int    setenv(const char*, const char*, int);
long   random(void);
void   srandom(unsigned);
int    usleep(unsigned);
char*  realpath(const char*, char*);
long   getline(char**, size_t*, FILE*);
char*  getcwd(char*, size_t);
#ifdef __cplusplus
}
#endif
EOF

cat > "$shim/dlfcn.h" <<'EOF'
#pragma once
#define RTLD_LAZY 0x1
#define RTLD_NOW  0x2
#define RTLD_LOCAL 0x4
#define RTLD_GLOBAL 0x8
#ifdef __cplusplus
extern "C" {
#endif
void* dlopen(const char*, int);
int   dlclose(void*);
void* dlsym(void*, const char*);
const char* dlerror(void);
#ifdef __cplusplus
}
#endif
EOF

cat > "$shim/dirent.h" <<'EOF'
#pragma once
#include <sys/types.h>
#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8
struct dirent { unsigned char d_type; char d_name[256]; };
typedef struct DIR DIR;
#ifdef __cplusplus
extern "C" {
#endif
DIR* opendir(const char*);
struct dirent* readdir(DIR*);
int  closedir(DIR*);
void rewinddir(DIR*);
#ifdef __cplusplus
}
#endif
EOF

cat > "$shim/sys/ioctl.h" <<'EOF'
#pragma once
struct winsize { unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel; };
#define TIOCGWINSZ 0x5413
#ifdef __cplusplus
extern "C" {
#endif
int ioctl(int, unsigned long, ...);
#ifdef __cplusplus
}
#endif
EOF

cat > "$shim/poll.h" <<'EOF'
#pragma once
struct pollfd { int fd; short events, revents; };
typedef unsigned long nfds_t;
#ifdef __cplusplus
extern "C" {
#endif
int poll(struct pollfd*, nfds_t, int);
#ifdef __cplusplus
}
#endif
EOF

# Networking is disabled (-D__DISABLE_NETWORK__); these only need to satisfy #include directives.
printf '#pragma once\n' > "$shim/netinet/in.h"
printf '#pragma once\n' > "$shim/arpa/inet.h"
printf '#pragma once\n' > "$shim/sys/socket.h"
printf '#pragma once\n' > "$shim/sys/termios.h"

# The stubs TU: resolves the disabled-feature symbols at link time. None are exercised in the
# embedded build (no chugins, no filesystem, no tty) - SndBuf/LiSa file loads fail gracefully.
cat > "$shim/chuck_posix_stubs.c" <<'EOF'
/* Bare-metal stubs for the hosted-OS symbols ChucK references but never exercises in the embedded
   build: chugin dlopen, directory scan, tty, BSD random/usleep, path expansion, and libsndfile. */
#include <stddef.h>
typedef long long sf_count_t;
typedef struct DIR DIR;
struct dirent { unsigned char d_type; char d_name[256]; };
/* dynamic loading (chugins disabled) */
void* dlopen(const char* f, int g){ (void)f;(void)g; return NULL; }
int   dlclose(void* h){ (void)h; return 0; }
void* dlsym(void* h, const char* s){ (void)h;(void)s; return NULL; }
const char* dlerror(void){ return "dl disabled"; }
/* directory scan (chugin auto-load disabled) */
DIR*  opendir(const char* p){ (void)p; return NULL; }
struct dirent* readdir(DIR* d){ (void)d; return NULL; }
int   closedir(DIR* d){ (void)d; return 0; }
void  rewinddir(DIR* d){ (void)d; }
/* tty / misc POSIX */
int   ioctl(int fd, unsigned long r, ...){ (void)fd;(void)r; return -1; }
int   isatty(int fd){ (void)fd; return 0; }
int   fileno(void* f){ (void)f; return -1; }
int   setenv(const char* n, const char* v, int o){ (void)n;(void)v;(void)o; return 0; }
/* BSD random()/srandom(): a real xorshift32 PRNG (ChucK's Math.random* route here under
   __OLDSCHOOL_RANDOM__ via util_math.cpp ck_random()->random(); the old return-0 stub froze all
   randomness). 31-bit non-negative like BSD random(). Seeded per-boot from the Cortex-M7 DWT cycle
   counter so the sequence differs each power-up; an explicit nonzero srandom() is honored (reproducible).
   _ck_rng==0 means "not yet seeded" -> seed from hardware on first use. */
static unsigned long _ck_rng = 0;
static unsigned long _ck_hw_seed(void){
    /* enable + read DWT->CYCCNT via raw addresses (no CMSIS dep): DEMCR.TRCENA, DWT_CTRL.CYCCNTENA. */
    volatile unsigned long *demcr    = (volatile unsigned long*)0xE000EDFCUL;
    volatile unsigned long *dwt_ctrl = (volatile unsigned long*)0xE0001000UL;
    volatile unsigned long *dwt_cyc  = (volatile unsigned long*)0xE0001004UL;
    *demcr |= (1UL<<24); *dwt_ctrl |= 1UL;
    unsigned long c = *dwt_cyc;
    return c ? c : 0x2545F491UL;
}
long  random(void){ unsigned long x=_ck_rng?_ck_rng:_ck_hw_seed(); x^=x<<13; x^=x>>17; x^=x<<5; _ck_rng=x; return (long)(x & 0x7FFFFFFFUL); }
void  srandom(unsigned s){ _ck_rng = s ? (unsigned long)s : _ck_hw_seed(); }
int   usleep(unsigned us){ (void)us; return 0; }
char* realpath(const char* p, char* r){ (void)p;(void)r; return NULL; }
long  getline(char** l, size_t* n, void* f){ (void)l;(void)n;(void)f; return -1; }
char* getcwd(char* b, size_t n){ if(b&&n) b[0]=0; return b; }
int   glob(const char* p, int f, void* e, void* g){ (void)p;(void)f;(void)e;(void)g; return 1; }
void  globfree(void* g){ (void)g; }
int   wordexp(const char* s, void* w, int f){ (void)s;(void)w;(void)f; return 1; }
void  wordfree(void* w){ (void)w; }
/* libsndfile (disk I/O disabled): SndBuf/LiSa loads fail gracefully */
void* sf_open(const char* p, int m, void* i){ (void)p;(void)m;(void)i; return NULL; }
void* sf_open_fd(int fd, int m, void* i, int c){ (void)fd;(void)m;(void)i;(void)c; return NULL; }
int   sf_close(void* s){ (void)s; return 0; }
sf_count_t sf_seek(void* s, sf_count_t n, int w){ (void)s;(void)n;(void)w; return -1; }
sf_count_t sf_readf_float(void* s, float* p, sf_count_t n){ (void)s;(void)p;(void)n; return 0; }
sf_count_t sf_readf_double(void* s, double* p, sf_count_t n){ (void)s;(void)p;(void)n; return 0; }
int   sf_error(void* s){ (void)s; return 1; }
const char* sf_strerror(void* s){ (void)s; return "sndfile disabled"; }
EOF

if [ "$do_build" = 0 ]; then
    echo "==> --no-build: source fetched + shim written. Build later with: scripts/fetch_chuck.sh"
    exit 0
fi

# --- 3) cross-build libchuck.a (cortex-m7, double-precision FPU, bare metal) --------------------
# MCU flags MATCH lib/libDaisy/core/Makefile so the archive is ABI-compatible with the firmware:
#   -mfpu=fpv5-d16 = the STM32H7 Cortex-M7's HARDWARE double-precision FPU (ChucK's t_CKFLOAT=double
#   UGen math is hardware-accelerated, not software-emulated).
MCU="-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard"
# Feature defines = WebChucK's emscripten set (no MIDI/HID/serial/OTF/threads/shell) +:
#   __USE_CHUCK_YACC__  : use the pre-generated parser (core/chuck_yacc.h), so NO flex/bison needed.
#   __PLATFORM_LINUX__  : pick a platform branch (avoids a bare `#elif` bug in util_platforms.h and
#                         gives a CHUCK_PLATFORM_STRING); the OS gaps it implies are covered by the shim.
#   CPU_IS_LITTLE_ENDIAN / sndfile count types: satisfy the vendored util_sndfile.h (we stub sf_*).
DEFS="-D__PLATFORM_LINUX__ -D__USE_CHUCK_YACC__ \
 -DCPU_IS_LITTLE_ENDIAN=1 -DCPU_IS_BIG_ENDIAN=0 -DTYPEOF_SF_COUNT_T=__INT64_TYPE__ -DSIZEOF_SF_COUNT_T=8 \
 -D__DISABLE_WATCHDOG__ -D__DISABLE_NETWORK__ -D__DISABLE_OTF_SERVER__ \
 -D__ALTER_HID__ -D__DISABLE_HID__ -D__DISABLE_SERIAL__ -D__DISABLE_ASYNCH_IO__ -D__DISABLE_THREADS__ \
 -D__DISABLE_KBHIT__ -D__DISABLE_PROMPTER__ -D__DISABLE_SHELL__ -D__OLDSCHOOL_RANDOM__"
INC="-I$core -I$shim"
PRE="-include $shim/ck_prelude.h"
CXXFLAGS="$MCU -Os -std=c++17 -fdata-sections -ffunction-sections $DEFS $INC $PRE"
CFLAGS="$MCU -Os -fdata-sections -ffunction-sections $DEFS $INC"

# The compiled source subset = WebChucK's EMSCRIPTENSRCS, minus host-web/chuck_emscripten.cpp (we
# provide our own host glue) and minus util_sndfile.c (won't build bare-metal; sf_* stubbed instead),
# PLUS midiio_rtmidi (the ChucK MidiIn/MidiOut device classes). MIDI is no longer __DISABLE_MIDI__'d:
# the patch step below strips midiio_rtmidi's RtMidi backend (no OS MIDI API / callback thread on bare
# metal) and adds MidiInManager::inject(), so the host feeds the UART into the VM. rtmidi.cpp itself is
# deliberately NOT compiled (the 166 KB OS backend stays out). See docs/dev/chuck-midi-in-porting.md.
CPP_SRCS="chuck chuck_absyn chuck_carrier chuck_compile chuck_dl chuck_emit chuck_errmsg chuck_frame \
 chuck_globals chuck_instr chuck_io chuck_lang chuck_oo chuck_parse chuck_scan chuck_stats chuck_symbol midiio_rtmidi \
 chuck_table chuck_type chuck_ugen chuck_vm uana_extract uana_xform ugen_filter ugen_osc ugen_stk \
 ugen_xxx ulib_ai ulib_doc ulib_machine ulib_math ulib_std util_buffers util_math util_platforms util_string"
C_SRCS="util_raw util_xforms chuck_yacc"

bdir="$out/build"
mkdir -p "$bdir" "$out/lib"
rm -f "$bdir"/*.o

echo "==> compiling ChucK core ($JOBS jobs, cortex-m7) ..."
# Bounded-parallel compile. A failed job touches a flag (a `die` in a backgrounded subshell can't
# abort the parent), checked after each barrier so a broken TU stops the build instead of silently
# yielding an archive with missing symbols.
fail="$bdir/.fail"; rm -f "$fail"
n=0
for f in $CPP_SRCS; do
    { "$CXX" $CXXFLAGS -c "$core/$f.cpp" -o "$bdir/$f.o" || { echo "compile failed: $f.cpp" >&2; touch "$fail"; }; } &
    n=$((n+1)); [ $((n % JOBS)) -eq 0 ] && wait
done
wait; [ -f "$fail" ] && die "one or more C++ TUs failed to compile (see above)"
for f in $C_SRCS; do
    { "$CC" $CFLAGS -c "$core/$f.c" -o "$bdir/$f.o" || { echo "compile failed: $f.c" >&2; touch "$fail"; }; } &
done
wait; [ -f "$fail" ] && die "one or more C TUs failed to compile (see above)"
# the POSIX/sndfile stub TU (folded into the archive so the firmware link needs only libchuck.a)
"$CC" $CFLAGS -c "$shim/chuck_posix_stubs.c" -o "$bdir/chuck_posix_stubs.o" || die "stubs failed"

echo "==> archiving libchuck.a"
rm -f "$out/lib/libchuck.a"
"$AR" rcs "$out/lib/libchuck.a" "$bdir"/*.o

# --- 4) verify: archive exists AND links (the M0 proof) ----------------------------------------
lib="$out/lib/libchuck.a"
[ -f "$lib" ] || die "build finished but $lib is missing"

# M0 link probe: a TU that creates a ChucK, compiles a program from a string, and runs one block -
# the embedded contract end-to-end. If this links (against libc/libstdc++ + the folded-in stubs), the
# archive is firmware-linkable. Uses the same MCU flags + nano/nosys specs the firmware link uses.
echo "==> link-test: new ChucK() + compileCode() + run() ..."
probe="$bdir/m0_probe"
cat > "$probe.cpp" <<'EOF'
#include "chuck.h"
extern "C" int chuck_m0_probe() {
    ChucK* ck = new ChucK();
    ck->setParam(CHUCK_PARAM_SAMPLE_RATE, 48000);
    ck->setParam(CHUCK_PARAM_INPUT_CHANNELS, 2);
    ck->setParam(CHUCK_PARAM_OUTPUT_CHANNELS, 2);
    ck->init();
    ck->compileCode("SinOsc s => dac; while(true){ 1::samp => now; }", "", 1);
    float in[512] = {0}, out[512] = {0};
    ck->run(in, out, 128);
    return ck->vm_running() ? 1 : 0;
}
int main() { return chuck_m0_probe(); }
EOF
if "$CXX" $CXXFLAGS -c "$probe.cpp" -o "$probe.o" 2>"$probe.err" \
   && "$CXX" $MCU --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections \
        "$probe.o" "$lib" -o "$probe.elf" 2>>"$probe.err"; then
    echo "    OK - $(arm-none-eabi-size "$probe.elf" | awk 'NR==2{printf "text=%dKB data=%dKB bss=%dKB", $1/1024,$2/1024,$3/1024}')"
else
    echo "    link-test FAILED (see $probe.err):" >&2; tail -20 "$probe.err" >&2
    die "libchuck.a built but the M0 link probe failed"
fi

echo "==> done: $lib ($(du -h "$lib" | cut -f1))"
echo "    headers: $core   shim+prelude: $shim"
echo "    source pinned at CHUCK_REF=$CHUCK_REF; now build the engine: make engine-chuck"
