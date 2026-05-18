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


# C source — see 16-cat.c
#
#     __attribute__((noreturn)) void _start(void) {
#       static char buf[4096];
#       long n;
#       while ((n = os_read(STDIN, buf, sizeof(buf))) > 0)
#         os_write(STDOUT, buf, n);
#       os_exit(n < 0 ? 1 : 0);
#     }


#PURPOSE:  Copy stdin → stdout in 4 KiB chunks.  First demo with
#          block I/O via the read (14) and write (15) syscalls.
#          Unlike read_char (12), these take a buffer + length and
#          return the actual bytes transferred — and they signal
#          EOF as a return value of 0, so no sentinel character is
#          needed.
#
#VARIABLES:
#   $t0   bytes-read count, saved between read and write
#   $a0   syscall arg 1: fd
#   $a1   syscall arg 2: buffer address
#   $a2   syscall arg 3: length
#   $v0   syscall selector / return value
#           (14 = read, 15 = write; both take fd in $a0,
#            buffer in $a1, length in $a2; return bytes-actually-
#            transferred in $v0)

        .data
buf:    .space 4096                  # 4 KiB scratch buffer

        .text
        .globl main
main:
loop:
        # n = read(STDIN, buf, 4096);
        li $v0, 14                   # syscall 14 = read
        li $a0, 0                    # fd = 0 (stdin)
        la $a1, buf                  # arg = address of buf
        li $a2, 4096                 # arg = max bytes
        syscall                      # $v0 = bytes actually read

        blez $v0, end                # n <= 0 -> exit loop
        move $t0, $v0                # stash n before $v0 gets reused

        # write(STDOUT, buf, n);
        li $v0, 15                   # syscall 15 = write
        li $a0, 1                    # fd = 1 (stdout)
        la $a1, buf                  # arg = address of buf (same)
        move $a2, $t0                # arg = bytes-actually-read
        syscall

        j loop                       # next chunk

end:
        # $v0 is the failing-read return: 0 = EOF (clean), <0 = error.
        bltz $v0, error              # error -> exit 1
        li $v0, 0                    # EOF -> exit 0
        jr $ra

error:
        li $v0, 1
        jr $ra
