# -assemble writes each segment as one .word list.  This data segment ends
# one byte past a word (the .byte), and the list must still end in its last
# word, not in a trailing ", ".
	.data
	.word 0x11223344
	.byte 5
	.text
main:
	jr $ra
