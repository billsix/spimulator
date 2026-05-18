# (MIT license abbreviated)


# C source — see 36-tac.c
#
#     Read all of stdin/file into a sbrk-grown buffer.
#     Walk backwards: find each line, write it.  Final line
#     without trailing newline gets one appended.


#PURPOSE:  Reverse line order.  Second sbrk demo (after
#          09-sieve), this one uses sbrk INCREMENTALLY —
#          calling it repeatedly to grow the buffer as bytes
#          arrive without knowing the final size up front.


#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     fd            $s1                  (STDIN=0 or opened fd)
#     buf           $s2                  (base address from initial sbrk)
#     total         $s3                  (bytes read so far)
#     capacity      $s4                  (current allocated size)
#     argv[1]       $s5                  (for error message)

        .data
usageMsg:   .asciiz "usage: tac [FILE|-]\n"
errMsg:     .asciiz "tac: cannot open "
nlMsg:      .asciiz "\n"
oneByte:    .space 1

        .text
        .globl main
main:
        move $s0, $ra
        li $s1, 0                    # fd = STDIN
        move $s5, $0

        li $t0, 1
        beq $a0, $t0, alloc_init
        li $t0, 2
        bne $a0, $t0, usage

        lw $s5, 4($a1)
        lb $t0, 0($s5)
        bne $t0, '-', do_open
        lb $t0, 1($s5)
        beq $t0, $0, alloc_init

do_open:
        move $a0, $s5
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_failed
        move $s1, $v0

alloc_init:
        # Get current data segment top (no growth yet)
        move $a0, $0                 # sbrk(0) — query
        li $v0, 9
        syscall
        move $s2, $v0                # buf base
        li $s3, 0                    # total = 0
        li $s4, 0                    # capacity = 0

read_loop:
        li $v0, 14
        move $a0, $s1
        la $a1, oneByte
        li $a2, 1
        syscall
        blez $v0, read_done

        # if total >= capacity: grow by 4096
        blt $s3, $s4, store_byte
        li $a0, 4096
        li $v0, 9
        syscall
        addi $s4, $s4, 4096

store_byte:
        # buf[total] = c
        lb $t0, oneByte
        add $t1, $s2, $s3
        sb $t0, ($t1)
        addi $s3, $s3, 1
        j read_loop

read_done:
        # close fd if not STDIN
        beqz $s1, walk_setup
        li $v0, 16
        move $a0, $s1
        syscall

walk_setup:
        move $s6, $s3                # i = total

walk_loop:
        blez $s6, exit_ok
        # end = i
        move $t0, $s6                # end

        # If buf[end-1] == '\n': start scan from end-1
        #   else (final line without '\n'): start = end
        # Either way, walk back while buf[start-1] != '\n'.
        # After the loop, write buf[start..end-1].  If
        # buf[end-1] != '\n', also append '\n'.

        addi $t1, $t0, -1            # end - 1
        add $t2, $s2, $t1
        lb $t3, ($t2)
        beq $t3, '\n', has_nl

        # No trailing '\n': start = end
        move $t4, $t0
        j find_start

has_nl:
        move $t4, $t1                # start = end - 1

find_start:
        # Walk back: while (start > 0 && buf[start-1] != '\n') start--
fs_loop:
        blez $t4, write_line
        addi $t5, $t4, -1
        add $t6, $s2, $t5
        lb $t7, ($t6)
        beq $t7, '\n', write_line
        move $t4, $t5
        j fs_loop

write_line:
        # write(STDOUT, buf + start, end - start)
        sub $t6, $t0, $t4
        blez $t6, after_line         # nothing to write (shouldn't happen for non-empty)
        li $v0, 15
        li $a0, 1
        add $a1, $s2, $t4
        move $a2, $t6
        syscall

after_line:
        # If buf[end-1] != '\n', append '\n'
        addi $t5, $t0, -1
        add $t6, $s2, $t5
        lb $t7, ($t6)
        beq $t7, '\n', advance
        li $v0, 15
        li $a0, 1
        la $a1, nlMsg
        li $a2, 1
        syscall

advance:
        move $s6, $t4                # i = start
        j walk_loop

exit_ok:
        move $ra, $s0
        jr $ra

open_failed:
        li $v0, 4
        la $a0, errMsg
        syscall
        li $v0, 4
        move $a0, $s5
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
