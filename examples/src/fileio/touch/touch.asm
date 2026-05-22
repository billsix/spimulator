# Copyright (c) 2021-2026 William Emerison Six
#
# (license preamble, abbreviated for brevity in the curriculum's
# Part 7 demos; same MIT terms as elsewhere)


# C source — see touch.c
#
#     int my_main(int argc, char **argv) {
#       if (argc != 2) usage;
#       int fd = open(argv[1], O_WRONLY | O_CREAT, 0644);
#       if (fd < 0) error;
#       close(fd);
#       return 0;
#     }


#PURPOSE:  touch FILE.  Creates an empty file via
#          open(O_WRONLY | O_CREAT, 0644) and closes it
#          immediately.  Flag value 65 = 1 | 0x40 =
#          O_WRONLY | O_CREAT.  Mode 420 = 0644.

        .data
usageMsg:   .asciiz "usage: touch FILE\n"
errMsg:     .asciiz "touch: cannot create "
nlMsg:      .asciiz "\n"

        .text
        .globl main
main:
        move $s0, $ra

        li $t0, 2
        bne $a0, $t0, usage

        # fd = open(argv[1], O_WRONLY | O_CREAT, 0644)
        lw $a0, 4($a1)
        move $s1, $a0                # save argv[1] for error message
        li $v0, 13                   # open
        li $a1, 65                   # O_WRONLY | O_CREAT
        li $a2, 420                  # 0644
        syscall
        bltz $v0, open_failed

        # close(fd)
        move $a0, $v0
        li $v0, 16
        syscall

        move $ra, $s0
        jr $ra

open_failed:
        li $v0, 4
        la $a0, errMsg
        syscall
        li $v0, 4
        move $a0, $s1
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
