# Recursion past the stack segment's limit: each frame takes 1024 bytes,
# and run-test.sh runs this with -lstack 65536, so the stack cannot grow
# enough.  The run stops with "Can't expand stack segment" and exits 1.
	.text
	.globl main
main:
	li $t0, 0
deeper:
	addiu $sp, $sp, -1024
	sw $t0, 0($sp)
	addiu $t0, $t0, 1
	b deeper
