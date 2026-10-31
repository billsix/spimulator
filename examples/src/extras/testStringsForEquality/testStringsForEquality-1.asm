# Copyright (c) 2021-2026 William Emerison Six
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.


# C source — see testStringsForEquality.c
# NOTE: this .asm is a simplified port of the C — it does NOT print the
# descriptive "strN compared to strN is " prefix, only the result digit
# and a newline.  The student can add it back as an exercise.


#PURPOSE:  Compare three string pairs with `int streq(const char*,
#          const char*)` and print each result on its own line.
#          streq returns 0 when equal and 1 when different — the
#          inverse of what the name suggests, matching the book demo.
#
#          STACK-VISIBLE STYLE: main makes three `jal streq` calls, so
#          it saves $ra on its frame.  streq's returns r1/r2/r3 are
#          transient (each printed immediately), so they stay in $v0.
#          streq itself is a leaf helper (register-passing: args in
#          $a0/$a1 walked in place, no frame), kept register-based.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
#          INTENTIONAL BUG preserved: main's C never initializes its
#          return value, yet reads it on the way out.  Here that is a
#          `return_value` slot at -12($fp) that is NEVER written; the
#          epilogue reads it into $v0.  On a fresh spim stack it
#          surfaces 0, but it is formally reading uninitialized memory
#          — the quirk this demo carries.  (It does not affect the
#          printed output, only the process exit status.)
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: main)
#
#  C variable    | memory addr     | register   | C type    | bytes | role / notes
#  --------------+-----------------+------------+-----------+-------+---------------------------
#  r1, r2, r3    | —               | $v0        | int       |   4   | streq results; each printed at once
#  str1,str2,str3| str* (.data)    | $a0/$a1    | char *    |   4   | addresses loaded before each jal
#  return_value  | -12($fp)        | $v0 (copy) | int       |   4   | NEVER written (intentional bug); read at exit
#  (saved $ra)   | -4($fp)         | —          | code addr |   4   | survives the three jal streq calls
#  (saved $fp)   | -8($fp)         | —          | frame ptr |   4   | caller's frame pointer
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: streq — leaf helper, register-passing)
#
#  C variable   | memory addr | register | C type       | bytes | role / notes
#  -------------+-------------+----------+--------------+-------+------------------------------
#  (arg) s1     | —           | $a0      | const char * |   4   | walked in place over string 1
#  (arg) s2     | —           | $a1      | const char * |   4   | walked in place over string 2
#  *s1          | —           | $t0      | char         |   4   | one byte of s1 this pass
#  *s2          | —           | $t1      | char         |   4   | one byte of s2 this pass
#  return value | —           | $v0      | int          |   4   | 0 if equal, 1 if different
#
# ------------------------------------------------------------------
#  SNAPSHOT (no args; just after the 1st `jal streq(str1, str2)`
#            returned — the strings differ, so $v0 = 1, about to print)
#
#        main's stack frame             scratch
#   +----------------------------+     +----------------------+
#   | -4($fp)  saved $ra         |     | $v0 = 1 (r1: differ)
#   | -8($fp)  saved $fp         |     +----------------------+
#   | -12($fp) return_value =  ? |   (never written)
#   +----------------------------+
#         $sp = $fp - 12
#
#   Prints 1 (str1 vs str2 differ), 0 (str1 vs str3 equal), 1
#   (str2 vs str3 differ), each on its own line.
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  args:
#  stdout:
#  | 1
#  | 0
#  | 1

        .data
nl:    .asciiz     "\n"
str1:    .asciiz     "str1"
str2:    .asciiz     "str2"
str3:    .asciiz     "str1"

        .text
        .globl main


# ---------- int streq(const char *s1, const char *s2) ----------
# Leaf helper: only $a0/$a1 (the args, walked in place) and $t0/$t1
# scratch; $sp and $fp are not touched, and $ra returns to the caller.
streq:

loopBegin:
        # while (*s1 == *s2) {       -- load one byte from each pointer
        lb $t0, ($a0)                # $t0 = *s1
        lb $t1, ($a1)                # $t1 = *s2
        bne $t0, $t1, loopEnd        # bytes differ -> jump to "return 1"

        #   if (*s1 == 0) return 0;  -- both bytes equal; if both NUL
        #                               the strings ended together, equal
        bne $t0, 0, incrementAndContinue
        li $v0, 0                    # equal -> return value = 0
        j streq_exit

incrementAndContinue:
        #   s1++;  s2++;
        addi $a0, $a0, 1             # advance the s1 pointer by one byte
        addi $a1, $a1, 1             # advance the s2 pointer by one byte
        j loopBegin                  # } back to the top of the loop

loopEnd:
        # return 1;                   -- bytes differed
        li $v0, 1

streq_exit:
        jr $ra                       # $ra holds the caller's return address


# ---------- _start (main) ----------
main:
        # ---- prologue: 12-byte frame (saved $ra, saved $fp, and the
        #      never-written return_value slot) ------------------------
        addiu $sp, $sp, -12
        sw $ra, 8($sp)               # save $ra          -> -4($fp)
        sw $fp, 4($sp)               # save caller's $fp -> -8($fp)
        addiu $fp, $sp, 12
        # NOTE: -12($fp) (return_value) is intentionally left unwritten.

        # int r1 = streq(str1, str2);
        la $a0, str1
        la $a1, str2
        jal streq                    # $v0 = result (1: "str1" != "str2")

        # print_int(r1); print_string("\n");
        move $a0, $v0
        li $v0, 1
        syscall
        li $v0, 4
        la $a0, nl
        syscall

        # int r2 = streq(str1, str3);
        la $a0, str1
        la $a1, str3
        jal streq                    # $v0 = 0 ("str1" == "str1")

        # print_int(r2); print_string("\n");
        move $a0, $v0
        li $v0, 1
        syscall
        li $v0, 4
        la $a0, nl
        syscall

        # int r3 = streq(str2, str3);
        la $a0, str2
        la $a1, str3
        jal streq                    # $v0 = 1 ("str2" != "str1")

        # print_int(r3); print_string("\n");
        move $a0, $v0
        li $v0, 1
        syscall
        li $v0, 4
        la $a0, nl
        syscall

        # ---- epilogue ----------------------------------------------
        # return return_value;  -- reads the UNINITIALIZED -12($fp) slot
        # (the intentional bug; surfaces 0 on a fresh spim stack).
        lw $v0, -12($fp)
        lw $ra, -4($fp)              # restore $ra
        lw $fp, -8($fp)              # restore caller's $fp
        addiu $sp, $sp, 12           # pop the frame
        jr $ra
