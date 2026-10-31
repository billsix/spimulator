# (MIT license abbreviated)


# C source — see cut.c
#
#     cut -c N-M [FILE|-]:  print byte positions N..M (1-indexed,
#     inclusive) of each line.


# Invocations:
#   spimulator -f cut.asm -c 1-3              # reads stdin, cols 1..3
#   spimulator -f cut.asm -c 5-10 /etc/motd   # reads the file
#   echo hello world | spimulator -f cut.asm -c 7-11   # prints "world"


#PURPOSE:  Range slicing on byte positions.  Parses "N-M" out of argv[2].
#
#          STACK-VISIBLE STYLE: main's C locals — the range lo/hi, the fd, the
#          argv vector, and the current line length len — live in stack slots; a
#          scratch register holds only a working COPY.  lo/hi/len are passed to
#          emit_range as arguments ($a0/$a1/$a2) rather than as shared callee-save
#          registers; emit_range homes its own index `i`.  The line buffer is
#          file-scope, so it stays in .data.  main makes `jal emit_range`, so it
#          saves $ra; emit_range is leaf.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr  | register   | C type    | bytes | role / notes
#  ------------+--------------+------------+-----------+-------+---------------------------
#  lo          | -12($fp)     | $t1 (copy) | int       |   4   | range start (1-indexed)
#  hi          | -16($fp)     | $t1 (copy) | int       |   4   | range end (1-indexed, inclusive)
#  fd          | -20($fp)     | $t0 (copy) | int       |   4   | STDIN (0) or opened file descriptor
#  argv        | -24($fp)     | $a1 (copy) | char **   |   4   | kept for argv[2]/argv[3] + error msg
#  len         | -28($fp)     | $t2 (copy) | int       |   4   | current line length
#  line[]      | line (.data) | —          | char[4096]| 4096  | file-scope line buffer
#  (saved $ra) | -4($fp)      | —          | code addr |   4   | jal emit_range
#  (saved $fp) | -8($fp)      | —          | frame ptr |   4   | caller's frame pointer
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  args: -c 1-3
#  stdin: transforms/cut/cut.input
#  stdout:
#  | abc
#  | ghi

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
        addiu $sp, $sp, -32
        sw $ra, 28($sp)              # -> -4($fp)
        sw $fp, 24($sp)              # -> -8($fp)
        addiu $fp, $sp, 32

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

        # Parse argv[2] = "N-M"   (argc stays in $a0 for the fd choice below)
        sw $a1, -24($fp)             # argv
        lw $t0, 8($a1)               # argv[2]
        li $t1, 0                    # lo accumulator
parse_lo:
        lb $t2, ($t0)
        blt $t2, '0', check_dash
        bgt $t2, '9', check_dash
        li $t3, 10
        mult $t1, $t3
        mflo $t1
        addi $t2, $t2, -48
        add $t1, $t1, $t2
        addi $t0, $t0, 1
        j parse_lo

check_dash:
        bne $t2, '-', bad_range
        sw $t1, -12($fp)             # lo
        addi $t0, $t0, 1
        li $t1, 0                    # hi accumulator
        lb $t2, ($t0)
        blt $t2, '0', bad_range
        bgt $t2, '9', bad_range
parse_hi:
        lb $t2, ($t0)
        blt $t2, '0', range_done
        bgt $t2, '9', range_done
        li $t3, 10
        mult $t1, $t3
        mflo $t1
        addi $t2, $t2, -48
        add $t1, $t1, $t2
        addi $t0, $t0, 1
        j parse_hi

range_done:
        sw $t1, -16($fp)             # hi
        # bounds: 1 <= lo <= hi
        lw $t1, -12($fp)             # lo
        blez $t1, bad_range
        lw $t2, -16($fp)             # hi
        blt $t2, $t1, bad_range

        # fd selection
        li $t0, 0
        sw $t0, -20($fp)             # fd = STDIN
        li $t0, 3
        beq $a0, $t0, read_setup     # argc == 3: no file arg

        lw $t0, -24($fp)             # argv
        lw $t0, 12($t0)              # argv[3]
        lb $t1, 0($t0)
        bne $t1, '-', do_open
        lb $t1, 1($t0)
        beq $t1, $0, read_setup

do_open:
        move $a0, $t0                # argv[3]
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_failed
        sw $v0, -20($fp)             # fd

read_setup:
        li $t0, 0
        sw $t0, -28($fp)             # len = 0

read_loop:
        li $v0, 14
        lw $a0, -20($fp)             # fd
        la $a1, oneByte
        li $a2, 1
        syscall
        blez $v0, after_read

        lb $t0, oneByte
        bne $t0, '\n', append

        # newline: emit_range(lo, hi, len); print '\n'; len = 0
        lw $a0, -12($fp)             # lo
        lw $a1, -16($fp)             # hi
        lw $a2, -28($fp)             # len
        jal emit_range
        li $a0, '\n'
        li $v0, 11
        syscall
        li $t0, 0
        sw $t0, -28($fp)             # len = 0
        j read_loop

append:
        # if (len < 4096) line[len++] = c
        lw $t2, -28($fp)             # len
        li $t1, 4096
        bge $t2, $t1, read_loop
        la $t1, line
        add $t1, $t1, $t2
        sb $t0, ($t1)
        addi $t2, $t2, 1
        sw $t2, -28($fp)             # len++
        j read_loop

after_read:
        # if (len > 0) flush final line
        lw $t2, -28($fp)             # len
        blez $t2, close_and_exit
        lw $a0, -12($fp)             # lo
        lw $a1, -16($fp)             # hi
        lw $a2, -28($fp)             # len
        jal emit_range
        li $a0, '\n'
        li $v0, 11
        syscall

close_and_exit:
        lw $t0, -20($fp)             # fd
        beqz $t0, exit_ok
        li $v0, 16
        lw $a0, -20($fp)
        syscall
exit_ok:
        li $v0, 0                    # exit status: __start passes this through syscall 17
        lw $ra, -4($fp)
        lw $fp, -8($fp)
        addiu $sp, $sp, 32
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
        lw $t0, -24($fp)             # argv
        lw $a0, 12($t0)              # argv[3]
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


# ---------- emit_range(lo=$a0, hi=$a1, len=$a2) — leaf ----------
# print line[lo-1 .. min(hi,len)-1].  i lives on this frame; the args stay in
# $a1/$a2 (preserved across the print syscall, which only touches $a0/$v0).
emit_range:
        addiu $sp, $sp, -8
        sw $fp, 4($sp)               # -> -4($fp)
        addiu $fp, $sp, 8
        addi $t0, $a0, -1            # i = lo - 1
        sw $t0, -8($fp)
er_loop:
        lw $t0, -8($fp)              # i
        bge $t0, $a1, er_done        # i >= hi -> done
        bge $t0, $a2, er_done        # i >= len -> done
        la $t1, line
        add $t1, $t1, $t0
        lb $a0, ($t1)
        li $v0, 11
        syscall
        lw $t0, -8($fp)
        addi $t0, $t0, 1
        sw $t0, -8($fp)              # i++
        j er_loop
er_done:
        lw $fp, -4($fp)
        addiu $sp, $sp, 8
        jr $ra
