# (MIT license abbreviated)


# C source — see nl.c
#
#     for each line: print "     N\t<line>" with N right-aligned
#     in 6 chars, then \t, then the line itself.


# Invocations:
#   spimulator -f nl.asm             # reads stdin
#   spimulator -f nl.asm -           # reads stdin (explicit "-")
#   spimulator -f nl.asm /etc/motd   # reads the file


#PURPOSE:  Number lines.  Width-padded int output via a `print_padded` helper
#          that counts digits and emits leading spaces.
#
#          STACK-VISIBLE STYLE: main's C locals — fd, the argv[1] path, the
#          running line number `line_num`, and the `line_started` flag — live in
#          stack slots; a scratch register holds only a working COPY.  main makes
#          `jal print_padded`, so it saves $ra.  print_padded is a register-based
#          display helper (like print_uint).  The one-byte read pad stays .data.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable    | memory addr | register   | C type    | bytes | role / notes
#  --------------+-------------+------------+-----------+-------+---------------------------
#  fd            | -12($fp)    | $t0 (copy) | int       |   4   | STDIN (0) or opened file descriptor
#  argv1         | -16($fp)    | $t0 (copy) | char *    |   4   | argv[1]; "-"/FILE, kept for error msg
#  line_num      | -20($fp)    | $t0 (copy) | int       |   4   | current line number (1-based)
#  line_started  | -24($fp)    | $t0 (copy) | int       |   4   | has the current line's number been printed?
#  (saved $ra)   | -4($fp)     | —          | code addr |   4   | jal print_padded
#  (saved $fp)   | -8($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
#  No #CANONICAL block: each line is prefixed with a right-aligned number and a
#  tab ("     1\t..."), significant leading whitespace a comment anchor can't
#  carry.  Gated by before/after output equivalence.

        .data
usageMsg:   .asciiz "usage: nl [FILE|-]\n"
errMsg:     .asciiz "nl: cannot open "
nlMsg:      .asciiz "\n"
oneByte:    .space 1

        .text
        .globl main
main:
        addiu $sp, $sp, -24
        sw $ra, 20($sp)              # -> -4($fp)
        sw $fp, 16($sp)              # -> -8($fp)
        addiu $fp, $sp, 24

        li $t0, 0
        sw $t0, -12($fp)             # fd = STDIN (0)
        sw $zero, -16($fp)           # argv1 = 0
        li $t0, 1
        sw $t0, -20($fp)             # line_num = 1
        li $t0, 0
        sw $t0, -24($fp)             # line_started = 0

        li $t0, 1
        beq $a0, $t0, read_loop
        li $t0, 2
        bne $a0, $t0, usage
        lw $t0, 4($a1)
        sw $t0, -16($fp)             # argv1
        lb $t1, 0($t0)
        bne $t1, '-', do_open
        lb $t1, 1($t0)
        beq $t1, $0, read_loop

do_open:
        lw $a0, -16($fp)
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_failed
        sw $v0, -12($fp)             # fd

read_loop:
        li $v0, 14
        lw $a0, -12($fp)             # fd
        la $a1, oneByte
        li $a2, 1
        syscall
        blez $v0, after_read

        # if (!line_started) print padded N + '\t'
        lw $t0, -24($fp)             # line_started
        bnez $t0, emit_char
        lw $a0, -20($fp)             # line_num
        li $a1, 6
        jal print_padded
        li $a0, '\t'
        li $v0, 11
        syscall
        li $t0, 1
        sw $t0, -24($fp)             # line_started = 1

emit_char:
        lb $a0, oneByte
        li $v0, 11
        syscall
        bne $a0, '\n', read_loop
        lw $t0, -20($fp)
        addi $t0, $t0, 1
        sw $t0, -20($fp)             # line_num++
        li $t0, 0
        sw $t0, -24($fp)             # line_started = 0
        j read_loop

after_read:
        lw $t0, -12($fp)             # fd
        beqz $t0, exit_ok
        li $v0, 16
        lw $a0, -12($fp)
        syscall
exit_ok:
        li $v0, 0
        lw $ra, -4($fp)
        lw $fp, -8($fp)
        addiu $sp, $sp, 24
        jr $ra

open_failed:
        li $v0, 4
        la $a0, errMsg
        syscall
        li $v0, 4
        lw $a0, -16($fp)             # argv1
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


# ---------- print_padded(int n, int width) — register-based display helper ----
# Count digits, emit (width - digits) leading spaces, then print_int(n).
# Input:  $a0 = n, $a1 = width
print_padded:
        move $t0, $a0                # save n
        move $t1, $a1                # save width

        # Count digits in $t0
        li $t2, 1                    # digits = 1
        move $a0, $t0                # work value
pp_digit_loop:
        li $a1, 10
        bge $a0, $a1, pp_more_digits
        j pp_have_digits
pp_more_digits:
        div $a0, $a1
        mflo $a0
        addi $t2, $t2, 1
        j pp_digit_loop

pp_have_digits:
        # spaces = width - digits
        sub $t1, $t1, $t2
pp_space_loop:
        blez $t1, pp_print_n
        li $a0, ' '
        li $v0, 11
        syscall
        addi $t1, $t1, -1
        j pp_space_loop

pp_print_n:
        move $a0, $t0
        li $v0, 1
        syscall
        jr $ra
