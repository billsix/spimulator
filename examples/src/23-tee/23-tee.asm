# Copyright (c) 2021-2026 William Emerison Six
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.


# C source — see 23-tee.c
#
#     int my_main(int argc, char **argv) {
#       if (argc - 1 > MAX_OUT) usage;
#       int fds[MAX_OUT]; int nfds = 0;
#       for (int i = 1; i < argc; i++) {
#         int fd = open(argv[i],
#                       O_WRONLY|O_CREAT|O_TRUNC, 0644);
#         if (fd < 0) error;
#         fds[nfds++] = fd;
#       }
#       char buf[4096];
#       long n;
#       while ((n = read(STDIN, buf, 4096)) > 0) {
#         write(STDOUT, buf, n);
#         for (int i = 0; i < nfds; i++) write(fds[i], buf, n);
#       }
#       for (int i = 0; i < nfds; i++) close(fds[i]);
#       return n < 0 ? 1 : 0;
#     }
#
# Invocation:
#   echo hello | spimulator -f 23-tee.asm a.txt b.txt


#PURPOSE:  Read stdin block-at-a-time, write each block to stdout
#          AND to every file named on the command line.  Capstone
#          of Phase C: variable argc, an fd array, and a per-block
#          fan-out write loop.
#
#NOTES:    Flags 577 = O_WRONLY|O_CREAT|O_TRUNC =
#          1 | 0x040 | 0x200 on Linux x86_64 (spim hands the flags
#          straight to the host's `open()`, so the host's
#          <fcntl.h> values are what matter here — NOT the values
#          a real MIPS-Linux kernel would expect, which differ).
#          Mode 420 = octal 0644 (rw-r--r--).
#
#          MAX_OUT = 8.  More than 8 named files is a usage error
#          rather than silent truncation of the file list.
#
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     argc          $s1                  (saved on entry from $a0)
#     argv          $s2                  (saved on entry from $a1)
#     i (open loop) $s3                  (1..argc-1; the argv index
#                                          driving the open-each-file loop)
#     nfds          $s4                  (count of fds collected; doubles
#                                          as the next slot to write into
#                                          fdArray)
#     j (fan loop)  $s6                  (0..nfds-1; the inner index
#                                          walking fdArray on each block;
#                                          reused for the close loop too)
#     n (read)      $s7                  (bytes-read return from the
#                                          most recent read syscall;
#                                          held all the way to exit_decide
#                                          where its sign decides the
#                                          process exit status)
#     fds[]         `fdArray` (.data)    (8 .word entries — the open()
#                                          return values, indexed by j)
#     buf           `buf` (.data)        (4 KiB scratch read buffer)
#     &fds[i]       $t0                  (transient address computation,
#                                          (la fdArray) + (i << 2))
#     fdArray base  $t1                  (transient for the same address
#                                          calc)
#
#   Cross-call saves: NONE.
#
#     This demo issues no `jal` instructions at all — every call is
#     a syscall.  Spim's runtime preserves caller state across
#     `syscall` in practice, but the convention is to treat
#     volatiles ($t*, $a*, $v0) as clobbered.  Hence everything
#     that has to live longer than one syscall is parked in $s*
#     (i.e. EVERYTHING that the C source declares as a local —
#     argc, argv, the two loop indices, nfds, and n).  This is
#     the cleanest demo for showing "the $s* registers are how
#     the demo's state outlives every syscall it issues."
#
#   Volatile (no preserved meaning across a syscall):
#     $a0   syscall arg 1
#     $a1   syscall arg 2
#     $a2   syscall arg 3
#     $v0   syscall selector / return value
#     $t0, $t1  address-arithmetic scratch (fdArray element-addr
#                                            computation in both the
#                                            open and the fan loops)

        .data
usageMsg:   .asciiz "tee: too many output files\n"
openErr1:   .asciiz "tee: cannot open "
newline:    .asciiz "\n"
fdArray:    .space 32                    # MAX_OUT * 4 bytes
buf:        .space 4096

        .text
        .globl main
main:
        move $s0, $ra
        move $s1, $a0                    # argc
        move $s2, $a1                    # argv

        # if (argc - 1 > 8) usage
        addi $t0, $s1, -1
        li $t1, 8
        bgt $t0, $t1, too_many

        # for (i = 1, nfds = 0; i < argc; i++) open argv[i]
        li $s3, 1
        li $s4, 0
open_loop:
        bge $s3, $s1, open_done

        sll $t0, $s3, 2
        add $t0, $s2, $t0                # &argv[i]
        lw $a0, ($t0)                    # argv[i]

        li $v0, 13                       # 13 = open
        li $a1, 577                      # O_WRONLY|O_CREAT|O_TRUNC
        li $a2, 420                      # mode 0644
        syscall                          # $v0 = fd (negative on error)

        bltz $v0, open_failed

        # fdArray[nfds] = fd
        sll $t0, $s4, 2
        la $t1, fdArray
        add $t0, $t1, $t0
        sw $v0, ($t0)

        addi $s4, $s4, 1
        addi $s3, $s3, 1
        j open_loop

open_done:
read_loop:
        # n = read(STDIN, buf, 4096)
        li $v0, 14
        li $a0, 0                        # fd = stdin
        la $a1, buf
        li $a2, 4096
        syscall
        move $s7, $v0                    # remember return for exit decision
        blez $s7, close_all              # 0 = EOF, <0 = error

        # write(STDOUT, buf, n)
        li $v0, 15
        li $a0, 1
        la $a1, buf
        move $a2, $s7
        syscall

        # for (j = 0; j < nfds; j++) write(fdArray[j], buf, n)
        li $s6, 0
fan_loop:
        bge $s6, $s4, read_loop
        sll $t0, $s6, 2
        la $t1, fdArray
        add $t0, $t1, $t0
        lw $a0, ($t0)
        li $v0, 15
        la $a1, buf
        move $a2, $s7
        syscall
        addi $s6, $s6, 1
        j fan_loop

close_all:
        # for (j = 0; j < nfds; j++) close(fdArray[j])
        li $s6, 0
close_loop:
        bge $s6, $s4, exit_decide
        sll $t0, $s6, 2
        la $t1, fdArray
        add $t0, $t1, $t0
        lw $a0, ($t0)
        li $v0, 16
        syscall
        addi $s6, $s6, 1
        j close_loop

exit_decide:
        # exit 0 on clean EOF, 1 on read error
        bltz $s7, exit_err
        move $ra, $s0
        jr $ra
exit_err:
        li $a0, 1
        li $v0, 17
        syscall

open_failed:
        li $v0, 4
        la $a0, openErr1
        syscall

        sll $t0, $s3, 2
        add $t0, $s2, $t0
        lw $a0, ($t0)                    # argv[i] (the one that failed)
        li $v0, 4
        syscall

        li $v0, 4
        la $a0, newline
        syscall

        li $a0, 1
        li $v0, 17                       # exit2(1)
        syscall

too_many:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall
