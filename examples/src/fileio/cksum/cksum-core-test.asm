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


# C source — see cksum-core-test.c

# Invocation (no input; runs the test battery):
#   spimulator -f cksum-core-test.asm   ->  "<label> => <crc> <byte_count>" per case

# PURPOSE:  Unit tests for the I/O-separated cksum core.  cksum_of walks a BYTE
#           BUFFER (no I/O), so it can be driven by a table of hardcoded buffers.
#           main loops the cases, printing "<label> => <crc> <byte_count>";
#           run-demo.sh pins the golden (the oracle is system `cksum`).
#
#           STACK-VISIBLE STYLE: main's C locals — the case index i, and the
#           current case's crc ck and byte count len — live in stack slots; a
#           scratch register holds only a working COPY.  main saves $ra for its
#           jals.  cksum_of is cksum-core.asm's pure core verbatim (ck/i in its
#           own frame).  The case tables (labels, data buffers, lengths) and
#           crctab are C `static`, so they stay in .data; print_uint is a shared
#           leaf helper.
#           Style reference: tasks/reference/mips-asm-teaching-style.md
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr   | register   | C type       | bytes | role / notes
#  ------------+---------------+------------+--------------+-------+---------------------------
#  i           | -12($fp)      | $t0 (copy) | int          |    4  | case index 0..4
#  ck          | -16($fp)      | $v0 (copy) | unsigned int |    4  | this case's crc (from cksum_of)
#  len         | -20($fp)      | $a1 (copy) | unsigned int |    4  | this case's byte count
#  dptrs/lens/ | *ptrs (.data) | —          | tables       |       | static case fixtures
#   lptrs      |               |            |              |       |
#  (saved $ra) | -4($fp)       | —          | code addr    |    4  | jal cksum_of / jal print_uint
#  (saved $fp) | -8($fp)       | —          | frame ptr    |    4  | caller's frame pointer
#
# ------------------------------------------------------------------
#  SNAPSHOT (no args; case i = 3 — label "hello"+LF, data "hello\n",
#            len 6 — just after cksum_of returned)
#
#        main's stack frame             scratch
#   +----------------------------+     +----------------------+
#   | -4  saved $ra              |     | $v0 = 3015617425 (ck)
#   | -8  saved $fp              |     +----------------------+
#   | -12 i   = 3                |
#   | -16 ck  = 3015617425       |
#   | -20 len = 6                |
#   +----------------------------+
#         $sp = $fp - 20
#
#   Prints 'hello"+LF => 3015617425 6', then advances to case 4.
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  args:
#  stdout:
#  | "" => 4294967295 0
#  | "a" => 1220704766 1
#  | "abc" => 1219131554 3
#  | "hello"+LF => 3015617425 6
#  | "123456789" => 930766865 9

        .data
arrow:      .asciiz " => "
space:      .asciiz " "
newline:    .asciiz "\n"
digitsBuf:  .space 16

# labels (printed) — the quote chars and the backslash-n are literal text
l0:         .asciiz "\"\""
l1:         .asciiz "\"a\""
l2:         .asciiz "\"abc\""
l3:         .asciiz "\"hello\"+LF"
l4:         .asciiz "\"123456789\""
            .align 2
lptrs:      .word l0, l1, l2, l3, l4

# data buffers (checksummed) — d3 holds a real newline byte (len 6)
d0:         .asciiz ""
d1:         .asciiz "a"
d2:         .asciiz "abc"
d3:         .asciiz "hello\n"
d4:         .asciiz "123456789"
            .align 2
dptrs:      .word d0, d1, d2, d3, d4
lens:       .word 0, 1, 3, 6, 9

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
        # ---- prologue: build main's 20-byte stack frame ------------
        addiu $sp, $sp, -20
        sw $ra, 16($sp)              # save $ra          -> -4($fp)
        sw $fp, 12($sp)              # save caller's $fp -> -8($fp)
        addiu $fp, $sp, 20

        li   $t0, 0
        sw   $t0, -12($fp)           # i = 0

mt_loop:
        lw   $t0, -12($fp)           # i
        li   $t1, 5
        bge  $t0, $t1, mt_done       # while (i < 5)

        # ck = cksum_of(dptrs[i], lens[i]);
        sll  $t2, $t0, 2             # i * 4
        la   $t1, dptrs
        add  $t1, $t1, $t2
        lw   $a0, 0($t1)             # datas[i]
        la   $t1, lens
        add  $t1, $t1, $t2
        lw   $a1, 0($t1)             # lens[i]
        sw   $a1, -20($fp)           # len = lens[i]  (save for printing)
        jal  cksum_of
        sw   $v0, -16($fp)           # ck = result

        # print the label: dptrs[i] -> lptrs[i]
        lw   $t0, -12($fp)           # i
        sll  $t2, $t0, 2
        la   $t1, lptrs
        add  $t1, $t1, $t2
        lw   $a0, 0($t1)
        li   $v0, 4
        syscall
        # print " => "
        la   $a0, arrow
        li   $v0, 4
        syscall
        # print_uint(ck)
        lw   $a0, -16($fp)
        jal  print_uint
        # print " "
        la   $a0, space
        li   $v0, 4
        syscall
        # print_uint(len)
        lw   $a0, -20($fp)
        jal  print_uint
        # print "\n"
        la   $a0, newline
        li   $v0, 4
        syscall

        # i++;
        lw   $t0, -12($fp)
        addi $t0, $t0, 1
        sw   $t0, -12($fp)
        j    mt_loop

mt_done:
        # ---- epilogue ----------------------------------------------
        li   $v0, 0
        lw   $ra, -4($fp)
        lw   $fp, -8($fp)
        addiu $sp, $sp, 20
        jr   $ra


# ======== cores — cksum-core.asm's routines, verbatim ========

# ---------- unsigned int cksum_of(const uchar *data, unsigned int len) ------
# The PURE core (no I/O).  Leaf (no jal) -> saves $fp but not $ra; ck and i
# live in this frame, data/len stay in $a0/$a1.
cksum_of:
        addiu $sp, $sp, -12
        sw $fp, 8($sp)               # save caller's $fp -> -4($fp)
        addiu $fp, $sp, 12
        li   $t0, 0
        sw   $t0, -8($fp)            # ck = 0
        li   $t0, 0
        sw   $t0, -12($fp)           # i = 0

co_loop:
        lw   $t1, -12($fp)           # i
        bge  $t1, $a1, co_fold       # while (i < len)
        lw   $t0, -8($fp)            # ck
        add  $t2, $a0, $t1           # &data[i]
        lbu  $t4, 0($t2)             # data[i]
        srl  $t5, $t0, 24
        xor  $t5, $t5, $t4
        sll  $t6, $t5, 2
        la   $t7, crctab
        add  $t6, $t7, $t6
        lw   $t7, 0($t6)
        sll  $t8, $t0, 8
        xor  $t0, $t8, $t7
        sw   $t0, -8($fp)            # ck
        lw   $t1, -12($fp)           # i
        addi $t1, $t1, 1
        sw   $t1, -12($fp)           # i++
        j    co_loop

co_fold:
        move $t4, $a1                # n = len (transient fold copy)
cf_loop:
        beq  $t4, $0, cf_done
        lw   $t0, -8($fp)            # ck
        andi $t5, $t4, 0xff
        srl  $t6, $t0, 24
        xor  $t5, $t6, $t5
        sll  $t6, $t5, 2
        la   $t7, crctab
        add  $t6, $t7, $t6
        lw   $t7, 0($t6)
        sll  $t8, $t0, 8
        xor  $t0, $t8, $t7
        sw   $t0, -8($fp)            # ck
        srl  $t4, $t4, 8
        j    cf_loop

cf_done:
        lw   $t0, -8($fp)
        nor  $t0, $t0, $0            # ck = ~ck
        move $v0, $t0
        lw   $fp, -4($fp)
        addiu $sp, $sp, 12
        jr   $ra


# ---------- print_uint($a0) — hand-format an unsigned 32-bit value ----------
# Shared leaf helper, register-based.
print_uint:
        la   $t0, digitsBuf
        addi $t9, $t0, 16
        sb   $0, ($t9)
        move $t9, $a0
        addi $t2, $t0, 15
        li   $t3, 10
pu_loop:
        divu $t9, $t3
        mfhi $t0
        mflo $t9
        addi $t0, $t0, 48
        sb   $t0, ($t2)
        beq  $t9, $0, pu_done
        addi $t2, $t2, -1
        j    pu_loop
pu_done:
        move $a0, $t2
        li   $v0, 4
        syscall
        jr   $ra
