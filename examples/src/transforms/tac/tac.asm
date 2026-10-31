# (MIT license abbreviated)


# C source — see tac.c
#
#     Read all of stdin/file into a sbrk-grown buffer.
#     Walk backwards: find each line, write it.  Final line
#     without trailing newline gets one appended.


# Invocations:
#   spimulator -f tac.asm             # reads stdin
#   spimulator -f tac.asm -           # reads stdin (explicit "-")
#   spimulator -f tac.asm /etc/motd   # reads the file
#   seq 1 5 | spimulator -f tac.asm   # prints 5 4 3 2 1


#PURPOSE:  Reverse line order.  Second sbrk demo (after sieve); this one uses
#          sbrk INCREMENTALLY — calling it repeatedly to grow the buffer as
#          bytes arrive without knowing the final size up front.
#
#          STACK-VISIBLE STYLE: main's C locals — fd, the sbrk buffer base, the
#          byte count `total`, the allocated `capacity`, the argv[1] path, and
#          the backward-walk cursor `i` — live in stack slots; a scratch register
#          holds only a working COPY.  Within each backward step the line's
#          `end`/`start` are transient working values derived from `i` (kept in
#          registers across the write syscall, which preserves $t).  The bytes
#          live in the sbrk heap (the buffer base is the stack-homed pointer).
#          This demo makes NO `jal` (only syscalls, which preserve $ra), so it
#          saves $fp but not $ra.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  fd          | -8($fp)     | $t0 (copy) | int       |   4   | STDIN (0) or opened file descriptor
#  buf         | -12($fp)    | $t8 (copy) | char *    |   4   | sbrk buffer base (bytes live in the heap)
#  total       | -16($fp)    | $t2 (copy) | int       |   4   | bytes read so far
#  capacity    | -20($fp)    | $t1 (copy) | int       |   4   | currently allocated size
#  argv1       | -24($fp)    | $t0 (copy) | char *    |   4   | argv[1]; "-"/FILE, kept for error msg
#  i           | -28($fp)    | $t0 (copy) | int       |   4   | backward-walk cursor (end of the next line)
#  (saved $fp) | -4($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  stdin: transforms/tac/tac.input
#  stdout:
#  | cherry
#  | banana
#  | apple

        .data
usageMsg:   .asciiz "usage: tac [FILE|-]\n"
errMsg:     .asciiz "tac: cannot open "
nlMsg:      .asciiz "\n"
oneByte:    .space 1

        .text
        .globl main
main:
        addiu $sp, $sp, -32
        sw $fp, 28($sp)              # save caller's $fp -> -4($fp)
        addiu $fp, $sp, 32

        li $t0, 0
        sw $t0, -8($fp)              # fd = STDIN (0)
        sw $zero, -24($fp)           # argv1 = 0

        li $t0, 1
        beq $a0, $t0, alloc_init
        li $t0, 2
        bne $a0, $t0, usage
        lw $t0, 4($a1)
        sw $t0, -24($fp)             # argv1
        lb $t1, 0($t0)
        bne $t1, '-', do_open
        lb $t1, 1($t0)
        beq $t1, $0, alloc_init

do_open:
        lw $a0, -24($fp)
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_failed
        sw $v0, -8($fp)              # fd

alloc_init:
        # Get current data segment top (no growth yet)
        move $a0, $0                 # sbrk(0) — query
        li $v0, 9
        syscall
        sw $v0, -12($fp)             # buf base
        li $t0, 0
        sw $t0, -16($fp)             # total = 0
        sw $t0, -20($fp)             # capacity = 0

read_loop:
        li $v0, 14
        lw $a0, -8($fp)              # fd
        la $a1, oneByte
        li $a2, 1
        syscall
        blez $v0, read_done

        # if total >= capacity: grow by 4096
        lw $t0, -16($fp)             # total
        lw $t1, -20($fp)             # capacity
        blt $t0, $t1, store_byte
        li $a0, 4096
        li $v0, 9
        syscall
        lw $t1, -20($fp)
        addi $t1, $t1, 4096
        sw $t1, -20($fp)             # capacity += 4096

store_byte:
        # buf[total] = c
        lb $t0, oneByte
        lw $t1, -12($fp)             # buf base
        lw $t2, -16($fp)             # total
        add $t1, $t1, $t2            # &buf[total]
        sb $t0, ($t1)
        lw $t2, -16($fp)
        addi $t2, $t2, 1
        sw $t2, -16($fp)             # total++
        j read_loop

read_done:
        # close fd if not STDIN
        lw $t0, -8($fp)              # fd
        beqz $t0, walk_setup
        li $v0, 16
        lw $a0, -8($fp)
        syscall

walk_setup:
        lw $t0, -16($fp)             # total
        sw $t0, -28($fp)             # i = total

walk_loop:
        lw $t0, -28($fp)             # i (also serves as `end` this step)
        blez $t0, exit_ok

        # If buf[end-1] == '\n': start scan from end-1; else start = end.
        addi $t1, $t0, -1            # end - 1
        lw $t8, -12($fp)             # buf base
        add $t2, $t8, $t1
        lb $t3, ($t2)
        beq $t3, '\n', has_nl
        move $t4, $t0                # no trailing '\n': start = end
        j find_start
has_nl:
        move $t4, $t1                # start = end - 1

find_start:
        # while (start > 0 && buf[start-1] != '\n') start--
fs_loop:
        blez $t4, write_line
        addi $t5, $t4, -1
        lw $t8, -12($fp)             # buf base
        add $t6, $t8, $t5
        lb $t7, ($t6)
        beq $t7, '\n', write_line
        move $t4, $t5
        j fs_loop

write_line:
        # write(STDOUT, buf + start, end - start)
        sub $t6, $t0, $t4
        blez $t6, after_line
        li $v0, 15
        li $a0, 1
        lw $t8, -12($fp)             # buf base
        add $a1, $t8, $t4            # buf + start
        move $a2, $t6
        syscall

after_line:
        # If buf[end-1] != '\n', append '\n'
        addi $t5, $t0, -1
        lw $t8, -12($fp)             # buf base
        add $t6, $t8, $t5
        lb $t7, ($t6)
        beq $t7, '\n', advance
        li $v0, 15
        li $a0, 1
        la $a1, nlMsg
        li $a2, 1
        syscall

advance:
        sw $t4, -28($fp)             # i = start
        j walk_loop

exit_ok:
        li $v0, 0                    # exit status: __start passes this through syscall 17
        lw $fp, -4($fp)
        addiu $sp, $sp, 32
        jr $ra

open_failed:
        li $v0, 4
        la $a0, errMsg
        syscall
        li $v0, 4
        lw $a0, -24($fp)             # argv1
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
