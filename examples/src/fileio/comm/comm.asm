# (MIT license abbreviated)


# C source — see comm.c
#
#     Three-column line-by-line diff of two sorted files.


# Invocation:
#   spimulator -f comm.asm A.sorted B.sorted
#
#          Each input must be sorted; comm compares line-by-line.
#          Try: seq 1 5 > /tmp/a; seq 3 7 > /tmp/b; comm /tmp/a /tmp/b


#PURPOSE:  Open TWO files for reading.  Maintain a current-line buffer
#          for each; read both, compare, emit, advance whichever is
#          lower.
#
#          STACK-VISIBLE STYLE: main's C locals — argv, the two file
#          descriptors fa/fb, and the two current-line lengths la/lb —
#          live in stack slots; a scratch register holds only a working
#          COPY.  main makes `jal read_line`/`jal line_cmp`, so it
#          saves $ra.  The line buffers line_a/line_b (and read_line's
#          one-byte scratch) are `static` in C, so they stay in .data.
#          read_line and line_cmp are leaf helpers (no jal), kept
#          register-based like atoi/streq.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
#          No #CANONICAL block: the output columns are prefixed with
#          tabs ('\t', '\t\t'), significant leading whitespace a
#          comment-based anchor cannot carry robustly.  Gated by
#          before/after output equivalence instead.
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr   | register   | C type    | bytes | role / notes
#  ------------+---------------+------------+-----------+-------+---------------------------
#  argc        | —             | $a0        | int       |    4  | entry only; tested (== 3), then dead
#  argv        | -12($fp)      | $t0 (copy) | char **   |    4  | homed; argv[1]/argv[2] opened
#  fa          | -16($fp)      | $t0 (copy) | int       |    4  | file A descriptor
#  fb          | -20($fp)      | $t0 (copy) | int       |    4  | file B descriptor
#  la          | -24($fp)      | $t0 (copy) | int       |    4  | current line length of A (-1 at EOF)
#  lb          | -28($fp)      | $t1 (copy) | int       |    4  | current line length of B (-1 at EOF)
#  line_a/b    | line* (.data) | —          | char[1024]| 1024  | static current-line buffers (-> .data)
#  (saved $ra) | -4($fp)       | —          | code addr |    4  | jal read_line / jal line_cmp
#  (saved $fp) | -8($fp)       | —          | frame ptr |    4  | caller's frame pointer
#
# ------------------------------------------------------------------
#  SNAPSHOT (input: comm A B with A=apple/banana/cherry,
#            B=banana/cherry/date; at `loop` on the first pass)
#
#        main's stack frame             .data
#   +----------------------------+     +---------------------------+
#   | -4  saved $ra              |     | line_a: "apple"           |
#   | -8  saved $fp              |     | line_b: "banana"          |
#   | -12 argv = &av[0]          |     +---------------------------+
#   | -16 fa   = 3               |
#   | -20 fb   = 4               |
#   | -24 la   = 5 ("apple")     |
#   | -28 lb   = 6 ("banana")    |
#   +----------------------------+
#         $sp = $fp - 28
#
#   line_cmp("apple","banana") < 0, so "apple" prints (only-A) and A
#   advances.  Final output: apple / \t\tbanana / \t\tcherry / \tdate.

        .data
usageMsg:   .asciiz "usage: comm A B\n"
errMsg:     .asciiz "comm: cannot open "
nlMsg:      .asciiz "\n"
tab1:       .asciiz "\t"
tab2:       .asciiz "\t\t"
oneByte:    .space 1
lineA:      .space 1024
lineB:      .space 1024

        .text
        .globl main
main:
        # ---- prologue: build main's 28-byte stack frame ------------
        addiu $sp, $sp, -28
        sw $ra, 24($sp)              # save $ra          -> -4($fp)
        sw $fp, 20($sp)              # save caller's $fp -> -8($fp)
        addiu $fp, $sp, 28

        # if (argc != 3) usage;   (argc arrives in $a0)
        li $t0, 3
        bne $a0, $t0, usage
        sw $a1, -12($fp)             # argv

        # fa = open(argv[1], O_RDONLY);
        lw $t0, -12($fp)
        lw $a0, 4($t0)
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_a_failed
        sw $v0, -16($fp)             # fa

        # fb = open(argv[2], O_RDONLY);
        lw $t0, -12($fp)
        lw $a0, 8($t0)
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_b_failed
        sw $v0, -20($fp)             # fb

        # la = read_line(fa, line_a);
        lw $a0, -16($fp)
        la $a1, lineA
        jal read_line
        sw $v0, -24($fp)             # la

        # lb = read_line(fb, line_b);
        lw $a0, -20($fp)
        la $a1, lineB
        jal read_line
        sw $v0, -28($fp)             # lb

loop:
        # while (la >= 0 || lb >= 0)
        lw $t0, -24($fp)             # la
        lw $t1, -28($fp)             # lb
        bltz $t0, check_b_only       # la < 0
        bltz $t1, only_a             # la >= 0, lb < 0 -> only in A

        # both valid: compare
        la $a0, lineA
        la $a1, lineB
        jal line_cmp
        bltz $v0, only_a
        bgtz $v0, only_b

        # equal: in both ("\t\t<line>")
        la $a0, tab2
        li $v0, 4
        syscall
        la $a0, lineA
        li $v0, 4
        syscall
        la $a0, nlMsg
        li $v0, 4
        syscall
        # advance both
        lw $a0, -16($fp)
        la $a1, lineA
        jal read_line
        sw $v0, -24($fp)             # la
        lw $a0, -20($fp)
        la $a1, lineB
        jal read_line
        sw $v0, -28($fp)             # lb
        j loop

only_a:
        # only in A ("<line>")
        la $a0, lineA
        li $v0, 4
        syscall
        la $a0, nlMsg
        li $v0, 4
        syscall
        lw $a0, -16($fp)
        la $a1, lineA
        jal read_line
        sw $v0, -24($fp)             # la
        j loop

only_b:
        # only in B ("\t<line>")
        la $a0, tab1
        li $v0, 4
        syscall
        la $a0, lineB
        li $v0, 4
        syscall
        la $a0, nlMsg
        li $v0, 4
        syscall
        lw $a0, -20($fp)
        la $a1, lineB
        jal read_line
        sw $v0, -28($fp)             # lb
        j loop

check_b_only:
        # la < 0; if lb < 0 too, both files are done
        lw $t1, -28($fp)             # lb
        bltz $t1, exit_ok
        j only_b

exit_ok:
        li $v0, 16
        lw $a0, -16($fp)             # close fa
        syscall
        li $v0, 16
        lw $a0, -20($fp)             # close fb
        syscall
        # ---- epilogue (success, exit 0) ----------------------------
        li $v0, 0
        lw $ra, -4($fp)
        lw $fp, -8($fp)
        addiu $sp, $sp, 28
        jr $ra

open_a_failed:
        li $v0, 4
        la $a0, errMsg
        syscall
        lw $t0, -12($fp)
        lw $a0, 4($t0)
        li $v0, 4
        syscall
        la $a0, nlMsg
        li $v0, 4
        syscall
        li $a0, 1
        li $v0, 17
        syscall

open_b_failed:
        # close fa first
        li $v0, 16
        lw $a0, -16($fp)
        syscall
        li $v0, 4
        la $a0, errMsg
        syscall
        lw $t0, -12($fp)
        lw $a0, 8($t0)
        li $v0, 4
        syscall
        la $a0, nlMsg
        li $v0, 4
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


# ---------- int read_line(int fd, char *buf) ------------------------
# Leaf helper (no jal): read one line from fd into buf, NUL-terminate,
# strip the trailing '\n'.  Returns length, or -1 at EOF before any
# byte.  fd/buf/len stay in scratch registers; the 1-byte read landing
# pad `oneByte` is static (.data).
#
#  SYMBOL TABLE (routine: read_line — leaf helper, register-based)
#
#  C variable  | memory addr     | register | C type | bytes | role / notes
#  ------------+-----------------+----------+--------+-------+---------------------------
#  (arg) fd    | —               | $t0      | int    |   4   | file descriptor to read
#  (arg) buf   | —               | $t1      | char * |   4   | destination line buffer
#  len         | —               | $t2      | int    |   4   | bytes stored so far
#  c           | —               | $t3      | char   |   4   | the byte just read
#  oneByte     | oneByte (.data) | —        | char   |   1   | static 1-byte read landing pad
read_line:
        move $t0, $a0                # fd
        move $t1, $a1                # buf base
        li $t2, 0                    # len

rl_loop:
        # n = read(fd, &oneByte, 1)
        li $v0, 14
        move $a0, $t0
        la $a1, oneByte
        li $a2, 1
        syscall

        blez $v0, rl_eof

        lb $t3, oneByte
        beq $t3, '\n', rl_done

        # if (len < 1023) buf[len++] = c
        li $t4, 1023
        bge $t2, $t4, rl_loop
        add $t4, $t1, $t2
        sb $t3, ($t4)
        addi $t2, $t2, 1
        j rl_loop

rl_eof:
        beqz $t2, rl_eof_empty
        # final line without newline
rl_done:
        add $t4, $t1, $t2
        sb $0, ($t4)                 # NUL-terminate
        move $v0, $t2
        jr $ra

rl_eof_empty:
        li $v0, -1
        jr $ra


# ---------- int line_cmp(const char *a, const char *b) --------------
# Leaf helper (no jal): byte-compare two NUL-terminated strings; return
# negative/zero/positive like strcmp.  Pointers walked in place.
#
#  SYMBOL TABLE (routine: line_cmp — leaf helper, register-based)
#
#  C variable   | memory addr | register | C type       | bytes | role / notes
#  -------------+-------------+----------+--------------+-------+------------------------------
#  (arg) a      | —           | $a0      | const char * |   4   | walked in place over string a
#  (arg) b      | —           | $a1      | const char * |   4   | walked in place over string b
#  *a / *b      | —           | $t0/$t1  | char         |   4   | the two bytes this pass
#  return value | —           | $v0      | int          |   4   | *a - *b at first difference, else 0
line_cmp:
        lb $t0, ($a0)
        lb $t1, ($a1)
        beq $t0, $t1, lc_advance
        sub $v0, $t0, $t1
        jr $ra
lc_advance:
        beq $t0, $0, lc_equal
        addi $a0, $a0, 1
        addi $a1, $a1, 1
        j line_cmp
lc_equal:
        li $v0, 0
        jr $ra
