# Copyright 2002 Jonathan Bartlett
# Copyright 2026 William Emerison Six (MIPS/spimulator port)
#
# Permission is granted to copy, distribute and/or modify this program
# under the terms of the GNU Free Documentation License, Version 1.1 or
# any later version published by the Free Software Foundation; with no
# Invariant Sections, with no Front-Cover Texts, and with no Back-Cover
# Texts.  This program is an example from "Programming from the Ground
# Up"; a copy of the license is in the book's GNU FDL appendix (fdlap).


# PURPOSE: A tiny memory manager — allocate and deallocate blocks of
#          memory on the heap.  PGU's allocator, retargeted to
#          MIPS/spimulator.
#
# Each block is preceded by an 8-byte HEADER:
#
#    +----------------+----------------+----------------------+
#   | available flag | size of block  | the block itself ... |
#   |   (4 bytes)    |   (4 bytes)    |                      |
#   +----------------+----------------+----------------------+
#   ^ header start (offset 0)          ^ pointer we hand back
#                                         (header start + 8)
#
#   header offsets:  avail = 0   size = 4   HEADER_SIZE = 8
#   available flag:  1 = free/available,  0 = in use/unavailable
#
# We hand the caller a pointer to the usable bytes (past the header),
# so they never see the bookkeeping.
#
#
# brk vs. sbrk
# ============
# PGU's i386 version used the Linux `brk` syscall, which SETS the
# program break to an absolute address.  spimulator provides `sbrk`
# (syscall 9) instead, which GROWS the heap by a number of bytes and
# returns the address of the newly added region:
#
#     sbrk(0)  -> returns the current top of the heap (a query)
#     sbrk(n)  -> grows the heap by n bytes, returns the OLD top
#                 (i.e. the base of the n new bytes)
#
# So `allocate_init` records the starting top with sbrk(0), and when
# `allocate` runs out of free blocks it calls sbrk(HEADER+size) to get
# a fresh region.
#
# This file demonstrates itself: `main` allocates two blocks, frees the
# first, then allocates a third that fits in the freed block — and
# checks that the allocator REUSED the freed block (returns the same
# pointer).  It exits 0 on success, 1 on failure.
#
# Invocation:  spimulator -f alloc.asm ; echo $?      # -> 0


        .data
heap_begin:
        .word 0                  # first address we manage
current_break:
        .word 0                  # one past the last address we manage


        .text
        .globl main
main:
        jal  allocate_init

        li   $a0, 100            # p1 = allocate(100)
        jal  allocate
        move $s0, $v0

        li   $a0, 50             # p2 = allocate(50)
        jal  allocate
        move $s1, $v0

        move $a0, $s0            # deallocate(p1)
        jal  deallocate

        li   $a0, 80             # p3 = allocate(80) — should reuse p1's block
        jal  allocate
        move $s2, $v0

        # success (exit 0) iff p3 reused p1
        bne  $s2, $s0, fail
        li   $a0, 0
        j    finish
fail:
        li   $a0, 1
finish:
        li   $v0, 17
        syscall


# ---------- allocate_init() — record the starting heap top ---------
# doc-region-begin allocate_init
allocate_init:
        move $a0, $zero          # sbrk(0): query the current heap top
        li   $v0, 9
        syscall                  # $v0 = current break
        sw   $v0, heap_begin     # first address we will manage
        sw   $v0, current_break  # nothing handed out yet
        jr   $ra
# doc-region-end allocate_init


# ---------- allocate(size=$a0) -> pointer in $v0 (0 on failure) -----
# Scan the managed regions for a free one big enough; if none, grow
# the heap with sbrk.
# doc-region-begin allocate
allocate:
        move $t0, $a0            # requested size
        lw   $t1, heap_begin     # search cursor
        lw   $t2, current_break  # end of managed memory

alloc_loop:
        beq  $t1, $t2, move_break    # reached the end: need more memory
        lw   $t4, 0($t1)         # this region's available flag
        lw   $t3, 4($t1)         # this region's size
        beq  $t4, $zero, next_region # 0 = unavailable: skip it
        bge  $t3, $t0, allocate_here # free AND big enough: take it
next_region:
        addi $t1, $t1, 8         # step past the header ...
        add  $t1, $t1, $t3       # ... and past the data
        j    alloc_loop

allocate_here:
        sw   $zero, 0($t1)       # mark it unavailable
        addi $v0, $t1, 8         # hand back the address past the header
        jr   $ra

move_break:
        addi $a0, $t0, 8         # grow by HEADER_SIZE + requested size
        li   $v0, 9              # sbrk
        syscall                  # $v0 = base of the new region
        move $t6, $v0
        sw   $zero, 0($t6)       # available flag = unavailable
        sw   $t0, 4($t6)         # store the size
        addi $t7, $t6, 8         # current_break = base + HEADER + size
        add  $t7, $t7, $t0
        sw   $t7, current_break
        addi $v0, $t6, 8         # return the address past the header
        jr   $ra
# doc-region-end allocate


# ---------- deallocate(pointer=$a0) — return a block to the pool ----
# doc-region-begin deallocate
deallocate:
        addi $t0, $a0, -8        # back up to the header
        li   $t1, 1              # 1 = available
        sw   $t1, 0($t0)         # mark the block free
        jr   $ra
# doc-region-end deallocate
