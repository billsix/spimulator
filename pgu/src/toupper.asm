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
#     int main(int argc, char **argv) {
#       int in  = open(argv[1], O_RDONLY);
#       int out = open(argv[2], O_WRONLY|O_CREAT|O_TRUNC, 0644);
#       char buf[500];
#       int n;
#       while ((n = read(in, buf, sizeof buf)) > 0) {
#         for (int i = 0; i < n; i++)
#           if (buf[i] >= 'a' && buf[i] <= 'z') buf[i] -= 32;  // to upper
#         write(out, buf, n);
#       }
#       close(in); close(out);
#       return 0;
#     }
#
# Invocation:  spimulator -f toupper.asm INPUT OUTPUT
#   e.g.  echo hello | ... ; spimulator -f toupper.asm in.txt out.txt
#
#
#PURPOSE:  Copy an input file to an output file, converting every
#          lowercase letter to uppercase.  PGU's "toupper" program,
#          retargeted to MIPS/spimulator.  Demonstrates command-line
#          arguments (argv), block file I/O through a buffer, and an
#          in-place byte transform.
#
#
# Command-line arguments on spimulator
# ====================================
# `main` is entered with argc in $a0 and argv (a pointer to the array
# of string pointers) in $a1, the same as a C `main`.  argv[0] is the
# program; argv[1] and argv[2] are our input and output filenames, at
# byte offsets 4 and 8 into the argv array (each pointer is 4 bytes).
# A syscall clobbers $a0/$a1, so we grab both filename pointers into
# callee-saved registers BEFORE the first `open`.
#
#
#SYMBOL TABLE  (C -> MIPS location)
#   argv[1]/in name   $s0
#   argv[2]/out name  $s1
#   in  (fd)          $s2
#   out (fd)          $s3
#   n (bytes read)    $s4
#   i (byte index)    $t2
#   buf               buffer (.data), BUFFER_SIZE = 500


        .data
buffer:
        .space 500               # BUFFER_SIZE; byte access, no alignment needed


        .text
        .globl main
main:
        # Save the two filename pointers before any syscall clobbers $a1.
        lw   $s0, 4($a1)         # argv[1] = input filename
        lw   $s1, 8($a1)         # argv[2] = output filename

        # in = open(argv[1], O_RDONLY)
        move $a0, $s0
        li   $a1, 0              # O_RDONLY
        li   $a2, 0
        li   $v0, 13
        syscall
        move $s2, $v0            # input fd

        # out = open(argv[2], O_WRONLY|O_CREAT|O_TRUNC, 0644)
        move $a0, $s1
        li   $a1, 577            # 1 | 0x40 | 0x200 = O_WRONLY|O_CREAT|O_TRUNC
        li   $a2, 420            # mode 0644
        li   $v0, 13
        syscall
        move $s3, $v0            # output fd

read_loop:
        # n = read(in, buffer, BUFFER_SIZE)
        move $a0, $s2
        la   $a1, buffer
        li   $a2, 500
        li   $v0, 14
        syscall
        move $s4, $v0            # bytes read
        blez $s4, done           # 0 = EOF, negative = error: stop

# doc-region-begin convert
        # Convert buffer[0 .. n) to uppercase, in place.
        li   $t2, 0              # i = 0
convert_loop:
        bge  $t2, $s4, convert_done
        la   $t0, buffer
        add  $t3, $t0, $t2       # &buffer[i]
        lb   $t4, 0($t3)         # c = buffer[i]
        blt  $t4, 'a', next_byte # below 'a'? leave it
        bgt  $t4, 'z', next_byte # above 'z'? leave it
        addi $t4, $t4, -32       # 'a'-'A' = 32; subtract to uppercase
        sb   $t4, 0($t3)         # store it back
next_byte:
        addi $t2, $t2, 1
        j    convert_loop
convert_done:
# doc-region-end convert

        # write(out, buffer, n)
        move $a0, $s3
        la   $a1, buffer
        move $a2, $s4
        li   $v0, 15
        syscall

        j    read_loop

done:
        move $a0, $s2
        li   $v0, 16             # close(in)
        syscall
        move $a0, $s3
        li   $v0, 16             # close(out)
        syscall

        li   $v0, 0
        jr   $ra
