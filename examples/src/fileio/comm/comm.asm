# (MIT license abbreviated)


# C source — see comm.c
#
#     Three-column line-by-line diff of two sorted files.


#PURPOSE:  Open TWO files for reading.  Maintain a current-line
#          buffer for each; read both, compare, emit, advance
#          whichever is lower.


        .data
usageMsg:   .asciiz "usage: comm A B\n"
errMsg:     .asciiz "comm: cannot open "
nlMsg:      .asciiz "\n"
tab1:       .asciiz "\t"
tab2:       .asciiz "\t\t"
oneByte:    .space 1
lineA:      .space 1024
lineB:      .space 1024
lenA:       .word -1
lenB:       .word -1

        .text
        .globl main
main:
        move $s0, $ra

        li $t0, 3
        bne $a0, $t0, usage

        move $s7, $a1                # argv

        # fa = open(argv[1])
        lw $a0, 4($s7)
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_a_failed
        move $s1, $v0                # fa

        # fb = open(argv[2])
        lw $a0, 8($s7)
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_b_failed
        move $s2, $v0                # fb

        # Initial reads
        move $a0, $s1
        la $a1, lineA
        jal read_line
        la $t0, lenA
        sw $v0, ($t0)

        move $a0, $s2
        la $a1, lineB
        jal read_line
        la $t0, lenB
        sw $v0, ($t0)

loop:
        la $t0, lenA
        lw $s3, ($t0)                # la
        la $t0, lenB
        lw $s4, ($t0)                # lb

        # both EOF?
        bltz $s3, check_b_only
        # la >= 0
        bltz $s4, only_a

        # both valid: compare
        la $a0, lineA
        la $a1, lineB
        jal line_cmp
        bltz $v0, only_a
        bgtz $v0, only_b

        # equal: in both
        la $a0, tab2
        li $v0, 4
        syscall
        la $a0, lineA
        li $v0, 4
        syscall
        la $a0, nlMsg
        li $v0, 4
        syscall

        # advance both
        move $a0, $s1
        la $a1, lineA
        jal read_line
        la $t0, lenA
        sw $v0, ($t0)
        move $a0, $s2
        la $a1, lineB
        jal read_line
        la $t0, lenB
        sw $v0, ($t0)
        j loop

only_a:
        la $a0, lineA
        li $v0, 4
        syscall
        la $a0, nlMsg
        li $v0, 4
        syscall
        move $a0, $s1
        la $a1, lineA
        jal read_line
        la $t0, lenA
        sw $v0, ($t0)
        j loop

only_b:
        la $a0, tab1
        li $v0, 4
        syscall
        la $a0, lineB
        li $v0, 4
        syscall
        la $a0, nlMsg
        li $v0, 4
        syscall
        move $a0, $s2
        la $a1, lineB
        jal read_line
        la $t0, lenB
        sw $v0, ($t0)
        j loop

check_b_only:
        bltz $s4, exit_ok
        # la < 0 and lb >= 0
        j only_b

exit_ok:
        li $v0, 16
        move $a0, $s1
        syscall
        li $v0, 16
        move $a0, $s2
        syscall
        move $ra, $s0
        jr $ra

open_a_failed:
        li $v0, 4
        la $a0, errMsg
        syscall
        lw $a0, 4($s7)
        li $v0, 4
        syscall
        la $a0, nlMsg
        li $v0, 4
        syscall
        li $a0, 1
        li $v0, 17
        syscall

open_b_failed:
        # close fa
        li $v0, 16
        move $a0, $s1
        syscall

        li $v0, 4
        la $a0, errMsg
        syscall
        lw $a0, 8($s7)
        li $v0, 4
        syscall
        la $a0, nlMsg
        li $v0, 4
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


# ---------- read_line(fd in $a0, buf in $a1) ----------
# Returns $v0 = length on success, -1 on EOF before any byte.
# NUL-terminates buf.  Strips trailing '\n'.
read_line:
        move $t0, $a0                # fd
        move $t1, $a1                # buf base
        li $t2, 0                    # len

rl_loop:
        # n = read(fd, &oneByte, 1)
        li $v0, 14
        move $a0, $t0
        la $a1, oneByte
        li $a2, 1
        syscall

        blez $v0, rl_eof

        lb $t3, oneByte
        beq $t3, '\n', rl_done

        # if (len < 1023) buf[len++] = c
        li $t4, 1023
        bge $t2, $t4, rl_loop
        add $t4, $t1, $t2
        sb $t3, ($t4)
        addi $t2, $t2, 1
        j rl_loop

rl_eof:
        beqz $t2, rl_eof_empty
        # final line without newline
rl_done:
        add $t4, $t1, $t2
        sb $0, ($t4)                 # NUL-terminate
        move $v0, $t2
        jr $ra

rl_eof_empty:
        li $v0, -1
        jr $ra


# ---------- line_cmp($a0, $a1) -> $v0 (negative/zero/positive) ----
line_cmp:
        lb $t0, ($a0)
        lb $t1, ($a1)
        beq $t0, $t1, lc_advance
        sub $v0, $t0, $t1
        jr $ra
lc_advance:
        beq $t0, $0, lc_equal
        addi $a0, $a0, 1
        addi $a1, $a1, 1
        j line_cmp
lc_equal:
        li $v0, 0
        jr $ra
