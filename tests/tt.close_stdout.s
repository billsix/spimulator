# Close fd 1 (syscall 16), then print: the C engine's write fails silently and
# the program still exits 0.  The ports must not crash on the closed descriptor.
	.text
	.globl main
main:
	li $a0, 1
	li $v0, 16
	syscall
	la $a0, msg
	li $v0, 4
	syscall
	li $v0, 10
	syscall
	.data
msg:	.asciiz "hello\n"
