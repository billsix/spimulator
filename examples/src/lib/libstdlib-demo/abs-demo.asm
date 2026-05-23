#==============================================================
# abs-demo.asm — exercise libstdlib's abs + labs.  Produces
# byte-identical output to abs-demo.c.
#
# Load order: libstdlib.asm provides labs.  libctype.asm must
# ALSO be loaded — even though abs/labs don't call it, libstdlib
# as a whole contains atoi which references isspace+isdigit, and
# those symbols must resolve for the whole load to parse.
#
#   spimulator -f libctype.asm -f libstdlib.asm -f abs-demo.asm
#==============================================================

        .data
# Inputs as a .word table.  Same set as abs-demo.c.
        .align  2
cases:    .word   0
          .word   1
          .word   -1
          .word   100
          .word   -100
          .word   2147483647         # INT_MAX
          .word   -2147483647        # -INT_MAX
          .word   -2147483648        # INT_MIN — abs returns INT_MIN
n_cases:  .word   8

# Format strings.  Library uses the longer names absolute /
# labsolute (vs. the C library's abs / labs).
fmt_abs:  .asciiz "absolute("
fmt_mid:  .asciiz ") = "
fmt_lab:  .asciiz "    labsolute("
fmt_nl:   .asciiz "\n"

        .text
        .globl  main
main:
        # ── Prologue ──
        addiu   $sp, $sp, -24
        sw      $ra,  0($sp)
        sw      $s0,  4($sp)        # i (loop counter)
        sw      $s1,  8($sp)        # base of cases table
        sw      $s2, 12($sp)        # loop bound
        sw      $s3, 16($sp)        # current input value (cached so we
                                    # don't re-load between abs and labs)

        li      $s0, 0
        la      $s1, cases
        lw      $s2, n_cases

loop:
        bge     $s0, $s2, done

        # Load cases[i] into $s3.
        sll     $t0, $s0, 2
        addu    $t0, $s1, $t0
        lw      $s3, 0($t0)

        # print "abs("
        la      $a0, fmt_abs
        jal     _ps
        # print input value
        move    $a0, $s3
        jal     _pi
        # print ") = "
        la      $a0, fmt_mid
        jal     _ps
        # call absolute(input)
        move    $a0, $s3
        jal     absolute
        move    $a0, $v0
        jal     _pi

        # print "    labsolute("
        la      $a0, fmt_lab
        jal     _ps
        # print input value (same value, as long)
        move    $a0, $s3
        jal     _pi
        # print ") = "
        la      $a0, fmt_mid
        jal     _ps
        # call labsolute(input)
        move    $a0, $s3
        jal     labsolute
        move    $a0, $v0
        jal     _pi

        # newline
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
        li      $v0, 0
        jr      $ra

#--------------------------------------------------------------
# Private print helpers (factor into libio.asm later).
#--------------------------------------------------------------

_ps:
        li      $v0, 4
        syscall
        jr      $ra

_pi:
        li      $v0, 1
        syscall
        jr      $ra
