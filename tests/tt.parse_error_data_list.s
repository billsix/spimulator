# SPIM S20 MIPS simulator.
# A token that cannot start an expression, inside a .word list, must
# produce ONE parse error and end the run with exit status 2.  The list
# parser used to report the error without consuming the token and then
# ask for the next expression again, printing the same error forever
# (found 2026-11-10 while testing tasks/engine-port-prereq-c-fixes.md).
#
# Driver:  spimulator -exception_file src/exceptions.s -f tests/tt.parse_error_data_list.s

        .data
x:      .word 1.5

        .text
        .globl main
main:
        jr $ra
