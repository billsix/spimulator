# jr.hb and jalr.hb: assembled to their own encodings (jr/jalr with the
# hazard-barrier bit, bit 10) and executed as jr/jalr.  The run-test.sh case
# also checks the encodings in the disassembly.
	.text
	.globl main
main:
	move $s7, $ra		# main's own return address

	# jalr.hb SRC1: link in $ra.
	la $t0, sub_ra
	li $s0, 0
	jalr.hb $t0
	li $t1, 1
	bne $s0, $t1, fail

	# jalr.hb DEST SRC1: link in $s1.
	la $t0, sub_s1
	li $s0, 0
	jalr.hb $s1, $t0
	li $t1, 2
	bne $s0, $t1, fail

	la $a0, passed
	li $v0, 4
	syscall
	move $ra, $s7
	jr $ra

sub_ra:
	li $s0, 1
	jr.hb $ra		# return through the hazard-barrier jr

sub_s1:
	li $s0, 2
	jr.hb $s1

fail:
	la $a0, failed
	li $v0, 4
	syscall
	li $v0, 10
	syscall

	.data
passed:	.asciiz "Passed all tests\n"
failed:	.asciiz "Failed: jalr.hb or jr.hb\n"
