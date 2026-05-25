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
#     void integer_to_string(unsigned int n, char *buf);  // decimal text
#
#     int main(void) {
#       char buf[16];
#       integer_to_string(824, buf);
#       print_string(buf);
#       print_char('\n');
#       return 0;
#     }
#
# Invocation:  spimulator -f conversion-program.asm        # prints: 824
#
#
#PURPOSE:  Convert a binary integer into its decimal-text form and
#          print it.  PGU's conversion-program, retargeted to
#          MIPS/spim.  The conversion itself is the lesson of the
#          "Counting Like a Computer" chapter: a number in a register
#          is NOT the same thing as the characters "8", "2", "4".
#
#
# How integer -> string works
# ===========================
# Repeatedly divide by 10.  Each remainder is one decimal digit, in
# REVERSE order (least-significant first), so we write digits from the
# end of the buffer backwards and return a pointer to the first one.
# `div` puts quotient in LO (read with mflo) and remainder in HI
# (read with mfhi).  Adding '0' (48) turns a 0..9 value into its
# ASCII character.
#
#
#SYMBOL TABLE  (C -> MIPS location)
#   n             $a0 / $t0   the value being divided down
#   buf           $a1         caller's buffer (write cursor walks it)
#   digit         $t2         one remainder, 0..9, then its ASCII byte
#   ten           $t3         constant divisor


        .data
tmp_buffer:
        .space 16                # room for the decimal text + null


        .text
        .globl main
main:
        # `jal` below overwrites $ra, so stash the runtime's return
        # address first — otherwise the final `jr $ra` jumps back into
        # main instead of exiting.  (This is THE lesson about $ra.)
        move $s0, $ra

        # integer_to_string(824, tmp_buffer)
        li   $a0, 824
        la   $a1, tmp_buffer
        jal  integer_to_string

        # print_string(tmp_buffer)
        la   $a0, tmp_buffer
        li   $v0, 4
        syscall

        # print_char('\n')
        li   $a0, '\n'
        li   $v0, 11
        syscall

        move $ra, $s0           # restore the runtime return address
        li   $v0, 0
        jr   $ra


# ---------- integer_to_string(n=$a0, buf=$a1) ----------------------
# Writes the decimal text of unsigned n into buf, null-terminated.
# doc-region-begin integer to string
integer_to_string:
        move $t0, $a0            # n (the running quotient)
        li   $t3, 10             # divisor

        # Fill from the end of a 16-byte scratch window backwards.
        addi $t1, $a1, 15        # cursor = buf + 15
        sb   $zero, 0($t1)       # final null terminator

its_loop:
        addi $t1, $t1, -1        # step the cursor left one byte
        div  $t0, $t3            # LO = n/10, HI = n%10
        mfhi $t2                 # digit = n % 10
        mflo $t0                 # n = n / 10
        addi $t2, $t2, 48        # digit -> ASCII ('0' + digit)
        sb   $t2, 0($t1)         # store the character
        bne  $t0, $zero, its_loop    # more digits while n != 0

        # The text may not start at buf[0]; copy/return its start.
        # Simplest: move the produced text to the front of buf.
        move $t4, $a1            # dest = buf
its_copy:
        lb   $t5, 0($t1)
        sb   $t5, 0($t4)
        beq  $t5, $zero, its_done
        addi $t1, $t1, 1
        addi $t4, $t4, 1
        j    its_copy
its_done:
        jr   $ra
# doc-region-end integer to string
