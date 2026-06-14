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
#       int fd = open("test.dat", O_RDONLY);
#       char rec[324];
#       while (read(fd, rec, 324) == 324)      // a short read means EOF
#         { print_string(rec /* firstname at offset 0 */); print_char('\n'); }
#       close(fd);
#       return 0;
#     }
#
# Invocation:  spimulator -f read-records.asm        # needs test.dat
#                                                    # (run write-records first)
# Expected output:
#     Fredrick
#     Marilyn
#     Derrick
#
#
#PURPOSE:  Read the fixed-size person records written by
#          write-records.asm and print each firstname.  PGU's
#          record-reading program, retargeted to MIPS/spim.
#
#
# Record layout (same struct as write-records.asm)
#   offset 0 firstname[40], 40 lastname[40], 80 address[240], 320 age(word)
#   RECORD_SIZE = 324.  firstname is null-padded, so printing from
#   offset 0 with print_string (syscall 4) stops at the first 0 byte.
#
# Syscalls:
#   open  = 13   ($a0=path, $a1=flags=0 for O_RDONLY)  -> $v0 = fd
#   read  = 14   ($a0=fd, $a1=buf, $a2=count)          -> $v0 = bytes read
#   close = 16   ($a0=fd)
# read returning fewer than RECORD_SIZE bytes means end-of-file.


        .data
file_name:
        .asciiz "test.dat"
        # Word-align the buffer: a record's age field is a word at
        # offset 320, so any code that does `lw/sw 320($buffer)` (e.g.
        # add-year.asm) needs the buffer on a 4-byte boundary.
        .align 2
record_buffer:
        .space 324               # one RECORD_SIZE scratch buffer


        .text
        .globl main
main:
# doc-region-begin read open
        # fd = open("test.dat", O_RDONLY)
        la   $a0, file_name
        li   $a1, 0              # O_RDONLY
        li   $a2, 0
        li   $v0, 13            # open
        syscall
        move $s0, $v0            # save the file descriptor
# doc-region-end read open

# doc-region-begin read loop
record_read_loop:
        # bytes = read(fd, record_buffer, RECORD_SIZE)
        move $a0, $s0
        la   $a1, record_buffer
        li   $a2, 324
        li   $v0, 14            # read
        syscall

        # A read of fewer than RECORD_SIZE bytes is EOF (or error): stop.
        li   $t0, 324
        bne  $v0, $t0, finished_reading

        # print_string(firstname) — firstname sits at offset 0 and is
        # null-padded, so print_string stops at its terminator.
        la   $a0, record_buffer
        li   $v0, 4
        syscall

        # print_char('\n')
        li   $a0, '\n'
        li   $v0, 11
        syscall

        j    record_read_loop
# doc-region-end read loop

finished_reading:
        move $a0, $s0
        li   $v0, 16            # close(fd)
        syscall

        li   $v0, 0
        jr   $ra
