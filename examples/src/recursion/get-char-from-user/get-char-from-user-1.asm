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


# C source — see get-char-from-user.c
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


#PURPOSE:  Read characters from standard input until EOF and, for
#          each one that is not a newline, print
#             ch was <CHAR>, value <DEC>
#          The loop stops when read_char returns -1 (Ctrl-D in an
#          interactive terminal; pipe close when stdin is piped).
#
#          This is also the FIRST demo in the series that puts
#          data on the stack — demos 01 through 03 kept their
#          locals in registers.  Here we want to hold two values
#          across the loop: `ch` (one byte) and the program's
#          return code (an int32_t).  Allocating a stack frame
#          seems like the natural place to keep them.
#
#          The frame size we picked below is intentionally WRONG.
#          Walking through why -1 fails is the lesson; 26-get-char-
#          from-user-2.asm pads to 8 bytes so the int lands at a
#          word-aligned offset, and works.
#
#NOTES:    Bug #1 (the alignment lesson this demo is built around):
#          the int32_t at 1($fp) is not word-aligned.  MIPS requires
#          32-bit `sw`/`lw` to land on multiples-of-4 addresses, so
#          SPIM raises an alignment fault here.
#
#          Bug #2 (independent of the alignment story): the '\n'
#          compare loads the *address* of the string "\n" rather
#          than the character value '\n'.  As a result the loop's
#          newline-skip never fires; each newline gets formatted
#          with the rest of the input.
#
#STORAGE LAYOUT  (intentionally broken — see NOTES above)
#
#   5-byte stack frame.  The misaligned int32_t at 1($fp) IS the
#   lesson here; get-char-from-user-2.asm pads to 8 bytes so
#   the int lands at a word-aligned offset.
#
#         higher addresses
#           +-------------+
#           | return code |   1..4($fp)  (4 bytes, STARTS AT ODD OFFSET 1)
#    $fp -> | ch          |   0($fp)     (1 byte; written with `sw` which
#           +-------------+               actually stores 4, stomping the
#         lower addresses                 int's bytes 1..3 next door)
#
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     ch             0($fp)              declared 1 byte, but the body
#                                        uses word-sized `sw`/`lw` on it.
#                                        Also kept transiently in $v0
#                                        right after each `read_char`
#                                        syscall — the asm never loads
#                                        ch back from memory once read.
#     return_value   1($fp)              4-byte int at the misaligned
#                                        odd offset.  Word-sized `sw`/
#                                        `lw` here triggers Bug #1 in
#                                        NOTES (the alignment fault).
#     return_value   8($fp)  (on exit)   Bug #3 — the read on the way
#                                        out is `lw $v0, 8($fp)`, three
#                                        bytes past the end of the frame.
#
#   Volatile working registers:
#     $a0   syscall arg
#     $v0   syscall selector / read_char return (and the transient
#           home of `ch` between read and use)
#     $t0   scratch.  Also where `la $t0, nl` deposits the .data
#           ADDRESS that the broken newline-skip compare uses in
#           place of the character byte — Bug #2 in NOTES.

        .data
nl:    .asciiz     "\n"
commaSpaceValue:    .asciiz     ", value "
chWas:    .asciiz     "ch was "

        .text
        .globl main
main:

        #### NOTE - this code does not work when run "spimulator -f get-char-from-user/get-char-from-user-1.asm"
        ####  THIS IS BECAUSE INTEGERS NEED TO BE ALIGNED ON 32 BIT BOUNDARIES!!!!!
        ####  THE FIX IS IN get-char-from-user/get-char-from-user-2.asm

        # ---- frame setup ----
        move $fp, $sp                # set the frame pointer to the stack pointer
        addi $fp, $fp, -5            # 1 byte for ch + 4 for return_value

        # ch = 0;                     -- placeholder; overwritten by the read below
        li $t0, 0                    # load 0 into $t0
        sw $t0, 0($fp)               # store $t0 into the ch slot

        # return_value = 0;
        li $t0, 0                    # load 0 into $t0
        sw $t0, 1($fp)               # store $t0 into the return-code slot
                                     # (MISALIGNED — bug noted in NOTES)

        # ch = read_char();           -- result lands in $v0
        li $v0 12                    # syscall 12 = read_char
        syscall                      # ask the OS for one character

loopTest:
        # while (ch != -1) {           -- -1 is the EOF signal
        bltz $v0, loopEnd            # branch if ch < 0  (== -1 = EOF)

        # if (ch != '\n') { ... }     -- BUG: this loads &"\n" (an
        #                                address in .data), not the
        #                                byte '\n'.  Bug #2 in NOTES.
        la $t0, nl                   # $t0 = ADDRESS of the string "\n"
        beq $v0, $t0, getNextChar    # branch over the print block

        # print_string("ch was ");
        li $v0, 4                    # syscall 4 = print_string
        la $a0, chWas                # arg = address of "ch was "
        syscall                      # ask the OS to print the string

        # print_char((char)ch);
        # BUG: $v0 also holds the syscall selector — overwritten above
        # to 4 for print_string, so this prints "4" instead of the char.
        move $a0, $v0                # arg = whatever $v0 currently holds
        li $v0, 1                    # syscall 1 = print_int
        syscall                      # ask the OS to print

        # print_string(", value ");
        li $v0, 4                    # syscall 4 = print_string
        la $a0, commaSpaceValue      # arg = address of ", value "
        syscall                      # ask the OS to print the string

        # ch = read_char();           -- next iteration's read
        li $v0 12                    # syscall 12 = read_char
        syscall                      # ask the OS for one character

        # }   end of while body
        j loopTest                   # unconditional jump to the loop top

loopEnd:
        # print_string("\n");         -- prints final newline before exit
        li $v0, 4                    # syscall 4 = print_string
        la $a0, nl                   # arg = address of "\n"
        syscall                      # ask the OS to print the string

getNextChar:
        # Despite the name, this label is the function epilogue — both
        # the loop-body skip (`beq ..., getNextChar`) and the
        # fall-through from `loopEnd` land here and exit.

        # return return_value;
        lw $v0, 8($fp)               # return code goes in $v0
                                     # (NOTE: reads 8($fp), beyond the 5-byte
                                     #  frame; another bug worth noting)
        addi $fp, $fp, 5             # tear down the frame
        jr $ra                       # jump to the address in $ra
