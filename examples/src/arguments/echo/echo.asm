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


# C source — see echo.c
# Invocation:
#   spimulator -f echo.asm one two three


#PURPOSE:  Echo command-line arguments to stdout, space-separated,
#          terminated by a newline.  First /examples demo to use
#          argv.  On entry to `main`, spim has already placed argc in
#          $a0 and argv in $a1 — no syscall needed; just iterate.
#
#          STACK-VISIBLE STYLE: because argc, argv, and the loop index
#          `i` all live across the print syscalls, each gets a stack
#          home; $a0/$a1 only carry them in from the runtime.  A
#          scratch register holds a working COPY only while an
#          instruction needs it.  This demo makes NO `jal` (only
#          syscalls, which preserve $ra), so it saves $fp but not $ra.
#          (The old version parked everything in callee-save $s*
#          registers; the stack homes are now the lesson.)
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  argc        | -8($fp)     | $t1 (copy) | int       |   4   | arrives in $a0; stored, reloaded to test
#  argv        | -12($fp)    | $t1 (copy) | char **   |   4   | arrives in $a1; argv[i] fetched each pass
#  i           | -16($fp)    | $t0 (copy) | int       |   4   | loop index 1..argc-1
#  (saved $fp) | -4($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
#  ($t1 is a reused scratch: it holds argc when testing, argv when
#   computing &argv[i]; never two live values at once.)
#
# ------------------------------------------------------------------
#  SNAPSHOT (input: echo one two three; at loop top with i = 2)
#
#        main's stack frame              scratch
#   +-----------------------------+     +---------------------------+
#   | -4($fp)  saved $fp          |     | $t0 = 2 (copy of i)
#   | -8($fp)  argc     =    4    |     | $t1 = argc or argv (reused)
#   | -12($fp) argv     = &av[0]  |     +---------------------------+
#   | -16($fp) i        =    2    | <-- | $t0
#   +-----------------------------+
#         $sp = $fp - 16
#
#   Prints argv[1..3] = "one two three", a space between each, then a
#   trailing newline, and exits 0.
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  args: one two three
#  stdout:
#  | one two three

        .data
spaceStr:   .asciiz " "
newlineStr: .asciiz "\n"

        .text
        .globl main
main:
        # ---- prologue: build a 16-byte frame (no $ra: no `jal`) ----
        addiu $sp, $sp, -16          # allocate the frame
        sw $fp, 12($sp)              # save caller's $fp -> -4($fp)
        addiu $fp, $sp, 16           # $fp = top of our frame
        sw $a0, -8($fp)              # argc = $a0  (home it on the stack)
        sw $a1, -12($fp)             # argv = $a1
        li $t0, 1
        sw $t0, -16($fp)             # i = 1   (skip argv[0])

loop:
        # while (i < argc)
        lw $t0, -16($fp)             # $t0 = i
        lw $t1, -8($fp)              # $t1 = argc
        bge $t0, $t1, done

        # print_string(argv[i]);
        lw $t1, -12($fp)             # $t1 = argv
        sll $t0, $t0, 2              # i * 4   (each argv pointer is 4 bytes)
        add $t0, $t1, $t0            # &argv[i]
        lw $a0, ($t0)                # argv[i]
        li $v0, 4
        syscall

        # if (i + 1 < argc) print_char(' ');
        lw $t0, -16($fp)             # $t0 = i
        addi $t0, $t0, 1             # i + 1
        lw $t1, -8($fp)              # $t1 = argc
        bge $t0, $t1, no_space
        li $v0, 4
        la $a0, spaceStr
        syscall

no_space:
        # i++;
        lw $t0, -16($fp)             # $t0 = i
        addi $t0, $t0, 1
        sw $t0, -16($fp)             # store the new i back
        j loop

done:
        # print_char('\n');
        li $v0, 4
        la $a0, newlineStr
        syscall

        # ---- epilogue: tear down the frame, return to the runtime --
        li $v0, 0                    # exit status: __start passes this through syscall 17
        lw $fp, -4($fp)              # restore caller's $fp
        addiu $sp, $sp, 16           # pop the frame
        jr $ra                       # $ra untouched (we made no `jal`)
