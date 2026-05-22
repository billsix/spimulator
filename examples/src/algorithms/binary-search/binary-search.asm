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


# C source — see binary-search.c
#
#     int my_main(int argc, char **argv) {
#       int target = parse_int(argv[1]);
#       int data[256], n = 0, value;
#       while (n < 256 && read_int_from_stdin(&value) == 0)
#         data[n++] = value;
#       print_result("linear: ", linear_search(data, n, target));
#       print_result("binary: ", binary_search(data, n, target));
#     }


#PURPOSE:  linear + binary search for TARGET on a sorted array
#          read from stdin.  TARGET from argv[1].  Array up to
#          256 ints.  Same stdin-int-reader pattern as
#          bubble-sort.


#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     target        $s1                  (parsed argv[1])
#     n             $s2                  (count of ints read)
#     data          `data` (.data)       (256 .word cells)

        .data
data:       .space 1024                  # 256 ints * 4 bytes
linMsg:     .asciiz "linear: "
binMsg:     .asciiz "binary: "
nfMsg:      .asciiz "not found"
usageMsg:   .asciiz "usage: binary-search TARGET   (sorted ints from stdin)\n"

        .text
        .globl main
main:
        move $s0, $ra

        li $t0, 2
        bne $a0, $t0, usage

        # target = atoi(argv[1])
        lw $a0, 4($a1)
        jal atoi
        move $s1, $v0

        # Read ints from stdin
        li $s2, 0                    # n = 0

read_loop:
        li $t0, 256
        bge $s2, $t0, after_read

        li $v0, 5                    # read_int
        syscall
        bnez $a3, after_read         # EOF

        sll $t0, $s2, 2
        la $t1, data
        add $t1, $t1, $t0
        sw $v0, ($t1)
        addi $s2, $s2, 1
        j read_loop

after_read:
        # ---- linear search ----
        li $v0, 4
        la $a0, linMsg
        syscall

        move $a0, $s1
        jal linear_search
        jal print_idx_or_nf

        # ---- binary search ----
        li $v0, 4
        la $a0, binMsg
        syscall

        move $a0, $s1
        jal binary_search
        jal print_idx_or_nf

        move $ra, $s0
        li $v0, 0                    # exit status: __start passes this through syscall 17
        jr $ra

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall


# ---------- linear_search($a0 = target) -> $v0 ----------
linear_search:
        la $t0, data
        li $t1, 0                    # i
ls_loop:
        bge $t1, $s2, ls_no
        sll $t2, $t1, 2
        add $t3, $t0, $t2
        lw $t4, ($t3)
        beq $t4, $a0, ls_yes
        addi $t1, $t1, 1
        j ls_loop
ls_yes:
        move $v0, $t1
        jr $ra
ls_no:
        li $v0, -1
        jr $ra


# ---------- binary_search($a0 = target) -> $v0 ----------
binary_search:
        la $t0, data
        li $t1, 0                    # lo
        addi $t2, $s2, -1            # hi = n - 1
bs_loop:
        bgt $t1, $t2, bs_no

        add $t3, $t1, $t2
        srl $t3, $t3, 1              # mid = (lo+hi)/2
        sll $t4, $t3, 2
        add $t4, $t0, $t4
        lw $t5, ($t4)                # data[mid]

        beq $t5, $a0, bs_yes
        bgt $t5, $a0, bs_high
        addi $t1, $t3, 1
        j bs_loop
bs_high:
        addi $t2, $t3, -1
        j bs_loop
bs_yes:
        move $v0, $t3
        jr $ra
bs_no:
        li $v0, -1
        jr $ra


# ---------- print_idx_or_nf — print $v0 as int, or "not found" + '\n'
print_idx_or_nf:
        bltz $v0, pin_nf
        move $a0, $v0
        li $v0, 1
        syscall
        j pin_nl
pin_nf:
        li $v0, 4
        la $a0, nfMsg
        syscall
pin_nl:
        li $v0, 11
        li $a0, '\n'
        syscall
        jr $ra


# ---------- atoi subroutine -----------------------------------------
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
