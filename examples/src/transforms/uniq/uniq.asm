# (MIT license abbreviated)


# C source — see uniq.c
#
#     Collapse adjacent duplicate lines.  stdin-or-file argv shape.


#PURPOSE:  uniq.  Maintain `prev_line` buffer; compare each new
#          line to it.  Emit line + '\n' if it differs from prev,
#          then copy current into prev.

        .data
usageMsg:   .asciiz "usage: uniq [FILE|-]\n"
errMsg:     .asciiz "uniq: cannot open "
nlMsg:      .asciiz "\n"
prevLine:   .space 1024
curLine:    .space 1024
oneByte:    .space 1
prevLen:    .word -1                 # -1 = no previous line yet
curLen:     .word 0

        .text
        .globl main
main:
        move $s0, $ra
        li $s1, 0                    # fd = STDIN
        move $s4, $0                 # argv[1] address (for error msg)

        li $t0, 1
        beq $a0, $t0, read_setup
        li $t0, 2
        bne $a0, $t0, usage

        lw $s4, 4($a1)
        lb $t0, 0($s4)
        bne $t0, '-', do_open
        lb $t0, 1($s4)
        beq $t0, $0, read_setup

do_open:
        move $a0, $s4
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_failed
        move $s1, $v0

read_setup:
read_loop:
        li $v0, 14
        move $a0, $s1
        la $a1, oneByte
        li $a2, 1
        syscall
        blez $v0, after_read

        lb $t0, oneByte
        bne $t0, '\n', append

        # newline: emit_if_new
        jal emit_if_new
        la $t1, curLen
        sw $0, ($t1)
        j read_loop

append:
        # if (cur_len < 1024) cur_line[cur_len++] = c
        la $t1, curLen
        lw $t2, ($t1)
        li $t3, 1024
        bge $t2, $t3, read_loop
        la $t3, curLine
        add $t3, $t3, $t2
        sb $t0, ($t3)
        addi $t2, $t2, 1
        sw $t2, ($t1)
        j read_loop

after_read:
        # if (cur_len > 0) emit_if_new
        la $t1, curLen
        lw $t2, ($t1)
        blez $t2, close_and_exit
        jal emit_if_new

close_and_exit:
        beqz $s1, exit_ok
        li $v0, 16
        move $a0, $s1
        syscall
exit_ok:
        move $ra, $s0
        li $v0, 0                    # exit status: __start passes this through syscall 17
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


# ---------- emit_if_new ----------
# Compare curLine[0..curLen] to prevLine[0..prevLen].  If they
# differ (or there's no prev yet), write the current line +
# '\n' to stdout and copy current into prev.  Trashes lots of
# $t-regs; main uses $s* so it survives.
emit_if_new:
        # First-line case: prevLen == -1 -> always emit
        la $t0, prevLen
        lw $t1, ($t0)
        bltz $t1, do_emit

        # Compare lengths
        la $t0, curLen
        lw $t2, ($t0)
        bne $t1, $t2, do_emit

        # Lengths equal; compare bytes
        la $t3, prevLine
        la $t4, curLine
        li $t5, 0                    # index
cmp_loop:
        bge $t5, $t1, lines_match
        add $t6, $t3, $t5
        add $t7, $t4, $t5
        lb $t8, ($t6)
        lb $t9, ($t7)
        bne $t8, $t9, do_emit
        addi $t5, $t5, 1
        j cmp_loop

lines_match:
        jr $ra                       # same as prev — do nothing

do_emit:
        # write(STDOUT, curLine, curLen)
        li $v0, 15
        li $a0, 1
        la $a1, curLine
        la $t0, curLen
        lw $a2, ($t0)
        syscall

        # write(STDOUT, "\n", 1)
        li $v0, 15
        li $a0, 1
        la $a1, nlMsg
        li $a2, 1
        syscall

        # Copy curLine to prevLine; prevLen = curLen
        la $t0, curLen
        lw $t1, ($t0)
        la $t0, prevLen
        sw $t1, ($t0)

        la $t3, curLine
        la $t4, prevLine
        li $t5, 0
copy_loop:
        bge $t5, $t1, copy_done
        add $t6, $t3, $t5
        add $t7, $t4, $t5
        lb $t8, ($t6)
        sb $t8, ($t7)
        addi $t5, $t5, 1
        j copy_loop
copy_done:
        jr $ra
