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
#       int *p = data_items;
#       int max = *p;                 // seed with the first item
#       while (*p != 0) {             // 0 terminates the list
#         if (*p > max) max = *p;
#         p++;
#       }
#       return max;                   // the shell sees this as $?
#     }
#
# Invocation:  spimulator -f maximum.asm ; echo $?      # -> 222
#
#
#PURPOSE:  Find the largest value in a 0-terminated array of words
#          and hand it back to the shell as the exit status.  This is
#          Programming from the Ground Up's "maximum" program,
#          retargeted from i386 Linux to MIPS on spim.
#
#
# What changes moving from x86 to MIPS
# ====================================
# The x86 original read each array element in ONE instruction that
# folded the array base, the index, and the *4 scaling together:
#
#     movl data_items(,%edi,4), %eax     # eax = data_items[edi]
#
# MIPS is a load/store architecture: arithmetic instructions only
# touch registers, and memory is reached ONLY through explicit `lw`
# (load word) and `sw` (store word).  There is no "compute an address
# and read it" in a single arithmetic op.  So we keep a pointer in a
# register and step it forward four bytes (one word) at a time:
#
#     lw   $t2, 0($t0)      # current = *cursor
#     addi $t0, $t0, 4      # cursor++  (4 bytes = one word)
#
# This is more typing, but it makes the cost of every memory access
# visible — which is the whole point of teaching at this level.
#
#
#VARIABLES / SYMBOL TABLE  (C -> MIPS location)
#
#   p (cursor)    $t0    address of the item being examined
#   max           $t1    largest item found so far (becomes $?)
#   current       $t2    the item currently under examination
#   data_items    .data  the 0-terminated .word array


        .data
# doc-region-begin maximum data
data_items:
        # These are the data items.  A 0 terminates the list.
        .word 3, 67, 34, 222, 45, 75, 54, 34, 44, 33, 22, 11, 66, 0
# doc-region-end maximum data


        .text
        .globl main
main:
# doc-region-begin maximum loop
        la   $t0, data_items     # $t0 = &data_items[0]
        lw   $t1, 0($t0)         # max = data_items[0]; the first item
                                 # is the biggest seen so far

start_loop:
        lw   $t2, 0($t0)         # current = *cursor
        beq  $t2, $zero, loop_exit   # a 0 marks the end of the data
        ble  $t2, $t1, skip_update   # if current <= max, leave max alone
        move $t1, $t2                # otherwise current is the new max
skip_update:
        addi $t0, $t0, 4         # step the cursor forward one word
        j    start_loop          # and around again

loop_exit:
# doc-region-end maximum loop
        # $t1 holds the maximum; hand it to the shell as the status code.
        move $a0, $t1            # status = max
        li   $v0, 17            # syscall 17 = exit2 (status in $a0)
        syscall                 # never returns
