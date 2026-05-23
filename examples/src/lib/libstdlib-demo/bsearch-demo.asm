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
#
# Frame layout (40 bytes):
#   0..15   arg-pass area for bsearch (shadow slots for $a0..$a3)
#   16..19  cmp pointer (5th arg to bsearch — set once before loop)
#   20..23  key scratch (current key copied here so we can pass &)
#   24..27  result scratch (bsearch return value, survives prints)
#   28..31  $ra
#   32..35  $s0 — current pointer into keys[]
#   36..39  $s1 — end pointer (keys + N_KEYS)
#--------------------------------------------------------------
        .globl  main
main:
        addiu   $sp, $sp, -40
        sw      $ra, 28($sp)
        sw      $s0, 32($sp)
        sw      $s1, 36($sp)

        # Stash the cmp pointer at 16($sp) — bsearch will read it
        # from this exact slot.  Set once; doesn't change.
        la      $t0, int_cmp
        sw      $t0, 16($sp)

        # Initialize loop pointers.
        la      $s0, keys
        lw      $t0, N_KEYS
        sll     $t0, $t0, 2         # N_KEYS * 4 bytes
        addu    $s1, $s0, $t0       # $s1 = &keys[N_KEYS]

loop:
        bge     $s0, $s1, done

        # Copy current key into the scratch slot (so we can pass &).
        lw      $t0, 0($s0)
        sw      $t0, 20($sp)

        # Print "bsearch("
        la      $a0, fmt_pre
        li      $v0, 4
        syscall

        # Print the key value
        lw      $a0, 20($sp)
        li      $v0, 1
        syscall

        # Print ") = "
        la      $a0, fmt_eq
        li      $v0, 4
        syscall

        # Call bsearch(&key, data, N, sizeof(int), int_cmp).
        # 5th arg (int_cmp ptr) already at 16($sp).
        addiu   $a0, $sp, 20        # &key
        la      $a1, data
        lw      $a2, N
        li      $a3, 4              # sizeof(int)
        jal     bsearch

        # $v0 = result pointer or NULL.
        beqz    $v0, not_found
        # Found — stash pointer (prints below may clobber $t*).
        sw      $v0, 24($sp)
        la      $a0, fmt_idx
        li      $v0, 4
        syscall
        # Compute and print index = (ptr - data) / sizeof(int).
        lw      $t0, 24($sp)
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

        addiu   $s0, $s0, 4         # next key
        j       loop

done:
        lw      $ra, 28($sp)
        lw      $s0, 32($sp)
        lw      $s1, 36($sp)
        addiu   $sp, $sp, 40
        li      $v0, 0
        jr      $ra
