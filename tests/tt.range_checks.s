# Out-of-range operands the assembler must reject, each on its own line:
# .align takes 0..31 (line 9; the engine shifts a 32-bit mask by it), and an
# FP register is $f0..$f31 (line 12).  Assembling this file reports both
# errors and exits 2.
	.data
	.word 1
	.align 2
	.word 2
	.align 32
	.text
main:
	mov.s $f32, $f0
	jr $ra
