#==============================================================
# atoi-demo.asm — exercise libstdlib's atoi over the same input
# set as the C version, producing byte-identical output for diff.
#
# Load three files in order (libctype provides isspace/isdigit
# which atoi calls into):
#   spimulator -f libctype.asm -f libstdlib.asm -f atoi-demo.asm
#==============================================================

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
        # ── Prologue: save $ra and the $s regs we'll use ──
        addiu   $sp, $sp, -24
        sw      $ra,  0($sp)
        sw      $s0,  4($sp)        # loop counter i
        sw      $s1,  8($sp)        # base of inputs table
        sw      $s2, 12($sp)        # loop bound (n_inputs)
        sw      $s3, 16($sp)        # current input pointer

        li      $s0, 0              # i = 0
        la      $s1, inputs
        lw      $s2, n_inputs

loop:
        bge     $s0, $s2, done

        # Load inputs[i] into $s3.
        sll     $t0, $s0, 2         # i * 4
        addu    $t0, $s1, $t0
        lw      $s3, 0($t0)

        # print "atoi(\""
        la      $a0, fmt_pre
        jal     _ps
        # print the input string
        move    $a0, $s3
        jal     _ps
        # print "\") = "
        la      $a0, fmt_mid
        jal     _ps
        # call atoi(input)
        move    $a0, $s3
        jal     atoi
        # print the int result
        move    $a0, $v0
        jal     _pi
        # print newline
        la      $a0, fmt_nl
        jal     _ps

        addiu   $s0, $s0, 1
        j       loop

done:
        # ── Epilogue ──
        lw      $ra,  0($sp)
        lw      $s0,  4($sp)
        lw      $s1,  8($sp)
        lw      $s2, 12($sp)
        lw      $s3, 16($sp)
        addiu   $sp, $sp, 24
        li      $v0, 0              # exit status 0
        jr      $ra

#--------------------------------------------------------------
# Private print helpers (factor into libio.asm later).
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
