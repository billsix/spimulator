# cop2 is rejected: spim has no coprocessor 2.  Assembling this file must
# report that error and exit 2, not assemble the line to nothing.
	.text
	.globl main
main:
	cop2 0x10
	jr $ra
