# (MIT license abbreviated)


# C source — see cp.c
#
#     int my_main(int argc, char **argv) {
#       int src = open(argv[1], O_RDONLY, 0);
#       int dst = open(argv[2], O_WRONLY|O_CREAT|O_TRUNC, 0644);
#       char buf[4096];
#       long n;
#       while ((n = read(src, buf, 4096)) > 0) write(dst, buf, n);
#       close(src); close(dst);
#     }


#PURPOSE:  Copy SRC to DST.  Block I/O.  First demo to call
#          `open` with O_CREAT | O_WRONLY | O_TRUNC (flags = 577).

        .data
usageMsg:   .asciiz "usage: cp SRC DST\n"
openSrcErr: .asciiz "cp: cannot open "
openDstErr: .asciiz "cp: cannot create "
nlMsg:      .asciiz "\n"
buf:        .space 4096

        .text
        .globl main
main:
        move $s0, $ra

        li $t0, 3
        bne $a0, $t0, usage

        move $s4, $a1                # save argv

        # src = open(argv[1], O_RDONLY)
        lw $a0, 4($s4)
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, src_failed
        move $s1, $v0                # src fd

        # dst = open(argv[2], O_WRONLY|O_CREAT|O_TRUNC, 0644)
        lw $a0, 8($s4)
        li $v0, 13
        li $a1, 577                  # 1 | 0x40 | 0x200
        li $a2, 420                  # 0644
        syscall
        bltz $v0, dst_failed
        move $s2, $v0                # dst fd

read_loop:
        li $v0, 14
        move $a0, $s1
        la $a1, buf
        li $a2, 4096
        syscall
        blez $v0, close_and_exit

        move $s3, $v0                # n

        li $v0, 15
        move $a0, $s2
        la $a1, buf
        move $a2, $s3
        syscall
        j read_loop

close_and_exit:
        li $v0, 16
        move $a0, $s1
        syscall
        li $v0, 16
        move $a0, $s2
        syscall
        move $ra, $s0
        jr $ra

src_failed:
        li $v0, 4
        la $a0, openSrcErr
        syscall
        lw $a0, 4($s4)
        li $v0, 4
        syscall
        li $v0, 4
        la $a0, nlMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall

dst_failed:
        # close src first
        li $v0, 16
        move $a0, $s1
        syscall

        li $v0, 4
        la $a0, openDstErr
        syscall
        lw $a0, 8($s4)
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
