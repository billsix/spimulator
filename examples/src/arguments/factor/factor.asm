# (MIT license abbreviated for Part 7 demos)


# C source — see factor.c
#
#     int my_main(int argc, char **argv) {
#       int n = parse_int(argv[1]);
#       print_int(n); print_char(':');
#       for (int d = 2; d * d <= n; d++)
#         while (n % d == 0) { print_char(' '); print_int(d); n /= d; }
#       if (n > 1) { print_char(' '); print_int(n); }
#       print_char('\n');
#     }


#PURPOSE:  Trial-divide factorization.  argv-only; no I/O.
#          Pairs with sieve (which also generates primes).

        .data
usageMsg:   .asciiz "usage: factor N\n"
badNMsg:    .asciiz "factor: N must be >= 1\n"

        .text
        .globl main
main:
        move $s0, $ra

        li $t0, 2
        bne $a0, $t0, usage

        # n = atoi(argv[1])
        lw $a0, 4($a1)
        jal atoi
        move $s1, $v0
        blez $s1, badn

        # print_int(n); print_char(':')
        move $a0, $s1
        li $v0, 1
        syscall
        li $a0, ':'
        li $v0, 11
        syscall

        li $t0, 1
        beq $s1, $t0, done           # n == 1 -> just print newline

        li $s2, 2                    # d = 2

outer:
        # while (d * d <= n)
        mult $s2, $s2
        mflo $t0                     # d*d
        bgt $t0, $s1, after_loop

inner:
        # while (n % d == 0) { print " " + d; n /= d; }
        div $s1, $s2
        mfhi $t0                     # n % d
        mflo $t1                     # n / d
        bnez $t0, next_d

        # print " " + d
        li $a0, ' '
        li $v0, 11
        syscall
        move $a0, $s2
        li $v0, 1
        syscall

        move $s1, $t1                # n /= d
        j inner

next_d:
        addi $s2, $s2, 1
        j outer

after_loop:
        # if (n > 1) print " " + n
        li $t0, 1
        ble $s1, $t0, done
        li $a0, ' '
        li $v0, 11
        syscall
        move $a0, $s1
        li $v0, 1
        syscall

done:
        li $a0, '\n'
        li $v0, 11
        syscall
        move $ra, $s0
        li $v0, 0                    # exit status: __start passes this through syscall 17
        jr $ra

badn:
        li $v0, 4
        la $a0, badNMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall


atoi:
        li $v0, 0
        li $t1, 1
        lb $t0, ($a0)
        bne $t0, '-', atoi_loop
        li $t1, -1
        addi $a0, $a0, 1
atoi_loop:
        lb $t0, ($a0)
        blt $t0, '0', atoi_done
        bgt $t0, '9', atoi_done
        addi $t0, $t0, -48
        li $t2, 10
        mult $v0, $t2
        mflo $v0
        add $v0, $v0, $t0
        addi $a0, $a0, 1
        j atoi_loop
atoi_done:
        mult $v0, $t1
        mflo $v0
        jr $ra
