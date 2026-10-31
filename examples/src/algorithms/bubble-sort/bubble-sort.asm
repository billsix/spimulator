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


# C source — see bubble-sort.c


#PURPOSE:  Bubble sort N ints read from stdin.  N up to 256.  Uses syscall 5
#          (read_int) with the $a3 EOF flag; output is one int per line, sorted.
#
#INVOCATION (one int per line, pipe-friendly):
#    printf '10\n2\n11\n4\n' | spimulator -f bubble-sort.asm
#    spimulator -f bubble-sort.asm < nums.txt
#
#NOTES:    spim's read_int (syscall 5): $v0 = parsed int, $a3 = 0 ok / 1 EOF.
#
#          STACK-VISIBLE STYLE: the C locals `n` (count read), `i` (outer), and
#          `j` (inner) live in stack slots; a scratch register holds only a
#          working COPY (compared elements / addresses are transient).  The array
#          `data` is file-scope, so it stays in .data.  This demo makes NO `jal`
#          (only syscalls, which preserve $ra), so it saves $fp but not $ra.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  n           | -8($fp)     | $t8 (copy) | int       |   4   | count of ints read (also print bound)
#  i           | -12($fp)    | $t0 (copy) | int       |   4   | outer pass index (also the print index)
#  j           | -16($fp)    | $t3 (copy) | int       |   4   | inner comparison index
#  data[]      | data (.data)| —          | int[256]  | 1024  | file-scope array; sorted in place
#  (saved $fp) | -4($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  stdin: algorithms/bubble-sort/bubble-sort.input
#  stdout:
#  | 1
#  | 4
#  | 7
#  | 9

        .data
data:       .space 1024                  # 256 ints * 4 bytes
usageMsg:   .asciiz "usage: bubble-sort   (reads ints from stdin)\n"

        .text
        .globl main
main:
        addiu $sp, $sp, -16
        sw $fp, 12($sp)              # -> -4($fp)
        addiu $fp, $sp, 16

        # if (argc != 1) usage
        li $t0, 1
        bne $a0, $t0, usage

        # Read ints from stdin until EOF.
        li $t0, 0
        sw $t0, -8($fp)              # n = 0
read_loop:
        lw $t0, -8($fp)              # n
        li $t1, 256
        bge $t0, $t1, after_read     # cap at 256
        li $v0, 5                    # syscall 5 = read_int
        syscall
        bnez $a3, after_read         # $a3 = 1 -> EOF
        lw $t0, -8($fp)              # n
        sll $t1, $t0, 2
        la $t2, data
        add $t2, $t2, $t1
        sw $v0, ($t2)                # data[n] = value
        addi $t0, $t0, 1
        sw $t0, -8($fp)              # n++
        j read_loop

after_read:
        # bubble sort data[0 .. n)
        li $t0, 0
        sw $t0, -12($fp)             # i = 0
outer:
        lw $t0, -12($fp)             # i
        lw $t8, -8($fp)              # n
        addi $t1, $t8, -1            # n - 1
        bge $t0, $t1, after_sort     # for (i = 0; i < n-1; i++)
        li $t0, 0
        sw $t0, -16($fp)             # j = 0
inner:
        lw $t3, -16($fp)             # j
        lw $t8, -8($fp)              # n
        lw $t0, -12($fp)             # i
        sub $t2, $t8, $t0
        addi $t2, $t2, -1            # n - 1 - i
        bge $t3, $t2, next_outer     # for (j = 0; j < n-1-i; j++)
        sll $t4, $t3, 2
        la $t5, data
        add $t5, $t5, $t4            # &data[j]
        lw $t6, 0($t5)               # data[j]
        lw $t7, 4($t5)               # data[j+1]
        ble $t6, $t7, no_swap
        sw $t7, 0($t5)
        sw $t6, 4($t5)
no_swap:
        lw $t3, -16($fp)
        addi $t3, $t3, 1
        sw $t3, -16($fp)             # j++
        j inner
next_outer:
        lw $t0, -12($fp)
        addi $t0, $t0, 1
        sw $t0, -12($fp)             # i++
        j outer

after_sort:
        li $t0, 0
        sw $t0, -12($fp)             # reuse i's slot as the print index
print_loop:
        lw $t0, -12($fp)
        lw $t8, -8($fp)              # n
        bge $t0, $t8, print_done
        sll $t1, $t0, 2
        la $t2, data
        add $t2, $t2, $t1
        lw $a0, ($t2)
        li $v0, 1                    # print_int
        syscall
        li $a0, '\n'
        li $v0, 11                   # print_char
        syscall
        lw $t0, -12($fp)
        addi $t0, $t0, 1
        sw $t0, -12($fp)
        j print_loop

print_done:
        li $v0, 0                    # exit status: __start passes this through syscall 17
        lw $fp, -4($fp)
        addiu $sp, $sp, 16
        jr $ra

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall
