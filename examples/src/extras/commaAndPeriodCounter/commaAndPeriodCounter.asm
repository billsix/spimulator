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
#
#     __attribute__((noreturn)) void _start(void) {
#       int comma_count = 0;
#       int period_count = 0;
#       int this_char = read_char();
#       while (this_char != -1) {           /* EOF */
#         if (this_char == '.') period_count = period_count + 1;
#         if (this_char == ',') comma_count = comma_count + 1;
#         this_char = read_char();
#       }
#       print_int(comma_count);  print_string(" commas, ");
#       print_int(period_count); print_string(" stops\n");
#       os_exit(0);
#     }


#PURPOSE:  Count the commas and periods on standard input and
#          print a summary line of the form
#             <N> commas, <M> stops
#
#NOTES:    `read_char` returns -1 at EOF; the loop branches on
#          `bltz $t2, loopEnd`.  (This demo's only purpose vs
#          wc is the comma/period counts; same byte-loop shape.)
#
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     comma_count   $t0                  (running tally)
#     period_count  $t1                  (running tally)
#     this_char     $t2                  (most recently read byte)
#
#   Three $t-regs reused across every read_char / print_int /
#   print_string syscall.  Strictly speaking MIPS $t* are CALLER-
#   save and these should be spilled across each syscall — but
#   spim's syscalls preserve $t-regs in practice, and this demo
#   predates the introduction of the $s* save discipline (which
#   shows up first in cksum where it becomes load-bearing).
#
#   No subroutine calls, hence no Cross-call saves section.
#
#   Volatile (no preserved meaning across a syscall, by convention):
#     $a0   syscall arg
#     $v0   syscall selector / read_char return
#           (12 = read_char, 4 = print_string, 1 = print_int)

        .data
commasText:    .asciiz     " commas, "
stopsText:    .asciiz     " stops\n"

        .text
        .globl main
main:
        li $t0, 0                    # comma_count = 0
        li $t1, 0                    # period_count = 0

        # this_char = read_char();
        li $v0, 12                   # syscall 12 = read_char
        syscall                      # result in $v0
        move $t2, $v0                # this_char = $v0

loopBegin:
        # while (this_char != -1) { ...    -- -1 is the EOF signal
        bltz $t2, loopEnd

        # if (this_char == '.') period_count++;
        bne $t2, '.', notAPeriod
        addi $t1, $t1, 1
notAPeriod:

        # if (this_char == ',') comma_count++;
        bne $t2, ',', notAComma
        addi $t0, $t0, 1
notAComma:

        # this_char = read_char();
        li $v0, 12
        syscall
        move $t2, $v0

        j loopBegin                  # } end of while

loopEnd:
        # print_int(comma_count);
        move $a0, $t0
        li $v0, 1
        syscall

        # print_string(" commas, ");
        li $v0, 4
        la $a0, commasText
        syscall

        # print_int(period_count);
        move $a0, $t1
        li $v0, 1
        syscall

        # print_string(" stops\n");
        li $v0, 4
        la $a0, stopsText
        syscall

        li $v0, 0                    # exit status 0
        jr $ra                       # return to the runtime
