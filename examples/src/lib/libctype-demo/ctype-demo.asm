#==============================================================
# ctype-demo.asm — exercise every libctype function across the
# printable-ASCII range (32..126), one row per byte.
#
# Output is byte-for-byte identical to the C version
# (ctype-demo.c) so the meson test can diff against the same
# golden file.
#
# Load alongside libctype.asm:
#   spimulator -f libctype.asm -f ctype-demo.asm
#
# This file uses libctype's globals (isalpha, isalnum, isdigit,
# isupper, islower, isspace, toupper, tolower).  The forward
# references resolve at load time because spim's symbol table
# accumulates across -f files (see /spimulator/tasks/
# cli-multi-file-load.md).
#==============================================================
#
# STACK-VISIBLE STYLE: the one C local, the loop variable c, lives in
# a stack slot; each use reloads a working COPY into $a0 (c is passed
# to every ctype function and to the int/char printers).  main saves
# $ra for its many `jal`s.  The ctype functions are library routines
# (loaded separately, register-based); _ps/_pi/_pc are leaf print
# helpers.  No #CANONICAL: a driver that `jal`s into libctype can't be
# assembled standalone by the value-checker; golden-tested via
# run-demo.sh.
# Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  c           | -12($fp)    | $a0 (copy) | int       |   4   | loop variable 32..126 (one row per byte)
#  (saved $ra) | -4($fp)     | —          | code addr |   4   | across the per-row jal <ctype fn> / jal _p*
#  (saved $fp) | -8($fp)     | —          | frame ptr |   4   | caller's frame pointer

        .data
msg_c_eq:    .asciiz "c="
msg_sp_quote: .asciiz " '"
msg_q_alpha: .asciiz "'  alpha="
msg_alnum:   .asciiz " alnum="
msg_digit:   .asciiz " digit="
msg_upper:   .asciiz " upper="
msg_lower:   .asciiz " lower="
msg_space:   .asciiz " space="
msg_up_q:    .asciiz "  up='"
msg_q_lo_q:  .asciiz "' lo='"
msg_q_nl:    .asciiz "'\n"

        .text
        .globl  main
main:
        # ---- prologue: build main's 16-byte stack frame ------------
        addiu   $sp, $sp, -16
        sw      $ra, 12($sp)        # -> -4($fp)
        sw      $fp, 8($sp)         # -> -8($fp)
        addiu   $fp, $sp, 16

        li      $t0, 32
        sw      $t0, -12($fp)       # c = 32

loop:
        # ---- "c=" + int(c) ----
        la      $a0, msg_c_eq
        jal     _ps
        lw      $a0, -12($fp)       # c
        jal     _pi

        # ---- " '" + char(c) ----
        la      $a0, msg_sp_quote
        jal     _ps
        lw      $a0, -12($fp)       # c
        jal     _pc

        # ---- "'  alpha=" + isalpha(c) ----
        la      $a0, msg_q_alpha
        jal     _ps
        lw      $a0, -12($fp)       # c
        jal     isalpha
        move    $a0, $v0
        jal     _pi

        # ---- " alnum=" + isalnum(c) ----
        la      $a0, msg_alnum
        jal     _ps
        lw      $a0, -12($fp)       # c
        jal     isalnum
        move    $a0, $v0
        jal     _pi

        # ---- " digit=" + isdigit(c) ----
        la      $a0, msg_digit
        jal     _ps
        lw      $a0, -12($fp)       # c
        jal     isdigit
        move    $a0, $v0
        jal     _pi

        # ---- " upper=" + isupper(c) ----
        la      $a0, msg_upper
        jal     _ps
        lw      $a0, -12($fp)       # c
        jal     isupper
        move    $a0, $v0
        jal     _pi

        # ---- " lower=" + islower(c) ----
        la      $a0, msg_lower
        jal     _ps
        lw      $a0, -12($fp)       # c
        jal     islower
        move    $a0, $v0
        jal     _pi

        # ---- " space=" + isspace(c) ----
        la      $a0, msg_space
        jal     _ps
        lw      $a0, -12($fp)       # c
        jal     isspace
        move    $a0, $v0
        jal     _pi

        # ---- "  up='" + char(toupper(c)) ----
        la      $a0, msg_up_q
        jal     _ps
        lw      $a0, -12($fp)       # c
        jal     toupper
        move    $a0, $v0
        jal     _pc

        # ---- "' lo='" + char(tolower(c)) + "'\n" ----
        la      $a0, msg_q_lo_q
        jal     _ps
        lw      $a0, -12($fp)       # c
        jal     tolower
        move    $a0, $v0
        jal     _pc
        la      $a0, msg_q_nl
        jal     _ps

        # ---- advance: c++; while (c < 127) ----
        lw      $t0, -12($fp)       # c
        addiu   $t0, $t0, 1
        sw      $t0, -12($fp)
        blt     $t0, 127, loop

done:
        # ---- epilogue ----------------------------------------------
        li      $v0, 0              # exit status 0 (via __start's syscall 17)
        lw      $ra, -4($fp)
        lw      $fp, -8($fp)
        addiu   $sp, $sp, 16
        jr      $ra

#--------------------------------------------------------------
# Private print helpers — leaf wrappers around the syscalls.
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

# _pc($a0=byte) — print one byte via syscall 11
_pc:
        li      $v0, 11
        syscall
        jr      $ra
