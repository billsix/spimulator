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


# C source — see 11-head.c
#
#     int my_main(int argc, char **argv) {
#       int fd = STDIN, n = 10;
#       const char *file_arg = NULL;
#       switch (argc) {
#         case 1: break;
#         case 2: file_arg = argv[1]; break;
#         case 3: if (!str_eq(argv[1], "-n")) usage;
#                 n = parse_int(argv[2]); break;
#         case 4: if (!str_eq(argv[1], "-n")) usage;
#                 n = parse_int(argv[2]); file_arg = argv[3]; break;
#         default: usage;
#       }
#       if (file_arg && !is_dash(file_arg)) fd = open(file_arg);
#       int line_count = 0;
#       char c;
#       while (line_count < n && read(fd, &c, 1) > 0) {
#         print_char(c);
#         if (c == '\n') line_count++;
#       }
#       if (fd != STDIN) close(fd);
#       return 0;
#     }


#PURPOSE:  head with real-Unix argv handling.  Accepts the
#          full `head [-n N] [FILE|-]` shape.  Subsumes both the
#          old "head" (stdin, hardcoded N=10) and the old
#          "head-file" (-n N FILE) into a single demo.


#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     fd            $s1                  (STDIN=0 or opened fd)
#     n             $s2                  (line limit, default 10)
#     file_arg      $s3                  (NULL or pointer to argv[i])
#     argv          $s4                  (entry $a1, kept across calls)
#     line_count    $s5                  (0..n)
#     c             `oneByte` (.data)    (1-byte read pad)
#
#   In str_eq:  $a0, $a1 = pointers; $v0 = 1 if equal, 0 if not.
#   In atoi:    $a0 = pointer; $v0 = parsed int.

        .data
usageMsg:   .asciiz "usage: head [-n N] [FILE|-]\n"
errMsg:     .asciiz "head: cannot open "
nlMsg:      .asciiz "\n"
flagN:      .asciiz "-n"
oneByte:    .space 1

        .text
        .globl main
main:
        move $s0, $ra
        li $s1, 0                    # fd = STDIN
        li $s2, 10                   # n = 10
        move $s3, $0                 # file_arg = NULL
        move $s4, $a1                # argv

        # Dispatch on argc.
        li $t0, 1
        beq $a0, $t0, open_if_needed   # argc==1 -> stdin, n=10

        li $t0, 2
        beq $a0, $t0, argc_two

        li $t0, 3
        beq $a0, $t0, argc_three

        li $t0, 4
        beq $a0, $t0, argc_four

        j usage

argc_two:
        # head FILE_OR_DASH
        lw $s3, 4($s4)               # file_arg = argv[1]
        j open_if_needed

argc_three:
        # head -n N
        lw $a0, 4($s4)               # argv[1]
        la $a1, flagN
        jal str_eq
        beqz $v0, usage
        lw $a0, 8($s4)               # argv[2]
        jal atoi
        move $s2, $v0
        j open_if_needed

argc_four:
        # head -n N FILE_OR_DASH
        lw $a0, 4($s4)
        la $a1, flagN
        jal str_eq
        beqz $v0, usage
        lw $a0, 8($s4)
        jal atoi
        move $s2, $v0
        lw $s3, 12($s4)              # file_arg = argv[3]
        # fall through

open_if_needed:
        beqz $s3, read_setup         # no file_arg -> stdin
        lb $t0, 0($s3)
        bne $t0, '-', do_open
        lb $t0, 1($s3)
        beq $t0, $0, read_setup      # file_arg is "-" -> stdin

do_open:
        move $a0, $s3
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_failed
        move $s1, $v0

read_setup:
        li $s5, 0                    # line_count = 0

read_loop:
        # while (line_count < n)
        bge $s5, $s2, read_done
        # read(fd, &oneByte, 1)
        li $v0, 14
        move $a0, $s1
        la $a1, oneByte
        li $a2, 1
        syscall
        blez $v0, read_done

        # print_char(c)
        lb $a0, oneByte
        li $v0, 11
        syscall
        bne $a0, '\n', read_loop
        addi $s5, $s5, 1
        j read_loop

read_done:
        beqz $s1, exit_ok
        li $v0, 16
        move $a0, $s1
        syscall
exit_ok:
        move $ra, $s0
        jr $ra

open_failed:
        li $v0, 4
        la $a0, errMsg
        syscall
        li $v0, 4
        move $a0, $s3
        syscall
        li $v0, 4
        la $a0, nlMsg
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
# Returns $v0 = 1 if equal, 0 if not.
# Input:  $a0, $a1 = pointers
str_eq:
        lb $t0, ($a0)
        lb $t1, ($a1)
        bne $t0, $t1, str_eq_no
        beq $t0, $0, str_eq_yes
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
# Parse decimal int from $a0 (a NUL-terminated string).  $v0 = result.
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
