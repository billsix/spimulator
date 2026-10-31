#==============================================================
# exit-demo.asm — print a one-line message then _Exit(42).
# Companion to exit-demo.c; produces byte-identical stdout
# AND identical shell exit status (42).
#
# Load order: libctype is required (libstdlib bundles atoi
# which references isspace + isdigit, even though this demo
# doesn't use atoi directly — the whole .asm parses as a
# unit).
#
#   spimulator -f libctype.asm -f libstdlib.asm -f exit-demo.asm
#   echo "exit code: $?"
#==============================================================
#
# STACK-VISIBLE STYLE: this driver has NO C local variables — it
# prints a fixed string and tail-calls _Exit(42), which never
# returns — so there is no stack frame and nothing to home on the
# stack (a frame would be ceremony).  There is no #CANONICAL block:
# the single-file value-checker can't assemble a driver that `jal`s
# into libstdlib (_Exit); this demo is golden-tested via run-demo.sh
# (which loads the library .asm alongside), including its exit status.
# Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable | memory addr | register | C type | bytes | role / notes
#  -----------+-------------+----------+--------+-------+---------------------------
#  (none)     | —           | —        | —      |   —   | no C locals; prints a string, tail-calls _Exit(42)

        .data
msg:    .asciiz "calling _Exit(42)\n"

        .text
        .globl  main
main:
        # print the message
        li      $v0, 4              # syscall 4 = print_string
        la      $a0, msg
        syscall

        # _Exit(42).  We `jal` for the tail call so anyone reading
        # this can see the call mechanic; the function itself never
        # returns, so the (untouched) $ra is irrelevant.
        li      $a0, 42
        jal     _Exit

        # Unreachable — _Exit's syscall transfers control to the
        # kernel.  If for some reason it did return, we'd fall
        # through into whatever bytes follow.  Belt-and-braces
        # final exit:
        li      $v0, 17
        li      $a0, 99             # distinct status so we'd notice
        syscall
