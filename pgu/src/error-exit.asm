# Copyright 2002 Jonathan Bartlett
# Copyright 2026 William Emerison Six (MIPS/spimulator port)
#
# Permission is granted to copy, distribute and/or modify this program
# under the terms of the GNU Free Documentation License, Version 1.1 or
# any later version published by the Free Software Foundation; with no
# Invariant Sections, with no Front-Cover Texts, and with no Back-Cover
# Texts.  This program is an example from "Programming from the Ground
# Up"; a copy of the license is in the book's GNU FDL appendix (fdlap).


# C source equivalent:
#
#     __attribute__((noreturn))
#     void error_exit(const char *code, const char *msg) {
#       fputs(code, stderr);            // write(2, ...)
#       fputs(msg,  stderr);
#       fputs("\n", stderr);
#       _exit(1);                       // shell sees $? == 1
#     }
#
#     int main(void) {                  // a demonstration call
#       error_exit("0001: ", "Can't Open Input File");
#     }
#
# Invocation:  spimulator -f error-exit.asm ; echo $?
#   prints "0001: Can't Open Input File" on STDERR, exits 1.
#
#
#PURPOSE:  A last-resort error reporter for the "Developing Robust
#          Programs" chapter.  PGU's error_exit, retargeted to
#          MIPS/spimulator.  A robust program, on detecting a failed
#          syscall, writes a diagnostic to STANDARD ERROR (fd 2, so it
#          does not corrupt a pipeline's data) and exits with a
#          NON-ZERO status (so a script can tell it failed).
#
#
# Calling convention
# ==================
# error_exit(code=$a0, msg=$a1): two null-terminated strings.  It never
# returns, so a caller reaches it with a plain `j error_exit` (or `jal`;
# the saved $ra simply goes unused).  PGU's i386 version passed the two
# strings on the stack; MIPS passes them in $a0/$a1.
#
#
#SYMBOL TABLE  (C -> MIPS location)
#   code            $a0 (saved in $s0)   the error-code string
#   msg             $a1 (saved in $s1)   the human-readable message
#   in puts_stderr: s   $a0              string to write
#                   cursor $t0, start $t1, byte $t2


        .data
err_code:
        .asciiz "0001: "
err_msg:
        .asciiz "Can't Open Input File"
nl:
        .asciiz "\n"


        .text
        .globl main
main:
        # Demonstrate error_exit with a sample code and message.
        la   $a0, err_code
        la   $a1, err_msg
        j    error_exit          # tail call: error_exit never returns


# ---------- error_exit(code=$a0, msg=$a1) -- does not return --------
# doc-region-begin error exit
error_exit:
        move $s0, $a0            # save the two string pointers, because
        move $s1, $a1            #   puts_stderr reuses $a0/$a1

        move $a0, $s0            # write the error code
        jal  puts_stderr
        move $a0, $s1            # write the message
        jal  puts_stderr
        la   $a0, nl             # and a newline
        jal  puts_stderr

        li   $a0, 1              # exit status 1 = failure
        li   $v0, 17            # exit2
        syscall
# doc-region-end error exit


# ---------- puts_stderr(s=$a0) -- write a string to fd 2 (stderr) ---
# Leaf function: computes the length, then one `write` syscall.
puts_stderr:
        move $t0, $a0            # cursor
        move $t1, $a0            # start
ps_len:
        lb   $t2, 0($t0)
        beq  $t2, $zero, ps_write
        addi $t0, $t0, 1
        j    ps_len
ps_write:
        sub  $a2, $t0, $t1       # length = end - start
        move $a1, $t1            # buffer = string start
        li   $a0, 2              # fd 2 = standard error
        li   $v0, 15            # write
        syscall
        jr   $ra
