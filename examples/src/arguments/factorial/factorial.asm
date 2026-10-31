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


# C source — see factorial.c
# Invocation: spimulator -f factorial.asm 5


#PURPOSE:  Compute factorial(N) where N comes from argv[1].  First
#          demo where a numeric command-line argument drives the
#          calculation.  Demonstrates the atoi pattern (string ->
#          int) and tight `mult`+`mflo` iteration.
#
#NOTES:    At N > 12 the result overflows 32 bits and prints as
#          gibberish — a teaching observation in its own right.
#          (The C version overflows the same way.)
#
#          STACK-VISIBLE STYLE: main keeps its C locals (n, result)
#          in stack slots, not registers.  A scratch register holds
#          only a working COPY while an instruction needs it, then
#          the result is stored straight back to the slot.  Read the
#          memory-addr column of the symbol table as "where this C
#          variable lives"; the register column is "which temporary
#          is holding a copy of it right now".  (atoi and print_uint
#          are shared leaf helpers, kept register-based — see their
#          own tables.)
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type       | bytes | role / notes
#  ------------+-------------+------------+--------------+-------+---------------------------
#  argc        | —           | $a0        | int          |   4   | entry only; tested, then dead
#  argv        | —           | $a1        | char **      |   4   | entry only; argv[1] fetched
#  n           | -12($fp)    | $t0 (copy) | int          |   4   | loop counter; home on stack
#  result      | -16($fp)    | $t1 (copy) | unsigned int |   4   | running product
#  (saved $ra) | -4($fp)     | —          | code addr    |   4   | return into the runtime
#  (saved $fp) | -8($fp)     | —          | frame ptr    |   4   | caller's frame pointer
#
# ------------------------------------------------------------------
#  SNAPSHOT (input: factorial 5, at factorial_loop pass 3,
#            just after n and result are loaded for the multiply)
#
#          main's stack frame                 scratch registers
#     +--------------------------+           +----------------------+
#     | -4($fp)  saved $ra       |           | $t0 =  3  (copy of n)
#     | -8($fp)  saved $fp       |           | $t1 = 20  (copy of result)
#     | -12($fp) n       =   3   | <-------- | $t0
#     | -16($fp) result  =  20   | <-------- | $t1
#     +--------------------------+           +----------------------+
#           $sp = $fp - 16
#
#     Next `mult $t1,$t0; mflo $t1` makes $t1 = 60; the following
#     `sw $t1,-16($fp)` writes 60 back to result's slot, then n is
#     decremented to 2 and stored.  Passes run n = 5,4,3,2 with
#     result = 5,20,60,120; at n = 1 the loop exits with result 120.
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  args: 5
#  stdout:
#  | 120

        .data
usageMsg:   .asciiz "usage: factorial N\n"
digitsBuf:  .space 16

        .text
        .globl main
main:
        # ---- prologue: build main's 16-byte stack frame ------------
        addiu $sp, $sp, -16          # allocate the frame
        sw $ra, 12($sp)              # save $ra          -> -4($fp)
        sw $fp, 8($sp)               # save caller's $fp -> -8($fp)
        addiu $fp, $sp, 16           # $fp = top of our frame (a fixed
                                     #  base: every local is -N($fp))

        # if (argc != 2) usage;   (argc arrives in $a0)
        li $t0, 2
        bne $a0, $t0, usage

        # n = atoi(argv[1]);      (argv arrives in $a1)
        lw $a0, 4($a1)               # $a0 = argv[1]  (pointers are 4 bytes)
        jal atoi                     # $v0 = parsed int (atoi clobbers $ra,
                                     #  but our $ra is safe in its stack slot)
        sw $v0, -12($fp)             # n = $v0

        # result = 1;
        li $t0, 1
        sw $t0, -16($fp)             # result = 1

factorial_loop:
        # while (n > 1) { result = result * n; n = n - 1; }
        lw $t0, -12($fp)             # $t0 = n            (copy out to test)
        li $t1, 1
        ble $t0, $t1, factorial_done
        lw $t1, -16($fp)             # $t1 = result       (copy out to multiply)
        mult $t1, $t0                # result * n
        mflo $t1
        sw $t1, -16($fp)             # result = result * n (store back)
        addiu $t0, $t0, -1           # n - 1
        sw $t0, -12($fp)             # n = n - 1           (store back)
        j factorial_loop

factorial_done:
        # print_uint(result);
        lw $a0, -16($fp)             # $a0 = result
        jal print_uint

        # print_char('\n');
        li $v0, 11
        li $a0, '\n'
        syscall

        # ---- epilogue: tear down the frame, return to the runtime --
        li $v0, 0                    # exit status (runtime passes to syscall 17)
        lw $ra, -4($fp)              # restore $ra
        lw $fp, -8($fp)              # restore caller's $fp
        addiu $sp, $sp, 16           # pop the frame
        jr $ra

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        # Real exit-1 needs syscall 17 (jr $ra exits 0 in spim — see
        # nologin's NOTES for the reasoning).
        li $a0, 1
        li $v0, 17
        syscall


# ---------- atoi subroutine -----------------------------------------
# Shared leaf helper (string -> signed int), kept register-based: it
# takes its arg in $a0, returns in $v0, and calls nothing, so it needs
# no frame.  Repeated per demo by the self-contained convention.
#
#  SYMBOL TABLE (routine: atoi — leaf helper, register-based)
#
#  C variable | memory addr | register | C type | bytes | role / notes
#  -----------+-------------+----------+--------+-------+------------------------------
#  (arg) str  | —           | $a0      | char * |   4   | address of the string, walked
#  value      | —           | $v0      | int    |   4   | accumulator; becomes the return
#  sign       | —           | $t1      | int    |   4   | +1 or -1
#  digit      | —           | $t0      | int    |   4   | one byte decoded to 0..9
#  ten        | —           | $t2      | int    |   4   | constant multiplier
#
# Input:  $a0 = address of null-terminated decimal string
# Output: $v0 = parsed value
# Trashes: $t0..$t2
atoi:
        li $v0, 0                    # value = 0  (accumulator)
        li $t1, 1                    # sign = +1

        lb $t0, ($a0)
        bne $t0, '-', atoi_loop
        li $t1, -1                   # negative
        addi $a0, $a0, 1

atoi_loop:
        lb $t0, ($a0)
        blt $t0, '0', atoi_done
        bgt $t0, '9', atoi_done
        addi $t0, $t0, -48           # digit value (char - '0')
        # value = value * 10 + digit
        li $t2, 10
        mult $v0, $t2
        mflo $v0
        add $v0, $v0, $t0
        addi $a0, $a0, 1
        j atoi_loop

atoi_done:
        mult $v0, $t1                # apply sign
        mflo $v0
        jr $ra


# ---------- print_uint subroutine -----------------------------------
# Shared leaf helper.  Same shape as cksum's print_uint.  Avoids
# $t1 collisions by using $t9 for the running value.
#
#  SYMBOL TABLE (routine: print_uint — leaf helper, register-based)
#
#  C variable | memory addr      | register | C type       | bytes | role / notes
#  -----------+------------------+----------+--------------+-------+----------------------
#  (arg) n    | —                | $a0->$t9 | unsigned int |   4   | value, divided down to 0
#  base       | —                | $t3      | int          |   4   | constant 10
#  ptr        | &digitsBuf[..]   | $t2      | char *       |   4   | write cursor (into .data)
#  digit      | —                | $t0      | int          |   4   | one remainder, 0..9
#  digitsBuf  | digitsBuf (.data)|  —       | char[16]     |  16   | ASCII scratch buffer
#
# Input:  $a0 = unsigned value to print
print_uint:
        la $t0, digitsBuf
        addi $t9, $t0, 16
        sb $0, ($t9)
        move $t9, $a0
        addi $t2, $t0, 15
        li $t3, 10
pu_loop:
        divu $t9, $t3
        mfhi $t0
        mflo $t9
        addi $t0, $t0, 48
        sb $t0, ($t2)
        beq $t9, $0, pu_done
        addi $t2, $t2, -1
        j pu_loop
pu_done:
        move $a0, $t2
        li $v0, 4
        syscall
        jr $ra
