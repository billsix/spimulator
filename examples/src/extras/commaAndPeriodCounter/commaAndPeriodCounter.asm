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


# C source — see commaAndPeriodCounter.c


#PURPOSE:  Count the commas and periods on standard input and print a
#          summary line of the form   <N> commas, <M> stops
#
#NOTES:    `read_char` returns -1 at EOF; the loop branches on
#          `bltz ..., loopEnd`.  (Same byte-loop shape as wc; the
#          comma/period tallies are the only difference.)
#
#          STACK-VISIBLE STYLE: the three C locals comma_count,
#          period_count, and this_char each live in a stack slot; a
#          scratch register holds only a working COPY while an
#          instruction needs it, then the value is stored straight
#          back.  (The old version kept all three in $t0/$t1/$t2 across
#          every syscall, relying on spim preserving $t*; the stack
#          homes make the counters' lifetime explicit.)  This demo
#          makes NO `jal` (only syscalls, which preserve $ra), so it
#          saves $fp but not $ra.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: main)
#
#  C variable   | memory addr | register   | C type    | bytes | role / notes
#  -------------+-------------+------------+-----------+-------+---------------------------
#  comma_count  | -8($fp)     | $t1 (copy) | int       |   4   | running tally of ','
#  period_count | -12($fp)    | $t1 (copy) | int       |   4   | running tally of '.'
#  this_char    | -16($fp)    | $t0 (copy) | int       |   4   | most recent read_char() byte; -1 at EOF
#  (saved $fp)  | -4($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
# ------------------------------------------------------------------
#  SNAPSHOT (stdin "a,b.c,,.."; mid-loop, having just read the 2nd ','
#            — so two commas and zero periods counted so far)
#
#        main's stack frame             scratch
#   +----------------------------+     +----------------------+
#   | -4($fp)  saved $fp         |     | $t0 = ',' (this_char)
#   | -8($fp)  comma_count  = 2  |     | $t1 = 2   (a count)
#   | -12($fp) period_count = 0  |     +----------------------+
#   | -16($fp) this_char    = ','|
#   +----------------------------+
#         $sp = $fp - 16
#
#   Runs to EOF, then prints "3 commas, 3 stops".
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  stdin: extras/commaAndPeriodCounter/commaAndPeriodCounter.input
#  stdout:
#  | 3 commas, 3 stops

        .data
commasText:    .asciiz     " commas, "
stopsText:    .asciiz     " stops\n"

        .text
        .globl main
main:
        # ---- prologue: build a 16-byte frame (no $ra: no `jal`) ----
        addiu $sp, $sp, -16
        sw $fp, 12($sp)              # save caller's $fp -> -4($fp)
        addiu $fp, $sp, 16

        # comma_count = 0; period_count = 0;
        li $t0, 0
        sw $t0, -8($fp)              # comma_count = 0
        sw $t0, -12($fp)             # period_count = 0

        # this_char = read_char();
        li $v0, 12                   # syscall 12 = read_char
        syscall
        sw $v0, -16($fp)             # this_char = result

loopBegin:
        # while (this_char != -1) { ...    -- -1 is the EOF signal
        lw $t0, -16($fp)             # $t0 = this_char
        bltz $t0, loopEnd

        # if (this_char == '.') period_count++;
        lw $t0, -16($fp)
        bne $t0, '.', notAPeriod
        lw $t1, -12($fp)             # period_count
        addi $t1, $t1, 1
        sw $t1, -12($fp)             # store back
notAPeriod:

        # if (this_char == ',') comma_count++;
        lw $t0, -16($fp)
        bne $t0, ',', notAComma
        lw $t1, -8($fp)              # comma_count
        addi $t1, $t1, 1
        sw $t1, -8($fp)              # store back
notAComma:

        # this_char = read_char();
        li $v0, 12
        syscall
        sw $v0, -16($fp)             # this_char = result

        j loopBegin                  # } end of while

loopEnd:
        # print_int(comma_count);
        lw $a0, -8($fp)
        li $v0, 1
        syscall

        # print_string(" commas, ");
        li $v0, 4
        la $a0, commasText
        syscall

        # print_int(period_count);
        lw $a0, -12($fp)
        li $v0, 1
        syscall

        # print_string(" stops\n");
        li $v0, 4
        la $a0, stopsText
        syscall

        # ---- epilogue ----------------------------------------------
        li $v0, 0                    # exit status 0
        lw $fp, -4($fp)              # restore caller's $fp
        addiu $sp, $sp, 16           # pop the frame
        jr $ra                       # $ra untouched (we made no `jal`)
