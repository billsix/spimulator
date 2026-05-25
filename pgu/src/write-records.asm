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
#     struct person { char firstname[40]; char lastname[40];
#                     char address[240];  int age; };   // 324 bytes
#
#     int main(void) {
#       int fd = open("test.dat", O_WRONLY|O_CREAT, 0644);
#       write(fd, &record1, sizeof(struct person));
#       write(fd, &record2, sizeof(struct person));
#       write(fd, &record3, sizeof(struct person));
#       close(fd);
#       return 0;
#     }
#
# Invocation:  spimulator -f write-records.asm        # creates test.dat
#
#
#PURPOSE:  Write three fixed-size person records to a file.  This is
#          Programming from the Ground Up's record-writing program,
#          retargeted to MIPS/spim.  Pairs with read-records.asm.
#
#
# The record layout (a struct, by hand)
# =====================================
# A "record" is just a fixed run of bytes with named offsets.  spim's
# assembler has no .struct/.equ, so the offsets are literal constants,
# documented here once:
#
#     offset   field       size
#     ------   ---------   ----
#        0     firstname    40   (null-padded text)
#       40     lastname     40   (null-padded text)
#       80     address     240   (null-padded text, may contain '\n')
#      320     age           4   (one word)
#      ----                ----
#      total               324   = RECORD_SIZE
#
# Each text field is laid down with `.ascii "..."` for the characters
# followed by `.space N` to pad the remainder with zero bytes — so
# every field is null-terminated and the whole record is exactly 324
# bytes regardless of how short the text is.  (x86 PGU used `.rept`
# to emit the padding; `.space` is spim's equivalent.)
#
# File I/O on spim uses the same syscalls as the Unix examples:
#   open  = 13   ($a0=path, $a1=flags, $a2=mode)  -> $v0 = fd
#   write = 15   ($a0=fd,   $a1=buf,   $a2=count)  -> $v0 = bytes
#   close = 16   ($a0=fd)
# Flag 65 = O_WRONLY(1) | O_CREAT(0x40); mode 420 = 0644.


        .data
# doc-region-begin record data
record1:
        .ascii "Fredrick"                       # firstname  (8 chars)
        .space 32                               #   pad to 40
        .ascii "Bartlett"                        # lastname   (8)
        .space 32                               #   pad to 40
        .ascii "4242 S Prairie\nTulsa, OK 55555" # address    (30)
        .space 210                              #   pad to 240
        .word 45                                # age
record2:
        .ascii "Marilyn"                         # 7
        .space 33
        .ascii "Taylor"                          # 6
        .space 34
        .ascii "2224 S Johannan St\nChicago, IL 12345"  # 36
        .space 204
        .word 29
record3:
        .ascii "Derrick"                         # 7
        .space 33
        .ascii "McIntire"                        # 8
        .space 32
        .ascii "500 W Oakland\nSan Diego, CA 54321"     # 33
        .space 207
        .word 36
# doc-region-end record data

file_name:
        .asciiz "test.dat"


        .text
        .globl main
main:
# doc-region-begin write open
        # fd = open("test.dat", O_WRONLY|O_CREAT, 0644)
        la   $a0, file_name
        li   $a1, 65             # O_WRONLY | O_CREAT
        li   $a2, 420            # mode 0644
        li   $v0, 13            # open
        syscall
        move $s0, $v0            # save the file descriptor
# doc-region-end write open

# doc-region-begin write records
        # write(fd, &recordN, RECORD_SIZE) three times
        move $a0, $s0
        la   $a1, record1
        li   $a2, 324            # RECORD_SIZE
        li   $v0, 15            # write
        syscall

        move $a0, $s0
        la   $a1, record2
        li   $a2, 324
        li   $v0, 15
        syscall

        move $a0, $s0
        la   $a1, record3
        li   $a2, 324
        li   $v0, 15
        syscall
# doc-region-end write records

        # close(fd)
        move $a0, $s0
        li   $v0, 16
        syscall

        li   $v0, 0             # exit status 0
        jr   $ra
