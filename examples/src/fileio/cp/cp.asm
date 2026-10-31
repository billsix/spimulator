# (MIT license abbreviated)


# C source — see cp.c


# Invocation:
#   spimulator -f cp.asm /etc/motd /tmp/copy
#
#          Two filename args.  DST is created if missing,
#          truncated if it exists.


#PURPOSE:  Copy SRC to DST.  Block I/O.  First demo to call `open`
#          with O_CREAT | O_WRONLY | O_TRUNC (flags = 577).
#
#          STACK-VISIBLE STYLE: the C locals — argv (kept to fetch the
#          two filenames and for error messages), the two file
#          descriptors src/dst, and the per-chunk byte count n — all
#          live in stack slots; a scratch register holds only a working
#          COPY.  The copy buffer `buf` is `static` in C, so it stays
#          in .data, not on the frame.  This routine makes NO `jal`
#          (only syscalls, which preserve $ra), so it saves $fp but
#          not $ra.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type     | bytes | role / notes
#  ------------+-------------+------------+------------+-------+---------------------------
#  argc        | —           | $a0        | int        |    4  | entry only; tested (== 3), then dead
#  argv        | -8($fp)     | $t0 (copy) | char **    |    4  | homed on entry; argv[1]/argv[2] fetched
#  src         | -12($fp)    | $t0 (copy) | int        |    4  | source fd (O_RDONLY)
#  dst         | -16($fp)    | $t0 (copy) | int        |    4  | dest fd (O_WRONLY|O_CREAT|O_TRUNC)
#  n           | -20($fp)    | $v0 (copy) | int        |    4  | bytes read this chunk
#  buf         | buf (.data) | —          | char[4096] | 4096  | static copy buffer (C `static` -> .data)
#  (saved $fp) | -4($fp)     | —          | frame ptr  |    4  | caller's frame pointer
#
# ------------------------------------------------------------------
#  SNAPSHOT (input: cp src.txt dst.txt; in read_loop, just read n = 24
#            bytes from src into buf, about to write them to dst)
#
#        main's stack frame             scratch
#   +----------------------------+     +----------------------+
#   | -4($fp)  saved $fp         |     | $t0 = 4 (dst fd)
#   | -8($fp)  argv   = &av[0]   |     | $v0 = 24 (n)
#   | -12($fp) src    = 3        |     +----------------------+
#   | -16($fp) dst    = 4        |
#   | -20($fp) n      = 24       |
#   +----------------------------+
#         $sp = $fp - 20
#
#   Loops read(src)->write(dst) until EOF, closes both, exits 0.
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  (Uses a nonexistent SRC so open() fails BEFORE any DST is created,
#   so the checker creates no files; exercises the src-error path.)
#  args: /no_such_src_xyz /no_such_dst
#  stdout:
#  | cp: cannot open /no_such_src_xyz

        .data
usageMsg:   .asciiz "usage: cp SRC DST\n"
openSrcErr: .asciiz "cp: cannot open "
openDstErr: .asciiz "cp: cannot create "
nlMsg:      .asciiz "\n"
buf:        .space 4096

        .text
        .globl main
main:
        # ---- prologue: build a 20-byte frame (no $ra: no `jal`) ----
        addiu $sp, $sp, -20
        sw $fp, 16($sp)              # save caller's $fp -> -4($fp)
        addiu $fp, $sp, 20

        # if (argc != 3) usage;   (argc arrives in $a0)
        li $t0, 3
        bne $a0, $t0, usage

        # argv is in $a1; home it.
        sw $a1, -8($fp)              # argv

        # src = open(argv[1], O_RDONLY);
        lw $t0, -8($fp)              # argv
        lw $a0, 4($t0)               # argv[1]
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, src_failed
        sw $v0, -12($fp)             # src fd

        # dst = open(argv[2], O_WRONLY|O_CREAT|O_TRUNC, 0644);
        lw $t0, -8($fp)              # argv
        lw $a0, 8($t0)               # argv[2]
        li $v0, 13
        li $a1, 577                  # 1 | 0x40 | 0x200
        li $a2, 420                  # 0644
        syscall
        bltz $v0, dst_failed
        sw $v0, -16($fp)             # dst fd

read_loop:
        # n = read(src, buf, 4096);
        li $v0, 14
        lw $a0, -12($fp)             # src
        la $a1, buf
        li $a2, 4096
        syscall
        blez $v0, close_and_exit
        sw $v0, -20($fp)             # n

        # write(dst, buf, n);
        li $v0, 15
        lw $a0, -16($fp)             # dst
        la $a1, buf
        lw $a2, -20($fp)             # n
        syscall
        j read_loop

close_and_exit:
        # close(src); close(dst);
        li $v0, 16
        lw $a0, -12($fp)             # src
        syscall
        li $v0, 16
        lw $a0, -16($fp)             # dst
        syscall
        # ---- epilogue (success, exit 0) ----------------------------
        li $v0, 0
        lw $fp, -4($fp)
        addiu $sp, $sp, 20
        jr $ra

src_failed:
        li $v0, 4
        la $a0, openSrcErr
        syscall
        lw $t0, -8($fp)              # argv
        lw $a0, 4($t0)               # argv[1]
        li $v0, 4
        syscall
        li $v0, 4
        la $a0, nlMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall

dst_failed:
        # close src first
        li $v0, 16
        lw $a0, -12($fp)             # src
        syscall
        li $v0, 4
        la $a0, openDstErr
        syscall
        lw $t0, -8($fp)              # argv
        lw $a0, 8($t0)               # argv[2]
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
