/* os.h — Multi-arch Linux system-call wrappers for the book's C ports.
 *
 * The original assembly in /pgu/src is x86-32 Linux only.  These
 * inline wrappers pick the right syscall instruction, register
 * convention, and syscall numbers based on the target arch.
 *
 *   __i386__     : int $0x80,  eax/ebx/ecx/edx
 *   __x86_64__   : syscall,    rax/rdi/rsi/rdx
 *   __arm__      : svc 0,      r7=nr, r0..r3 args (EABI)
 *   __aarch64__  : svc #0,     x8=nr, x0..x5 args
 *   __mips__     : syscall,    v0=nr, a0..a3 args (o32 ABI)
 *
 * Linux ARM64 has no plain `open` syscall, so os_open() is built
 * on top of openat(AT_FDCWD, ...) on that arch.
 *
 * On MIPS, syscalls signal failure via the $a3 register rather
 * than a negative return value, so we fold that back into the
 * standard "negative = -errno" convention used by every other
 * arch in this header.
 */

#ifndef OS_H
#define OS_H

#include <stddef.h>

/* Force inlining at -O0 so the syscall asm gets emitted at every
 * call site rather than hidden behind a `call os_write`.  This is
 * the equivalent of writing macros, but with real C type checking
 * and proper local variables.  Used together with `static inline`,
 * e.g.  `ALWAYS_INLINE static inline long os_write(...)`.
 */
#define ALWAYS_INLINE __attribute__((always_inline))

#define STDIN 0
#define STDOUT 1
#define STDERR 2

#define END_OF_FILE 0

/* Portable open-flag constants (Linux asm-generic / EABI / x86 /
 * arm / aarch64 all share these; MIPS overrides below).  We
 * define our own rather than #include <fcntl.h> so a freestanding
 * build does not need libc headers in the include path.
 */
#if defined(__mips__)
#define OS_O_RDONLY 0
#define OS_O_WRONLY 1
#define OS_O_CREAT 0x0100
#define OS_O_TRUNC 0x0200
#else
#define OS_O_RDONLY 0
#define OS_O_WRONLY 1
#define OS_O_CREAT 0100  /* octal */
#define OS_O_TRUNC 01000 /* octal */
#endif

/* ================================================================
 * i386 Linux
 * ================================================================ */
#if defined(__i386__) && defined(__linux__)

#define _NR_EXIT 1
#define _NR_READ 3
#define _NR_WRITE 4
#define _NR_OPEN 5
#define _NR_CLOSE 6
#define _NR_BRK 45

ALWAYS_INLINE static inline long os_write(int fd, const void *buf, size_t len) {
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "0"(_NR_WRITE), "b"(fd), "c"(buf), "d"(len)
                     : "memory");
    return ret;
}
ALWAYS_INLINE static inline long os_read(int fd, void *buf, size_t len) {
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "0"(_NR_READ), "b"(fd), "c"(buf), "d"(len)
                     : "memory");
    return ret;
}
ALWAYS_INLINE static inline long os_open(const char *path, int flags,
                                         int mode) {
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "0"(_NR_OPEN), "b"(path), "c"(flags), "d"(mode)
                     : "memory");
    return ret;
}
ALWAYS_INLINE static inline long os_close(int fd) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "0"(_NR_CLOSE), "b"(fd));
    return ret;
}
__attribute__((noreturn)) ALWAYS_INLINE static inline void os_exit(int status) {
    __asm__ volatile("int $0x80" : : "a"(_NR_EXIT), "b"(status));
    __builtin_unreachable();
}
ALWAYS_INLINE static inline void *os_brk(void *addr) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "0"(_NR_BRK), "b"(addr));
    return (void *)ret;
}

/* ================================================================
 * x86_64 Linux
 *
 * The `syscall` instruction clobbers %rcx (return addr) and %r11
 * (saved RFLAGS), so they must be in the clobber list.
 * ================================================================ */
#elif defined(__x86_64__) && defined(__linux__)

#define _NR_READ 0
#define _NR_WRITE 1
#define _NR_OPEN 2
#define _NR_CLOSE 3
#define _NR_BRK 12
#define _NR_EXIT 60

ALWAYS_INLINE static inline long os_write(int fd, const void *buf, size_t len) {
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "0"((long)_NR_WRITE), "D"((long)fd), "S"(buf), "d"(len)
                     : "rcx", "r11", "memory");
    return ret;
}
ALWAYS_INLINE static inline long os_read(int fd, void *buf, size_t len) {
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "0"((long)_NR_READ), "D"((long)fd), "S"(buf), "d"(len)
                     : "rcx", "r11", "memory");
    return ret;
}
ALWAYS_INLINE static inline long os_open(const char *path, int flags,
                                         int mode) {
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "0"((long)_NR_OPEN), "D"(path), "S"((long)flags),
                       "d"((long)mode)
                     : "rcx", "r11", "memory");
    return ret;
}
ALWAYS_INLINE static inline long os_close(int fd) {
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "0"((long)_NR_CLOSE), "D"((long)fd)
                     : "rcx", "r11", "memory");
    return ret;
}
__attribute__((noreturn)) ALWAYS_INLINE static inline void os_exit(int status) {
    __asm__ volatile("syscall" : : "a"((long)_NR_EXIT), "D"((long)status));
    __builtin_unreachable();
}
ALWAYS_INLINE static inline void *os_brk(void *addr) {
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "0"((long)_NR_BRK), "D"(addr)
                     : "rcx", "r11", "memory");
    return (void *)ret;
}

/* ================================================================
 * ARM 32-bit Linux (EABI)
 *
 * On EABI the syscall number lives in r7 and arguments are in
 * r0..r3 (more on the stack for >4 args, which we never need).
 * ================================================================ */
#elif defined(__arm__) && defined(__linux__)

#define _NR_EXIT 1
#define _NR_READ 3
#define _NR_WRITE 4
#define _NR_OPEN 5
#define _NR_CLOSE 6
#define _NR_BRK 45

ALWAYS_INLINE static inline long os_write(int fd, const void *buf, size_t len) {
    register long _r0 __asm__("r0") = fd;
    register long _r1 __asm__("r1") = (long)buf;
    register long _r2 __asm__("r2") = len;
    register long _r7 __asm__("r7") = _NR_WRITE;
    __asm__ volatile("svc #0"
                     : "+r"(_r0)
                     : "r"(_r1), "r"(_r2), "r"(_r7)
                     : "memory");
    return _r0;
}
ALWAYS_INLINE static inline long os_read(int fd, void *buf, size_t len) {
    register long _r0 __asm__("r0") = fd;
    register long _r1 __asm__("r1") = (long)buf;
    register long _r2 __asm__("r2") = len;
    register long _r7 __asm__("r7") = _NR_READ;
    __asm__ volatile("svc #0"
                     : "+r"(_r0)
                     : "r"(_r1), "r"(_r2), "r"(_r7)
                     : "memory");
    return _r0;
}
ALWAYS_INLINE static inline long os_open(const char *path, int flags,
                                         int mode) {
    register long _r0 __asm__("r0") = (long)path;
    register long _r1 __asm__("r1") = flags;
    register long _r2 __asm__("r2") = mode;
    register long _r7 __asm__("r7") = _NR_OPEN;
    __asm__ volatile("svc #0"
                     : "+r"(_r0)
                     : "r"(_r1), "r"(_r2), "r"(_r7)
                     : "memory");
    return _r0;
}
ALWAYS_INLINE static inline long os_close(int fd) {
    register long _r0 __asm__("r0") = fd;
    register long _r7 __asm__("r7") = _NR_CLOSE;
    __asm__ volatile("svc #0" : "+r"(_r0) : "r"(_r7) : "memory");
    return _r0;
}
__attribute__((noreturn)) ALWAYS_INLINE static inline void os_exit(int status) {
    register long _r0 __asm__("r0") = status;
    register long _r7 __asm__("r7") = _NR_EXIT;
    __asm__ volatile("svc #0" : : "r"(_r0), "r"(_r7));
    __builtin_unreachable();
}
ALWAYS_INLINE static inline void *os_brk(void *addr) {
    register long _r0 __asm__("r0") = (long)addr;
    register long _r7 __asm__("r7") = _NR_BRK;
    __asm__ volatile("svc #0" : "+r"(_r0) : "r"(_r7) : "memory");
    return (void *)_r0;
}

/* ================================================================
 * AArch64 Linux
 *
 * Generic syscall ABI: nr in x8, args in x0..x5.  No `open`
 * syscall — we synthesize it via openat(AT_FDCWD, ...).
 * ================================================================ */
#elif defined(__aarch64__) && defined(__linux__)

#define _NR_OPENAT 56
#define _NR_CLOSE 57
#define _NR_READ 63
#define _NR_WRITE 64
#define _NR_EXIT 93
#define _NR_BRK 214
#define _AT_FDCWD (-100)

ALWAYS_INLINE static inline long os_write(int fd, const void *buf, size_t len) {
    register long _x0 __asm__("x0") = fd;
    register long _x1 __asm__("x1") = (long)buf;
    register long _x2 __asm__("x2") = len;
    register long _x8 __asm__("x8") = _NR_WRITE;
    __asm__ volatile("svc #0"
                     : "+r"(_x0)
                     : "r"(_x1), "r"(_x2), "r"(_x8)
                     : "memory");
    return _x0;
}
ALWAYS_INLINE static inline long os_read(int fd, void *buf, size_t len) {
    register long _x0 __asm__("x0") = fd;
    register long _x1 __asm__("x1") = (long)buf;
    register long _x2 __asm__("x2") = len;
    register long _x8 __asm__("x8") = _NR_READ;
    __asm__ volatile("svc #0"
                     : "+r"(_x0)
                     : "r"(_x1), "r"(_x2), "r"(_x8)
                     : "memory");
    return _x0;
}
ALWAYS_INLINE static inline long os_open(const char *path, int flags,
                                         int mode) {
    register long _x0 __asm__("x0") = _AT_FDCWD;
    register long _x1 __asm__("x1") = (long)path;
    register long _x2 __asm__("x2") = flags;
    register long _x3 __asm__("x3") = mode;
    register long _x8 __asm__("x8") = _NR_OPENAT;
    __asm__ volatile("svc #0"
                     : "+r"(_x0)
                     : "r"(_x1), "r"(_x2), "r"(_x3), "r"(_x8)
                     : "memory");
    return _x0;
}
ALWAYS_INLINE static inline long os_close(int fd) {
    register long _x0 __asm__("x0") = fd;
    register long _x8 __asm__("x8") = _NR_CLOSE;
    __asm__ volatile("svc #0" : "+r"(_x0) : "r"(_x8) : "memory");
    return _x0;
}
__attribute__((noreturn)) ALWAYS_INLINE static inline void os_exit(int status) {
    register long _x0 __asm__("x0") = status;
    register long _x8 __asm__("x8") = _NR_EXIT;
    __asm__ volatile("svc #0" : : "r"(_x0), "r"(_x8));
    __builtin_unreachable();
}
ALWAYS_INLINE static inline void *os_brk(void *addr) {
    register long _x0 __asm__("x0") = (long)addr;
    register long _x8 __asm__("x8") = _NR_BRK;
    __asm__ volatile("svc #0" : "+r"(_x0) : "r"(_x8) : "memory");
    return (void *)_x0;
}

/* ================================================================
 * MIPS 32-bit Linux (o32 ABI)
 *
 * Syscall numbers are the o32 series 4000+.  The `syscall`
 * instruction returns the result in $v0 and a non-zero $a3 to
 * signal error (with errno in $v0).  We renormalize that into
 * the negative-return convention.  All the temporaries that the
 * kernel may clobber go in the clobber list.
 * ================================================================ */
#elif defined(__mips__) && defined(__linux__)

#define _NR_BASE 4000
#define _NR_EXIT (_NR_BASE + 1)
#define _NR_READ (_NR_BASE + 3)
#define _NR_WRITE (_NR_BASE + 4)
#define _NR_OPEN (_NR_BASE + 5)
#define _NR_CLOSE (_NR_BASE + 6)
#define _NR_BRK (_NR_BASE + 45)

#define _MIPS_CLOBBERS                                                   \
    "memory", "$1", "$3", "$8", "$9", "$10", "$11", "$12", "$13", "$14", \
        "$15", "$24", "$25", "hi", "lo"

ALWAYS_INLINE static inline long os_write(int fd, const void *buf, size_t len) {
    register long _v0 __asm__("$2") = _NR_WRITE;
    register long _a0 __asm__("$4") = fd;
    register long _a1 __asm__("$5") = (long)buf;
    register long _a2 __asm__("$6") = len;
    register long _a3 __asm__("$7");
    __asm__ volatile("syscall"
                     : "+r"(_v0), "=r"(_a3)
                     : "r"(_a0), "r"(_a1), "r"(_a2)
                     : _MIPS_CLOBBERS);
    return _a3 ? -_v0 : _v0;
}
ALWAYS_INLINE static inline long os_read(int fd, void *buf, size_t len) {
    register long _v0 __asm__("$2") = _NR_READ;
    register long _a0 __asm__("$4") = fd;
    register long _a1 __asm__("$5") = (long)buf;
    register long _a2 __asm__("$6") = len;
    register long _a3 __asm__("$7");
    __asm__ volatile("syscall"
                     : "+r"(_v0), "=r"(_a3)
                     : "r"(_a0), "r"(_a1), "r"(_a2)
                     : _MIPS_CLOBBERS);
    return _a3 ? -_v0 : _v0;
}
ALWAYS_INLINE static inline long os_open(const char *path, int flags,
                                         int mode) {
    register long _v0 __asm__("$2") = _NR_OPEN;
    register long _a0 __asm__("$4") = (long)path;
    register long _a1 __asm__("$5") = flags;
    register long _a2 __asm__("$6") = mode;
    register long _a3 __asm__("$7");
    __asm__ volatile("syscall"
                     : "+r"(_v0), "=r"(_a3)
                     : "r"(_a0), "r"(_a1), "r"(_a2)
                     : _MIPS_CLOBBERS);
    return _a3 ? -_v0 : _v0;
}
ALWAYS_INLINE static inline long os_close(int fd) {
    register long _v0 __asm__("$2") = _NR_CLOSE;
    register long _a0 __asm__("$4") = fd;
    register long _a3 __asm__("$7");
    __asm__ volatile("syscall"
                     : "+r"(_v0), "=r"(_a3)
                     : "r"(_a0)
                     : _MIPS_CLOBBERS);
    return _a3 ? -_v0 : _v0;
}
__attribute__((noreturn)) ALWAYS_INLINE static inline void os_exit(int status) {
    register long _v0 __asm__("$2") = _NR_EXIT;
    register long _a0 __asm__("$4") = status;
    __asm__ volatile("syscall" : : "r"(_v0), "r"(_a0));
    __builtin_unreachable();
}
ALWAYS_INLINE static inline void *os_brk(void *addr) {
    register long _v0 __asm__("$2") = _NR_BRK;
    register long _a0 __asm__("$4") = (long)addr;
    register long _a3 __asm__("$7");
    __asm__ volatile("syscall"
                     : "+r"(_v0), "=r"(_a3)
                     : "r"(_a0)
                     : _MIPS_CLOBBERS);
    return _a3 ? (void *)-1 : (void *)_v0;
}

#else
#error "os.h: unsupported os/arch combination"
#endif

#endif /* OS_H */
