# Teaching-mode branch targets: every taken branch must name the address
# the PC moves to, with and without -delayed_branches.  Each branch has a
# nop after it, so the program runs the same in both modes.
        .text
main:   li      $t0, 1
        bne     $t0, $zero, fwd     # forward, taken
        nop
        li      $v0, 99
fwd:    li      $t1, 2
back:   addi    $t1, $t1, -1
        bgtz    $t1, back           # backward, taken once
        nop
        beq     $zero, $zero, out   # always taken
        nop
        li      $v0, 98
out:    li      $v0, 0
        jr      $ra
        nop
