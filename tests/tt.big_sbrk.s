# An sbrk of about 2 GB: the data segment's new size wraps negative as a C int,
# so the C engine's realloc fails ("realloc failed in expand_data", exit 255).
# The ports must give the same message and status (port/harness/corpus.txt).
	.text
	.globl main
main:
	li $a0, 0x7ffff000
	li $v0, 9
	syscall
	lw $a0, 0x10000000
	li $v0, 10
	syscall
