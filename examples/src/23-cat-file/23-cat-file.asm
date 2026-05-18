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


# C source — see 23-cat-file.c
#
#     int my_main(int argc, char **argv) {
#       if (argc != 2) { print_string("usage: cat-file FILE\n"); return 1; }
#       int fd = open(argv[1], O_RDONLY, 0);
#       if (fd < 0) { print_string("cat-file: cannot open ");
#                     print_string(argv[1]); print_char('\n'); return 1; }
#       char buf[4096];
#       long n;
#       while ((n = read(fd, buf, 4096)) > 0) write(STDOUT, buf, n);
#       close(fd);
#       return n < 0 ? 1 : 0;
#     }
#
# Invocation:
#   spimulator -f 23-cat-file.asm /etc/motd


#PURPOSE:  cat the contents of argv[1] to stdout.  First demo to
#          combine argv with file I/O: the hardcoded path in
#          17-nologin becomes a runtime-supplied one via argv[1].
#          Otherwise the loop body is byte-for-byte the
#          open/read/write/close pattern from 17-nologin and the
#          block-I/O shape of 16-cat.
#
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     argc          $a0 at entry only      (checked vs 2, then $a0 is
#                                            reused as a syscall arg)
#     argv          $s1                    (parked on entry; used to
#                                            fetch argv[1] for the open
#                                            and again on the open-error
#                                            path for the error message)
#     fd            $t0                    (return from open; held
#                                            across the read/write loop
#                                            AND the close syscall —
#                                            same long-lived-$t pattern
#                                            as 17-nologin)
#     n             $t1                    (bytes-read return; held
#                                            across the write syscall)
#     buf           `buf` (.data)          (4 KiB scratch read buffer)
#
#   Cross-call saves (callee-save $s* values held LIVE across `jal`):
#     $s0   <- runtime's $ra       (saved on entry; not strictly
#                                   load-bearing here since every exit
#                                   path uses syscall 17, but kept for
#                                   the uniform pattern — see 18-cksum
#                                   and 20-factorial for the case where
#                                   $s0 actually carries the runtime's
#                                   $ra back to exit cleanly)
#     $s1   <- argv                (held across every syscall in the
#                                   loop so the open-error path can
#                                   still print argv[1] — by the time
#                                   we'd want it from $a1, $a1 has
#                                   long since been reused for other
#                                   syscalls)
#
#   Volatile (no preserved meaning across a syscall, by convention):
#     $a0   syscall arg 1 (path / fd)
#     $a1   syscall arg 2 (flags / buffer)
#     $a2   syscall arg 3 (mode / length)
#     $v0   syscall selector / return value
#           (4=print_string, 13=open, 14=read,
#            15=write, 16=close, 17=exit2)

        .data
usageMsg:   .asciiz "usage: cat-file FILE\n"
openErr1:   .asciiz "cat-file: cannot open "
newline:    .asciiz "\n"
buf:        .space 4096

        .text
        .globl main
main:
        move $s0, $ra
        move $s1, $a1                # argv

        # if (argc != 2) usage; exit 1
        li $t0, 2
        bne $a0, $t0, usage

        # fd = open(argv[1], O_RDONLY, 0)
        lw $a0, 4($s1)               # argv[1]
        li $v0, 13                   # 13 = open
        li $a1, 0                    # O_RDONLY
        li $a2, 0                    # mode (ignored)
        syscall                      # $v0 = fd (negative on error)

        bltz $v0, open_failed
        move $t0, $v0                # stash fd

read_loop:
        # n = read(fd, buf, 4096)
        li $v0, 14                   # 14 = read
        move $a0, $t0
        la $a1, buf
        li $a2, 4096
        syscall

        blez $v0, close_and_exit     # 0 = EOF (clean), <0 = error
        move $t1, $v0                # stash n

        # write(STDOUT, buf, n)
        li $v0, 15                   # 15 = write
        li $a0, 1                    # fd = stdout
        la $a1, buf
        move $a2, $t1
        syscall

        j read_loop

close_and_exit:
        # close(fd)
        li $v0, 16
        move $a0, $t0
        syscall

        # exit2(0) — clean EOF.  syscall 17 because `jr $ra` always
        # exits 0 (which is what we want here) but we keep the same
        # explicit-exit2 pattern as 17-nologin so the path is uniform.
        li $a0, 0
        li $v0, 17
        syscall

open_failed:
        # print "cat-file: cannot open " + argv[1] + "\n"
        li $v0, 4
        la $a0, openErr1
        syscall

        lw $a0, 4($s1)               # argv[1]
        li $v0, 4
        syscall

        li $v0, 4
        la $a0, newline
        syscall

        li $a0, 1
        li $v0, 17                   # exit2(1)
        syscall

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17                   # exit2(1)
        syscall
