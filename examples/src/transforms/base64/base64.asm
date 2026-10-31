# (MIT license abbreviated)


# C source — see base64.c
#
#     RFC 4648 base64 encode.  3 input bytes -> 4 output chars.
#     '=' padding for partial final group.  Wrap at 76 cols.


# Invocations:
#   spimulator -f base64.asm             # reads stdin
#   spimulator -f base64.asm -           # reads stdin (explicit "-")
#   spimulator -f base64.asm /etc/motd   # reads the file
#   echo hello | spimulator -f base64.asm


#PURPOSE:  base64 encode.  Bit packing across byte triples.
#
#          STACK-VISIBLE STYLE: the three input bytes of the current group
#          (b0/b1/b2) and the count `filled` live in main's stack frame — not in
#          $t0..$t2 held across the four emit_char calls (which is why the old
#          emit_char had to avoid those registers; it no longer does).  fd and
#          the argv[1] path are homed too.  The wrap counter `out_col` is shared
#          between main and emit_char, so it lives in .data.  main makes
#          `jal emit_char`, so it saves $ra; emit_char is a leaf.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr   | register   | C type    | bytes | role / notes
#  ------------+---------------+------------+-----------+-------+---------------------------
#  fd          | -12($fp)      | $t0 (copy) | int       |   4   | STDIN (0) or opened file descriptor
#  argv1       | -16($fp)      | $t0 (copy) | char *    |   4   | argv[1]; "-"/FILE, kept for error msg
#  filled      | -20($fp)      | $t5 (copy) | int       |   4   | bytes read into the current trio (0..3)
#  b0,b1,b2    | -24/-28/-32   | $t0..$t2   | int       |   4   | the three input bytes of the group
#  out_col     | out_col (.data)| $t0 (copy)| int       |   4   | output column (shared with emit_char; wrap at 76)
#  (saved $ra) | -4($fp)       | —          | code addr |   4   | jal emit_char
#  (saved $fp) | -8($fp)       | —          | frame ptr |   4   | caller's frame pointer
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  stdin: transforms/base64/base64.input
#  stdout:
#  | aGVsbG8K

        .data
usageMsg:   .asciiz "usage: base64 [FILE|-]\n"
errMsg:     .asciiz "base64: cannot open "
nlMsg:      .asciiz "\n"
alpha:      .asciiz "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
out_col:    .word 0
oneByte:    .space 1

        .text
        .globl main
main:
        addiu $sp, $sp, -32
        sw $ra, 28($sp)              # -> -4($fp)
        sw $fp, 24($sp)              # -> -8($fp)
        addiu $fp, $sp, 32

        li $t0, 0
        sw $t0, -12($fp)             # fd = STDIN (0)
        sw $zero, -16($fp)           # argv1 = 0
        sw $zero, out_col            # out_col = 0

        li $t0, 1
        beq $a0, $t0, trio_loop
        li $t0, 2
        bne $a0, $t0, usage
        lw $t0, 4($a1)
        sw $t0, -16($fp)             # argv1
        lb $t1, 0($t0)
        bne $t1, '-', do_open
        lb $t1, 1($t0)
        beq $t1, $0, trio_loop

do_open:
        lw $a0, -16($fp)
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_failed
        sw $v0, -12($fp)             # fd

trio_loop:
        # b0 = b1 = b2 = 0; filled = 0
        sw $zero, -24($fp)           # b0
        sw $zero, -28($fp)           # b1
        sw $zero, -32($fp)           # b2
        sw $zero, -20($fp)           # filled

        # Read byte 0
        li $v0, 14
        lw $a0, -12($fp)
        la $a1, oneByte
        li $a2, 1
        syscall
        blez $v0, after_b0
        lbu $t0, oneByte
        sw $t0, -24($fp)             # b0
        li $t0, 1
        sw $t0, -20($fp)             # filled = 1

        # Read byte 1
        li $v0, 14
        lw $a0, -12($fp)
        la $a1, oneByte
        li $a2, 1
        syscall
        blez $v0, encode
        lbu $t0, oneByte
        sw $t0, -28($fp)             # b1
        li $t0, 2
        sw $t0, -20($fp)             # filled = 2

        # Read byte 2
        li $v0, 14
        lw $a0, -12($fp)
        la $a1, oneByte
        li $a2, 1
        syscall
        blez $v0, encode
        lbu $t0, oneByte
        sw $t0, -32($fp)             # b2
        li $t0, 3
        sw $t0, -20($fp)             # filled = 3
        j encode

after_b0:
        # No data at all -> done
        j after_all

encode:
        # c0 = alpha[(b0 >> 2) & 0x3f]
        lw $t0, -24($fp)             # b0
        srl $t3, $t0, 2
        andi $t3, $t3, 0x3f
        la $a0, alpha
        add $a0, $a0, $t3
        lb $a0, ($a0)
        jal emit_char

        # c1 = alpha[((b0 << 4) | (b1 >> 4)) & 0x3f]
        lw $t0, -24($fp)             # b0
        lw $t1, -28($fp)             # b1
        sll $t3, $t0, 4
        srl $t4, $t1, 4
        or $t3, $t3, $t4
        andi $t3, $t3, 0x3f
        la $a0, alpha
        add $a0, $a0, $t3
        lb $a0, ($a0)
        jal emit_char

        # c2: if filled > 1, alpha[((b1 << 2) | (b2 >> 6)) & 0x3f] else '='
        lw $t5, -20($fp)             # filled
        li $t3, 1
        ble $t5, $t3, pad2
        lw $t1, -28($fp)             # b1
        lw $t2, -32($fp)             # b2
        sll $t3, $t1, 2
        srl $t4, $t2, 6
        or $t3, $t3, $t4
        andi $t3, $t3, 0x3f
        la $a0, alpha
        add $a0, $a0, $t3
        lb $a0, ($a0)
        jal emit_char
        j c3
pad2:
        li $a0, '='
        jal emit_char

c3:
        # c3: if filled > 2, alpha[b2 & 0x3f] else '='
        lw $t5, -20($fp)             # filled
        li $t3, 2
        ble $t5, $t3, pad3
        lw $t2, -32($fp)             # b2
        andi $t3, $t2, 0x3f
        la $a0, alpha
        add $a0, $a0, $t3
        lb $a0, ($a0)
        jal emit_char
        j after_encode
pad3:
        li $a0, '='
        jal emit_char

after_encode:
        # if filled < 3, we're done (final group)
        lw $t5, -20($fp)             # filled
        li $t3, 3
        beq $t5, $t3, trio_loop
        j after_all

after_all:
        # if out_col > 0, print '\n'
        lw $t0, out_col
        blez $t0, close_and_exit
        li $a0, '\n'
        li $v0, 11
        syscall

close_and_exit:
        lw $t0, -12($fp)             # fd
        beqz $t0, exit_ok
        li $v0, 16
        lw $a0, -12($fp)
        syscall
exit_ok:
        li $v0, 0                    # exit status: __start passes this through syscall 17
        lw $ra, -4($fp)
        lw $fp, -8($fp)
        addiu $sp, $sp, 32
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


# ---------- emit_char(c in $a0) — leaf helper ----------
# Prints c; bumps the shared out_col global; at 76 prints '\n' and resets.
# (No longer needs to dodge $t0..$t2 — the trio bytes are on main's frame.)
emit_char:
        li $v0, 11
        syscall                      # print c ($a0)
        lw $t0, out_col
        addi $t0, $t0, 1
        sw $t0, out_col
        li $t1, 76
        bne $t0, $t1, ec_done
        li $a0, '\n'
        li $v0, 11
        syscall
        sw $zero, out_col
ec_done:
        jr $ra
