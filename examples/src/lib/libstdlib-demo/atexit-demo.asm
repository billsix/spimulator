#==============================================================
# atexit-demo.asm — register 3 cleanup handlers, then exit(42).
# Companion to atexit-demo.c; produces byte-identical stdout
# AND identical shell exit status (42).
#
# Demonstrates the LIFO order: we register h1, h2, h3 in that
# order and they run in the order h3, h2, h1.
#
# Load order:
#   spimulator -f libctype.asm -f libstdlib.asm -f atexit-demo.asm
#==============================================================
#
# STACK-VISIBLE STYLE: this driver has NO C local variables — it
# passes three handler ADDRESSES to atexit, then tail-calls exit(42)
# (which never returns).  main saves $ra on its frame for the atexit
# `jal`s (discipline; the teardown is unreachable since exit never
# returns).  h1/h2/h3 are leaf handlers.  No #CANONICAL: a driver that
# `jal`s into libstdlib can't be assembled standalone by the value-
# checker; golden-tested via run-demo.sh (incl. exit status).
# Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  (none)      | —           | —          | —         |   —   | no C locals; registers h1/h2/h3, exits 42
#  (saved $ra) | -4($fp)     | —          | code addr |   4   | across the three jal atexit
#  (saved $fp) | -8($fp)     | —          | frame ptr |   4   | caller's frame pointer

        .data
msg_h1:    .asciiz "handler 1 ran\n"
msg_h2:    .asciiz "handler 2 ran\n"
msg_h3:    .asciiz "handler 3 ran\n"
msg_main:  .asciiz "main about to exit\n"

        .text

# Three atexit handlers — each just prints a distinct line.
# Leaf functions, no frame needed.
h1:
        li      $v0, 4
        la      $a0, msg_h1
        syscall
        jr      $ra

h2:
        li      $v0, 4
        la      $a0, msg_h2
        syscall
        jr      $ra

h3:
        li      $v0, 4
        la      $a0, msg_h3
        syscall
        jr      $ra

        .globl  main
main:
        # ---- prologue: save $ra (and $fp) across the atexit jals ---
        addiu   $sp, $sp, -8
        sw      $ra, 4($sp)         # -> -4($fp)
        sw      $fp, 0($sp)         # -> -8($fp)
        addiu   $fp, $sp, 8

        # atexit(h1); atexit(h2); atexit(h3);
        la      $a0, h1
        jal     atexit
        la      $a0, h2
        jal     atexit
        la      $a0, h3
        jal     atexit

        # print "main about to exit\n"
        li      $v0, 4
        la      $a0, msg_main
        syscall

        # exit(42) — never returns; the teardown below is unreachable
        # but kept for the discipline.
        li      $a0, 42
        jal     exit

        # Unreachable.
        lw      $ra, -4($fp)
        lw      $fp, -8($fp)
        addiu   $sp, $sp, 8
        li      $v0, 17
        li      $a0, 99             # distinct status so we'd notice
        syscall
