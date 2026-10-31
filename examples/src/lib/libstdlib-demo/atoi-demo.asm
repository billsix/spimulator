#==============================================================
# atoi-demo.asm — exercise libstdlib's atoi over the same input
# set as the C version, producing byte-identical output for diff.
#
# Load three files in order (libctype provides isspace/isdigit
# which atoi calls into):
#   spimulator -f libctype.asm -f libstdlib.asm -f atoi-demo.asm
#==============================================================
#
# STACK-VISIBLE STYLE: main's C locals — the loop index i and the
# current input string pointer s — live in stack slots; a scratch
# register holds only a working COPY.  s needs a home because it is
# used across several `jal`s (print "atoi(", print s, atoi(s)).  main
# saves $ra for its jals.  The input table (inputs[]) is file-scope
# data in .data; _ps/_pi are leaf print helpers; atoi is the library
# routine (loaded separately, register-based).  No #CANONICAL: a driver
# that `jal`s into libstdlib can't be assembled standalone by the
# value-checker; golden-tested via run-demo.sh.
# Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr    | register   | C type    | bytes | role / notes
#  ------------+----------------+------------+-----------+-------+---------------------------
#  i           | -12($fp)       | $t0 (copy) | int       |   4   | loop index 0..n_inputs-1
#  s           | -16($fp)       | $t0 (copy) | char *    |   4   | inputs[i]; survives the jals
#  inputs[]    | inputs (.data) | —          | char*[12] |  48   | file-scope table of input strings
#  (saved $ra) | -4($fp)        | —          | code addr |   4   | jal _ps / jal atoi / jal _pi
#  (saved $fp) | -8($fp)        | —          | frame ptr |   4   | caller's frame pointer

        .data
# Inputs (same set as atoi-demo.c).  spim's .asciiz accepts \n,
# \t, \" escapes natively.
in0:     .asciiz "42"
in1:     .asciiz "-42"
in2:     .asciiz "+42"
in3:     .asciiz "0"
in4:     .asciiz "   17"
in5:     .asciiz "  -8"
in6:     .asciiz "123abc"
in7:     .asciiz "abc"
in8:     .asciiz ""
in9:     .asciiz "-2147483648"
in10:    .asciiz "2147483647"
in11:    .asciiz " \t \n  99"

# Table of pointers + count for the loop.
        .align  2
inputs:  .word   in0, in1, in2, in3, in4, in5, in6, in7, in8, in9, in10, in11
n_inputs: .word  12

# Format strings.
fmt_pre: .asciiz "atoi(\""
fmt_mid: .asciiz "\") = "
fmt_nl:  .asciiz "\n"

        .text
        .globl  main
main:
        # ---- prologue: build main's 16-byte stack frame ------------
        addiu   $sp, $sp, -16
        sw      $ra, 12($sp)        # -> -4($fp)
        sw      $fp, 8($sp)         # -> -8($fp)
        addiu   $fp, $sp, 16

        li      $t0, 0
        sw      $t0, -12($fp)       # i = 0

loop:
        lw      $t0, -12($fp)       # i
        lw      $t1, n_inputs
        bge     $t0, $t1, done

        # s = inputs[i];
        sll     $t2, $t0, 2         # i * 4
        la      $t1, inputs
        addu    $t1, $t1, $t2
        lw      $t0, 0($t1)
        sw      $t0, -16($fp)       # s = inputs[i]

        # print "atoi(\""
        la      $a0, fmt_pre
        jal     _ps
        # print the input string s
        lw      $a0, -16($fp)
        jal     _ps
        # print "\") = "
        la      $a0, fmt_mid
        jal     _ps
        # call atoi(s)
        lw      $a0, -16($fp)
        jal     atoi
        # print the int result (transient in $v0)
        move    $a0, $v0
        jal     _pi
        # print newline
        la      $a0, fmt_nl
        jal     _ps

        # i++;
        lw      $t0, -12($fp)
        addiu   $t0, $t0, 1
        sw      $t0, -12($fp)
        j       loop

done:
        # ---- epilogue ----------------------------------------------
        li      $v0, 0              # exit status 0
        lw      $ra, -4($fp)
        lw      $fp, -8($fp)
        addiu   $sp, $sp, 16
        jr      $ra

#--------------------------------------------------------------
# Private print helpers (leaf wrappers around the syscalls).
#--------------------------------------------------------------

# _ps($a0=ptr) — print NUL-terminated string via syscall 4
_ps:
        li      $v0, 4
        syscall
        jr      $ra

# _pi($a0=int) — print signed decimal via syscall 1
_pi:
        li      $v0, 1
        syscall
        jr      $ra
