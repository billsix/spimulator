# (MIT license abbreviated for Part 7 demos)


# C source — see factor.c


# Invocations:
#   spimulator -f factor.asm 360       # => 360: 2 2 2 3 3 5
#   spimulator -f factor.asm 65537     # => 65537: 65537   (prime)


#PURPOSE:  Trial-divide factorization.  argv-only; no I/O.
#          Pairs with sieve (which also generates primes).
#
#          STACK-VISIBLE STYLE: the two C locals `n` and `d` live in
#          stack slots; a scratch register holds only a working COPY
#          while an instruction needs it, then the result is stored
#          straight back.  argc / argv are used once at entry (before
#          the first syscall could clobber them), so they stay in
#          $a0 / $a1.  This routine makes a `jal atoi`, so it saves
#          $ra on the stack.  (atoi is a shared leaf helper, kept
#          register-based — see its own table.)
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  argc        | —           | $a0        | int       |   4   | entry only; tested (== 2), then dead
#  argv        | —           | $a1        | char **   |   4   | entry only; argv[1] fetched for atoi
#  n           | -12($fp)    | $t0 (copy) | int       |   4   | the number, divided down by its factors
#  d           | -16($fp)    | $t1 (copy) | int       |   4   | trial divisor, 2 upward
#  (saved $ra) | -4($fp)     | —          | code addr |   4   | return into the runtime (atoi uses jal)
#  (saved $fp) | -8($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
# ------------------------------------------------------------------
#  SNAPSHOT (input: factor 12; in `inner` with d = 2, n = 12,
#            12 % 2 == 0 so a factor is about to print)
#
#        main's stack frame             scratch
#   +--------------------------+       +----------------------+
#   | -4($fp)  saved $ra       |       | $t0 = 12 (copy of n)
#   | -8($fp)  saved $fp       |       | $t1 =  2 (copy of d)
#   | -12($fp) n       =  12   | <---- | $t0
#   | -16($fp) d       =   2   | <---- | $t1
#   +--------------------------+       +----------------------+
#         $sp = $fp - 16
#
#   Prints " 2", sets n = 6; divides again (" 2", n = 3); then d
#   advances to 3 (" 3", n = 1).  Final output: "12: 2 2 3".
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  args: 12
#  stdout:
#  | 12: 2 2 3

        .data
usageMsg:   .asciiz "usage: factor N\n"
badNMsg:    .asciiz "factor: N must be >= 1\n"

        .text
        .globl main
main:
        # ---- prologue: build main's 16-byte stack frame ------------
        addiu $sp, $sp, -16          # allocate the frame
        sw $ra, 12($sp)              # save $ra          -> -4($fp)
        sw $fp, 8($sp)               # save caller's $fp -> -8($fp)
        addiu $fp, $sp, 16           # $fp = top of our frame

        # if (argc != 2) usage;   (argc arrives in $a0)
        li $t0, 2
        bne $a0, $t0, usage

        # n = parse_int(argv[1]);  (argv arrives in $a1)
        lw $a0, 4($a1)               # $a0 = argv[1]
        jal atoi                     # $v0 = parsed int ($ra safe in its slot)
        sw $v0, -12($fp)             # n = atoi(argv[1])

        # if (n < 1) badn;
        lw $t0, -12($fp)             # $t0 = n
        blez $t0, badn

        # print_int(n); print_char(':');
        lw $a0, -12($fp)             # $a0 = n
        li $v0, 1
        syscall
        li $a0, ':'
        li $v0, 11
        syscall

        # if (n == 1) { print '\n'; return; }
        lw $t0, -12($fp)             # $t0 = n
        li $t1, 1
        beq $t0, $t1, done

        # d = 2;
        li $t0, 2
        sw $t0, -16($fp)             # d = 2

outer:
        # while (d * d <= n)
        lw $t0, -16($fp)             # $t0 = d
        mult $t0, $t0
        mflo $t1                     # $t1 = d*d
        lw $t0, -12($fp)             # $t0 = n
        bgt $t1, $t0, after_loop

inner:
        # while (n % d == 0) { print " " + d; n /= d; }
        lw $t0, -12($fp)             # $t0 = n
        lw $t1, -16($fp)             # $t1 = d
        div $t0, $t1
        mfhi $t2                     # $t2 = n % d
        bnez $t2, next_d             # remainder != 0 -> stop dividing by d

        # print_char(' '); print_int(d);
        li $a0, ' '
        li $v0, 11
        syscall
        lw $a0, -16($fp)             # $a0 = d
        li $v0, 1
        syscall

        # n /= d;
        lw $t0, -12($fp)             # $t0 = n
        lw $t1, -16($fp)             # $t1 = d
        div $t0, $t1
        mflo $t0                     # n / d
        sw $t0, -12($fp)             # store the new n back
        j inner

next_d:
        # d++;
        lw $t0, -16($fp)             # $t0 = d
        addi $t0, $t0, 1
        sw $t0, -16($fp)             # store the new d back
        j outer

after_loop:
        # if (n > 1) print " " + n;   (a prime factor larger than sqrt)
        lw $t0, -12($fp)             # $t0 = n
        li $t1, 1
        ble $t0, $t1, done
        li $a0, ' '
        li $v0, 11
        syscall
        lw $a0, -12($fp)             # $a0 = n
        li $v0, 1
        syscall

done:
        # print_char('\n');
        li $a0, '\n'
        li $v0, 11
        syscall

        # ---- epilogue: tear down the frame, return to the runtime --
        li $v0, 0                    # exit status: __start passes this through syscall 17
        lw $ra, -4($fp)              # restore $ra
        lw $fp, -8($fp)              # restore caller's $fp
        addiu $sp, $sp, 16           # pop the frame
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


# ---------- atoi subroutine -----------------------------------------
# Shared leaf helper (string -> signed int), kept register-based: it
# takes its arg in $a0, returns in $v0, and calls nothing, so it needs
# no frame.  Repeated per demo by the self-contained convention.
#
#  SYMBOL TABLE (routine: atoi — leaf helper, register-based)
#
#  C variable | memory addr | register | C type | bytes | role / notes
#  -----------+-------------+----------+--------+-------+------------------------------
#  (arg) str  | —           | $a0      | char * |   4   | address of the string, walked
#  value      | —           | $v0      | int    |   4   | accumulator; becomes the return
#  sign       | —           | $t1      | int    |   4   | +1 or -1
#  digit      | —           | $t0      | int    |   4   | one byte decoded to 0..9
#  ten        | —           | $t2      | int    |   4   | constant multiplier
#
# Input:  $a0 = address of null-terminated decimal string
# Output: $v0 = parsed value
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
