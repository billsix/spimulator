#==============================================================
# str-demo.asm — exercise every libstr function over a fixed set
# of hardcoded subcases.  One line per subcase:
#
#     strcmp_eq=PASS
#     memmove_bwd_overlap=PASS
#
# Produces output byte-for-byte identical to str-demo.c (which
# generates the golden, str-demo.expected).  Each subcase calls a
# libstr routine, compares the result to its known-correct value,
# and reports PASS/FAIL — since the library is correct, every line
# is PASS.
#
# Load alongside libstr.asm:
#   spimulator -f libstr.asm -f str-demo.asm
# The forward references to libstr's globals (strlen, strcmp, ...)
# resolve at load time because spim's symbol table accumulates
# across -f files.
#==============================================================

        .data
# ---- subcase labels (printed verbatim, then "=PASS"/"=FAIL") ----
lbl_strlen_empty:        .asciiz "strlen_empty"
lbl_strlen_hello:        .asciiz "strlen_hello"
lbl_strcmp_eq:           .asciiz "strcmp_eq"
lbl_strcmp_lt:           .asciiz "strcmp_lt"
lbl_strcmp_gt:           .asciiz "strcmp_gt"
lbl_strcmp_prefix:       .asciiz "strcmp_prefix"
lbl_strncmp_eq:          .asciiz "strncmp_eq"
lbl_strncmp_diff:        .asciiz "strncmp_diff"
lbl_strcpy_basic:        .asciiz "strcpy_basic"
lbl_strncpy_pad:         .asciiz "strncpy_pad"
lbl_strncpy_trunc:       .asciiz "strncpy_trunc"
lbl_strchr_found:        .asciiz "strchr_found"
lbl_strchr_notfound:     .asciiz "strchr_notfound"
lbl_strchr_nul:          .asciiz "strchr_nul"
lbl_memchr_found:        .asciiz "memchr_found"
lbl_memchr_notfound:     .asciiz "memchr_notfound"
lbl_memchr_bounded:      .asciiz "memchr_bounded"
lbl_memcpy_basic:        .asciiz "memcpy_basic"
lbl_memcpy_size0:        .asciiz "memcpy_size0"
lbl_memset_fill:         .asciiz "memset_fill"
lbl_memset_zero:         .asciiz "memset_zero"
lbl_memmove_fwd:         .asciiz "memmove_fwd_overlap"
lbl_memmove_bwd:         .asciiz "memmove_bwd_overlap"
lbl_memmove_nonoverlap:  .asciiz "memmove_nonoverlap"

# ---- test strings (read-only inputs) ----
s_empty:    .asciiz ""
s_hello:    .asciiz "hello"
s_abc:      .asciiz "abc"
s_abd:      .asciiz "abd"
s_ab:       .asciiz "ab"
s_abcXX:    .asciiz "abcXX"
s_abcYY:    .asciiz "abcYY"
s_hi:       .asciiz "hi"
s_abcdef:   .asciiz "abcdef"
s_xyz:      .asciiz "xyz"
s_ZZ:       .asciiz "ZZ"

# ---- PASS/FAIL suffixes ----
s_pass:     .asciiz "=PASS\n"
s_fail:     .asciiz "=FAIL\n"

# ---- mutable work buffers ----
        .align  2
buf:        .space  16
mm_fwd:     .asciiz "abcdef"        # mutated in place by memmove
mm_bwd:     .asciiz "abcdef"        # mutated in place by memmove
mm_dst:     .space  8

        .text
        .globl  main
main:
        addiu   $sp, $sp, -4
        sw      $ra, 0($sp)         # save the runtime's return address

        # ---- strlen_empty: strlen("") == 0 ----
        la      $a0, s_empty
        jal     strlen
        sltiu   $a1, $v0, 1         # ok = (len == 0)
        la      $a0, lbl_strlen_empty
        jal     report

        # ---- strlen_hello: strlen("hello") == 5 ----
        la      $a0, s_hello
        jal     strlen
        li      $t0, 5
        seq     $a1, $v0, $t0
        la      $a0, lbl_strlen_hello
        jal     report

        # ---- strcmp_eq: strcmp("abc","abc") == 0 ----
        la      $a0, s_abc
        la      $a1, s_abc
        jal     strcmp
        seq     $a1, $v0, $zero
        la      $a0, lbl_strcmp_eq
        jal     report

        # ---- strcmp_lt: strcmp("abc","abd") < 0 ----
        la      $a0, s_abc
        la      $a1, s_abd
        jal     strcmp
        slt     $a1, $v0, $zero
        la      $a0, lbl_strcmp_lt
        jal     report

        # ---- strcmp_gt: strcmp("abd","abc") > 0 ----
        la      $a0, s_abd
        la      $a1, s_abc
        jal     strcmp
        slt     $a1, $zero, $v0
        la      $a0, lbl_strcmp_gt
        jal     report

        # ---- strcmp_prefix: strcmp("abc","ab") > 0 ----
        la      $a0, s_abc
        la      $a1, s_ab
        jal     strcmp
        slt     $a1, $zero, $v0
        la      $a0, lbl_strcmp_prefix
        jal     report

        # ---- strncmp_eq: strncmp("abcXX","abcYY",3) == 0 ----
        la      $a0, s_abcXX
        la      $a1, s_abcYY
        li      $a2, 3
        jal     strncmp
        seq     $a1, $v0, $zero
        la      $a0, lbl_strncmp_eq
        jal     report

        # ---- strncmp_diff: strncmp("abc","abd",3) < 0 ----
        la      $a0, s_abc
        la      $a1, s_abd
        li      $a2, 3
        jal     strncmp
        slt     $a1, $v0, $zero
        la      $a0, lbl_strncmp_diff
        jal     report

        # ---- strcpy_basic: strcpy(buf,"hello"); strcmp(buf,"hello")==0 ----
        la      $a0, buf
        la      $a1, s_hello
        jal     strcpy
        la      $a0, buf
        la      $a1, s_hello
        jal     strcmp
        seq     $a1, $v0, $zero
        la      $a0, lbl_strcpy_basic
        jal     report

        # ---- strncpy_pad: strncpy(buf,"hi",5) -> 'h','i',0,0,0 ----
        la      $a0, buf
        la      $a1, s_hi
        li      $a2, 5
        jal     strncpy
        la      $t3, buf
        lbu     $t0, 0($t3)
        li      $t1, 'h'
        seq     $a1, $t0, $t1
        lbu     $t0, 1($t3)
        li      $t1, 'i'
        seq     $t2, $t0, $t1
        and     $a1, $a1, $t2
        lbu     $t0, 2($t3)
        seq     $t2, $t0, $zero
        and     $a1, $a1, $t2
        lbu     $t0, 3($t3)
        seq     $t2, $t0, $zero
        and     $a1, $a1, $t2
        lbu     $t0, 4($t3)
        seq     $t2, $t0, $zero
        and     $a1, $a1, $t2
        la      $a0, lbl_strncpy_pad
        jal     report

        # ---- strncpy_trunc: strncpy(buf,"hello",3) -> 'h','e','l' ----
        la      $a0, buf
        la      $a1, s_hello
        li      $a2, 3
        jal     strncpy
        la      $t3, buf
        lbu     $t0, 0($t3)
        li      $t1, 'h'
        seq     $a1, $t0, $t1
        lbu     $t0, 1($t3)
        li      $t1, 'e'
        seq     $t2, $t0, $t1
        and     $a1, $a1, $t2
        lbu     $t0, 2($t3)
        li      $t1, 'l'
        seq     $t2, $t0, $t1
        and     $a1, $a1, $t2
        la      $a0, lbl_strncpy_trunc
        jal     report

        # ---- strchr_found: strchr("hello",'l') == &s_hello[2] ----
        la      $a0, s_hello
        li      $a1, 'l'
        jal     strchr
        la      $t0, s_hello
        addiu   $t0, $t0, 2
        seq     $a1, $v0, $t0
        la      $a0, lbl_strchr_found
        jal     report

        # ---- strchr_notfound: strchr("hello",'z') == NULL ----
        la      $a0, s_hello
        li      $a1, 'z'
        jal     strchr
        seq     $a1, $v0, $zero
        la      $a0, lbl_strchr_notfound
        jal     report

        # ---- strchr_nul: strchr("hello",'\0') == &s_hello[5] ----
        la      $a0, s_hello
        li      $a1, 0
        jal     strchr
        la      $t0, s_hello
        addiu   $t0, $t0, 5
        seq     $a1, $v0, $t0
        la      $a0, lbl_strchr_nul
        jal     report

        # ---- memchr_found: memchr("hello",'l',5) == &s_hello[2] ----
        la      $a0, s_hello
        li      $a1, 'l'
        li      $a2, 5
        jal     memchr
        la      $t0, s_hello
        addiu   $t0, $t0, 2
        seq     $a1, $v0, $t0
        la      $a0, lbl_memchr_found
        jal     report

        # ---- memchr_notfound: memchr("hello",'z',5) == NULL ----
        la      $a0, s_hello
        li      $a1, 'z'
        li      $a2, 5
        jal     memchr
        seq     $a1, $v0, $zero
        la      $a0, lbl_memchr_notfound
        jal     report

        # ---- memchr_bounded: memchr("hello",'o',3) == NULL ('o' past n) ----
        la      $a0, s_hello
        li      $a1, 'o'
        li      $a2, 3
        jal     memchr
        seq     $a1, $v0, $zero
        la      $a0, lbl_memchr_bounded
        jal     report

        # ---- memcpy_basic: memcpy(buf,"abcdef",6) -> 'a'..'f' ----
        la      $a0, buf
        la      $a1, s_abcdef
        li      $a2, 6
        jal     memcpy
        la      $t3, buf
        lbu     $t0, 0($t3)
        li      $t1, 'a'
        seq     $a1, $t0, $t1
        lbu     $t0, 3($t3)
        li      $t1, 'd'
        seq     $t2, $t0, $t1
        and     $a1, $a1, $t2
        lbu     $t0, 5($t3)
        li      $t1, 'f'
        seq     $t2, $t0, $t1
        and     $a1, $a1, $t2
        la      $a0, lbl_memcpy_basic
        jal     report

        # ---- memcpy_size0: memcpy(buf,"ZZ",0) leaves buf[0]=='a' ----
        la      $a0, buf
        la      $a1, s_ZZ
        li      $a2, 0
        jal     memcpy
        la      $t3, buf
        lbu     $t0, 0($t3)
        li      $t1, 'a'
        seq     $a1, $t0, $t1
        la      $a0, lbl_memcpy_size0
        jal     report

        # ---- memset_fill: memset(buf,'x',4) -> 'x','x','x','x' ----
        la      $a0, buf
        li      $a1, 'x'
        li      $a2, 4
        jal     memset
        la      $t3, buf
        li      $t1, 'x'
        lbu     $t0, 0($t3)
        seq     $a1, $t0, $t1
        lbu     $t0, 1($t3)
        seq     $t2, $t0, $t1
        and     $a1, $a1, $t2
        lbu     $t0, 2($t3)
        seq     $t2, $t0, $t1
        and     $a1, $a1, $t2
        lbu     $t0, 3($t3)
        seq     $t2, $t0, $t1
        and     $a1, $a1, $t2
        la      $a0, lbl_memset_fill
        jal     report

        # ---- memset_zero: memset(buf,0,3) -> 0,0,0 ----
        la      $a0, buf
        li      $a1, 0
        li      $a2, 3
        jal     memset
        la      $t3, buf
        lbu     $t0, 0($t3)
        seq     $a1, $t0, $zero
        lbu     $t0, 1($t3)
        seq     $t2, $t0, $zero
        and     $a1, $a1, $t2
        lbu     $t0, 2($t3)
        seq     $t2, $t0, $zero
        and     $a1, $a1, $t2
        la      $a0, lbl_memset_zero
        jal     report

        # ---- memmove_fwd_overlap: memmove(mm_fwd, mm_fwd+1, 4) -> "bcdeef" ----
        la      $a0, mm_fwd
        la      $a1, mm_fwd
        addiu   $a1, $a1, 1
        li      $a2, 4
        jal     memmove
        la      $t3, mm_fwd
        lbu     $t0, 0($t3)
        li      $t1, 'b'
        seq     $a1, $t0, $t1
        lbu     $t0, 1($t3)
        li      $t1, 'c'
        seq     $t2, $t0, $t1
        and     $a1, $a1, $t2
        lbu     $t0, 2($t3)
        li      $t1, 'd'
        seq     $t2, $t0, $t1
        and     $a1, $a1, $t2
        lbu     $t0, 3($t3)
        li      $t1, 'e'
        seq     $t2, $t0, $t1
        and     $a1, $a1, $t2
        lbu     $t0, 4($t3)
        li      $t1, 'e'
        seq     $t2, $t0, $t1
        and     $a1, $a1, $t2
        lbu     $t0, 5($t3)
        li      $t1, 'f'
        seq     $t2, $t0, $t1
        and     $a1, $a1, $t2
        la      $a0, lbl_memmove_fwd
        jal     report

        # ---- memmove_bwd_overlap: memmove(mm_bwd+1, mm_bwd, 4) -> "aabcdf" ----
        la      $a0, mm_bwd
        addiu   $a0, $a0, 1
        la      $a1, mm_bwd
        li      $a2, 4
        jal     memmove
        la      $t3, mm_bwd
        lbu     $t0, 0($t3)
        li      $t1, 'a'
        seq     $a1, $t0, $t1
        lbu     $t0, 1($t3)
        li      $t1, 'a'
        seq     $t2, $t0, $t1
        and     $a1, $a1, $t2
        lbu     $t0, 2($t3)
        li      $t1, 'b'
        seq     $t2, $t0, $t1
        and     $a1, $a1, $t2
        lbu     $t0, 3($t3)
        li      $t1, 'c'
        seq     $t2, $t0, $t1
        and     $a1, $a1, $t2
        lbu     $t0, 4($t3)
        li      $t1, 'd'
        seq     $t2, $t0, $t1
        and     $a1, $a1, $t2
        lbu     $t0, 5($t3)
        li      $t1, 'f'
        seq     $t2, $t0, $t1
        and     $a1, $a1, $t2
        la      $a0, lbl_memmove_bwd
        jal     report

        # ---- memmove_nonoverlap: memmove(mm_dst,"xyz",4) -> "xyz\0" ----
        la      $a0, mm_dst
        la      $a1, s_xyz
        li      $a2, 4
        jal     memmove
        la      $t3, mm_dst
        lbu     $t0, 0($t3)
        li      $t1, 'x'
        seq     $a1, $t0, $t1
        lbu     $t0, 1($t3)
        li      $t1, 'y'
        seq     $t2, $t0, $t1
        and     $a1, $a1, $t2
        lbu     $t0, 2($t3)
        li      $t1, 'z'
        seq     $t2, $t0, $t1
        and     $a1, $a1, $t2
        lbu     $t0, 3($t3)
        seq     $t2, $t0, $zero
        and     $a1, $a1, $t2
        la      $a0, lbl_memmove_nonoverlap
        jal     report

        # ---- done ----
        lw      $ra, 0($sp)
        addiu   $sp, $sp, 4
        li      $v0, 0              # exit status 0 (via runtime __start)
        jr      $ra

#--------------------------------------------------------------
# report($a0 = label ptr, $a1 = ok) — print "<label>=PASS\n" or
# "<label>=FAIL\n".  Leaf: only syscalls (no jal), so $ra is
# untouched and needs no saving.
#--------------------------------------------------------------
report:
        la      $t0, s_fail         # assume FAIL...
        beqz    $a1, report_do
        la      $t0, s_pass         # ...unless ok is nonzero
report_do:
        li      $v0, 4
        syscall                     # print label ($a0)
        move    $a0, $t0
        li      $v0, 4
        syscall                     # print "=PASS\n" / "=FAIL\n"
        jr      $ra
