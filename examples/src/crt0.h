/* crt0.h — Multi-arch _start crt0 shim for argv-using demos.
 *
 * The Linux kernel hands _start the argv vector directly on the
 * stack with no preceding return address.  The C calling
 * convention can't express that layout, so this header provides
 * a tiny per-arch `_start` written in inline assembly that
 *
 *   1. pulls argc and argv off the kernel-supplied stack,
 *   2. calls `int my_main(int argc, char **argv)`, and
 *   3. exits the process with my_main's return value as the
 *      status.
 *
 * This is the same trick a minimal libc's crt0 does, just
 * inline here so the demo can stay -nostdlib.
 *
 * Demos use this by:
 *
 *     #include "io.h"
 *     #include "crt0.h"   // provides _start; calls my_main(argc, argv)
 *
 *     int my_main(int argc, char **argv) { ... return 0; }
 *
 * The reference implementation is the inline 5-arch version in
 * /pgu/src/c/toupper-nomm-simplified.c; if anything below
 * disagrees with that file, the /pgu version is canonical.
 */

#ifndef CRT0_H
#define CRT0_H

#if defined(__i386__)
__asm__(
    ".global _start\n"
    "_start:\n"
    "    movl  (%esp), %eax\n"     /* argc                       */
    "    leal  4(%esp), %edx\n"    /* &argv                      */
    "    pushl %edx\n"             /* push argv (2nd cdecl arg)  */
    "    pushl %eax\n"             /* push argc (1st cdecl arg)  */
    "    call  my_main\n"
    "    movl  %eax, %ebx\n"       /* status                     */
    "    movl  $1, %eax\n"         /* NR_exit (i386)             */
    "    int   $0x80\n");
#elif defined(__x86_64__)
__asm__(
    ".global _start\n"
    "_start:\n"
    "    movq  (%rsp), %rdi\n"     /* argc                       */
    "    leaq  8(%rsp), %rsi\n"    /* &argv                      */
    "    call  my_main\n"
    "    movl  %eax, %edi\n"       /* status                     */
    "    movl  $60, %eax\n"        /* NR_exit (x86_64)           */
    "    syscall\n");
#elif defined(__arm__)
__asm__(
    ".global _start\n"
    "_start:\n"
    "    ldr   r0, [sp]\n"         /* argc                       */
    "    add   r1, sp, #4\n"       /* &argv                      */
    "    bl    my_main\n"
    "    mov   r7, #1\n"           /* NR_exit (arm EABI)         */
    "    svc   #0\n");
#elif defined(__aarch64__)
__asm__(
    ".global _start\n"
    "_start:\n"
    "    ldr   w0, [sp]\n"         /* argc (32-bit; zero-extend) */
    "    add   x1, sp, #8\n"       /* &argv                      */
    "    bl    my_main\n"
    "    mov   x8, #93\n"          /* NR_exit (aarch64)          */
    "    svc   #0\n");
#elif defined(__mips__)
__asm__(
    ".global _start\n"
    "_start:\n"
    "    lw    $4, 0($29)\n"       /* $a0 = argc                 */
    "    addiu $5, $29, 4\n"       /* $a1 = &argv                */
    "    jal   my_main\n"
    "    nop\n"                    /* branch-delay slot          */
    "    move  $4, $2\n"           /* status = my_main return    */
    "    li    $2, 4001\n"         /* NR_exit (o32: 4000 + 1)    */
    "    syscall\n");
#else
#error "crt0.h: add a _start shim for this architecture"
#endif

#endif /* CRT0_H */
