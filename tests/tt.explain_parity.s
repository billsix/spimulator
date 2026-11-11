# Teaching-mode parity: the program's stdout and exit status must be the
# same with and without -explain.  The unaligned load raises Exception 4
# (AdEL); the default handler reports it and skips the load, and the
# program goes on to print and exit, so the run exits 128+4 = 132.
        .data
done:   .asciiz "done\n"
        .text
main:   li $v0, 1
        li $a0, 7
        syscall
        li $t0, 0x10010001
        lw $t1, 0($t0)
        li $v0, 4
        la $a0, done
        syscall
        li $v0, 0
        jr $ra
