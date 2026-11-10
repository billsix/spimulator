# SPIM S20 MIPS simulator.
# Regression tests for the engine fixes made before the ports
# (tasks/engine-port-prereq-c-fixes.md; review record
# tasks/review/2026/11/10/repo-review.md, the section number by each case).
# Each case branches to `fail` with a case number in $a0 on a wrong
# result; reaching the end prints "Passed all tests".
#
# Cases 5 and 6 test the instruction DECODER, which runs only when a program
# stores a word into its own text segment at run time (the assembler builds
# its instructions directly, and a `.word` in .text goes to the data
# segment).  So each stores the encoding over a placeholder `nop` with `sw`,
# then executes it.
#
# Driver:  spimulator -exception_file src/exceptions.s -f tests/tt.engine_fixes.s

        .data
fail_msg:   .asciiz "Failed engine-fixes case "
pass_msg:   .asciiz "Passed all tests\n"
newline:    .asciiz "\n"

# 3.20: `,` separates operands; it is not a decimal point.
two_words:  .word 1,2

# 3.5: forward data references keep their offset and their width.
fwd_word:   .word later+4
fwd_byte:   .byte later
guard_byte: .byte 0x5a          # must survive the .byte resolution
            .align 2
fwd_half:   .half later+2
guard_half: .half 0x1234        # must survive the .half resolution
            .align 2
later:      .word 0

one:        .float 1.0
two:        .float 2.0
three:      .float 3.0

        .text
        .globl main
main:
        # --- 3.20: .word 1,2 is two words --------------------------------
        li $a0, 1
        la $t0, two_words
        lw $t1, 0($t0)
        li $t2, 1
        bne $t1, $t2, fail
        lw $t1, 4($t0)
        li $t2, 2
        bne $t1, $t2, fail

        # --- 3.4: divu 0x80000000 / 0xffffffff: LO = 0, HI = 0x80000000 ----
        li $a0, 2
        li $t0, 0x80000000
        li $t1, 0xffffffff
        divu $t0, $t1
        mflo $t2
        bne $t2, $zero, fail
        mfhi $t2
        bne $t2, $t0, fail

        # --- 3.13: mult 0x80000000 * 1 = 0xffffffff:80000000 (no signed UB) --
        li $a0, 3
        li $t0, 0x80000000
        li $t1, 1
        mult $t0, $t1
        mflo $t2
        bne $t2, $t0, fail
        mfhi $t2
        li $t3, -1
        bne $t2, $t3, fail

        # --- 3.5: forward .word lab+4, .byte lab, .half lab+2 ---------------
        li $a0, 4
        la $t0, later
        lw $t1, fwd_word
        addiu $t2, $t0, 4
        bne $t1, $t2, fail
        lbu $t1, fwd_byte
        andi $t2, $t0, 0xff
        bne $t1, $t2, fail
        lbu $t1, guard_byte
        li $t2, 0x5a
        bne $t1, $t2, fail
        lhu $t1, fwd_half
        addiu $t2, $t0, 2
        andi $t2, $t2, 0xffff
        bne $t1, $t2, fail
        lhu $t1, guard_half
        li $t2, 0x1234
        bne $t1, $t2, fail

        # --- 3.7: an encoded c.lt.s $f2, $f4 compares $f2, not $f0 ---------
        li $a0, 5
        l.s $f0, three
        l.s $f2, one
        l.s $f4, two
        li $t5, 0x4604103c              # c.lt.s $f2, $f4: 1 < 2 is true
        la $t6, patch_clt
        sw $t5, 0($t6)
patch_clt:
        nop                             # overwritten by the sw above
        bc1f fail

        # --- 3.7: an encoded movn.s $f6, $f2, $t1 copies $f2 ---------------
        li $a0, 6
        l.s $f6, three
        li $t1, 1
        li $t5, 0x46091193              # movn.s $f6, $f2, $t1
        la $t6, patch_movn
        sw $t5, 0($t6)
patch_movn:
        nop                             # overwritten by the sw above
        c.eq.s $f6, $f2
        bc1f fail

        # --- 3.8: movn.s $f6, $f2, $t1 assembles with rt = 9 ---------------
        li $a0, 7
        la $t0, movn_site
        lw $t1, 0($t0)
        li $t2, 0x46091193
        bne $t1, $t2, fail

        # --- 3.10: a branch more than 32 KB forward reaches its target -----
        li $a0, 8
        li $t0, 0
        beq $zero, $zero, far_target
        li $t0, 1                        # not reached (no delay slot)
        j fail
movn_site:
        movn.s $f6, $f2, $t1             # read as data by case 7
        .align 16                         # 64 KB of padding
far_target:
        bne $t0, $zero, fail

        # --- done -----------------------------------------------------------
        la $a0, pass_msg
        li $v0, 4
        syscall
        li $v0, 0
        jr $ra

fail:
        move $t9, $a0
        la $a0, fail_msg
        li $v0, 4
        syscall
        move $a0, $t9
        li $v0, 1
        syscall
        la $a0, newline
        li $v0, 4
        syscall
        li $v0, 17
        li $a0, 1
        syscall
