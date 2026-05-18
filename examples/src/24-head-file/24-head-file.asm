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


# C source — see 24-head-file.c
#
#     int my_main(int argc, char **argv) {
#       if (argc != 4 || !str_eq(argv[1], "-n")) usage;
#       int n  = parse_int(argv[2]);
#       int fd = open(argv[3], O_RDONLY, 0);
#       if (fd < 0) error;
#       int lines = 0; char c;
#       while (lines < n) {
#         if (read(fd, &c, 1) <= 0) break;
#         write(STDOUT, &c, 1);
#         if (c == '\n') lines++;
#       }
#       close(fd); return 0;
#     }
#
# Invocation:
#   spimulator -f 24-head-file.asm -n 5 /etc/passwd


#PURPOSE:  Read up to N lines from a file named on the command
#          line.  Combines three earlier patterns in one demo:
#          string-equality (08), atoi (20), and open/read/write/
#          close (15, 21).  Per-byte read loop with newline-counting
#          terminator mirrors 11-head.
#
#NOTES:    Per-byte read via syscall 14 with $a2 = 1 (rather than
#          read_char / syscall 12) is what lets us source the bytes
#          from an arbitrary fd.  Slower than block-I/O but lets us
#          stop precisely at the Nth newline without buffering past
#          it.
#
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     argc          $a0 at entry only
#     argv          $s1                  (saved on entry; argv[2], argv[3]
#                                          are computed off it after both
#                                          subroutine calls)
#     N             $s2                  (parsed line limit, from atoi)
#     fd            $s3                  (returned from the open syscall)
#     lines         $s4                  (running line count)
#     c (one byte)  `oneByte` (.data)    (the per-byte read landing pad;
#                                          loaded into $t0 to compare
#                                          against '\n')
#     -n            `flagN` (.data)      (the literal "-n" string to
#                                          match against argv[1])
#
#   In str_eq subroutine:
#     a (=*$a0)     $a0                  (advanced in place)
#     b (=*$a1)     $a1                  (advanced in place)
#     *a            $t0                  (one byte loaded each iteration)
#     *b            $t1                  (one byte loaded each iteration)
#     return value  $v0                  (1 if equal, 0 if not)
#
#   In atoi subroutine:
#     value         $v0                  (accumulator — becomes return)
#     sign          $t1                  (+1 or -1)
#     digit         $t0                  (one byte, decoded to 0..9)
#     ten           $t2                  (constant multiplier)
#
#   Cross-call saves (callee-save $s* values held LIVE across `jal`):
#     $s0   <- runtime's $ra     (across BOTH `jal str_eq` and `jal atoi`;
#                                 restored before the final `jr $ra`)
#     $s1   <- argv              (held across BOTH jals so argv[2] and
#                                 argv[3] are still reachable for atoi
#                                 and for the open syscall after that.
#                                 str_eq actively advances $a0/$a1 — if
#                                 argv weren't parked in $s1 we'd have
#                                 lost it by then.)
#
#   Volatile (no preserved meaning across a syscall):
#     $a0   syscall arg / function arg
#     $v0   syscall selector / function return value

        .data
usageMsg:   .asciiz "usage: head-file -n N FILE\n"
openErr1:   .asciiz "head-file: cannot open "
newline:    .asciiz "\n"
flagN:      .asciiz "-n"
oneByte:    .space 1

        .text
        .globl main
main:
        move $s0, $ra
        move $s1, $a1                # argv

        # if (argc != 4) usage
        li $t0, 4
        bne $a0, $t0, usage

        # if (!str_eq(argv[1], "-n")) usage
        lw $a0, 4($s1)               # argv[1]
        la $a1, flagN
        jal str_eq
        beqz $v0, usage              # 0 = not equal

        # n = atoi(argv[2])
        lw $a0, 8($s1)
        jal atoi
        move $s2, $v0                # save n

        # fd = open(argv[3], O_RDONLY, 0)
        lw $a0, 12($s1)
        li $v0, 13                   # 13 = open
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_failed
        move $s3, $v0                # save fd

        li $s4, 0                    # lines = 0

read_loop:
        bge $s4, $s2, close_and_exit # while (lines < n)

        # read(fd, &oneByte, 1)
        li $v0, 14
        move $a0, $s3
        la $a1, oneByte
        li $a2, 1
        syscall

        blez $v0, close_and_exit     # EOF or error

        # write(STDOUT, &oneByte, 1)
        li $v0, 15
        li $a0, 1
        la $a1, oneByte
        li $a2, 1
        syscall

        # if (c == '\n') lines++
        lb $t0, oneByte
        bne $t0, '\n', read_loop
        addi $s4, $s4, 1
        j read_loop

close_and_exit:
        # close(fd)
        li $v0, 16
        move $a0, $s3
        syscall

        # exit 0
        move $ra, $s0
        jr $ra

open_failed:
        li $v0, 4
        la $a0, openErr1
        syscall

        lw $a0, 12($s1)              # argv[3]
        li $v0, 4
        syscall

        li $v0, 4
        la $a0, newline
        syscall

        li $a0, 1
        li $v0, 17
        syscall

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall


# ---------- str_eq subroutine ---------------------------------------
# Returns $v0 = 1 if equal, 0 if not.  (Note: opposite polarity to
# the str_eq in 33-testStringsForEquality, which returns 0-for-equal
# to match the C book it came from.  Here the polarity reads more
# naturally at the call site.)
# Input:  $a0, $a1 = two null-terminated strings
# Output: $v0      = 1 if equal, else 0
str_eq:
        lb $t0, ($a0)
        lb $t1, ($a1)
        bne $t0, $t1, str_eq_no
        beq $t0, $0, str_eq_yes      # both ended at the same byte
        addi $a0, $a0, 1
        addi $a1, $a1, 1
        j str_eq
str_eq_yes:
        li $v0, 1
        jr $ra
str_eq_no:
        li $v0, 0
        jr $ra


# ---------- atoi subroutine -----------------------------------------
# Same shape as 20-factorial/21-gcd's atoi.
# Input:  $a0 = address of decimal string
# Output: $v0 = parsed signed int
atoi:
        li $v0, 0
        li $t1, 1
        lb $t0, ($a0)
        bne $t0, '-', atoi_loop
        li $t1, -1
        addi $a0, $a0, 1
atoi_loop:
        lb $t0, ($a0)
        blt $t0, '0', atoi_done
        bgt $t0, '9', atoi_done
        addi $t0, $t0, -48
        li $t2, 10
        mult $v0, $t2
        mflo $v0
        add $v0, $v0, $t0
        addi $a0, $a0, 1
        j atoi_loop
atoi_done:
        mult $v0, $t1
        mflo $v0
        jr $ra
