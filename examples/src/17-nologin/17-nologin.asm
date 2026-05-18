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


# C source — see 17-nologin.c
#
#     __attribute__((noreturn)) void _start(void) {
#       static char buf[256];
#       int fd = os_open("/etc/nologin.txt", OS_O_RDONLY, 0);
#       if (fd >= 0) {
#         long n;
#         while ((n = os_read(fd, buf, sizeof(buf))) > 0)
#           os_write(STDOUT, buf, n);
#         os_close(fd);
#       } else {
#         os_write(STDOUT, "The account is currently unavailable.\n", 38);
#       }
#       os_exit(1);
#     }


#PURPOSE:  Print /etc/nologin.txt if it exists, else a fallback
#          message.  Always exit 1.  First demo using the open
#          (13) and close (16) syscalls.
#
#NOTES:    Returning from `main` via `jr $ra` ALWAYS makes spim
#          exit with status 0 — the runtime's `__start` code in
#          exceptions.s does `li $v0, 10; syscall` after the call
#          and the syscall-10 handler ignores $v0 and stores 0.
#          To exit with any non-zero status from spim we have to
#          issue syscall 17 (exit2) ourselves and put the desired
#          status in $a0.  Both paths in this demo do exactly that.
#
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     fd            $t0                  (return from open; held across
#                                          the entire read/write loop
#                                          AND the close syscall)
#     n             $t1                  (bytes read; held across the
#                                          write syscall)
#     buf           `buf` (.data)        (256-byte read buffer)
#     path          `path` (.data)       ("/etc/nologin.txt" string)
#
#   No subroutine calls; no Cross-call saves section.  This is
#   the first demo where a $t-reg has to hold a value across more
#   than one syscall in a row — `fd` lives in $t0 through read,
#   write, AND close — which is why the in-body comment notes
#   "(saved across the read/write loop)".  Same spim-preserves-$t
#   reliance as 06/12/13/14.
#
#   Volatile (no preserved meaning across a syscall, by convention):
#     $a0   syscall arg 1 (path / fd / char)
#     $a1   syscall arg 2 (flags / buffer)
#     $a2   syscall arg 3 (mode / length)
#     $v0   syscall selector / return value
#           (4=print_string, 13=open, 14=read, 15=write, 16=close, 17=exit2)

        .data
path:           .asciiz     "/etc/nologin.txt"
altMessage:     .asciiz     "The account is currently unavailable.\n"
buf:            .space 256

        .text
        .globl main
main:
        # fd = open(path, O_RDONLY, 0);
        li $v0, 13                   # syscall 13 = open
        la $a0, path                 # arg = address of "/etc/nologin.txt"
        li $a1, 0                    # arg = O_RDONLY (= 0 on Linux)
        li $a2, 0                    # arg = mode (ignored for O_RDONLY)
        syscall                      # $v0 = fd (negative on error)

        bltz $v0, open_failed
        move $t0, $v0                # stash fd in $t0

read_loop:
        # n = read(fd, buf, 256);
        li $v0, 14                   # syscall 14 = read
        move $a0, $t0                # fd
        la $a1, buf
        li $a2, 256
        syscall                      # $v0 = bytes read

        blez $v0, close_and_exit     # 0 = EOF, <0 = error
        move $t1, $v0                # stash n

        # write(STDOUT, buf, n);
        li $v0, 15                   # syscall 15 = write
        li $a0, 1                    # fd = stdout
        la $a1, buf
        move $a2, $t1
        syscall

        j read_loop

close_and_exit:
        # close(fd);
        li $v0, 16
        move $a0, $t0
        syscall

        # exit2(1) — see NOTES above for why we use syscall 17 here
        # instead of `li $v0, 1; jr $ra`.
        li $a0, 1
        li $v0, 17
        syscall

open_failed:
        # print fallback string
        li $v0, 4                    # syscall 4 = print_string
        la $a0, altMessage
        syscall

        # exit2(1)
        li $a0, 1
        li $v0, 17
        syscall
