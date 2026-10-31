# Copyright (c) 2021-2026 William Emerison Six
#
# (license preamble, abbreviated for brevity in the curriculum's
# Part 7 demos; same MIT terms as elsewhere)


# C source — see touch.c


# Invocation:
#   spimulator -f touch.asm /tmp/newfile


#PURPOSE:  touch FILE.  Creates an empty file via
#          open(O_WRONLY | O_CREAT, 0644) and closes it immediately.
#          Flag value 65 = 1 | 0x40 = O_WRONLY | O_CREAT.  Mode 420 = 0644.
#
#          STACK-VISIBLE STYLE: the C locals — the path (argv[1], kept
#          for the error message) and the file descriptor fd — live in
#          stack slots.  This routine makes NO `jal` (only syscalls,
#          which preserve $ra), so it saves $fp but not $ra.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  argc        | —           | $a0        | int       |   4   | entry only; tested (== 2), then dead
#  path        | -8($fp)     | $t0 (copy) | char *    |   4   | argv[1]; the file to create
#  fd          | -12($fp)    | $t0 (copy) | int       |   4   | open() result; closed immediately
#  (saved $fp) | -4($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
# ------------------------------------------------------------------
#  SNAPSHOT (input: touch newfile; just after the open syscall
#            returned a valid fd, about to close it)
#
#        main's stack frame             scratch
#   +----------------------------+     +----------------------+
#   | -4($fp)  saved $fp         |     | $t0 = fd (e.g. 3)
#   | -8($fp)  path  = &"newfile"|     +----------------------+
#   | -12($fp) fd    = 3         | <-- | $t0
#   +----------------------------+
#         $sp = $fp - 12
#
#   Closes fd and exits 0.  On open failure it prints
#   "touch: cannot create <path>" and exits 1.
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  (Uses a path whose parent directory does not exist, so open() fails
#   and NO file is created when the checker runs; exercises the error
#   path.  The success path writes no stdout, only a file side effect.)
#  args: /no_such_dir_xyz/f
#  stdout:
#  | touch: cannot create /no_such_dir_xyz/f

        .data
usageMsg:   .asciiz "usage: touch FILE\n"
errMsg:     .asciiz "touch: cannot create "
nlMsg:      .asciiz "\n"

        .text
        .globl main
main:
        # ---- prologue: build a 12-byte frame (no $ra: no `jal`) ----
        addiu $sp, $sp, -12
        sw $fp, 8($sp)               # save caller's $fp -> -4($fp)
        addiu $fp, $sp, 12

        # if (argc != 2) usage;   (argc arrives in $a0)
        li $t0, 2
        bne $a0, $t0, usage

        # path = argv[1];         (argv arrives in $a1)
        lw $t0, 4($a1)
        sw $t0, -8($fp)              # path = argv[1]

        # fd = open(path, O_WRONLY | O_CREAT, 0644);
        lw $a0, -8($fp)              # path
        li $v0, 13                   # open
        li $a1, 65                   # O_WRONLY | O_CREAT
        li $a2, 420                  # 0644
        syscall
        sw $v0, -12($fp)             # fd
        lw $t0, -12($fp)
        bltz $t0, open_failed

        # close(fd);
        lw $a0, -12($fp)             # fd
        li $v0, 16
        syscall

        # ---- epilogue (success, exit 0) ----------------------------
        li $v0, 0
        lw $fp, -4($fp)
        addiu $sp, $sp, 12
        jr $ra

open_failed:
        li $v0, 4
        la $a0, errMsg
        syscall
        lw $a0, -8($fp)              # path
        li $v0, 4
        syscall
        li $v0, 4
        la $a0, nlMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall
