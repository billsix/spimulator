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


# C source — see seq.c


# Invocations:
#   spimulator -f seq.asm 5            # 1..5
#   spimulator -f seq.asm 10 15        # 10..15
#   spimulator -f seq.asm 5 1          # 5..1 (descending -> prints nothing)


#PURPOSE:  seq with 1- or 2-arg form.  Pure argv → loop → stdout.
#          First curriculum demo with NO input I/O syscalls at all.
#
#          STACK-VISIBLE STYLE: the C locals `m`, `n`, and the loop
#          counter `i` live in stack slots; a scratch register holds
#          only a working COPY.  `argv` also gets a slot because it is
#          read across the `jal atoi` calls (and in the 2-arg form it
#          must survive the first atoi to reach argv[2]); argc is used
#          once at entry, so it stays in $a0.  This routine makes
#          `jal atoi`, so it saves $ra on the stack.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  argc        | —           | $a0        | int       |   4   | entry only; selects 1- or 2-arg form
#  argv        | -12($fp)    | $t0 (copy) | char **   |   4   | homed on entry; survives the atoi calls
#  m           | -16($fp)    | $t0 (copy) | int       |   4   | range start (1, or argv[1])
#  n           | -20($fp)    | $t1 (copy) | int       |   4   | range end (argv[1] or argv[2])
#  i           | -24($fp)    | $t0 (copy) | int       |   4   | loop counter m..n
#  (saved $ra) | -4($fp)     | —          | code addr |   4   | return into the runtime (atoi uses jal)
#  (saved $fp) | -8($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
# ------------------------------------------------------------------
#  SNAPSHOT (input: seq 10 15; in `loop` with i = 12, m = 10, n = 15)
#
#        main's stack frame             scratch
#   +---------------------------+      +----------------------+
#   | -4($fp)  saved $ra        |      | $t0 = 12 (copy of i)
#   | -8($fp)  saved $fp        |      | $t1 = 15 (copy of n)
#   | -12($fp) argv    = &av[0] |      +----------------------+
#   | -16($fp) m       =  10    |
#   | -20($fp) n       =  15    | <--- | $t1
#   | -24($fp) i       =  12    | <--- | $t0
#   +---------------------------+
#         $sp = $fp - 24
#
#   Prints 10..15, one per line, then exits 0.
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  args: 10 15
#  stdout:
#  | 10
#  | 11
#  | 12
#  | 13
#  | 14
#  | 15

        .data
usageMsg:   .asciiz "usage: seq N | seq M N\n"

        .text
        .globl main
main:
        # ---- prologue: build main's 24-byte stack frame ------------
        addiu $sp, $sp, -24          # allocate the frame
        sw $ra, 20($sp)              # save $ra          -> -4($fp)
        sw $fp, 16($sp)              # save caller's $fp -> -8($fp)
        addiu $fp, $sp, 24           # $fp = top of our frame
        sw $a1, -12($fp)             # argv (needed across the atoi calls)

        # dispatch on argc (argc arrives in $a0)
        li $t0, 2
        beq $a0, $t0, one_arg        # 1-arg form: argc == 2
        li $t0, 3
        beq $a0, $t0, two_arg        # 2-arg form: argc == 3
        j usage

one_arg:
        # m = 1;
        li $t0, 1
        sw $t0, -16($fp)             # m = 1
        # n = parse_int(argv[1]);
        lw $t0, -12($fp)             # $t0 = argv
        lw $a0, 4($t0)               # argv[1]
        jal atoi
        sw $v0, -20($fp)             # n = atoi(argv[1])
        j run

two_arg:
        # m = parse_int(argv[1]);
        lw $t0, -12($fp)             # $t0 = argv
        lw $a0, 4($t0)               # argv[1]
        jal atoi
        sw $v0, -16($fp)             # m = atoi(argv[1])
        # n = parse_int(argv[2]);
        lw $t0, -12($fp)             # $t0 = argv  (reloaded; atoi may clobber)
        lw $a0, 8($t0)               # argv[2]
        jal atoi
        sw $v0, -20($fp)             # n = atoi(argv[2])

run:
        # for (i = m; i <= n; i++)
        lw $t0, -16($fp)             # $t0 = m
        sw $t0, -24($fp)             # i = m

loop:
        lw $t0, -24($fp)             # $t0 = i
        lw $t1, -20($fp)             # $t1 = n
        bgt $t0, $t1, done           # while (i <= n)

        # print_int(i);
        lw $a0, -24($fp)             # $a0 = i
        li $v0, 1
        syscall
        # print_char('\n');
        li $a0, '\n'
        li $v0, 11
        syscall

        # i++;
        lw $t0, -24($fp)             # $t0 = i
        addi $t0, $t0, 1
        sw $t0, -24($fp)             # store the new i back
        j loop

done:
        # ---- epilogue: tear down the frame, return to the runtime --
        li $v0, 0                    # exit status: __start passes this through syscall 17
        lw $ra, -4($fp)              # restore $ra
        lw $fp, -8($fp)              # restore caller's $fp
        addiu $sp, $sp, 24           # pop the frame
        jr $ra

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall


# ---------- atoi subroutine -----------------------------------------
# Shared leaf helper (string -> signed int), kept register-based: arg
# in $a0, return in $v0, calls nothing, so it needs no frame.
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
