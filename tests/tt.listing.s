# tt.listing.s — exercises every assemble-time event kind so that the
# -listing flag can be regression-tested.  See tt.listing.expected_re for
# the patterns the listing must contain.

        .data
b1:     .byte   0x42
h1:     .half   0x1234
w1:     .word   0xdeadbeef
s1:     .asciiz "hi"

        .text
        .globl  main
main:
        jal     target          # forward reference to a label defined below
        li      $v0, 17         # exit2
        addi    $a0, $0, 0
        syscall
target:
        jr      $ra
