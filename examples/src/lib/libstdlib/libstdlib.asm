#==============================================================
# libstdlib.asm — MIPS implementations of the libstdlib functions.
#
# Adapted from musl libc (src/stdlib/, src/exit/).
#   musl:    https://musl.libc.org/
#   License: MIT — see LICENSE-musl in this directory.
#
# Calling-convention contract (same as libctype's):
#   $a0..$a3 — inputs
#   $v0      — return value
#   $s*      — preserved (callee-save)
#   $t*      — clobbered freely
#   $ra      — preserved by the library
#
# Functions currently implemented: atoi, absolute, labsolute,
# _Exit.
#
# Note on naming: `absolute` and `labsolute` correspond to the
# C library's `abs(int)` and `labs(long)`.  They're renamed
# here because `abs` is a reserved MIPS pseudoinstruction in
# spim (`abs $rd, $rs`) and would collide with a label of the
# same name.  Keeping the C and asm sides on matching names
# avoids interop friction; the longer names also self-document.
#
# Load order: libctype.asm must be loaded alongside this file
# (or before it).  Each spim invocation looks like
#   spimulator -f libctype.asm -f libstdlib.asm -f your-prog.asm
# — the cross-file forward references (atoi -> isspace, atoi ->
# isdigit) resolve at parse time because spim's symbol table
# accumulates across `-f` files.
#==============================================================

        .text

#--------------------------------------------------------------
# atoi(s) — parse a leading sign + decimal digits from s.
# Returns int.  Stops at the first non-digit.  Whitespace skipped.
#
# Adapted from musl src/stdlib/atoi.c.
# C:
#   int atoi(const char *s) {
#       int n=0, neg=0;
#       while (isspace(*s)) s++;
#       switch (*s) { case '-': neg=1; case '+': s++; }
#       while (isdigit(*s))
#           n = 10*n - (*s++ - '0');
#       return neg ? n : -n;
#   }
#
# This is the first NON-leaf function in the library tier — it
# calls into libctype (isspace, isdigit) in a loop, so it must
# save $ra across each `jal`.  It also keeps state across calls
# (the string pointer, the accumulator, the sign flag), which
# can't live in $t* (caller-save — clobbered across `jal`), so
# we use $s0..$s2 and save them at entry.
#
# Frame layout (20 bytes):
#   0($sp)  saved $ra
#   4($sp)  saved $s0   (string pointer, s)
#   8($sp)  saved $s1   (accumulator n, kept NEGATIVE)
#   12($sp) saved $s2   (sign flag — 0 = positive, 1 = negative)
#   16($sp) (unused; rounds frame to 5 words for alignment habit)
#--------------------------------------------------------------
        .globl  atoi
atoi:
        # ── Prologue: save $ra and the $s regs we'll use ──
        addiu   $sp, $sp, -20
        sw      $ra, 0($sp)
        sw      $s0, 4($sp)
        sw      $s1, 8($sp)
        sw      $s2, 12($sp)

        # Initialize: $s0 = s, $s1 = 0 (n), $s2 = 0 (neg)
        move    $s0, $a0
        li      $s1, 0
        li      $s2, 0

        # ── Skip whitespace: while (isspace(*s)) s++; ──
atoi_ws:
        lbu     $a0, 0($s0)         # load *s as unsigned byte
        jal     isspace
        beqz    $v0, atoi_ws_done   # not whitespace -> exit loop
        addiu   $s0, $s0, 1         # s++
        j       atoi_ws
atoi_ws_done:

        # ── Optional sign: switch (*s) { case '-': neg=1; case '+': s++; } ──
        lbu     $t0, 0($s0)
        li      $t1, '-'
        beq     $t0, $t1, atoi_sign_neg
        li      $t1, '+'
        beq     $t0, $t1, atoi_sign_pos
        j       atoi_digits         # neither sign char — fall straight into digits
atoi_sign_neg:
        li      $s2, 1              # neg = 1
atoi_sign_pos:
        addiu   $s0, $s0, 1         # s++ (consume the sign char in either case)

        # ── Accumulate digits: while (isdigit(*s)) n = 10*n - (*s++ - '0'); ──
        # n is kept NEGATIVE throughout so we can represent INT_MIN
        # exactly (a positive accumulator would overflow on it).
atoi_digits:
        lbu     $a0, 0($s0)
        jal     isdigit
        beqz    $v0, atoi_digits_done
        lbu     $t0, 0($s0)
        addiu   $t0, $t0, -48       # $t0 = *s - '0'  ('0' = 48)
        # 10*n via shift+add: 10n = 8n + 2n = (n<<3) + (n<<1)
        sll     $t1, $s1, 1         # $t1 = 2*n
        sll     $t2, $s1, 3         # $t2 = 8*n
        addu    $t1, $t1, $t2       # $t1 = 10*n
        subu    $s1, $t1, $t0       # n = 10*n - digit  (n stays negative)
        addiu   $s0, $s0, 1
        j       atoi_digits
atoi_digits_done:

        # ── return neg ? n : -n; ──
        beqz    $s2, atoi_negate
        move    $v0, $s1            # negative input — return n as-is
        j       atoi_done
atoi_negate:
        subu    $v0, $zero, $s1     # positive input — return -n
atoi_done:

        # ── Epilogue: restore $ra and $s regs, free frame, return ──
        lw      $ra, 0($sp)
        lw      $s0, 4($sp)
        lw      $s1, 8($sp)
        lw      $s2, 12($sp)
        addiu   $sp, $sp, 20
        jr      $ra

#--------------------------------------------------------------
# absolute(x)  — absolute value of an int.
# labsolute(x) — absolute value of a long.
#
# Library names for what the C standard library calls abs() and
# labs().  See the header note at the top of this file for the
# rename rationale.
#
# Adapted from musl src/stdlib/abs.c and src/stdlib/labs.c.
# C: return x > 0 ? x : -x;
#
# On MIPS32, `long` is 32 bits (same as int), so absolute(int)
# and labsolute(long) are byte-identical.  Single shared body —
# labsolute falls straight through into absolute.
#
# Leaf function: no $ra save, no frame, no $s* touched.
#
# INT_MIN edge case (-2147483648): both C and asm return
# INT_MIN unchanged because `-(-2^31)` overflows to itself in
# two's complement.  Standard C calls this UB; spim/MIPS are
# two's complement so the behavior is predictable.
#
# --- Sidebar: branchless alternative ---
# A common compiler trick for the same result without a branch:
#     sra   $t0, $a0, 31        # $t0 = sign-extended mask (0 or -1)
#     xor   $t1, $a0, $t0       # one's complement of $a0 if neg
#     subu  $v0, $t1, $t0       # adds 1 (via -(-1)) if neg
#     jr    $ra
# Pre-pipelined CPUs preferred this to avoid a branch stall;
# modern CPUs with good branch prediction don't care.  spim has
# no microarchitecture model, so both versions take the same
# "time" — the branchless variant is here for educational
# contrast only.
#--------------------------------------------------------------
        .globl  labsolute
labsolute:                          # labsolute falls through to absolute
        .globl  absolute
absolute:
        bgez    $a0, absolute_pos   # $a0 >= 0 -> already non-negative
        subu    $v0, $zero, $a0     # $v0 = -$a0
        jr      $ra
absolute_pos:
        move    $v0, $a0
        jr      $ra

#--------------------------------------------------------------
# _Exit(status) — terminate the program immediately with the
# given exit status visible to the parent process (the shell's
# `$?`).
#
# Adapted from musl src/exit/_Exit.c (which on Linux is a
# one-line syscall wrapper).
#
# Uses spim's syscall 17 (exit2), which honors $a0 as the
# status.  spim ALSO has syscall 10 ("exit") but that one
# ignores $a0 and always returns 0 to the host shell — broken
# for any pipeline that wants to see a meaningful status code.
# See /spimulator/tasks/unix-process-conformance.md for the
# fix that made __start translate main's $v0 through syscall
# 17 automatically.  Library functions should always use
# syscall 17 directly.
#
# Calling-convention note: _Exit never returns, so we don't
# need to preserve $ra or any $s* — just set up the syscall
# and fire.  The simplest possible library function.
#--------------------------------------------------------------
        .globl  _Exit
_Exit:
        li      $v0, 17             # syscall: exit2 ($a0 = status)
        syscall
        # No `jr $ra` — control transfers to the kernel and
        # never comes back.  Falling through here would be a
        # bug; spim would execute whatever bytes happen to
        # follow.  In practice the syscall itself stops
        # execution before that matters.
