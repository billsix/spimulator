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
#
# STACK-VISIBLE STYLE: main's C locals — the loop index i and the
# current case value v — live in stack slots; a scratch register holds
# only a working COPY.  v needs a home because it is used across
# several `jal`s (printed, passed to absolute, printed again, passed
# to labsolute).  main saves $ra for its jals.  The case table is
# file-scope data in .data; _ps/_pi are leaf print helpers; absolute/
# labsolute are library routines (loaded separately).  No #CANONICAL:
# a driver that `jal`s into libstdlib can't be assembled standalone by
# the value-checker; golden-tested via run-demo.sh.
# Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr   | register   | C type    | bytes | role / notes
#  ------------+---------------+------------+-----------+-------+---------------------------
#  i           | -12($fp)      | $t0 (copy) | int       |   4   | loop index 0..n_cases-1
#  v           | -16($fp)      | $t0 (copy) | int       |   4   | cases[i]; printed and passed to abs/labs
#  cases[]     | cases (.data) | —          | int[8]    |  32   | file-scope table of test values
#  (saved $ra) | -4($fp)       | —          | code addr |   4   | jal _ps / absolute / labsolute / _pi
#  (saved $fp) | -8($fp)       | —          | frame ptr |   4   | caller's frame pointer

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
        # ---- prologue: build main's 16-byte stack frame ------------
        addiu   $sp, $sp, -16
        sw      $ra, 12($sp)        # -> -4($fp)
        sw      $fp, 8($sp)         # -> -8($fp)
        addiu   $fp, $sp, 16

        li      $t0, 0
        sw      $t0, -12($fp)       # i = 0

loop:
        lw      $t0, -12($fp)       # i
        lw      $t1, n_cases
        bge     $t0, $t1, done

        # v = cases[i];
        sll     $t2, $t0, 2         # i * 4
        la      $t1, cases
        addu    $t1, $t1, $t2
        lw      $t0, 0($t1)
        sw      $t0, -16($fp)       # v = cases[i]

        # print "absolute("
        la      $a0, fmt_abs
        jal     _ps
        # print v
        lw      $a0, -16($fp)
        jal     _pi
        # print ") = "
        la      $a0, fmt_mid
        jal     _ps
        # absolute(v)
        lw      $a0, -16($fp)
        jal     absolute
        move    $a0, $v0
        jal     _pi

        # print "    labsolute("
        la      $a0, fmt_lab
        jal     _ps
        # print v (same value, as long)
        lw      $a0, -16($fp)
        jal     _pi
        # print ") = "
        la      $a0, fmt_mid
        jal     _ps
        # labsolute(v)
        lw      $a0, -16($fp)
        jal     labsolute
        move    $a0, $v0
        jal     _pi

        # newline
        la      $a0, fmt_nl
        jal     _ps

        # i++;
        lw      $t0, -12($fp)
        addiu   $t0, $t0, 1
        sw      $t0, -12($fp)
        j       loop

done:
        # ---- epilogue ----------------------------------------------
        li      $v0, 0
        lw      $ra, -4($fp)
        lw      $fp, -8($fp)
        addiu   $sp, $sp, 16
        jr      $ra

#--------------------------------------------------------------
# Private print helpers (leaf wrappers around the syscalls).
#--------------------------------------------------------------

_ps:
        li      $v0, 4
        syscall
        jr      $ra

_pi:
        li      $v0, 1
        syscall
        jr      $ra
