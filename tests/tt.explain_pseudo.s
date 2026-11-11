# Teaching-mode pseudo-op headers: each comes from the assembler's record
# of what an instruction was emitted for, not from the source text.
        .data
word:   .word   12
msg:    .asciiz "x"
        .text
main:   li      $v0, 4              # a pseudo-op behind a label: one ori
        la      $a0, msg            # lui + ori
        syscall                     # written as itself: no header
        lw      $t1, word           # a real instruction expanded to lui + lw
        bgt     $t1, 10, big        # slti + beq
big:    li      $v0, 0
        jr      $ra
