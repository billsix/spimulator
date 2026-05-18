# (MIT license abbreviated)


# C source — see 39-base64.c
#
#     RFC 4648 base64 encode.  3 input bytes -> 4 output chars.
#     '=' padding for partial final group.  Wrap at 76 cols.


#PURPOSE:  base64 encode.  Bit packing across byte triples.

        .data
usageMsg:   .asciiz "usage: base64 [FILE|-]\n"
errMsg:     .asciiz "base64: cannot open "
nlMsg:      .asciiz "\n"
alpha:      .asciiz "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
oneByte:    .space 1

        .text
        .globl main
main:
        move $s0, $ra
        li $s1, 0                    # fd = STDIN
        move $s4, $0
        li $s5, 0                    # out_col

        li $t0, 1
        beq $a0, $t0, encode_loop_setup
        li $t0, 2
        bne $a0, $t0, usage

        lw $s4, 4($a1)
        lb $t0, 0($s4)
        bne $t0, '-', do_open
        lb $t0, 1($s4)
        beq $t0, $0, encode_loop_setup

do_open:
        move $a0, $s4
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_failed
        move $s1, $v0

encode_loop_setup:
        # Read up to 3 bytes into trio (held in $t0, $t1, $t2).
        # $s2 = filled (0..3)
trio_loop:
        li $t0, 0                    # b0
        li $t1, 0                    # b1
        li $t2, 0                    # b2
        li $s2, 0                    # filled

        # Read byte 0
        li $v0, 14
        move $a0, $s1
        la $a1, oneByte
        li $a2, 1
        syscall
        blez $v0, after_b0
        lbu $t0, oneByte
        li $s2, 1

        # Read byte 1
        li $v0, 14
        move $a0, $s1
        la $a1, oneByte
        li $a2, 1
        syscall
        blez $v0, encode
        lbu $t1, oneByte
        li $s2, 2

        # Read byte 2
        li $v0, 14
        move $a0, $s1
        la $a1, oneByte
        li $a2, 1
        syscall
        blez $v0, encode
        lbu $t2, oneByte
        li $s2, 3
        j encode

after_b0:
        # No data at all -> done
        j after_all

encode:
        # Emit 4 chars from (b0, b1, b2).
        # c0 = alpha[(b0 >> 2) & 0x3f]
        srl $t3, $t0, 2
        andi $t3, $t3, 0x3f
        la $a0, alpha
        add $a0, $a0, $t3
        lb $a0, ($a0)
        jal emit_char

        # c1 = alpha[((b0 << 4) | (b1 >> 4)) & 0x3f]
        sll $t3, $t0, 4
        srl $t4, $t1, 4
        or $t3, $t3, $t4
        andi $t3, $t3, 0x3f
        la $a0, alpha
        add $a0, $a0, $t3
        lb $a0, ($a0)
        jal emit_char

        # c2: if filled > 1, alpha[((b1 << 2) | (b2 >> 6)) & 0x3f] else '='
        li $t3, 1
        ble $s2, $t3, pad2
        sll $t3, $t1, 2
        srl $t4, $t2, 6
        or $t3, $t3, $t4
        andi $t3, $t3, 0x3f
        la $a0, alpha
        add $a0, $a0, $t3
        lb $a0, ($a0)
        jal emit_char
        j c3

pad2:
        li $a0, '='
        jal emit_char

c3:
        # c3: if filled > 2, alpha[b2 & 0x3f] else '='
        li $t3, 2
        ble $s2, $t3, pad3
        andi $t3, $t2, 0x3f
        la $a0, alpha
        add $a0, $a0, $t3
        lb $a0, ($a0)
        jal emit_char
        j after_encode

pad3:
        li $a0, '='
        jal emit_char

after_encode:
        # if filled < 3, we're done (final group)
        li $t3, 3
        beq $s2, $t3, trio_loop
        j after_all

after_all:
        # if out_col > 0, print '\n'
        blez $s5, close_and_exit
        li $a0, '\n'
        li $v0, 11
        syscall

close_and_exit:
        beqz $s1, exit_ok
        li $v0, 16
        move $a0, $s1
        syscall
exit_ok:
        move $ra, $s0
        jr $ra

open_failed:
        li $v0, 4
        la $a0, errMsg
        syscall
        li $v0, 4
        move $a0, $s4
        syscall
        li $v0, 4
        la $a0, nlMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall


# ---------- emit_char(c in $a0) ----------
# Prints c.  If out_col reaches 76, prints '\n' and resets.
emit_char:
        # save c
        move $t9, $a0
        li $v0, 11
        syscall
        addi $s5, $s5, 1
        li $t0, 76
        bne $s5, $t0, ec_done
        li $a0, '\n'
        li $v0, 11
        syscall
        li $s5, 0
ec_done:
        jr $ra
