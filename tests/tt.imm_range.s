# An immediate that doesn't fit its 16-bit field: the assembler reports it on
# the instruction's own line (line 7), not the next one, then widens the code.
	.text
	.globl main
main:
	li $v0, 0
	addi $t0, $zero, 0xff98
	jr $ra
