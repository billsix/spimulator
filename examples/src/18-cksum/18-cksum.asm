# Copyright (c) 2021-2026 William Emerison Six
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.


# C source — see 18-cksum.c
#
#     int my_main(int argc, char **argv) {
#       int fd = STDIN;
#       const char *filename = NULL;
#       if (argc > 2) usage;
#       if (argc == 2 && argv[1] != "-") { filename = argv[1];
#                                           fd = open(filename); }
#       /* ... CRC over read(fd, buf, 4096) chunks ... */
#       print_uint(ck); print_char(' '); print_uint(len);
#       if (filename) { print_char(' '); print_string(filename); }
#       print_char('\n');
#       if (fd != STDIN) close(fd);
#       return 0;
#     }


#PURPOSE:  cksum with real-Unix argv handling.  Stdin mode
#          prints "<crc> <bytes>"; file mode prints
#          "<crc> <bytes> <filename>" (matches `cksum FILE`).


#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     fd            $s4                  (STDIN=0 or opened fd)
#     filename ptr  $s5                  (0 if stdin, else argv[1])
#     ck            $t0                  (running CRC)
#     len           $t1                  (byte count)
#     buf           `buf` (.data)        (4 KiB read buffer)
#     crctab        `crctab` (.data)     (256 .word polynomial table)
#
#   $s0 holds runtime's $ra across BOTH `jal print_uint` calls.
#   print_uint promises not to touch $t1 (so `len` survives the
#   first jal) — see the NOTE at the top of print_uint below.

        .data
usageMsg:   .asciiz "usage: cksum [FILE|-]\n"
errMsg:     .asciiz "cksum: cannot open "
buf:    .space 4096                  # 4 KiB read buffer
space:  .asciiz " "
newline:    .asciiz "\n"
digitsBuf:  .space 16                # scratch for uint -> ascii (right-aligned)

crctab:
        .word   0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b, 0x1a864db2, 0x1e475005
        .word   0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61, 0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd
        .word   0x4c11db70, 0x48d0c6c7, 0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75
        .word   0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3, 0x709f7b7a, 0x745e66cd
        .word   0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039, 0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5
        .word   0xbe2b5b58, 0xbaea46ef, 0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d
        .word   0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb, 0xceb42022, 0xca753d95
        .word   0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1, 0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d
        .word   0x34867077, 0x30476dc0, 0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072
        .word   0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca
        .word   0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde, 0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02
        .word   0x5e9f46bf, 0x5a5e5b08, 0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba
        .word   0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc, 0xb6238b25, 0xb2e29692
        .word   0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6, 0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a
        .word   0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e, 0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2
        .word   0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34, 0xdc3abded, 0xd8fba05a
        .word   0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637, 0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb
        .word   0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53
        .word   0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5, 0x3f9b762c, 0x3b5a6b9b
        .word   0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff, 0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623
        .word   0xf12f560e, 0xf5ee4bb9, 0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b
        .word   0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f, 0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3
        .word   0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7, 0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b
        .word   0x9b3660c6, 0x9ff77d71, 0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3
        .word   0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2, 0x470cdd2b, 0x43cdc09c
        .word   0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8, 0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24
        .word   0x119b4be9, 0x155a565e, 0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec
        .word   0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a, 0x2d15ebe3, 0x29d4f654
        .word   0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0, 0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c
        .word   0xe3a1cbc1, 0xe760d676, 0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4
        .word   0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662, 0x933eb0bb, 0x97ffad0c
        .word   0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668, 0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4

        .text
        .globl main
main:
        move $s0, $ra
        li $s4, 0                    # fd = STDIN
        move $s5, $0                 # filename = NULL

        # argc dispatch
        li $t0, 1
        beq $a0, $t0, start_crc
        li $t0, 2
        bgt $a0, $t0, usage

        # argc == 2
        lw $t1, 4($a1)               # argv[1]
        lb $t0, 0($t1)
        bne $t0, '-', do_open
        lb $t0, 1($t1)
        beq $t0, $0, start_crc       # exactly "-" -> stdin

do_open:
        move $s5, $t1                # filename = argv[1]
        move $a0, $t1
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_failed
        move $s4, $v0

start_crc:
        # ck = 0; len = 0;
        li $t0, 0
        li $t1, 0

read_loop:
        # n = read(fd, buf, 4096)
        li $v0, 14
        move $a0, $s4
        la $a1, buf
        li $a2, 4096
        syscall

        blez $v0, after_read

        la $t2, buf
        add $t3, $t2, $v0
        add $t1, $t1, $v0

byte_loop:
        beq $t2, $t3, read_loop
        lbu $t4, ($t2)
        srl $t5, $t0, 24
        xor $t5, $t5, $t4
        sll $t6, $t5, 2
        la $t7, crctab
        add $t6, $t7, $t6
        lw $t7, ($t6)
        sll $t8, $t0, 8
        xor $t0, $t8, $t7
        addi $t2, $t2, 1
        j byte_loop

after_read:
        # fold len into ck
        move $t4, $t1

fold_loop:
        beq $t4, $0, after_fold
        andi $t5, $t4, 0xff
        srl $t6, $t0, 24
        xor $t5, $t6, $t5
        sll $t6, $t5, 2
        la $t7, crctab
        add $t6, $t7, $t6
        lw $t7, ($t6)
        sll $t8, $t0, 8
        xor $t0, $t8, $t7
        srl $t4, $t4, 8
        j fold_loop

after_fold:
        # close fd if not STDIN
        beqz $s4, do_print
        li $v0, 16
        move $a0, $s4
        syscall

do_print:
        nor $t0, $t0, $0              # ck = ~ck

        # print_uint(ck)
        move $a0, $t0
        jal print_uint

        # print_char(' ')
        li $v0, 4
        la $a0, space
        syscall

        # print_uint(len)
        move $a0, $t1
        jal print_uint

        # if (filename) print_char(' '); print_string(filename);
        beqz $s5, no_filename
        li $v0, 4
        la $a0, space
        syscall
        li $v0, 4
        move $a0, $s5
        syscall
no_filename:

        # print_char('\n')
        li $v0, 4
        la $a0, newline
        syscall

        move $ra, $s0
        jr $ra

open_failed:
        li $v0, 4
        la $a0, errMsg
        syscall
        li $v0, 4
        move $a0, $t1                # argv[1]
        syscall
        li $v0, 4
        la $a0, newline
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


# ---------- print_uint subroutine -----------------------------------
# Same as before: hand-format an unsigned 32-bit value into a
# decimal string and print via syscall 4.  Avoids $t1 so main's
# `len` survives the first call.
print_uint:
        la $t0, digitsBuf
        addi $t9, $t0, 16
        sb $0, ($t9)
        move $t9, $a0
        addi $t2, $t0, 15
        li $t3, 10
pu_loop:
        divu $t9, $t3
        mfhi $t0
        mflo $t9
        addi $t0, $t0, 48
        sb $t0, ($t2)
        beq $t9, $0, pu_done
        addi $t2, $t2, -1
        j pu_loop
pu_done:
        move $a0, $t2
        li $v0, 4
        syscall
        jr $ra
