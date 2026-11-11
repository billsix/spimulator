# The pair pseudo-instructions use a register and the next one, so starting
# a pair at register 31 would write register 32, past the register file.
# Each such line is an error on its own line (10, 11, 12, 13); the pairs
# from 30 assemble.  Assembling this file reports four errors and exits 2.
	.data
pair:	.word 1, 2
	.text
main:
	la $t0, pair
	ld $31, 0($t0)
	sd $ra, 0($t0)
	mfc1.d $31, $f30
	mtc1.d $30, $f31
	ld $30, 0($t0)
	mfc1.d $30, $f30
	jr $ra
