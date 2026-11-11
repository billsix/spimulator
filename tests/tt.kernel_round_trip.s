# A user -> kernel -> user round trip: teq raises a trap, the default
# exception handler runs in kernel mode and returns with eret, and the
# program carries on in user mode after the trapping instruction.  It
# prints the Status register's EXL bit (bit 1) before and after the trap:
# 0 both times, since eret clears it.
	.data
newline: .asciiz "\n"
	.text
	.globl main
main:
	mfc0 $a0, $12		# Status
	andi $a0, $a0, 2	# EXL
	li $v0, 1
	syscall
	la $a0, newline
	li $v0, 4
	syscall
	teq $zero, $zero	# trap: always equal
	mfc0 $a0, $12
	andi $a0, $a0, 2
	li $v0, 1
	syscall
	la $a0, newline
	li $v0, 4
	syscall
	li $v0, 0
	jr $ra
