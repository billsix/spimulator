#==============================================================
# libctype.asm — MIPS implementations of the libctype functions.
#
# Adapted from musl libc src/ctype/ (algorithm-by-algorithm).
#   musl:    https://musl.libc.org/
#   License: MIT — see LICENSE-musl in this directory.
#
# Calling-convention contract (every function in this file):
#   $a0  — input byte (low 8 bits; upper bits ignored)
#   $v0  — return value (0/1 for classifiers; converted byte for
#           toupper/tolower)
#   $s*  — preserved (none touched)
#   $t*  — clobbered freely
#   $ra  — preserved; every function is a leaf (no jal inside)
#
# Note for isalnum / toupper / tolower:
#   The C versions call into the other libctype functions
#   (isalpha+isdigit, islower, isupper).  The asm versions inline
#   those checks to keep every function a leaf — no $ra save, no
#   stack frame.  This is a deliberate departure from the C
#   structure to preserve the leaf-function discipline that
#   makes libctype the simplest of the three libraries.
#==============================================================

        .text

#--------------------------------------------------------------
# isdigit(c) — 1 if c in '0'..'9' else 0
#
# Adapted from musl src/ctype/isdigit.c.
# C: return (unsigned)c - '0' < 10;
#--------------------------------------------------------------
        .globl  isdigit
isdigit:
        addiu   $v0, $a0, -48       # $v0 = c - '0'
        sltiu   $v0, $v0, 10        # $v0 = ($v0 <u 10) ? 1 : 0
        jr      $ra

#--------------------------------------------------------------
# isalpha(c) — 1 if c in 'A'..'Z' or 'a'..'z' else 0
#
# Adapted from musl src/ctype/isalpha.c.
# C: return ((unsigned)c | 32) - 'a' < 26;
#
# Bit-OR with 32 folds 'A'..'Z' to 'a'..'z' (bit 5 is the only
# difference between the two ranges), so one unsigned range
# check covers both.
#--------------------------------------------------------------
        .globl  isalpha
isalpha:
        ori     $t0, $a0, 32        # $t0 = c | 32 (fold case)
        addiu   $t0, $t0, -97       # $t0 -= 'a'
        sltiu   $v0, $t0, 26        # $v0 = ($t0 <u 26) ? 1 : 0
        jr      $ra

#--------------------------------------------------------------
# isalnum(c) — 1 if isalpha(c) || isdigit(c) else 0
#
# Adapted from musl src/ctype/isalnum.c.
# C: return isalpha(c) || isdigit(c);
#
# The C version calls isalpha+isdigit; the asm inlines both to
# avoid a frame.  Try isalpha first (folded-case range check);
# if that fails, fall through to the isdigit range check.
#--------------------------------------------------------------
        .globl  isalnum
isalnum:
        ori     $t0, $a0, 32        # isalpha inline: $t0 = c | 32
        addiu   $t0, $t0, -97       #                 $t0 -= 'a'
        sltiu   $t0, $t0, 26        #                 in 0..25?
        bnez    $t0, isalnum_yes    # yes -> return 1
        addiu   $t0, $a0, -48       # isdigit inline: $t0 = c - '0'
        sltiu   $v0, $t0, 10        #                 in 0..9? -> $v0
        jr      $ra
isalnum_yes:
        li      $v0, 1
        jr      $ra

#--------------------------------------------------------------
# isupper(c) — 1 if c in 'A'..'Z' else 0
#
# Adapted from musl src/ctype/isupper.c.
# C: return (unsigned)c - 'A' < 26;
#--------------------------------------------------------------
        .globl  isupper
isupper:
        addiu   $v0, $a0, -65       # $v0 = c - 'A'
        sltiu   $v0, $v0, 26        # in 0..25?
        jr      $ra

#--------------------------------------------------------------
# islower(c) — 1 if c in 'a'..'z' else 0
#
# Adapted from musl src/ctype/islower.c.
# C: return (unsigned)c - 'a' < 26;
#--------------------------------------------------------------
        .globl  islower
islower:
        addiu   $v0, $a0, -97       # $v0 = c - 'a'
        sltiu   $v0, $v0, 26        # in 0..25?
        jr      $ra

#--------------------------------------------------------------
# isspace(c) — 1 if c is C-locale whitespace, else 0
#
# Adapted from musl src/ctype/isspace.c.
# C: return c == ' ' || (unsigned)c - '\t' < 5;
#
# The whitespace set is { ' ', '\t', '\n', '\v', '\f', '\r' }.
# '\t' through '\r' are 5 consecutive bytes (0x09..0x0d); a
# single unsigned range check handles those five.  ' ' (0x20)
# is checked separately.
#--------------------------------------------------------------
        .globl  isspace
isspace:
        li      $t0, 32             # ' '
        beq     $a0, $t0, isspace_yes
        addiu   $t0, $a0, -9        # $t0 = c - '\t'
        sltiu   $v0, $t0, 5         # in 0..4?
        jr      $ra
isspace_yes:
        li      $v0, 1
        jr      $ra

#--------------------------------------------------------------
# toupper(c) — uppercase if c is lowercase, else c
#
# Adapted from musl src/ctype/toupper.c.
# C: if (islower(c)) return c & 0x5f; return c;
#
# `c & 0x5f` clears bit 5, the only bit difference between
# 'a' (0x61) and 'A' (0x41).  Guarded by islower so non-letters
# pass through unchanged.
#--------------------------------------------------------------
        .globl  toupper
toupper:
        addiu   $t0, $a0, -97       # islower inline: $t0 = c - 'a'
        sltiu   $t0, $t0, 26        #                 in 0..25?
        beqz    $t0, toupper_keep   # not lowercase -> pass through
        andi    $v0, $a0, 0x5f      # clear bit 5
        jr      $ra
toupper_keep:
        move    $v0, $a0
        jr      $ra

#--------------------------------------------------------------
# tolower(c) — lowercase if c is uppercase, else c
#
# Adapted from musl src/ctype/tolower.c.
# C: if (isupper(c)) return c | 32; return c;
#
# `c | 32` sets bit 5.  Guarded by isupper so non-letters pass
# through unchanged.
#--------------------------------------------------------------
        .globl  tolower
tolower:
        addiu   $t0, $a0, -65       # isupper inline: $t0 = c - 'A'
        sltiu   $t0, $t0, 26        #                 in 0..25?
        beqz    $t0, tolower_keep   # not uppercase -> pass through
        ori     $v0, $a0, 32        # set bit 5
        jr      $ra
tolower_keep:
        move    $v0, $a0
        jr      $ra
