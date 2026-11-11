# Run with -sstack 1073741824 -lstack 2000000000: touching just below the stack
# doubles it past 2 GB, the new size wraps negative as a C int, and the C
# engine's xmalloc fails ("Out of memory at request for ... bytes.", exit 255).
	.text
	.globl main
main:
	li $t0, 0x3ffffff0
	sw $zero, 0($t0)
	li $v0, 10
	syscall
