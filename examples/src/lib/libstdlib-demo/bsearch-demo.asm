#==============================================================
# bsearch-demo.asm — exercise libstdlib's bsearch on a sorted
# int array.  Byte-identical stdout to bsearch-demo.c.
#
# Load order:
#   spimulator -f libctype.asm -f libstdlib.asm -f bsearch-demo.asm
#
# This demo also teaches the caller side of the 5th-arg ABI:
# setting up `cmp` at 16($sp) before `jal bsearch`.  See the
# header block in libstdlib.asm for the bsearch side.
#==============================================================
#
# STACK-VISIBLE STYLE: main's C locals — the keys cursor k, the end
# pointer, the current key value (its address is passed to bsearch),
# and the result pointer — live in -N($fp) stack slots; a scratch
# register holds only a working COPY.  main saves $ra for the bsearch
# calls.  The OUTGOING-ARGUMENT area stays at the bottom of the frame
# addressed via $sp, per the O32 calling convention: args 1-4 ride in
# $a0-$a3 and the 5th arg (the cmp pointer) sits at 16($sp), which is
# exactly where bsearch reads it.  int_cmp is a leaf comparator; data/
# keys are file-scope data.  No #CANONICAL: a driver that `jal`s into
# libstdlib can't be assembled standalone by the value-checker;
# golden-tested via run-demo.sh.
# Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable   | memory addr  | register   | C type    | bytes | role / notes
#  -------------+--------------+------------+-----------+-------+---------------------------
#  k            | -12($fp)     | $t0 (copy) | int *     |   4   | cursor walking keys[]
#  end          | -16($fp)     | $t1 (copy) | int *     |   4   | &keys[N_KEYS] (loop bound)
#  key          | -20($fp)     | $t2 (copy) | int       |   4   | *k; its ADDRESS ($fp-20) is bsearch arg 1
#  result       | -24($fp)     | $v0 (copy) | int *     |   4   | bsearch return (NULL if not found)
#  cmp (5th arg)| 16($sp)      | —          | fn ptr    |   4   | int_cmp — O32 outgoing-arg slot bsearch reads
#  data/keys    | data/keys    | —          | int[]     |       | file-scope sorted array / query keys (.data)
#  (saved $ra)  | -4($fp)      | —          | code addr |   4   | across the jal bsearch calls
#  (saved $fp)  | -8($fp)      | —          | frame ptr |   4   | caller's frame pointer
#
#  SNAPSHOT (loop over keys; querying key = 47, which is data[4])
#
#        main's stack frame               scratch / ABI area
#   +-----------------------------+      +-----------------------------+
#   | -4  saved $ra               |      | $a0 = $fp-20 (&key)
#   | -8  saved $fp               |      | $a1 = &data, $a2 = 10, $a3=4
#   | -12 k    = &keys[1]         |      | 16($sp) = &int_cmp (5th arg)
#   | -16 end  = &keys[9]         |      +-----------------------------+
#   | -20 key  = 47               |
#   | -24 result = &data[4]       |
#   |  ... 16($sp): cmp ptr ...   |
#   +-----------------------------+
#         $sp = $fp - 48
#
#   bsearch returns &data[4]; index = (&data[4]-data)/4 = 4, so the
#   line "bsearch(47) = idx 4" prints.

        .data
        .align  2
data:    .word   5, 12, 23, 34, 47, 56, 67, 78, 89, 100
N:       .word   10

keys:    .word   5, 47, 100, 23, 89, 50, 0, 200, 13
N_KEYS:  .word   9

# Format strings
fmt_pre: .asciiz "bsearch("
fmt_eq:  .asciiz ") = "
fmt_idx: .asciiz "idx "
fmt_nf:  .asciiz "not found"
fmt_nl:  .asciiz "\n"

        .text

#--------------------------------------------------------------
# int_cmp(a, b) — return *(int*)a - *(int*)b
#
# Leaf comparator for bsearch.  Same calling convention as any
# C function: args in $a0 / $a1, result in $v0, $ra preserved.
# Will be `jalr`d by bsearch (not jal'd).
#--------------------------------------------------------------
int_cmp:
        lw      $t0, 0($a0)
        lw      $t1, 0($a1)
        subu    $v0, $t0, $t1
        jr      $ra

#--------------------------------------------------------------
# main — loop over keys[], call bsearch on each, print the
# result (index or "not found").
#--------------------------------------------------------------
        .globl  main
main:
        # ---- prologue: 48-byte frame.  Low 0..16($sp) is the O32
        #      outgoing-argument area (shadow + the 5th arg); named
        #      locals live at -N($fp). -------------------------------
        addiu   $sp, $sp, -48
        sw      $ra, 44($sp)        # -> -4($fp)
        sw      $fp, 40($sp)        # -> -8($fp)
        addiu   $fp, $sp, 48

        # Stash the cmp pointer at 16($sp) — bsearch reads its 5th arg
        # from this exact slot.  Set once; doesn't change.
        la      $t0, int_cmp
        sw      $t0, 16($sp)

        # k = keys;  end = &keys[N_KEYS];
        la      $t0, keys
        sw      $t0, -12($fp)       # k = keys
        lw      $t1, N_KEYS
        sll     $t1, $t1, 2         # N_KEYS * 4 bytes
        addu    $t1, $t0, $t1
        sw      $t1, -16($fp)       # end = &keys[N_KEYS]

loop:
        lw      $t0, -12($fp)       # k
        lw      $t1, -16($fp)       # end
        bge     $t0, $t1, done

        # key = *k;   (copy out so we can pass its address)
        lw      $t2, 0($t0)
        sw      $t2, -20($fp)       # key

        # print "bsearch("
        la      $a0, fmt_pre
        li      $v0, 4
        syscall
        # print the key value
        lw      $a0, -20($fp)
        li      $v0, 1
        syscall
        # print ") = "
        la      $a0, fmt_eq
        li      $v0, 4
        syscall

        # result = bsearch(&key, data, N, sizeof(int), int_cmp);
        # 5th arg (int_cmp ptr) already at 16($sp).
        addiu   $a0, $fp, -20       # &key
        la      $a1, data
        lw      $a2, N
        li      $a3, 4              # sizeof(int)
        jal     bsearch
        beqz    $v0, not_found

        # Found — stash the pointer (prints below clobber $t*).
        sw      $v0, -24($fp)       # result
        la      $a0, fmt_idx
        li      $v0, 4
        syscall
        # index = (result - data) / sizeof(int)
        lw      $t0, -24($fp)       # result
        la      $t1, data
        subu    $t0, $t0, $t1
        srl     $t0, $t0, 2
        move    $a0, $t0
        li      $v0, 1
        syscall
        j       after

not_found:
        la      $a0, fmt_nf
        li      $v0, 4
        syscall

after:
        # Newline.
        la      $a0, fmt_nl
        li      $v0, 4
        syscall

        # k++  (next key: advance one int)
        lw      $t0, -12($fp)       # k
        addiu   $t0, $t0, 4
        sw      $t0, -12($fp)
        j       loop

done:
        # ---- epilogue ----------------------------------------------
        li      $v0, 0
        lw      $ra, -4($fp)
        lw      $fp, -8($fp)
        addiu   $sp, $sp, 48
        jr      $ra
