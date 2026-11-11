# Four COP1 words (0x46...) whose low bits match COP1X (0x4c...) rows decode
# to the COP1 instruction, as GNU as and objdump give them
# (mips64-linux-gnu-as -mabi=32 -mips32r2 -mfp64).  main stores each word
# over a nop slot, runs the floor.w.s and trunc.w.s ones on 2.75, and
# prints the results; run-test.sh also disassembles the stored words.
	.data
words:
	.word 0x46001008	# round.l.s $f0, $f2
	.word 0x46001009	# trunc.l.s $f0, $f2
	.word 0x4600100d	# trunc.w.s $f0, $f2
	.word 0x4600100f	# floor.w.s $f0, $f2
value:
	.float -2.75
newline:
	.asciiz "\n"

	.text
	.globl main
main:
	addiu $sp, $sp, -4
	sw $ra, 0($sp)
	la $t0, words
	lw $t1, 0($t0)
	sw $t1, slot_round_l
	lw $t1, 4($t0)
	sw $t1, slot_trunc_l
	lw $t1, 8($t0)
	sw $t1, slot_trunc_w
	lw $t1, 12($t0)
	sw $t1, slot_floor_w
	l.s $f2, value
	jal run_trunc_w		# -2.75 truncated: -2
	jal print_f0
	jal run_floor_w		# -2.75 floored: -3
	jal print_f0
	lw $ra, 0($sp)
	addiu $sp, $sp, 4
	li $v0, 0
	jr $ra

print_f0:
	mfc1 $a0, $f0
	li $v0, 1
	syscall
	la $a0, newline
	li $v0, 4
	syscall
	jr $ra

run_trunc_w:
	j slot_trunc_w
run_floor_w:
	j slot_floor_w

slot_round_l:
	nop			# round.l.s $f0, $f2, never run
slot_trunc_l:
	nop			# trunc.l.s $f0, $f2, never run
slot_trunc_w:
	nop			# trunc.w.s $f0, $f2
	jr $ra
slot_floor_w:
	nop			# floor.w.s $f0, $f2
	jr $ra
