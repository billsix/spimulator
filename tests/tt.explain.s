# tt.explain.s
#
# Smoke test for teaching mode (-explain). Hits one instruction from each
# pedagogically important MVP category for an undergraduate MIPS course
# (Patterson & Hennessy Computer Organization and Design, Ch. 2–3):
#   - arithmetic R-type signed and unsigned (add, addu, sub, subu)
#   - arithmetic I-type signed and unsigned (addi, addiu)
#   - logical R-type and immediate (and, or, xor, nor, andi, ori, xori)
#   - shifts (sll, srl, sra)
#   - set-less-than family (slt, slti, sltu, sltiu)
#   - loads with sign-extend AND zero-extend contrasts (lb/lbu, lh/lhu)
#   - loads and stores by symbol AND by N($reg) base+offset (lw, sw, sb, sh)
#   - branches: 2-reg taken/not-taken (beq, bne) and zero-compare (bgez,
#     bgtz, blez, bltz); also a backward branch forming a loop
#   - direct lui+ori to build a 32-bit constant (H&P Fig 2.18)
#   - mult/div signed AND unsigned, with HI:LO and mfhi/mflo
#   - jumps and procedure calls: unconditional (j), direct call (jal),
#     indirect call (jalr), return (jr)
#   - syscalls: print_string=4, print_int=1, print_char=11, exit=10
#     (read_int=5 and read_string=8 are present but commented out —
#      they block on stdin)
#
# Run with:  spimulator -explain -f tests/tt.explain.s

        .data
src:    .word   0x12345678
dst:    .word   0
hello:  .asciiz "hi\n"

        .text
        .globl  main
main:
        li      $t0, 5              # I-type immediate (addiu)
        li      $t1, 7
        add     $t2, $t0, $t1       # R-type signed add
        and     $t3, $t0, $t1       # R-type logical
        sll     $t4, $t2, 2         # shift
        lw      $t5, src            # load from data segment
        sw      $t2, dst            # store to data segment
        beq     $t0, $t1, skip      # branch (not taken)
        addi    $t6, $t0, 1         # falls through
skip:   bne     $t0, $t1, done      # branch (taken)
        nop
done:   la      $a0, hello          # pseudo-op for address load

        # --- base+offset addressing ($reg + N) ---
        # The forms above (lw/sw with a symbol) go through the lui+lw
        # pseudo-op expansion. These exercise the real N($reg) form that
        # every undergrad sees for stack frames, arrays, and struct fields.
        la      $t6, src            # $t6 points into .data
        lw      $t7, 0($t6)         # load with zero offset
        lb      $t8, 0($t6)         # byte load via base+offset
        addiu   $sp, $sp, -8        # allocate an 8-byte stack frame
        sw      $t7, 4($sp)         # store with non-zero offset into frame
        lw      $t9, 4($sp)         # load back from the frame slot
        addiu   $sp, $sp, 8         # tear down the frame

        # --- unsigned arithmetic variants (no overflow trap) ---
        addu    $s0, $t0, $t1       # unsigned add
        subu    $s1, $t1, $t0       # unsigned sub

        # --- logical immediates + remaining shifts ---
        andi    $s2, $t0, 0x0f      # AND with 16-bit immediate
        ori     $s3, $t0, 0xf0      # OR  with 16-bit immediate
        xori    $s4, $t0, 0xff      # XOR with 16-bit immediate
        srl     $s5, $t2, 1         # logical shift right (zero fill)
        sra     $s6, $t2, 1         # arithmetic shift right (sign fill)

        # --- set-less-than family ---
        slt     $s7, $t0, $t1       # signed:    ($t0 < $t1) ? 1 : 0
        slti    $t3, $t0, 100       # signed immediate
        sltu    $t4, $t0, $t1       # unsigned compare
        sltiu   $t5, $t0, 100       # unsigned immediate

        # --- zero-compare branches (all four MVP forms, all taken) ---
        li      $t6, 1
        bgtz    $t6, zb1            # 1 > 0
        nop
zb1:    bgez    $t6, zb2            # 1 >= 0
        nop
zb2:    li      $t6, -1
        bltz    $t6, zb3            # -1 < 0
        nop
zb3:    blez    $t6, zb4            # -1 <= 0
        nop
zb4:

        # --- byte and halfword stores via base+offset ---
        la      $t7, dst
        li      $t8, 0xab
        sb      $t8, 0($t7)         # byte store
        li      $t8, 0xcdef
        sh      $t8, 2($t7)         # halfword store at offset 2

        # --- direct lui + ori to build a 32-bit constant ---
        # H&P Fig 2.18 idiom — done without the `li` pseudo-op.
        lui     $t9, 0xdead
        ori     $t9, $t9, 0xbeef    # $t9 now = 0xdeadbeef

        # --- mult / div with HI:LO and mfhi / mflo ---
        li      $t0, 7
        li      $t1, 6
        mult    $t0, $t1            # HI:LO = 7 * 6
        mflo    $s0                 # $s0 = 42 (low word)
        mfhi    $s1                 # $s1 = 0  (high word)
        li      $t0, 100
        li      $t1, 7
        div     $t0, $t1            # LO = quotient, HI = remainder
        mflo    $s0                 # $s0 = 14
        mfhi    $s1                 # $s1 = 2

        # --- a real loop (backward bne) ---
        # Target label is above current PC — exercises taken-branch
        # narration when the target address is lower than PC.
        li      $t2, 3
        li      $s2, 0
loop:   addu    $s2, $s2, $t2       # accumulator += counter
        addi    $t2, $t2, -1
        bne     $t2, $zero, loop    # iterate while counter != 0

        # --- R-type bitwise (register-to-register) ---
        # H&P teaches these alongside `and`. `nor` is the basis for `not`
        # (nor reg, reg, $zero is bitwise NOT).
        or      $s0, $t0, $t1       # bitwise OR
        xor     $s1, $t0, $t1       # bitwise XOR
        nor     $s2, $t0, $zero     # bitwise NOT of $t0 via NOR with $zero

        # --- sub (signed; traps on overflow, vs subu above which does not) ---
        sub     $s3, $t1, $t0       # 7 - 5 = 2 (well within range, no trap)

        # --- multu / divu (unsigned mul/div) ---
        # Contrast with mult/div above. With 0xfffffffe as unsigned ~4.3B,
        # the high word of the product is nonzero — easy to see in HI/LO.
        li      $t0, 0xfffffffe
        li      $t1, 3
        multu   $t0, $t1            # 64-bit unsigned product
        mflo    $s4
        mfhi    $s5
        divu    $t0, $t1            # unsigned divide
        mflo    $s4
        mfhi    $s5

        # --- lbu / lh / lhu (load-width + sign-extension contrast) ---
        # dst now holds {0xab, 0x00, 0xef, 0xcd} after the sb/sh above.
        # lb sign-extends a byte; lbu zero-extends — the classic ASCII bug
        # where any byte >= 0x80 becomes negative under lb.
        la      $t0, dst
        lb      $s6, 0($t0)         # 0xab → 0xffffffab  (signed -85)
        lbu     $s7, 0($t0)         # 0xab → 0x000000ab  (unsigned 171)
        lh      $t2, 2($t0)         # halfword 0xcdef → 0xffffcdef
        lhu     $t3, 2($t0)         # halfword 0xcdef → 0x0000cdef

        # --- j (unconditional jump; no $ra save, unlike jal) ---
        j       afterj
        nop                         # delay slot (executed in -delayed mode)
afterj:

        # --- procedure call + return (jal / jr $ra) ---
        li      $a0, 5
        jal     square              # call helper; result in $v0 = 25

        # --- jalr (indirect call via register; e.g. function pointer) ---
        la      $t9, square
        li      $a0, 8
        jalr    $t9                 # call $t9; return address goes to $ra
        # $v0 now = 64

        # --- print_int (syscall 1) ---
        addu    $a0, $v0, $zero     # $a0 = 64 (the squared result)
        li      $v0, 1              # syscall: print_int
        syscall

        # --- print_char (syscall 11) ---
        li      $a0, 0x0a           # ASCII newline
        li      $v0, 11             # syscall: print_char
        syscall

        # --- pseudo-op narration exercises ---
        # These are pseudo-ops the assembler expands. The explain header for
        # each should name the pseudo-op the student wrote, then narrate the
        # real expanded instruction(s). Multi-instruction expansions also
        # emit a "(continuation of ...)" hint on the second emitted insn.
        li      $t0, 5
        li      $t1, 7
        move    $s0, $t0            # single-inst pseudo-op (→ addu)
        not     $s1, $t0            # single-inst pseudo-op (→ nor with $0)
        neg     $s2, $t0            # single-inst pseudo-op (→ sub from $0)
        nop                         # single-inst pseudo-op (→ sll $0,$0,0)
        beqz    $zero, ps_b1        # single-inst zero-compare pseudo-op
        nop                         # never reached (always taken)
ps_b1:  bnez    $t0, ps_b2          # taken (5 != 0)
        nop
ps_b2:  blt     $t0, $t1, ps_b3     # multi-inst pseudo-op (→ slt + bne)
        nop
ps_b3:  bge     $t1, $t0, ps_b4     # multi-inst pseudo-op (→ slt + beq)
        nop
ps_b4:  b       ps_end              # unconditional branch pseudo-op (→ beq $0,$0)
        nop                         # never reached
ps_end:

        # --- input syscalls (commented; uncomment + pipe stdin to test) ---
        # These would block waiting on stdin. To exercise read_int /
        # read_string narration, uncomment and run:
        #     echo 42 | spimulator -explain -f tests/tt.explain.s
        #
        # li      $v0, 5              # syscall: read_int  → result in $v0
        # syscall
        # la      $a0, dst            # buffer address
        # li      $a1, 16             # buffer length
        # li      $v0, 8              # syscall: read_string
        # syscall

        la      $a0, hello          # reload (jal arg clobbered it)
        li      $v0, 4              # syscall: print_string
        syscall
        li      $v0, 10             # syscall: exit
        syscall

# --- helper procedure: square(x) -> x*x ---
# Argument in $a0, result in $v0. Exercises jal/jr round-trip.
square: mult    $a0, $a0
        mflo    $v0
        jr      $ra
