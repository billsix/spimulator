# Helper file for the multi-`-f` CLI regression test.
#
# Exports one function: helper_double($a0) -> $v0 = $a0 * 2.
# A leaf function — no $ra save, no frame.  The point of this
# file is just "labels in this file are callable from the other
# file once both are loaded into the same symbol table."
#
# This file does NOT define `main` — it's library-only.  Load
# it alongside tt.multifile.s to give that file's main something
# to call.

        .text
        .globl  helper_double
helper_double:
        sll     $v0, $a0, 1         # $v0 = $a0 * 2 (shift-left = multiply)
        jr      $ra
