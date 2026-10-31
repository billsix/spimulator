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


# Invocations (sorted ints, one per line — see bubble-sort for why):
#   printf '1\n3\n5\n7\n9\n' | spimulator -f binary-search.asm 5
#   seq 1 100               | spimulator -f binary-search.asm 42
#   spimulator -f binary-search.asm 5 < sorted-ints.txt


#PURPOSE:  linear + binary search for TARGET on a sorted array read from stdin.
#          TARGET from argv[1].  Array up to 256 ints.  (The .asm reads the array
#          from stdin and tracks a runtime count n, unlike binary-search.c's
#          fixed static array — there is no shared golden.)
#
#          STACK-VISIBLE STYLE: main's C locals — the target and the count n of
#          ints read — live in stack slots; `n` is passed to the search routines
#          in $a1 (instead of a de-facto shared register).  Each search routine
#          homes its own cursor on its frame: linear_search's index `i`,
#          binary_search's `lo`/`hi` (the `mid` is a transient recomputed each
#          iteration).  The array `data` is file-scope, so it stays in .data.
#          main makes jals, so it saves $ra; the searches are leaf.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  target      | -12($fp)    | $a0 (copy) | int       |   4   | search key (argv[1]); passed in $a0
#  n           | -16($fp)    | $a1 (copy) | int       |   4   | count of ints read; passed in $a1
#  data[]      | data (.data)| —          | int[256]  | 1024  | file-scope array filled from stdin
#  (saved $ra) | -4($fp)     | —          | code addr |   4   | jal atoi / *_search / print_idx_or_nf
#  (saved $fp) | -8($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
#  SYMBOL TABLE (routine: linear_search — leaf; args target=$a0, n=$a1)
#    i -> -8($fp) (working copy in $t0); saved $fp -> -4($fp).
#  SYMBOL TABLE (routine: binary_search — leaf; args target=$a0, n=$a1)
#    lo -> -8($fp), hi -> -12($fp) (mid is a transient in $t3); saved $fp -> -4($fp).
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  args: 5
#  stdin: algorithms/binary-search/binary-search.input
#  stdout:
#  | linear: 2
#  | binary: 2

        .data
data:       .space 1024                  # 256 ints * 4 bytes
linMsg:     .asciiz "linear: "
binMsg:     .asciiz "binary: "
nfMsg:      .asciiz "not found"
usageMsg:   .asciiz "usage: binary-search TARGET   (sorted ints from stdin)\n"

        .text
        .globl main
main:
        addiu $sp, $sp, -16
        sw $ra, 12($sp)              # -> -4($fp)
        sw $fp, 8($sp)               # -> -8($fp)
        addiu $fp, $sp, 16

        li $t0, 2
        bne $a0, $t0, usage

        # target = atoi(argv[1])
        lw $a0, 4($a1)
        jal atoi
        sw $v0, -12($fp)             # target

        # Read ints from stdin into data[], counting n.
        li $t0, 0
        sw $t0, -16($fp)             # n = 0
read_loop:
        lw $t0, -16($fp)             # n
        li $t1, 256
        bge $t0, $t1, after_read
        li $v0, 5                    # read_int
        syscall
        bnez $a3, after_read         # EOF
        lw $t0, -16($fp)             # n
        sll $t1, $t0, 2
        la $t2, data
        add $t2, $t2, $t1
        sw $v0, ($t2)                # data[n] = value
        addi $t0, $t0, 1
        sw $t0, -16($fp)             # n++
        j read_loop

after_read:
        # ---- linear search ----
        li $v0, 4
        la $a0, linMsg
        syscall
        lw $a0, -12($fp)             # target
        lw $a1, -16($fp)             # n
        jal linear_search
        jal print_idx_or_nf

        # ---- binary search ----
        li $v0, 4
        la $a0, binMsg
        syscall
        lw $a0, -12($fp)             # target
        lw $a1, -16($fp)             # n
        jal binary_search
        jal print_idx_or_nf

        li $v0, 0                    # exit status: __start passes this through syscall 17
        lw $ra, -4($fp)
        lw $fp, -8($fp)
        addiu $sp, $sp, 16
        jr $ra

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall


# ---------- linear_search(target=$a0, n=$a1) -> $v0 (leaf) ----------
linear_search:
        addiu $sp, $sp, -8
        sw $fp, 4($sp)               # -> -4($fp)
        addiu $fp, $sp, 8
        li $t0, 0
        sw $t0, -8($fp)              # i = 0
ls_loop:
        lw $t0, -8($fp)              # i
        bge $t0, $a1, ls_no          # i >= n
        la $t1, data
        sll $t2, $t0, 2
        add $t3, $t1, $t2
        lw $t4, ($t3)                # data[i]
        beq $t4, $a0, ls_yes
        lw $t0, -8($fp)
        addi $t0, $t0, 1
        sw $t0, -8($fp)              # i++
        j ls_loop
ls_yes:
        lw $v0, -8($fp)              # return i
        lw $fp, -4($fp)
        addiu $sp, $sp, 8
        jr $ra
ls_no:
        li $v0, -1
        lw $fp, -4($fp)
        addiu $sp, $sp, 8
        jr $ra


# ---------- binary_search(target=$a0, n=$a1) -> $v0 (leaf) ----------
binary_search:
        addiu $sp, $sp, -16
        sw $fp, 12($sp)              # -> -4($fp)
        addiu $fp, $sp, 16
        li $t0, 0
        sw $t0, -8($fp)              # lo = 0
        addi $t0, $a1, -1            # hi = n - 1
        sw $t0, -12($fp)
bs_loop:
        lw $t1, -8($fp)              # lo
        lw $t2, -12($fp)             # hi
        bgt $t1, $t2, bs_no
        add $t3, $t1, $t2
        srl $t3, $t3, 1              # mid = (lo+hi)/2  (transient)
        la $t0, data
        sll $t4, $t3, 2
        add $t4, $t0, $t4
        lw $t5, ($t4)                # data[mid]
        beq $t5, $a0, bs_yes
        bgt $t5, $a0, bs_high
        addi $t1, $t3, 1             # data[mid] < target: lo = mid+1
        sw $t1, -8($fp)
        j bs_loop
bs_high:
        addi $t2, $t3, -1            # hi = mid-1
        sw $t2, -12($fp)
        j bs_loop
bs_yes:
        move $v0, $t3                # return mid
        lw $fp, -4($fp)
        addiu $sp, $sp, 16
        jr $ra
bs_no:
        li $v0, -1
        lw $fp, -4($fp)
        addiu $sp, $sp, 16
        jr $ra


# ---------- print_idx_or_nf — print $v0 as int, or "not found", + '\n' (leaf) ----------
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


# ---------- atoi subroutine (shared leaf helper, register-based) ----
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
