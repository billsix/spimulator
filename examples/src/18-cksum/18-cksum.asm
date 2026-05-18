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
#     __attribute__((noreturn)) void _start(void) {
#       static unsigned char buf[4096];
#       unsigned int ck = 0;
#       unsigned int len = 0;
#       long n;
#       while ((n = os_read(STDIN, buf, sizeof(buf))) > 0) {
#         for (long i = 0; i < n; i++)
#           ck = (ck << 8) ^ crctab[(ck >> 24) ^ buf[i]];
#         len += n;
#       }
#       unsigned int i = len;
#       while (i != 0) {
#         ck = (ck << 8) ^ crctab[(ck >> 24) ^ (i & 0xff)];
#         i >>= 8;
#       }
#       ck = ~ck;
#       print_uint(ck);  print_char(' ');  print_uint(len);  print_char('\n');
#       os_exit(0);
#     }


#PURPOSE:  Compute the POSIX CRC32 checksum of stdin and print
#          "<crc> <byte_count>".  First demo with bitwise ops
#          (shifts + XOR), an indexed lookup table in .data, and
#          per-byte algorithmic work beyond what 06/12 do.
#
#NOTES:    SPIM's print_int syscall is signed (`%d`), so we can't
#          use it for the CRC — values above 2^31 would render as
#          negative.  Instead we hand-format the digits into a
#          small buffer with a divu loop, then print the buffer
#          via syscall 4 (print_string).  See print_uint at the
#          bottom of this file.
#
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     ck            $t0                  (running CRC, treated as unsigned 32-bit)
#     len           $t1                  (byte count, accumulated across reads)
#     buf           `buf` (.data)        (4 KiB scratch read buffer)
#     ptr           $t2                  (cursor into buf during byte_loop)
#     end           $t3                  (one-past-last byte of current read)
#     i             $t4                  (also used for the per-byte input in
#                                         byte_loop; reused for the len-fold
#                                         loop's iteration variable)
#     idx           $t5                  ((ck>>24)^byte; 0..255 table index)
#     &crctab[idx]  $t6                  (computed address into the .data table)
#     crctab[idx]   $t7                  (the loaded 32-bit table word)
#     ck << 8       $t8                  (intermediate shift result)
#     crctab        `crctab` (.data)     (256 .word entries; the CRC32 poly table)
#
#   In print_uint subroutine:
#     n             $t9                  (remaining int, divided down to 0)
#     base          $t3                  (constant 10)
#     ptr           $t2                  (write cursor into digitsBuf)
#     digit         $t0                  (one remainder, 0..9)
#     digitsBuf     `digitsBuf` (.data)  (16-byte scratch for ASCII digits)
#
#   Cross-call saves (callee-save $s* values held LIVE across `jal`):
#     $s0   <- runtime's $ra      (held across BOTH `jal print_uint` calls;
#                                  restored before the final `jr $ra`)
#
#     CALLER-SAVE caveat (worth naming explicitly): `ck` lives in
#     $t0 and `len` lives in $t1.  MIPS convention says $t* are
#     CALLER-save — main SHOULD spill them around each `jal
#     print_uint`.  This demo instead asks print_uint to be
#     conservative: print_uint's prologue comment promises not to
#     touch $t1, and its own uses of $t0 don't matter because
#     main reads `ck` (via `move $a0, $t0`) BEFORE the first jal
#     and never reads it again.  `len` IS needed after the first
#     jal print_uint (we pass it as the second `jal print_uint`'s
#     arg), which is the load-bearing reason print_uint must not
#     touch $t1.  See the "Trashes:" comment at the top of
#     print_uint.
#
#   Volatile working registers:
#     $a0, $a1, $a2  syscall args / function args
#     $v0            syscall selector / read return / function arg
#                    (14 = read, 4 = print_string, 11 = print_char)

        .data
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
        # Save $ra in a callee-save register so the `jal print_uint`
        # calls below don't clobber the runtime's return address.
        # ($ra is set by the `jal main` in exceptions.s.)
        move $s0, $ra

        # ck = 0; len = 0;
        li $t0, 0
        li $t1, 0

read_loop:
        # n = read(STDIN, buf, 4096);
        li $v0, 14                   # syscall 14 = read
        li $a0, 0                    # fd = 0
        la $a1, buf
        li $a2, 4096
        syscall                      # $v0 = bytes read

        blez $v0, after_read         # 0 = EOF, <0 = error

        # Set up pointers for the inner per-byte loop.
        la $t2, buf                  # ptr = &buf[0]
        add $t3, $t2, $v0            # end = ptr + n
        add $t1, $t1, $v0            # len += n

byte_loop:
        beq $t2, $t3, read_loop      # ptr == end -> next chunk
        lbu $t4, ($t2)               # byte = *ptr (unsigned 8-bit load)
        # idx = (ck >> 24) ^ byte
        srl $t5, $t0, 24
        xor $t5, $t5, $t4
        # ck = (ck << 8) ^ crctab[idx]
        sll $t6, $t5, 2              # idx * 4 (word-size offset)
        la $t7, crctab
        add $t6, $t7, $t6            # &crctab[idx]
        lw $t7, ($t6)
        sll $t8, $t0, 8
        xor $t0, $t8, $t7
        addi $t2, $t2, 1             # ptr++
        j byte_loop

after_read:
        # --- fold len into ck, one byte at a time ---
        # while (i != 0) { ck = (ck<<8) ^ crctab[(ck>>24) ^ (i&0xff)]; i >>= 8; }
        # We re-use $t4 for i (since $t4 was the per-byte input above).
        move $t4, $t1                # i = len

fold_loop:
        beq $t4, $0, after_fold
        andi $t5, $t4, 0xff          # i & 0xff
        srl $t6, $t0, 24
        xor $t5, $t6, $t5            # idx = (ck>>24) ^ (i&0xff)
        sll $t6, $t5, 2
        la $t7, crctab
        add $t6, $t7, $t6
        lw $t7, ($t6)
        sll $t8, $t0, 8
        xor $t0, $t8, $t7
        srl $t4, $t4, 8              # i >>= 8
        j fold_loop

after_fold:
        # ck = ~ck
        nor $t0, $t0, $0

        # print_uint(ck);
        move $a0, $t0
        jal print_uint

        # print_char(' ');
        li $v0, 4
        la $a0, space
        syscall

        # print_uint(len);
        move $a0, $t1
        jal print_uint

        # print_char('\n');
        li $v0, 4
        la $a0, newline
        syscall

        # Restore $ra and return to the runtime.
        move $ra, $s0
        li $v0, 0                    # (spim ignores this; see REFERENCE-encodings.md)
        jr $ra


# ---------- print_uint subroutine -----------------------------------
# Writes an unsigned 32-bit decimal to stdout via syscall 4.
# Input:  $a0 = unsigned value
# Trashes: $t0, $t2, $t3, $t9, $a0, $v0
#
# Deliberately avoids $t1 because main uses $t1 to hold `len`
# across both print_uint calls.  $t* registers are caller-save in
# the MIPS convention, so strictly speaking main *should* save
# $t1 around the call — but for this demo it's simpler to make
# print_uint promise not to touch it.
print_uint:
        la $t0, digitsBuf            # base of the buffer
        addi $t9, $t0, 16            # one-past-the-end
        sb $0, ($t9)                 # NUL just past the buffer
        move $t9, $a0                # value (we'll consume it in the loop)
        addi $t2, $t0, 15            # ptr = &digitsBuf[15] (rightmost slot)
        li $t3, 10

digit_loop:
        divu $t9, $t3                # quotient -> LO, remainder -> HI
        mfhi $t0                     # remainder
        mflo $t9                     # quotient (new value)
        addi $t0, $t0, 48            # '0' + remainder
        sb $t0, ($t2)
        beq $t9, $0, digit_done
        addi $t2, $t2, -1
        j digit_loop

digit_done:
        # $t2 now points at the first digit we wrote (the most
        # significant).  Print from there as a null-terminated
        # string.
        move $a0, $t2
        li $v0, 4
        syscall
        jr $ra
