# SPIM S20 MIPS simulator.
# Code before the first `.data`, with no `.text` directive, belongs in the text
# segment: the assembler starts every file there.  The engine used to emit
# such code into the data segment, because the emit pass started in whichever
# segment the parse had ended in (found 2026-11-10 by the Python port).
# Expected output: 7, exit status 0.

main:   li $v0, 1
        li $a0, 7
        syscall
        li $v0, 10
        syscall

        .data
x:      .word 1
