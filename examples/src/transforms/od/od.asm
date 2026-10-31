# (MIT license abbreviated)


# C source — see od.c
#
#     od -c-style dump: 16 bytes per row, each byte as 3-char
#     printable/escape/octal, leading 7-digit octal offset.


# Invocations:
#   spimulator -f od.asm                     # reads stdin
#   spimulator -f od.asm -                   # reads stdin (explicit "-")
#   spimulator -f od.asm /etc/services       # reads the file
#   printf 'hi\t\\\n' | spimulator -f od.asm # see the per-byte escapes


#PURPOSE:  Hex/octal dump with per-byte formatted output.  Row-oriented output
#          + character escapes.
#
#          STACK-VISIBLE STYLE: main's C locals — fd, the argv[1] path, the
#          running byte `offset`, and the within-row `col` — live in stack slots;
#          a scratch register holds only a working COPY.  main makes jals
#          (print_offset, print_byte_c), so it saves $ra.  print_offset and
#          print_byte_c are register-based display helpers (they use offBuf/the
#          esc_* strings in .data).
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main): fd -> -12($fp), argv1 -> -16($fp), offset ->
#  -20($fp), col -> -24($fp); saved $ra/$fp at -4/-8($fp).
#
#  No #CANONICAL block: od's rows carry fixed column spacing (and a trailing
#  offset line) that a comment anchor would have to reproduce exactly; gated by
#  before/after output equivalence.


        .data
usageMsg:   .asciiz "usage: od [FILE|-]\n"
errMsg:     .asciiz "od: cannot open "
nlMsg:      .asciiz "\n"
spc:        .asciiz " "
esc_0:      .asciiz "  \1340"
esc_b:      .asciiz "  \134b"
esc_t:      .asciiz "  \134t"
esc_n:      .asciiz "  \134n"
esc_v:      .asciiz "  \134v"
esc_f:      .asciiz "  \134f"
esc_r:      .asciiz "  \134r"
offBuf:     .space 8                # 7-digit octal + NUL
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
        sw $t0, -20($fp)             # offset = 0
        sw $t0, -24($fp)             # col = 0

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

        # if col == 0: print offset
        lw $t0, -24($fp)             # col
        bnez $t0, after_offset
        lw $a0, -20($fp)             # offset
        jal print_offset

after_offset:
        lbu $a0, oneByte
        jal print_byte_c

        lw $t0, -24($fp)             # col
        addi $t0, $t0, 1
        sw $t0, -24($fp)             # col++
        lw $t1, -20($fp)             # offset
        addi $t1, $t1, 1
        sw $t1, -20($fp)             # offset++
        li $t2, 16
        bne $t0, $t2, read_loop
        # row full -> newline + reset col
        li $a0, '\n'
        li $v0, 11
        syscall
        li $t0, 0
        sw $t0, -24($fp)             # col = 0
        j read_loop

after_read:
        # if (col > 0) print '\n'
        lw $t0, -24($fp)             # col
        blez $t0, print_final_offset
        li $a0, '\n'
        li $v0, 11
        syscall

print_final_offset:
        lw $a0, -20($fp)             # offset
        jal print_offset
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


# ---------- print_offset(int off) — 7-digit octal, zero-padded ----
print_offset:
        # Fill offBuf[0..6] with octal digits, [7]=0 (pre-zeroed)
        la $t0, offBuf
        li $t1, 6                    # i = 6
po_loop:
        bltz $t1, po_print
        andi $t2, $a0, 7             # off & 7
        addi $t2, $t2, 48            # '0' + digit
        add $t3, $t0, $t1
        sb $t2, ($t3)
        srl $a0, $a0, 3
        addi $t1, $t1, -1
        j po_loop
po_print:
        # null-terminate explicitly (in case offBuf was mutated)
        li $t2, 0
        addi $t3, $t0, 7
        sb $t2, ($t3)
        la $a0, offBuf
        li $v0, 4
        syscall
        jr $ra


# ---------- print_byte_c(unsigned char b in $a0) ----------
# Returns nothing; emits 3-or-4 chars per byte (matches od -c
# column convention).
print_byte_c:
        andi $a0, $a0, 0xff
        beq $a0, 0, pb_0
        li $t0, 8
        beq $a0, $t0, pb_b
        li $t0, 9
        beq $a0, $t0, pb_t
        li $t0, 10
        beq $a0, $t0, pb_n
        li $t0, 11
        beq $a0, $t0, pb_v
        li $t0, 12
        beq $a0, $t0, pb_f
        li $t0, 13
        beq $a0, $t0, pb_r
        li $t0, 32
        blt $a0, $t0, pb_octal
        li $t0, 127
        bge $a0, $t0, pb_octal
        # Printable
        move $t1, $a0                # save byte
        li $a0, ' '
        li $v0, 11
        syscall
        li $a0, ' '
        syscall
        li $a0, ' '
        syscall
        move $a0, $t1
        syscall
        jr $ra

pb_0:   la $a0, esc_0
        li $v0, 4
        syscall
        jr $ra
pb_b:   la $a0, esc_b
        li $v0, 4
        syscall
        jr $ra
pb_t:   la $a0, esc_t
        li $v0, 4
        syscall
        jr $ra
pb_n:   la $a0, esc_n
        li $v0, 4
        syscall
        jr $ra
pb_v:   la $a0, esc_v
        li $v0, 4
        syscall
        jr $ra
pb_f:   la $a0, esc_f
        li $v0, 4
        syscall
        jr $ra
pb_r:   la $a0, esc_r
        li $v0, 4
        syscall
        jr $ra

pb_octal:
        move $t1, $a0                # save byte
        # print ' '
        li $a0, ' '
        li $v0, 11
        syscall
        # digit (b >> 6) & 7
        srl $t2, $t1, 6
        andi $t2, $t2, 7
        addi $a0, $t2, 48
        syscall
        # digit (b >> 3) & 7
        srl $t2, $t1, 3
        andi $t2, $t2, 7
        addi $a0, $t2, 48
        syscall
        # digit (b) & 7
        andi $t2, $t1, 7
        addi $a0, $t2, 48
        syscall
        jr $ra
