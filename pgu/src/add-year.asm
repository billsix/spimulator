# Copyright 2002 Jonathan Bartlett
# Copyright 2026 William Emerison Six (MIPS/spimulator port)
#
# Permission is granted to copy, distribute and/or modify this program
# under the terms of the GNU Free Documentation License, Version 1.1 or
# any later version published by the Free Software Foundation; with no
# Invariant Sections, with no Front-Cover Texts, and with no Back-Cover
# Texts.  This program is an example from "Programming from the Ground
# Up"; a copy of the license is in the book's GNU FDL appendix (fdlap).


# C source equivalent:
#
#     int main(void) {
#       int in  = open("test.dat", O_RDONLY);
#       int out = open("testout.dat", O_WRONLY|O_CREAT, 0644);
#       char rec[324];
#       while (read(in, rec, 324) == 324) {
#         int *age = (int *)(rec + 320);   // the age field
#         *age = *age + 1;                 // everyone gets one year older
#         write(out, rec, 324);
#       }
#       close(in); close(out);
#       return 0;
#     }
#
# Invocation:  spimulator -f add-year.asm      # reads test.dat -> testout.dat
#                                              # (run write-records first)
#
#
#PURPOSE:  Read each person record, add one to the age field, and
#          write the updated record to a new file.  PGU's "add a year"
#          program, retargeted to MIPS/spim.  Shows reading and
#          writing a numeric struct field by its byte offset.
#
#
# Record layout (same struct as write-records.asm)
#   offset 0 firstname[40], 40 lastname[40], 80 address[240], 320 age(word)
#   RECORD_SIZE = 324.  The age is a word at offset 320 — we load it
#   with `lw rec+320`, add 1, and `sw` it straight back into the
#   buffer before writing the record out.
#
# Syscalls: open=13, read=14, write=15, close=16.
#   input  flags 0  (O_RDONLY)
#   output flags 65 (O_WRONLY|O_CREAT), mode 420 (0644)


        .data
in_name:
        .asciiz "test.dat"
out_name:
        .asciiz "testout.dat"
        # The two strings above are 9 + 12 = 21 bytes, leaving the
        # next byte unaligned.  The record's age is a WORD at offset
        # 320, so the buffer must start word-aligned or `lw/sw
        # 320($buffer)` traps with an address error.  .align 2 rounds
        # the next label up to a 4-byte boundary.
        .align 2
record_buffer:
        .space 324


        .text
        .globl main
main:
        # in = open("test.dat", O_RDONLY)
        la   $a0, in_name
        li   $a1, 0
        li   $a2, 0
        li   $v0, 13
        syscall
        move $s0, $v0            # input fd

        # out = open("testout.dat", O_WRONLY|O_CREAT, 0644)
        la   $a0, out_name
        li   $a1, 65
        li   $a2, 420
        li   $v0, 13
        syscall
        move $s1, $v0            # output fd

add_year_loop:
        # bytes = read(in, record_buffer, RECORD_SIZE)
        move $a0, $s0
        la   $a1, record_buffer
        li   $a2, 324
        li   $v0, 14
        syscall
        li   $t0, 324
        bne  $v0, $t0, add_year_done    # short read => EOF

# doc-region-begin bump age
        # *(int *)(record_buffer + 320) += 1   — the age field
        la   $t1, record_buffer
        lw   $t2, 320($t1)       # age = record.age
        addi $t2, $t2, 1         # one year older
        sw   $t2, 320($t1)       # store it back into the buffer
# doc-region-end bump age

        # write(out, record_buffer, RECORD_SIZE)
        move $a0, $s1
        la   $a1, record_buffer
        li   $a2, 324
        li   $v0, 15
        syscall

        j    add_year_loop

add_year_done:
        move $a0, $s0
        li   $v0, 16             # close(in)
        syscall
        move $a0, $s1
        li   $v0, 16             # close(out)
        syscall

        li   $v0, 0
        jr   $ra
