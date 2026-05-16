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


# C source — see 20-factorial-1.c
#
#     int my_main(int argc, char **argv) {
#       if (argc != 2) { print_string("usage: factorial N\n"); return 1; }
#       int n = parse_int(argv[1]);
#       print_uint(factorial(n));
#       print_char('\n');
#       return 0;
#     }
#
#     unsigned int factorial(int n) {
#       unsigned int result = 1;
#       while (n > 1) { result = result * n; n--; }
#       return result;
#     }
#
# Invocation: spimulator -f 20-factorial.asm 5


#PURPOSE:  Compute factorial(N) where N comes from argv[1].  First
#          demo where a numeric command-line argument drives the
#          calculation.  Demonstrates the atoi pattern (string ->
#          int) and tight `mult`+`mflo` iteration.
#
#NOTES:    At N > 12 the result overflows 32 bits and prints as
#          gibberish — a teaching observation in its own right.
#          (The C version overflows the same way.)
#
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     argc          $a0 at entry only      (clobbered after first syscall)
#     argv          $a1 at entry           (walked once to fetch argv[1])
#     n             $s1                    (parsed from argv[1] via atoi)
#     result        $s2                    (running factorial product)
#
#   In atoi subroutine:
#     value         $v0                    (accumulator -- becomes the return)
#     sign          $t1                    (+1 or -1)
#     digit         $t0                    (one byte, decoded to 0..9)
#     ten           $t2                    (constant multiplier)
#
#   In print_uint subroutine:
#     n             $t9                    (remaining int, divided down to 0)
#     base          $t3                    (constant 10)
#     ptr           $t2                    (write cursor into digitsBuf)
#     digit         $t0                    (one remainder, 0..9)
#     digitsBuf     `digitsBuf` (.data)    (16-byte scratch for ASCII digits)
#
#   Cross-call saves (callee-save $s* values held LIVE across `jal`):
#     $s0   <- runtime's $ra     (survives both `jal atoi` and
#                                 `jal print_uint`, restored before
#                                 the final `jr $ra` to exit cleanly)
#
#     n ($s1) and result ($s2) are also in callee-save registers,
#     but neither needs to span a `jal` in this demo — they're in $s*
#     by convention rather than necessity.  The next demo (22-gcd)
#     does need that survival, e.g. holding A across `jal atoi` while
#     parsing B.
#
#   Volatile (no preserved meaning across a syscall):
#     $a0   syscall arg / function arg
#     $v0   syscall selector / return value

        .data
usageMsg:   .asciiz "usage: factorial N\n"
digitsBuf:  .space 16

        .text
        .globl main
main:
        # Save $ra so our `jal print_uint` doesn't lose it.
        move $s0, $ra

        # if (argc != 2) usage; exit 1.
        li $t0, 2
        bne $a0, $t0, usage

        # n = atoi(argv[1])
        lw $a0, 4($a1)               # argv[1] (each pointer 4 bytes)
        jal atoi                     # $v0 = parsed int  (jal overwrites
                                     #  $ra; we stashed the runtime's $ra
                                     #  in $s0 above so the final `jr $ra`
                                     #  still works -- see the symbol
                                     #  table's Cross-call saves entry)
        move $s1, $v0                # save N

        # result = 1
        li $s2, 1

factorial_loop:
        # while (n > 1) { result *= n; n--; }
        li $t0, 1
        ble $s1, $t0, factorial_done
        mult $s2, $s1
        mflo $s2
        addi $s1, $s1, -1
        j factorial_loop

factorial_done:
        # print_uint(result)
        move $a0, $s2
        jal print_uint

        # print_char('\n')
        li $v0, 11
        li $a0, '\n'
        syscall

        move $ra, $s0
        jr $ra

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        # Real exit-1 needs syscall 17 (jr $ra exits 0 in spim — see
        # 15-nologin's NOTES for the reasoning).
        li $a0, 1
        li $v0, 17
        syscall


# ---------- atoi subroutine -----------------------------------------
# Parse a null-terminated decimal string into a signed int.
# Input:  $a0 = address of string
# Output: $v0 = parsed value
# Trashes: $t0..$t3
atoi:
        li $v0, 0                    # accumulator
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
# Same shape as 18-cksum's print_uint.  Avoids $s1/$s2 (which main
# uses for N and result) and $s0 (which holds the saved $ra).
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
