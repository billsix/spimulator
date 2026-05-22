# tt.pseudo.s — exercises a variety of pseudo-ops so that the
# -show-expansion and -print-ast outputs can be regression-tested
# for the AST_PSEUDO wrapping introduced in Phase 3.

        .text
        .globl  main
main:
        li      $t0, 100000      # large li — encoder expands to lui + ori
        la      $a0, msg         # la — parser emits as ori (encoder lui+ori)
        move    $t1, $t0         # move → addu
        neg     $t2, $t1         # neg → sub
        not     $t3, $t2         # not → nor
        bge     $t1, $t0, done   # bge → slt + beq
done:
        li      $v0, 17          # small li → ori
        addi    $a0, $0, 0
        syscall

        .data
msg:    .asciiz "hi"
