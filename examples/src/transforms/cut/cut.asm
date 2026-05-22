# (MIT license abbreviated)


# C source — see cut.c
#
#     cut -c N-M [FILE|-]:  print byte positions N..M (1-indexed,
#     inclusive) of each line.


#PURPOSE:  Range slicing on byte positions.  Parses "N-M" out of
#          argv[2].

        .data
usageMsg:   .asciiz "usage: cut -c N-M [FILE|-]\n"
badRangeMsg:.asciiz "cut: bad range; need N-M with 1<=N<=M\n"
errMsg:     .asciiz "cut: cannot open "
nlMsg:      .asciiz "\n"
line:       .space 4096
oneByte:    .space 1

        .text
        .globl main
main:
        move $s0, $ra

        # Need argc 3 or 4
        li $t0, 3
        beq $a0, $t0, argc_ok
        li $t0, 4
        beq $a0, $t0, argc_ok
        j usage

argc_ok:
        # argv[1] must be exactly "-c"
        lw $t0, 4($a1)
        lb $t1, 0($t0)
        bne $t1, '-', usage
        lb $t1, 1($t0)
        bne $t1, 'c', usage
        lb $t1, 2($t0)
        bnez $t1, usage

        # Parse argv[2] = "N-M"
        move $s4, $a1                # save argv
        lw $t0, 8($s4)               # argv[2]
        # parse N
        li $s1, 0                    # lo
parse_lo:
        lb $t1, ($t0)
        blt $t1, '0', check_dash
        bgt $t1, '9', check_dash
        li $t2, 10
        mult $s1, $t2
        mflo $s1
        addi $t1, $t1, -48
        add $s1, $s1, $t1
        addi $t0, $t0, 1
        j parse_lo

check_dash:
        bne $t1, '-', bad_range
        addi $t0, $t0, 1
        # parse M
        li $s2, 0                    # hi
        lb $t1, ($t0)
        blt $t1, '0', bad_range
        bgt $t1, '9', bad_range
parse_hi:
        lb $t1, ($t0)
        blt $t1, '0', range_done
        bgt $t1, '9', range_done
        li $t2, 10
        mult $s2, $t2
        mflo $s2
        addi $t1, $t1, -48
        add $s2, $s2, $t1
        addi $t0, $t0, 1
        j parse_hi

range_done:
        # bounds: 1 <= lo <= hi
        blez $s1, bad_range
        blt $s2, $s1, bad_range

        # fd selection
        li $s3, 0                    # fd = STDIN
        li $t0, 3
        beq $a0, $t0, read_setup     # no file arg

        lw $t0, 12($s4)              # argv[3]
        lb $t1, 0($t0)
        bne $t1, '-', do_open
        lb $t1, 1($t0)
        beq $t1, $0, read_setup

do_open:
        move $a0, $t0
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_failed
        move $s3, $v0

read_setup:
        li $s5, 0                    # len = 0

read_loop:
        li $v0, 14
        move $a0, $s3
        la $a1, oneByte
        li $a2, 1
        syscall
        blez $v0, after_read

        lb $t0, oneByte
        bne $t0, '\n', append

        # newline: emit line[lo-1..hi-1] capped at len
        jal emit_range
        li $a0, '\n'
        li $v0, 11
        syscall
        li $s5, 0
        j read_loop

append:
        # if (len < 4096) line[len++] = c
        li $t1, 4096
        bge $s5, $t1, read_loop
        la $t1, line
        add $t1, $t1, $s5
        sb $t0, ($t1)
        addi $s5, $s5, 1
        j read_loop

after_read:
        # if (len > 0) flush final line
        blez $s5, close_and_exit
        jal emit_range
        li $a0, '\n'
        li $v0, 11
        syscall

close_and_exit:
        beqz $s3, exit_ok
        li $v0, 16
        move $a0, $s3
        syscall
exit_ok:
        move $ra, $s0
        li $v0, 0                    # exit status: __start passes this through syscall 17
        jr $ra

bad_range:
        li $v0, 4
        la $a0, badRangeMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall

open_failed:
        li $v0, 4
        la $a0, errMsg
        syscall
        lw $a0, 12($s4)
        li $v0, 4
        syscall
        li $v0, 4
        la $a0, nlMsg
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


# ---------- emit_range — print line[lo-1..min(hi,len)-1] -----------
emit_range:
        addi $t0, $s1, -1            # i = lo - 1
er_loop:
        bge $t0, $s2, er_done        # i >= hi -> done
        bge $t0, $s5, er_done        # i >= len -> done
        la $t1, line
        add $t1, $t1, $t0
        lb $a0, ($t1)
        li $v0, 11
        syscall
        addi $t0, $t0, 1
        j er_loop
er_done:
        jr $ra
