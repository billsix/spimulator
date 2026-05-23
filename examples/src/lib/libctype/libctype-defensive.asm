#==============================================================
# libctype-defensive.asm — a deliberately-over-cautious version
# of libctype.asm.
#
# WHY THIS FILE EXISTS:
#
# Every function in this file is a leaf — it doesn't `jal`
# anything and it only touches caller-save registers ($t0, $v0,
# $a0).  So the lean libctype.asm correctly omits all the
# save/restore boilerplate: each function fits in 3-7 lines.
#
# This "defensive" variant shows what the SAME functions would
# look like if they followed the full callee-save discipline
# anyway — saving $ra, $fp, $gp, and $s0..$s7 at entry and
# restoring them at exit.  The bodies are byte-identical to
# the lean version; only the prologue and epilogue change.
#
# Use this file as a contrast: read libctype.asm first, then
# this one.  Each function here is ~25 lines vs ~5 in the
# lean version — the difference is the cost of NOT being a
# leaf.  Once you internalize what's at stake, the lean
# version's omissions stop looking like cheating and start
# looking like the right answer for leaves.
#
# Adapted from musl libc src/ctype/ (same algorithms as
# libctype.asm).
#   musl:    https://musl.libc.org/
#   License: MIT — see LICENSE-musl in this directory.
#
# Calling-convention contract (same as libctype.asm):
#   $a0  — input byte
#   $v0  — return value
#   $s*, $fp, $gp, $ra — preserved (this file proves it
#                          explicitly via save/restore)
#   $t*  — clobbered freely
#
# Standard frame layout used by every function in this file
# (44 bytes total):
#   0($sp)  saved $ra
#   4($sp)  saved $fp
#   8($sp)  saved $gp
#   12($sp) saved $s0
#   16($sp) saved $s1
#   20($sp) saved $s2
#   24($sp) saved $s3
#   28($sp) saved $s4
#   32($sp) saved $s5
#   36($sp) saved $s6
#   40($sp) saved $s7
#==============================================================

        .text

#--------------------------------------------------------------
# isdigit_def(c) — defensive version of isdigit.
# C: return (unsigned)c - '0' < 10;
#--------------------------------------------------------------
        .globl  isdigit_def
isdigit_def:
        # ── Prologue: allocate frame and save 11 callee-save regs ──
        addiu   $sp, $sp, -44
        sw      $ra,  0($sp)
        sw      $fp,  4($sp)
        sw      $gp,  8($sp)
        sw      $s0, 12($sp)
        sw      $s1, 16($sp)
        sw      $s2, 20($sp)
        sw      $s3, 24($sp)
        sw      $s4, 28($sp)
        sw      $s5, 32($sp)
        sw      $s6, 36($sp)
        sw      $s7, 40($sp)

        # ── Body (identical to lean isdigit) ──
        addiu   $v0, $a0, -48
        sltiu   $v0, $v0, 10

        # ── Epilogue: restore 11 callee-save regs, free frame, return ──
        lw      $ra,  0($sp)
        lw      $fp,  4($sp)
        lw      $gp,  8($sp)
        lw      $s0, 12($sp)
        lw      $s1, 16($sp)
        lw      $s2, 20($sp)
        lw      $s3, 24($sp)
        lw      $s4, 28($sp)
        lw      $s5, 32($sp)
        lw      $s6, 36($sp)
        lw      $s7, 40($sp)
        addiu   $sp, $sp, 44
        jr      $ra

#--------------------------------------------------------------
# isalpha_def(c) — defensive version of isalpha.
# C: return ((unsigned)c | 32) - 'a' < 26;
#--------------------------------------------------------------
        .globl  isalpha_def
isalpha_def:
        addiu   $sp, $sp, -44
        sw      $ra,  0($sp)
        sw      $fp,  4($sp)
        sw      $gp,  8($sp)
        sw      $s0, 12($sp)
        sw      $s1, 16($sp)
        sw      $s2, 20($sp)
        sw      $s3, 24($sp)
        sw      $s4, 28($sp)
        sw      $s5, 32($sp)
        sw      $s6, 36($sp)
        sw      $s7, 40($sp)

        ori     $t0, $a0, 32
        addiu   $t0, $t0, -97
        sltiu   $v0, $t0, 26

        lw      $ra,  0($sp)
        lw      $fp,  4($sp)
        lw      $gp,  8($sp)
        lw      $s0, 12($sp)
        lw      $s1, 16($sp)
        lw      $s2, 20($sp)
        lw      $s3, 24($sp)
        lw      $s4, 28($sp)
        lw      $s5, 32($sp)
        lw      $s6, 36($sp)
        lw      $s7, 40($sp)
        addiu   $sp, $sp, 44
        jr      $ra

#--------------------------------------------------------------
# isalnum_def(c) — defensive version of isalnum.
# C: return isalpha(c) || isdigit(c);
# Inlines both checks (same as lean version) to stay a leaf.
#--------------------------------------------------------------
        .globl  isalnum_def
isalnum_def:
        addiu   $sp, $sp, -44
        sw      $ra,  0($sp)
        sw      $fp,  4($sp)
        sw      $gp,  8($sp)
        sw      $s0, 12($sp)
        sw      $s1, 16($sp)
        sw      $s2, 20($sp)
        sw      $s3, 24($sp)
        sw      $s4, 28($sp)
        sw      $s5, 32($sp)
        sw      $s6, 36($sp)
        sw      $s7, 40($sp)

        ori     $t0, $a0, 32
        addiu   $t0, $t0, -97
        sltiu   $t0, $t0, 26
        bnez    $t0, isalnum_def_yes
        addiu   $t0, $a0, -48
        sltiu   $v0, $t0, 10
        j       isalnum_def_done
isalnum_def_yes:
        li      $v0, 1
isalnum_def_done:

        lw      $ra,  0($sp)
        lw      $fp,  4($sp)
        lw      $gp,  8($sp)
        lw      $s0, 12($sp)
        lw      $s1, 16($sp)
        lw      $s2, 20($sp)
        lw      $s3, 24($sp)
        lw      $s4, 28($sp)
        lw      $s5, 32($sp)
        lw      $s6, 36($sp)
        lw      $s7, 40($sp)
        addiu   $sp, $sp, 44
        jr      $ra

#--------------------------------------------------------------
# isupper_def(c) — defensive version of isupper.
# C: return (unsigned)c - 'A' < 26;
#--------------------------------------------------------------
        .globl  isupper_def
isupper_def:
        addiu   $sp, $sp, -44
        sw      $ra,  0($sp)
        sw      $fp,  4($sp)
        sw      $gp,  8($sp)
        sw      $s0, 12($sp)
        sw      $s1, 16($sp)
        sw      $s2, 20($sp)
        sw      $s3, 24($sp)
        sw      $s4, 28($sp)
        sw      $s5, 32($sp)
        sw      $s6, 36($sp)
        sw      $s7, 40($sp)

        addiu   $v0, $a0, -65
        sltiu   $v0, $v0, 26

        lw      $ra,  0($sp)
        lw      $fp,  4($sp)
        lw      $gp,  8($sp)
        lw      $s0, 12($sp)
        lw      $s1, 16($sp)
        lw      $s2, 20($sp)
        lw      $s3, 24($sp)
        lw      $s4, 28($sp)
        lw      $s5, 32($sp)
        lw      $s6, 36($sp)
        lw      $s7, 40($sp)
        addiu   $sp, $sp, 44
        jr      $ra

#--------------------------------------------------------------
# islower_def(c) — defensive version of islower.
# C: return (unsigned)c - 'a' < 26;
#--------------------------------------------------------------
        .globl  islower_def
islower_def:
        addiu   $sp, $sp, -44
        sw      $ra,  0($sp)
        sw      $fp,  4($sp)
        sw      $gp,  8($sp)
        sw      $s0, 12($sp)
        sw      $s1, 16($sp)
        sw      $s2, 20($sp)
        sw      $s3, 24($sp)
        sw      $s4, 28($sp)
        sw      $s5, 32($sp)
        sw      $s6, 36($sp)
        sw      $s7, 40($sp)

        addiu   $v0, $a0, -97
        sltiu   $v0, $v0, 26

        lw      $ra,  0($sp)
        lw      $fp,  4($sp)
        lw      $gp,  8($sp)
        lw      $s0, 12($sp)
        lw      $s1, 16($sp)
        lw      $s2, 20($sp)
        lw      $s3, 24($sp)
        lw      $s4, 28($sp)
        lw      $s5, 32($sp)
        lw      $s6, 36($sp)
        lw      $s7, 40($sp)
        addiu   $sp, $sp, 44
        jr      $ra

#--------------------------------------------------------------
# isspace_def(c) — defensive version of isspace.
# C: return c == ' ' || (unsigned)c - '\t' < 5;
#--------------------------------------------------------------
        .globl  isspace_def
isspace_def:
        addiu   $sp, $sp, -44
        sw      $ra,  0($sp)
        sw      $fp,  4($sp)
        sw      $gp,  8($sp)
        sw      $s0, 12($sp)
        sw      $s1, 16($sp)
        sw      $s2, 20($sp)
        sw      $s3, 24($sp)
        sw      $s4, 28($sp)
        sw      $s5, 32($sp)
        sw      $s6, 36($sp)
        sw      $s7, 40($sp)

        li      $t0, 32
        beq     $a0, $t0, isspace_def_yes
        addiu   $t0, $a0, -9
        sltiu   $v0, $t0, 5
        j       isspace_def_done
isspace_def_yes:
        li      $v0, 1
isspace_def_done:

        lw      $ra,  0($sp)
        lw      $fp,  4($sp)
        lw      $gp,  8($sp)
        lw      $s0, 12($sp)
        lw      $s1, 16($sp)
        lw      $s2, 20($sp)
        lw      $s3, 24($sp)
        lw      $s4, 28($sp)
        lw      $s5, 32($sp)
        lw      $s6, 36($sp)
        lw      $s7, 40($sp)
        addiu   $sp, $sp, 44
        jr      $ra

#--------------------------------------------------------------
# toupper_def(c) — defensive version of toupper.
# C: if (islower(c)) return c & 0x5f; return c;
#--------------------------------------------------------------
        .globl  toupper_def
toupper_def:
        addiu   $sp, $sp, -44
        sw      $ra,  0($sp)
        sw      $fp,  4($sp)
        sw      $gp,  8($sp)
        sw      $s0, 12($sp)
        sw      $s1, 16($sp)
        sw      $s2, 20($sp)
        sw      $s3, 24($sp)
        sw      $s4, 28($sp)
        sw      $s5, 32($sp)
        sw      $s6, 36($sp)
        sw      $s7, 40($sp)

        addiu   $t0, $a0, -97
        sltiu   $t0, $t0, 26
        beqz    $t0, toupper_def_keep
        andi    $v0, $a0, 0x5f
        j       toupper_def_done
toupper_def_keep:
        move    $v0, $a0
toupper_def_done:

        lw      $ra,  0($sp)
        lw      $fp,  4($sp)
        lw      $gp,  8($sp)
        lw      $s0, 12($sp)
        lw      $s1, 16($sp)
        lw      $s2, 20($sp)
        lw      $s3, 24($sp)
        lw      $s4, 28($sp)
        lw      $s5, 32($sp)
        lw      $s6, 36($sp)
        lw      $s7, 40($sp)
        addiu   $sp, $sp, 44
        jr      $ra

#--------------------------------------------------------------
# tolower_def(c) — defensive version of tolower.
# C: if (isupper(c)) return c | 32; return c;
#--------------------------------------------------------------
        .globl  tolower_def
tolower_def:
        addiu   $sp, $sp, -44
        sw      $ra,  0($sp)
        sw      $fp,  4($sp)
        sw      $gp,  8($sp)
        sw      $s0, 12($sp)
        sw      $s1, 16($sp)
        sw      $s2, 20($sp)
        sw      $s3, 24($sp)
        sw      $s4, 28($sp)
        sw      $s5, 32($sp)
        sw      $s6, 36($sp)
        sw      $s7, 40($sp)

        addiu   $t0, $a0, -65
        sltiu   $t0, $t0, 26
        beqz    $t0, tolower_def_keep
        ori     $v0, $a0, 32
        j       tolower_def_done
tolower_def_keep:
        move    $v0, $a0
tolower_def_done:

        lw      $ra,  0($sp)
        lw      $fp,  4($sp)
        lw      $gp,  8($sp)
        lw      $s0, 12($sp)
        lw      $s1, 16($sp)
        lw      $s2, 20($sp)
        lw      $s3, 24($sp)
        lw      $s4, 28($sp)
        lw      $s5, 32($sp)
        lw      $s6, 36($sp)
        lw      $s7, 40($sp)
        addiu   $sp, $sp, 44
        jr      $ra
