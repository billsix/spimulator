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
#
#     int str_eq(const char *s1, const char *s2) {
#       while (*s1 == *s2) {
#         if (*s1 == 0) return 0;
#         s1++;
#         s2++;
#       }
#       return 1;
#     }
#
#     __attribute__((noreturn)) void _start(void) {
#       const char *str1 = "str1";
#       const char *str2 = "str2";
#       const char *str3 = "str1";
#       print_string("str1 compared to str2 is ");
#       print_int(str_eq(str1, str2));
#       print_string("\n");
#       /* ... and same pattern for str1/str3 and str2/str3 */
#       os_exit(0);
#     }
#
# NOTE: this .asm is a simplified port of the C — it does NOT print the
# descriptive "strN compared to strN is " prefix, only the result digit
# and a newline.  The student can add it back as an exercise.


#PURPOSE:  Compare three string pairs with `int str_eq(const char*,
#          const char*)` and print each result on its own line.
#          Demonstrates a byte-at-a-time loop over a C string and
#          the idiomatic MIPS register-passing + `jal`/`jr $ra`
#          subroutine convention.
#
#STORAGE LAYOUT
#
#   Single-cell stack frame in main; streq needs no frame.
#
#         higher addresses
#           +-------------+
#$fp,$sp -> | saved $ra   |   0($fp)
#           +-------------+
#         lower addresses
#
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     saved $ra      0($fp)             the runtime's $ra, parked on the
#                                       stack so the three `jal streq`s
#                                       can't lose it; reloaded into $ra
#                                       just before the final `jr $ra`
#     return_value   (never written — the body reads `4($fp)` on the
#                     way out, one word past the 1-cell frame.  See
#                     the in-body NOTE; this is the intentional bug
#                     this demo carries.)
#     r1, r2, r3     $v0 (transient)    streq's returns; each consumed
#                                       immediately by `move $a0, $v0`
#                                       before the print_int that follows
#     str1, str2, str3   `str1`, `str2`, `str3` (.data)
#                                       addresses loaded into $a0/$a1
#                                       just before each `jal streq`
#
#   In streq (callee — register-passing, no frame):
#     s1             $a0                input arg (advanced in place as
#                                       the byte loop walks the string)
#     s2             $a1                input arg  (")
#     *s1            $t0                one byte loaded each iteration
#     *s2            $t1                one byte loaded each iteration
#     return value   $v0                0 (equal) or 1 (different)
#
#   Cross-call saves (callee-save $s* values held LIVE across `jal`):
#     (none — like 07, this demo uses the older "save $ra to stack"
#      idiom rather than "save $ra in $s0".  The intermediate
#      returns r1/r2/r3 are short-lived enough that no save is
#      needed for them either.)
#
#   Volatile working registers:
#     $a0..$a1  function args at the call sites; $a0 also reused as
#               syscall arg between calls
#     $v0       syscall selector / function return value
#     $t0, $t1  byte-load scratch for the streq byte loop

        .data
nl:    .asciiz     "\n"
str1:    .asciiz     "str1"
str2:    .asciiz     "str2"
str3:    .asciiz     "str1"

        .text
        .globl main


# ---------- int streq(const char *s1, const char *s2) ----------
# No frame is needed: we only use caller-save scratch registers and the
# argument registers.  $sp and $fp are not touched.
streq:

loopBegin:
        # while (*s1 == *s2) {       -- load one byte from each pointer
        lb $t0, ($a0)                # $t0 = *s1
        lb $t1, ($a1)                # $t1 = *s2
        bne $t0, $t1, loopEnd        # bytes differ -> jump to "return 1"

        #   if (*s1 == 0) return 0;  -- both bytes are equal; if both NUL
        #                               then both strings ended together,
        #                               so the strings are equal.
        bne $t0, 0, incrementAndContinue
        li $v0, 0                    # equal -> return value = 0
        j str_eq_exit                # unconditional jump to the epilogue

incrementAndContinue:
        #   s1++;
        addi $a0, $a0, 1             # advance the s1 pointer by one byte
        #   s2++;
        addi $a1, $a1, 1             # advance the s2 pointer by one byte
        # }                           -- back to the top of the loop
        j loopBegin                  # unconditional jump to the loop top

loopEnd:
        # return 1;                   -- bytes differed
        li $v0, 1                    # not equal -> return value = 1

str_eq_exit:
        # The caller-saved $ra still has the address we should return
        # to (set by the `jal` that brought us here).
        jr $ra                       # jump to the address in $ra


# ---------- _start (main) ----------
main:
        # ---- frame setup ----
        move $fp, $sp                # set the frame pointer to the stack pointer
        addi $fp, $fp, -4            # reserve 1 cell for saved $ra

        # Save $ra — the upcoming `jal`s would otherwise clobber it.
        sw $ra, 0($fp)               # store $ra into the saved-$ra slot
        move $sp, $fp                # $sp pinned at $fp

        # int r1 = streq(str1, str2);
        la $a0, str1                 # arg = address of str1
        la $a1, str2                 # arg = address of str2
        jal streq                    # call streq; $v0 = result (1, differ)

        # print_int(r1);
        move $a0, $v0                # arg = r1
        li $v0, 1                    # syscall 1 = print_int
        syscall

        # print_string("\n");
        li $v0, 4                    # syscall 4 = print_string
        la $a0, nl                   # arg = address of "\n"
        syscall


        # int r2 = streq(str1, str3);
        la $a0, str1
        la $a1, str3
        jal streq                    # call streq; $v0 = 0 ("str1" == "str1")

        # print_int(r2);
        move $a0, $v0
        li $v0, 1
        syscall

        # print_string("\n");
        li $v0, 4
        la $a0, nl
        syscall


        # int r3 = streq(str2, str3);
        la $a0, str2
        la $a1, str3
        jal streq                    # call streq; $v0 = 1 ("str2" != "str1")

        # print_int(r3);
        move $a0, $v0
        li $v0, 1
        syscall

        # print_string("\n");
        li $v0, 4
        la $a0, nl
        syscall


        # Restore $ra and return.
        lw $ra, 0($fp)               # restore caller's $ra
        # NOTE: the next line reads 4($fp), one word past our 1-word
        # frame.  In the original source the return-value cell was never
        # set up; SPIM tends to surface a 0 here on a fresh stack, but
        # this is technically reading garbage.
        lw $v0, 4($fp)               # return code goes in $v0
        addi $fp, $fp, 4             # tear down the frame
        jr $ra                       # jump to the address in $ra
