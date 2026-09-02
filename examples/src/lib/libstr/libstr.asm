#==============================================================
# libstr.asm — MIPS implementations of the libstr functions.
#
# Adapted from musl libc src/string/ (algorithm-by-algorithm).
#   musl:    https://musl.libc.org/
#   License: MIT — see LICENSE-musl in this directory.
#
# Calling-convention contract (every function in this file):
#   $a0..$a3 — inputs, in C-call order
#   $v0      — return value (a pointer or an int)
#   $s*      — preserved (none are touched)
#   $t*      — clobbered freely
#   $ra      — preserved; every function is a leaf (no jal inside)
#
# There is NO .data in this file: every function operates only on
# the buffers its caller passes in.  Loading it alongside a demo:
#   spimulator -f libstr.asm -f str-demo.asm
# The forward references from the demo resolve at load time
# because spim's symbol table accumulates across -f files.
#==============================================================

        .text

#--------------------------------------------------------------
# strlen(s) — count bytes up to the NUL sentinel.
#
# Adapted from musl src/string/strlen.c.
# C: p = s; while (*p) p++; return p - s;
#
# The naive version walks $a0 forward and counts in $v0.  (musl
# walks a word at a time with a has-zero-byte test; the byte
# loop is the teaching shape.)
#--------------------------------------------------------------
        .globl  strlen
strlen:
        li      $v0, 0              # length = 0
strlen_loop:
        lbu     $t0, 0($a0)         # *p
        beqz    $t0, strlen_done    # NUL -> stop
        addiu   $a0, $a0, 1         # p++
        addiu   $v0, $v0, 1         # length++
        j       strlen_loop
strlen_done:
        jr      $ra

#--------------------------------------------------------------
# strcmp(a, b) — three-way compare, returns the byte difference.
#
# Adapted from musl src/string/strcmp.c.
# C: while (*a && *a == *b) { a++; b++; }
#    return (unsigned char)*a - (unsigned char)*b;
#
# lbu makes both bytes unsigned, so the subu difference has the
# same sign as the (unsigned char) subtraction in C.
#--------------------------------------------------------------
        .globl  strcmp
strcmp:
        lbu     $t0, 0($a0)         # *a
        lbu     $t1, 0($a1)         # *b
        bne     $t0, $t1, strcmp_done  # differ -> exit with the diff
        beqz    $t0, strcmp_done    # both NUL -> equal (diff is 0)
        addiu   $a0, $a0, 1         # a++
        addiu   $a1, $a1, 1         # b++
        j       strcmp
strcmp_done:
        subu    $v0, $t0, $t1       # (unsigned char)*a - (unsigned char)*b
        jr      $ra

#--------------------------------------------------------------
# strncmp(a, b, n) — strcmp bounded by a byte count.
#
# Adapted from musl src/string/strncmp.c.
# C: while (n && *a && *a == *b) { a++; b++; n--; }
#    if (n == 0) return 0;
#    return (unsigned char)*a - (unsigned char)*b;
#--------------------------------------------------------------
        .globl  strncmp
strncmp:
        beqz    $a2, strncmp_eq     # n == 0 -> compared prefix is equal
        lbu     $t0, 0($a0)         # *a
        lbu     $t1, 0($a1)         # *b
        bne     $t0, $t1, strncmp_done  # differ -> exit with the diff
        beqz    $t0, strncmp_done   # both NUL -> equal (diff is 0)
        addiu   $a0, $a0, 1         # a++
        addiu   $a1, $a1, 1         # b++
        addiu   $a2, $a2, -1        # n--
        j       strncmp
strncmp_done:
        subu    $v0, $t0, $t1
        jr      $ra
strncmp_eq:
        li      $v0, 0
        jr      $ra

#--------------------------------------------------------------
# strcpy(dst, src) — copy src (with its NUL) into dst; return dst.
#
# Adapted from musl src/string/strcpy.c.
# C: d = dst; while ((*d++ = *src++)) ; return dst;
#
# Copy the byte first, THEN test it, so the terminating NUL is
# written before the loop stops.
#--------------------------------------------------------------
        .globl  strcpy
strcpy:
        move    $v0, $a0            # return value = dst
strcpy_loop:
        lbu     $t0, 0($a1)         # *src
        sb      $t0, 0($a0)         # *dst = *src (NUL included)
        beqz    $t0, strcpy_done    # just wrote the NUL -> done
        addiu   $a0, $a0, 1         # dst++
        addiu   $a1, $a1, 1         # src++
        j       strcpy_loop
strcpy_done:
        jr      $ra

#--------------------------------------------------------------
# strncpy(dst, src, n) — copy up to n bytes, NUL-pad the rest.
#
# Adapted from musl src/string/strncpy.c.
# C: while (n && *src) { *d++ = *src++; n--; }
#    while (n) { *d++ = '\0'; n--; }
#    return dst;
#
# Note the famous gotcha: if src is n or more bytes long, no NUL
# terminator is written into dst.
#--------------------------------------------------------------
        .globl  strncpy
strncpy:
        move    $v0, $a0            # return value = dst
strncpy_copy:
        beqz    $a2, strncpy_done   # budget exhausted
        lbu     $t0, 0($a1)         # *src
        beqz    $t0, strncpy_pad    # src NUL reached -> pad the rest
        sb      $t0, 0($a0)         # *dst = *src
        addiu   $a0, $a0, 1         # dst++
        addiu   $a1, $a1, 1         # src++
        addiu   $a2, $a2, -1        # n--
        j       strncpy_copy
strncpy_pad:
        sb      $zero, 0($a0)       # *dst = '\0'
        addiu   $a0, $a0, 1         # dst++
        addiu   $a2, $a2, -1        # n--
        bnez    $a2, strncpy_pad
strncpy_done:
        jr      $ra

#--------------------------------------------------------------
# strchr(s, c) — first occurrence of c in s, or 0 (NUL matches).
#
# Adapted from musl src/string/strchr.c.
# C: ch = (char)c;
#    while (*s) { if (*s == ch) return s; s++; }
#    if (ch == 0) return s;   /* the terminator */
#    return NULL;
#
# The match test comes BEFORE the end-of-string test, so a search
# for '\0' returns the address of the terminator.
#--------------------------------------------------------------
        .globl  strchr
strchr:
        andi    $t1, $a1, 0xff      # ch = low byte of c
strchr_loop:
        lbu     $t0, 0($a0)         # *s
        beq     $t0, $t1, strchr_found  # match (also matches when ch==0)
        beqz    $t0, strchr_none    # end of string, no match
        addiu   $a0, $a0, 1         # s++
        j       strchr_loop
strchr_found:
        move    $v0, $a0
        jr      $ra
strchr_none:
        li      $v0, 0
        jr      $ra

#--------------------------------------------------------------
# memchr(s, c, n) — first byte equal to c in the first n bytes.
#
# Adapted from musl src/string/memchr.c.
# C: ch = (unsigned char)c;
#    while (n) { if (*p == ch) return p; p++; n--; }
#    return NULL;
#--------------------------------------------------------------
        .globl  memchr
memchr:
        andi    $t1, $a1, 0xff      # ch = low byte of c
memchr_loop:
        beqz    $a2, memchr_none    # count exhausted -> not found
        lbu     $t0, 0($a0)         # *p
        beq     $t0, $t1, memchr_found
        addiu   $a0, $a0, 1         # p++
        addiu   $a2, $a2, -1        # n--
        j       memchr_loop
memchr_found:
        move    $v0, $a0
        jr      $ra
memchr_none:
        li      $v0, 0
        jr      $ra

#--------------------------------------------------------------
# memcpy(dst, src, n) — copy n bytes; return dst.
#
# Adapted from musl src/string/memcpy.c.
# C: while (n) { *d++ = *s++; n--; } return dst;
#
# Undefined for overlapping regions — that is memmove's job.
#--------------------------------------------------------------
        .globl  memcpy
memcpy:
        move    $v0, $a0            # return value = dst
memcpy_loop:
        beqz    $a2, memcpy_done
        lbu     $t0, 0($a1)         # *src
        sb      $t0, 0($a0)         # *dst
        addiu   $a0, $a0, 1         # dst++
        addiu   $a1, $a1, 1         # src++
        addiu   $a2, $a2, -1        # n--
        j       memcpy_loop
memcpy_done:
        jr      $ra

#--------------------------------------------------------------
# memset(s, c, n) — write byte c into the first n bytes; return s.
#
# Adapted from musl src/string/memset.c.
# C: v = (unsigned char)c; while (n) { *p++ = v; n--; } return s;
#--------------------------------------------------------------
        .globl  memset
memset:
        move    $v0, $a0            # return value = s
        andi    $t1, $a1, 0xff      # v = low byte of c
memset_loop:
        beqz    $a2, memset_done
        sb      $t1, 0($a0)         # *p = v
        addiu   $a0, $a0, 1         # p++
        addiu   $a2, $a2, -1        # n--
        j       memset_loop
memset_done:
        jr      $ra

#--------------------------------------------------------------
# memmove(dst, src, n) — overlap-aware copy; return dst.
#
# Adapted from musl src/string/memmove.c.
# C: if (d < s) { while (n) { *d++ = *s++; n--; } }
#    else        { while (n) { n--; d[n] = s[n]; } }
#    return dst;
#
# The direction check is the whole lesson: dst below src copies
# forward (low->high); dst above src copies backward (high->low)
# so no source byte is clobbered before it is read.  bltu does an
# UNSIGNED pointer compare.
#--------------------------------------------------------------
        .globl  memmove
memmove:
        move    $v0, $a0            # return value = dst
        bltu    $a0, $a1, memmove_fwd  # dst < src -> forward is safe
memmove_bwd:                        # copy high -> low
        beqz    $a2, memmove_done
        addiu   $a2, $a2, -1        # n--
        addu    $t0, $a0, $a2       # &dst[n]
        addu    $t1, $a1, $a2       # &src[n]
        lbu     $t2, 0($t1)         # src[n]
        sb      $t2, 0($t0)         # dst[n] = src[n]
        j       memmove_bwd
memmove_fwd:                        # copy low -> high
        beqz    $a2, memmove_done
        lbu     $t2, 0($a1)         # *src
        sb      $t2, 0($a0)         # *dst
        addiu   $a0, $a0, 1         # dst++
        addiu   $a1, $a1, 1         # src++
        addiu   $a2, $a2, -1        # n--
        j       memmove_fwd
memmove_done:
        jr      $ra
