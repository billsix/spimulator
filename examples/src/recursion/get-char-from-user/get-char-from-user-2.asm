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


# C source — see get-char-from-user.c (-2.asm fixes the bugs
# from -1.asm but matches the same C demo).
#
#     __attribute__((noreturn)) void _start(void) {
#       int ch = read_char();
#       while (ch != -1) {
#         if (ch != '\n') {
#           print_string("ch was ");
#           print_char((char)ch);
#           print_string(", value ");
#           print_int(ch);
#           print_string("\n");
#         }
#         ch = read_char();
#       }
#       os_exit(0);
#     }


#PURPOSE:  Read characters from standard input until EOF and for
#          each non-newline character print
#             ch was <CHAR>, value <DEC>
#
#          This is the fixed counterpart to get-char-from-user-
#          1.asm, which fumbled the stack frame by giving the int32_t
#          return slot an odd byte offset.  Here we pad the frame to
#          8 bytes so the int lands at a word-aligned offset, and
#          the program runs.  The newline-skip compare is also
#          fixed: an immediate character constant (`'\n'`) rather
#          than an address-of-string load (which is what -1.asm got
#          wrong as Bug #2).
#
#NOTES:    Inside the loop body, the two syscall selectors for the
#          "print the character" and "print the value" steps end up
#          swapped relative to the C source.  The asm still completes
#          but the per-iteration output prints the int and the char
#          in the opposite order from the C demo.
#
#STORAGE LAYOUT
#
#   8-byte stack frame, two word-sized slots.  Padding the byte-
#   sized C variable `ch` to a full word puts the int32_t return
#   slot at offset 4, a word-aligned address — the alignment fix
#   for the bug in get-char-from-user-1.asm.
#
#         higher addresses
#           +-------------+
#           | return code |   4($fp)
#    $fp -> | ch          |   0($fp)   (held as a word)
#           +-------------+
#         lower addresses
#
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     ch             0($fp)              word-sized slot; reloaded into
#                                        $t0 at each use site (loopTest,
#                                        loopBody, getNextChar).  The
#                                        spill-then-reload pattern is
#                                        the point of this demo — `ch`
#                                        outlives every read_char syscall
#                                        because $v0 is clobbered each
#                                        time.
#     return_value   4($fp)              word-aligned at offset 4
#                                        (this is what fixes -1.asm)
#
#   Cross-call saves (callee-save $s* values held LIVE across `jal`):
#     (none — this demo has no `jal` at all.  Every call is a
#      `syscall`, and `ch` is preserved across those by spilling to
#      0($fp) rather than by living in a callee-save register.)
#
#   Volatile working registers:
#     $a0   syscall arg
#     $v0   syscall selector / read_char return value
#     $t0   ch-in-register at each use site (`lw $t0, 0($fp)` before
#           each compare and each print)

        .data
nl:    .asciiz     "\n"
commaSpaceValue:    .asciiz     ", value "
chWas:    .asciiz     "ch was "

        .text
        .globl main
main:
        # ---- frame setup ----
        move $fp, $sp                # set the frame pointer to the stack pointer
        addi $fp, $fp, -8            # 8 bytes — 1 char + 1 int32_t, padded

        # ch = 0;
        li $t0, 0                    # load 0 into $t0
        sw $t0, 0($fp)               # store $t0 into the ch slot

        # return_value = 0;
        li $t0, 0                    # load 0 into $t0
        sw $t0, 4($fp)               # store $t0 into the return-code slot

        # ch = read_char();
        li $v0 12                    # syscall 12 = read_char
        syscall                      # ask the OS for one character
        sw $v0, 0($fp)               # store the fresh ch into the frame

loopTest:
        # while (ch != -1) {           -- -1 is the EOF signal
        lw $t0, 0($fp)               # $t0 = ch
        bltz $t0, loopEnd            # EOF (Ctrl-D / pipe close) -> exit

loopBody:
        # if (ch != '\n') {
        lw $t0, 0($fp)               # $t0 = ch
        beq $t0, '\n', getNextChar   # newline -> skip the print block

        # print_string("ch was ");
        li $v0, 4                    # syscall 4 = print_string
        la $a0, chWas                # arg = address of "ch was "
        syscall                      # ask the OS to print the string

        # (intended: print_char((char)ch);  asm actually prints as int)
        lw $t0, 0($fp)               # $t0 = ch
        move $a0, $t0                # arg = ch
        li $v0, 1                    # syscall 1 = print_int
        syscall                      # ask the OS to print the integer

        # print_string(", value ");
        li $v0, 4                    # syscall 4 = print_string
        la $a0, commaSpaceValue      # arg = address of ", value "
        syscall                      # ask the OS to print the string

        # (intended: print_int(ch);  asm actually prints as char)
        lw $t0, 0($fp)               # $t0 = ch
        li $v0, 11                   # syscall 11 = print_char
        move $a0, $t0                # arg = ch
        syscall                      # ask the OS to print the character

        # print_string("\n");
        li $v0, 4                    # syscall 4 = print_string
        la $a0, nl                   # arg = address of "\n"
        syscall                      # ask the OS to print the string

getNextChar:
        # ch = read_char();
        li $v0 12                    # syscall 12 = read_char
        syscall                      # ask the OS for one character
        sw $v0, 0($fp)               # store the fresh ch into the frame

        # } end of while body — jump back to the test
        j loopTest                   # unconditional jump to the loop top

loopEnd:
        # return return_value;        -- os_exit(0) in the C source
        lw $v0, 4($fp)               # return code goes in $v0
        addi $fp, $fp, 8             # tear down the frame
        jr $ra                       # jump to the address in $ra
