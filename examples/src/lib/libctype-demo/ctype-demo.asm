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
        # Save the runtime's return address (we'll jal a lot;
        # restore at the end so __start can exit cleanly with $v0).
        move    $s0, $ra
        li      $s1, 32             # $s1 = c (loop variable)
        li      $s2, 127            # $s2 = end-exclusive

loop:
        # ---- "c=" + int(c) ----
        la      $a0, msg_c_eq
        jal     _ps
        move    $a0, $s1
        jal     _pi

        # ---- " '" + char(c) ----
        la      $a0, msg_sp_quote
        jal     _ps
        move    $a0, $s1
        jal     _pc

        # ---- "'  alpha=" + isalpha(c) ----
        la      $a0, msg_q_alpha
        jal     _ps
        move    $a0, $s1
        jal     isalpha
        move    $a0, $v0
        jal     _pi

        # ---- " alnum=" + isalnum(c) ----
        la      $a0, msg_alnum
        jal     _ps
        move    $a0, $s1
        jal     isalnum
        move    $a0, $v0
        jal     _pi

        # ---- " digit=" + isdigit(c) ----
        la      $a0, msg_digit
        jal     _ps
        move    $a0, $s1
        jal     isdigit
        move    $a0, $v0
        jal     _pi

        # ---- " upper=" + isupper(c) ----
        la      $a0, msg_upper
        jal     _ps
        move    $a0, $s1
        jal     isupper
        move    $a0, $v0
        jal     _pi

        # ---- " lower=" + islower(c) ----
        la      $a0, msg_lower
        jal     _ps
        move    $a0, $s1
        jal     islower
        move    $a0, $v0
        jal     _pi

        # ---- " space=" + isspace(c) ----
        la      $a0, msg_space
        jal     _ps
        move    $a0, $s1
        jal     isspace
        move    $a0, $v0
        jal     _pi

        # ---- "  up='" + char(toupper(c)) ----
        la      $a0, msg_up_q
        jal     _ps
        move    $a0, $s1
        jal     toupper
        move    $a0, $v0
        jal     _pc

        # ---- "' lo='" + char(tolower(c)) + "'\n" ----
        la      $a0, msg_q_lo_q
        jal     _ps
        move    $a0, $s1
        jal     tolower
        move    $a0, $v0
        jal     _pc
        la      $a0, msg_q_nl
        jal     _ps

        # ---- advance and loop ----
        addiu   $s1, $s1, 1
        blt     $s1, $s2, loop

        # ---- done ----
        move    $ra, $s0
        li      $v0, 0              # exit status 0 (via __start's syscall 17)
        jr      $ra

#--------------------------------------------------------------
# Private print helpers — leaf wrappers around the syscalls.
# Future work: factor these into a libio.asm.
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
