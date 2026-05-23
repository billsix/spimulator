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
        # main needs to save $ra across atexit calls (which jal,
        # though they don't internally jal anything themselves —
        # still caller-save discipline).  No $s regs used.
        addiu   $sp, $sp, -8
        sw      $ra, 0($sp)

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

        # exit(42) — this never returns; the frame teardown below
        # is unreachable but kept for the discipline.
        li      $a0, 42
        jal     exit

        # Unreachable.
        lw      $ra, 0($sp)
        addiu   $sp, $sp, 8
        li      $v0, 17
        li      $a0, 99             # distinct status so we'd notice
        syscall
