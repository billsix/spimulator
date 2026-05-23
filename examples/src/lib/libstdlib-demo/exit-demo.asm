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
        # returns, so the saved $ra is irrelevant.
        li      $a0, 42
        jal     _Exit

        # Unreachable — _Exit's syscall transfers control to the
        # kernel.  If for some reason it did return, we'd fall
        # through into whatever bytes follow.  Belt-and-braces
        # final exit:
        li      $v0, 17
        li      $a0, 99             # distinct status so we'd notice
        syscall
