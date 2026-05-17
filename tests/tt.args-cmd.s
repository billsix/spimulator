# SPIM S20 MIPS simulator.
# Test for the REPL `args` command (see tasks/repl-args-command.md).
#
# Invoke (after `meson install`), piping REPL commands via stdin:
#
#   printf 'read "tt.args-cmd.s"\nargs zebra\nrun\nargs amber\nrun\nquit\n' \
#       | spimulator
#
# Expected output to contain BOTH "argv1=z" and "argv1=a" — proves that
# `args` between `run`s actually replaces program_argv mid-session.
#
# If argc < 2 (no args supplied) prints "argv1=(none)" so a misbehaving
# `args` command surfaces visibly rather than hanging.

        .data
prefix:     .asciiz "argv1="
newline:    .asciiz "\n"
noArgMsg:   .asciiz "argv1=(none)\n"

        .text
        .globl main
main:
        # Park caller state in callee-save regs so the syscalls below
        # don't clobber $ra / $a0 / $a1.
        move $s0, $ra
        move $s1, $a0                # argc
        move $s2, $a1                # argv

        # If argc < 2 nothing to read — sentinel + return.
        li $t0, 2
        blt $s1, $t0, no_arg

        # print_string("argv1=");
        li $v0, 4
        la $a0, prefix
        syscall

        # print_char(*argv[1]);
        lw $t0, 4($s2)               # argv[1]   (each pointer is 4 bytes)
        lb $a0, ($t0)                # *argv[1]
        li $v0, 11                   # 11 = print_char
        syscall

        # print_string("\n");
        li $v0, 4
        la $a0, newline
        syscall

        j done

no_arg:
        li $v0, 4
        la $a0, noArgMsg
        syscall

done:
        move $ra, $s0
        jr $ra                       # back to runtime; runtime calls exit
