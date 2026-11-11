#!/usr/bin/env bash
# Test runner driven by meson test().
#
# Usage: run-test.sh <spim-binary> <exception-file> <test-name>
#
# Each test name names a known pattern below.  The driver handles
# stdin / argv / output checks per test.  Exits 0 on pass, 1 on fail.

set -u

if [ $# -lt 3 ]; then
  echo "usage: $0 <spim> <exception-file> <test-name>" >&2
  exit 2
fi

SPIM=$1
EF=$2
NAME=$3

# Resolve the spim binary and exception file to absolute paths before cd'ing
# into the tests dir, so the caller can pass relative paths.
case "$SPIM" in /*) ;; *) SPIM=$(realpath "$SPIM") ;; esac
case "$EF"   in /*) ;; *) EF=$(realpath "$EF") ;; esac

TESTS_DIR=$(cd "$(dirname "$0")" && pwd)
cd "$TESTS_DIR"

out=$(mktemp); trap 'rm -f "$out"' EXIT
fail() { echo "FAIL: $NAME — $1" >&2; [ -s "$out" ] && tail -5 "$out" >&2; exit 1; }
expect_sentinel() {
  tail -n 1 "$out" | grep -q '^Passed all tests$' || fail "missing 'Passed all tests' sentinel"
}
expect_exit() {
  [ "$1" = "$2" ] || fail "expected exit=$1, got exit=$2"
}

case "$NAME" in
  # --- Dockerfile smoke set: tests that print "Passed all tests" ---
  bare)
    "$SPIM" -delayed_branches -delayed_loads -noexception -f tt.bare.s >"$out" 2>&1; rc=$?
    expect_exit 0 "$rc"
    expect_sentinel
    ;;
  core)
    "$SPIM" -exception_file "$EF" -f tt.core.s <tt.in >"$out" 2>&1; rc=$?
    expect_exit 4 "$rc"
    expect_sentinel
    ;;
  core_full|core_default_handler)
    # tt.core.s's whole output, not just its last line: ~25 expected
    # exception messages and the float prints are on stderr/stdout lines
    # the sentinel check never read.  Goldens tests/golden.core.stdout and
    # tests/golden.core.stderr, captured from the C engine (the `Loaded:`
    # line names the exception file, so it is left out).  The program
    # returns from main with $v0 = 4 (left by its last print_str), and main's
    # return value is the exit status.  core_default_handler runs the same
    # program on the built-in (#embed) exception handler, the path users get,
    # which no other test takes; its output must be the same.
    if [ "$NAME" = core_full ]; then ef_args=(-exception_file "$EF"); else ef_args=(); fi
    err=$(mktemp); trap 'rm -f "$out" "$err"' EXIT
    env -i "$SPIM" "${ef_args[@]}" -f tt.core.s <tt.in >"$out" 2>"$err"; rc=$?
    expect_exit 4 "$rc"
    diff -u golden.core.stdout "$out" >/dev/null || fail "stdout differs from golden.core.stdout"
    grep -v '^Loaded: ' "$err" | diff -u golden.core.stderr - >/dev/null \
      || fail "stderr differs from golden.core.stderr"
    ;;
  alu_bare)
    # Upstream's ALU torture test, in bare-machine mode as upstream ran it
    # (edited for MIPS32; the file's header says what changed).
    "$SPIM" -bare -noexception -f tt.alu.bare.s >"$out" 2>&1; rc=$?
    expect_exit 0 "$rc"
    expect_sentinel
    ;;
  fpu_bare)
    # Upstream's FPU torture test, in bare-machine mode as upstream ran it.
    "$SPIM" -bare -noexception -f tt.fpu.bare.s >"$out" 2>&1; rc=$?
    expect_exit 0 "$rc"
    expect_sentinel
    ;;
  le)
    "$SPIM" -exception_file "$EF" -f tt.le.s >"$out" 2>&1; rc=$?
    expect_exit 4 "$rc"
    expect_sentinel
    ;;
  dir)
    "$SPIM" -exception_file "$EF" -f tt.dir.s >"$out" 2>&1; rc=$?
    expect_exit 4 "$rc"
    expect_sentinel
    ;;
  octal_escape)
    "$SPIM" -exception_file "$EF" -f tt.octal_escape.s >"$out" 2>&1; rc=$?
    expect_exit 4 "$rc"
    expect_sentinel
    ;;
  argv)
    "$SPIM" -exception_file "$EF" -f tt.argv.s alpha beta gamma >"$out" 2>&1; rc=$?
    expect_exit 4 "$rc"
    expect_sentinel
    ;;
  multifile)
    # Multi-`-f` accumulates files into one symbol table.  Run in both
    # orders to confirm cross-file forward references resolve either
    # way — load-order shouldn't matter.
    "$SPIM" -exception_file "$EF" \
      -f tt.multifile.helper.s -f tt.multifile.s >"$out" 2>&1; rc=$?
    expect_exit 0 "$rc"
    expect_sentinel
    "$SPIM" -exception_file "$EF" \
      -f tt.multifile.s -f tt.multifile.helper.s >"$out" 2>&1; rc=$?
    expect_exit 0 "$rc"
    expect_sentinel
    ;;
  read_int_eof)
    printf '5\n7\n' | "$SPIM" -exception_file "$EF" -f tt.read_int_eof.s >"$out" 2>&1; rc=$?
    expect_exit 4 "$rc"
    expect_sentinel
    ;;
  read_int_space_sep)
    # Space-separated ints on one line, one int per syscall-5 call
    # (scanf-style), mixed with newline separators and a negative.
    printf '43 3 12\n-5 8\n' | "$SPIM" -exception_file "$EF" \
      -f tt.read_int_space_sep.s >"$out" 2>&1; rc=$?
    expect_exit 4 "$rc"
    expect_sentinel
    ;;

  # --- Tests with custom pass criteria ---
  disasm)
    # REPL command: load a program, print its listing, confirm the
    # segment header and user-text addresses appear.
    printf 'load "tt.le.s"\ndisasm\nexit\n' | "$SPIM" -exception_file "$EF" \
      >"$out" 2>&1; rc=$?
    expect_exit 0 "$rc"
    grep -q 'USER TEXT SEGMENT' "$out" || fail "missing segment header"
    grep -q '\[0x004000' "$out" || fail "missing user-text addresses"
    ;;
  read_char_eof)
    # Test outputs 'abc' (no trailing newline) then 'Passed all tests\n', so
    # 'Passed all tests' is NOT on its own line.  grep -q for the substring,
    # not the sentinel pattern.
    echo -n abc | "$SPIM" -exception_file "$EF" -f tt.read_char_eof.s >"$out" 2>&1; rc=$?
    expect_exit 4 "$rc"
    grep -q 'Passed all tests' "$out" || fail "missing 'Passed all tests' substring"
    ;;
  args_cmd)
    # REPL test: read a file, then change argv twice via the 'args' command,
    # verify each run sees the new argv[1].
    printf 'read "tt.args-cmd.s"\nargs zebra\nrun\nargs amber\nrun\nquit\n' \
      | "$SPIM" -exception_file "$EF" >"$out" 2>&1; rc=$?
    expect_exit 0 "$rc"
    grep -q 'argv1=z' "$out" || fail "first run didn't see argv1=z"
    grep -q 'argv1=a' "$out" || fail "second run didn't see argv1=a"
    ;;

  # --- Unix-process conformance tests (exit codes) ---
  return_value)
    # main returns 42, should be the shell exit status.
    "$SPIM" -exception_file "$EF" -f tt.return_value.s >"$out" 2>&1; rc=$?
    expect_exit 42 "$rc"
    ;;
  unaligned)
    # Unaligned fetch raises Exception 4 (AdEL); spim should exit 128+4 = 132.
    "$SPIM" -exception_file "$EF" -f tt.unaligned.s >"$out" 2>&1; rc=$?
    expect_exit 132 "$rc"
    ;;
  missing_main)
    # `main` undefined → spim should exit 1.
    "$SPIM" -exception_file "$EF" -f tt.missing_main.s >"$out" 2>&1; rc=$?
    expect_exit 1 "$rc"
    ;;
  parse_error)
    # Intentional parse error → spim should exit 2.
    "$SPIM" -exception_file "$EF" -f tt.parse_error.s >"$out" 2>&1; rc=$?
    expect_exit 2 "$rc"
    ;;
  hazard_barrier)
    # jr.hb and jalr.hb run as jr and jalr, and keep their own encodings
    # (bit 10, the hazard barrier, set), matching GNU as.
    "$SPIM" -exception_file "$EF" -f tt.hazard_barrier.s >"$out" 2>&1; rc=$?
    expect_exit 4 "$rc"
    expect_sentinel
    printf 'load "tt.hazard_barrier.s"\ndisasm\nexit\n' | "$SPIM" -exception_file "$EF" >"$out" 2>&1; rc=$?
    expect_exit 0 "$rc"
    grep -q '0x0100fc09  jalr.hb' "$out" || fail "jalr.hb \$t0 is not 0x0100fc09"
    grep -q '0x01008c09  jalr.hb' "$out" || fail "jalr.hb \$s1, \$t0 is not 0x01008c09"
    grep -q '0x03e00408  jr.hb' "$out" || fail "jr.hb \$ra is not 0x03e00408"
    ;;
  cop2_rejected)
    # cop2 is an error (spim has no coprocessor 2): exit 2, with the reason.
    "$SPIM" -exception_file "$EF" -f tt.cop2.s >"$out" 2>&1; rc=$?
    expect_exit 2 "$rc"
    grep -q 'cop2 is not supported: spim has no coprocessor 2' "$out" || fail "missing the cop2 error"
    ;;
  cop1x_decode)
    # COP1 words whose low bits match a COP1X row decode to the COP1
    # instruction (GNU as's encodings): trunc.w.s and floor.w.s run on -2.75,
    # and the stored words disassemble as round.l.s/trunc.l.s/trunc.w.s/floor.w.s.
    "$SPIM" -exception_file "$EF" -f tt.cop1x_decode.s >"$out" 2>&1; rc=$?
    expect_exit 0 "$rc"
    [ "$(grep -v '^Loaded: ' "$out" | tr '\n' ' ')" = "-2 -3 " ] || fail "trunc.w.s/floor.w.s of -2.75 are not -2 and -3"
    printf 'load "tt.cop1x_decode.s"\nrun\ndisasm\nexit\n' | "$SPIM" -exception_file "$EF" >"$out" 2>&1; rc=$?
    expect_exit 0 "$rc"
    grep -q '0x46001008  round.l.s \$f0, \$f2' "$out" || fail "0x46001008 is not round.l.s"
    grep -q '0x46001009  trunc.l.s \$f0, \$f2' "$out" || fail "0x46001009 is not trunc.l.s"
    grep -q '0x4600100d  trunc.w.s \$f0, \$f2' "$out" || fail "0x4600100d is not trunc.w.s"
    grep -q '0x4600100f  floor.w.s \$f0, \$f2' "$out" || fail "0x4600100f is not floor.w.s"
    ;;
  range_checks)
    # .align above 31 and $f32 are errors on their own lines, not undefined
    # shifts or reads past the FP register file; the REPL's print treats
    # $f33 (and a number too long for an int) as a label, as it does $99.
    "$SPIM" -exception_file "$EF" -f tt.range_checks.s >"$out" 2>&1; rc=$?
    expect_exit 2 "$rc"
    grep -q 'Alignment out of range (0 .. 31) on line 9 of file' "$out" || fail "no .align 32 error on line 9"
    grep -q 'Expected FP register on line 12 of file' "$out" || fail "no \$f32 error on line 12"
    printf 'print $f31\nprint $f33\nprint $f4294967294\nexit\n' | "$SPIM" -exception_file "$EF" >"$out" 2>&1; rc=$?
    expect_exit 0 "$rc"
    grep -q '^FP reg 31 = 0 (single)$' "$out" || fail "print \$f31 does not print FP reg 31"
    grep -q '^Unknown label: f33$' "$out" || fail "print \$f33 is not an unknown label"
    grep -q '^Unknown label: f4294967294$' "$out" || fail "print \$f4294967294 is not an unknown label"
    ;;
  register_pair)
    # ld/sd/mfc1.d/mtc1.d starting at register 31 are errors on their own
    # lines, not writes to a 33rd register.
    "$SPIM" -exception_file "$EF" -f tt.register_pair.s >"$out" 2>&1; rc=$?
    expect_exit 2 "$rc"
    for line in 10 11 12 13; do
      grep -q "Register pair starts at the last register (31) on line $line of file" "$out" \
        || fail "no register-pair error on line $line"
    done
    [ "$(grep -c 'Register pair' "$out")" -eq 4 ] || fail "a pair from register 30 was rejected"
    ;;
  print_all_regs_twice)
    # A second print_all_regs prints only its own text: the decimal listing
    # is longer than the hex one, and the hex one must not end in its tail.
    printf 'print_all_regs\nexit\n' | "$SPIM" -exception_file "$EF" 2>&1 | grep -v '^Loaded: ' >"$out.dec"
    printf 'print_all_regs hex\nexit\n' | "$SPIM" -exception_file "$EF" 2>&1 | grep -v '^Loaded: ' >"$out.hex"
    printf 'print_all_regs\nprint_all_regs hex\nexit\n' | "$SPIM" -exception_file "$EF" >"$out" 2>&1; rc=$?
    expect_exit 0 "$rc"
    same=0
    cat "$out.dec" "$out.hex" | diff - <(grep -v '^Loaded: ' "$out") >/dev/null || same=1
    rm -f "$out.dec" "$out.hex"
    [ "$same" -eq 0 ] || fail "print_all_regs then print_all_regs hex is not the two listings"
    ;;
  assemble_odd_end)
    # -assemble writes <file>.out next to the source, so it runs on a copy.
    dir=$(mktemp -d)
    cp tt.assemble_odd_end.s "$dir/"
    "$SPIM" -assemble -exception_file "$EF" -f "$dir/tt.assemble_odd_end.s" >"$out" 2>&1; rc=$?
    last=$(tail -n 1 "$dir/tt.assemble_odd_end.s.out")
    rm -rf "$dir"
    expect_exit 0 "$rc"
    case "$last" in
      *", 0x11223344, 0x5") ;;
      *) fail "the data .word list does not end in its last word: ...${last: -30}" ;;
    esac
    ;;
  small_text_segments)
    # A kernel text segment that stops short of the exception vector, or an
    # empty text segment, is a startup error.  Each used to loop forever on
    # "Invalid address ... for instruction".
    timeout 10 "$SPIM" -sktext 0 -exception_file "$EF" -f tt.le.s >"$out" 2>&1; rc=$?
    expect_exit 255 "$rc"
    grep -q '^-sktext 0: the kernel text segment must hold the exception handler at 0x80000180 (at least 388 bytes)$' "$out" \
      || fail "missing the -sktext error"
    timeout 10 "$SPIM" -stext 0 -exception_file "$EF" -f tt.le.s >"$out" 2>&1; rc=$?
    expect_exit 255 "$rc"
    grep -q '^-stext 0: the text segment needs at least one word (4 bytes)$' "$out" \
      || fail "missing the -stext error"
    ;;
  stack_overflow)
    # Recursion past -lstack stops the run with the stack-growth message, and
    # a run-time error that stops a run exits 1 (it used to exit 0).
    timeout 10 "$SPIM" -lstack 65536 -exception_file "$EF" -f tt.stack_overflow.s >"$out" 2>&1; rc=$?
    expect_exit 1 "$rc"
    grep -q "^Can't expand stack segment by " "$out" || fail "missing the stack-growth message"
    ;;
  kernel_round_trip)
    # teq traps into the kernel handler, which returns with eret: the program
    # carries on in user mode (EXL clear before and after) and exits 128+13.
    "$SPIM" -exception_file "$EF" -f tt.kernel_round_trip.s >"$out" 2>&1; rc=$?
    expect_exit 141 "$rc"
    grep -q 'Exception 13  \[Trap\]  occurred and ignored' "$out" || fail "the trap was not handled"
    [ "$(grep -cx '0' "$out")" -eq 2 ] || fail "EXL is not clear before and after the trap"
    ;;
  imm_range)
    # An out-of-range immediate is reported on the instruction's own line (7),
    # not the next one, and the program still assembles and runs.
    "$SPIM" -exception_file "$EF" -f tt.imm_range.s >"$out" 2>&1; rc=$?
    expect_exit 0 "$rc"
    grep -q 'immediate value (65432) out of range (-32768 .. 32767) on line 7$' "$out" \
      || fail "the range error does not name line 7"
    ;;
  parse_error_data_list)
    # A float in a .word list: one parse error, exit 2 -- not an endless
    # repeat of the error (the timeout catches a regression to the loop).
    timeout 10 "$SPIM" -exception_file "$EF" -f tt.parse_error_data_list.s >"$out" 2>&1; rc=$?
    expect_exit 2 "$rc"
    n=$(grep -c 'Expected expression' "$out")
    [ "$n" = 1 ] || fail "expected one 'Expected expression' error, got $n"
    ;;
  text_before_data)
    # Code before .data with no .text runs from the text segment.
    out_only=$(timeout 10 "$SPIM" -exception_file "$EF" -f tt.text_before_data.s 2>/dev/null); rc=$?
    expect_exit 0 "$rc"
    [ "$out_only" = 7 ] || fail "stdout was '$out_only', expected '7'"
    ;;
  load_error)
    # A run-time error while the -f files load (here: a text segment too
    # small for the program) prints its message and exits 1.  It used to
    # longjmp through a jmp_buf no setjmp had filled, and crash.
    timeout 10 "$SPIM" -stext 64 -f tt.le.s >"$out" 2>&1; rc=$?
    expect_exit 1 "$rc"
    grep -q "Can't expand text segment" "$out" || fail "missing the text-segment message"
    ;;
  repl_no_operand)
    # print_all_regs and breakpoint without an operand: the command after
    # them must still run.  Each used to read on into the next line.
    printf 'print_all_regs\nprint $sp\nbreakpoint\nprint $ra\n' \
      | timeout 10 "$SPIM" -noexception >"$out" 2>&1; rc=$?
    expect_exit 0 "$rc"
    grep -q '^Reg 29 = ' "$out" || fail "'print \$sp' after print_all_regs did not run"
    grep -q 'Must supply an address for breakpoint' "$out" || fail "no breakpoint usage error"
    grep -q '^Reg 31 = ' "$out" || fail "'print \$ra' after breakpoint did not run"
    ;;
  repl_dot)
    # The REPL's `.` command assembles one line, after the loaded program:
    # it used to assemble the loaded file a second time first.
    printf 'load "tt.repl_dot.s"\n. addiu $t0, $zero, 7\ndisasm\n' \
      | timeout 10 "$SPIM" -noexception >"$out" 2>&1; rc=$?
    expect_exit 0 "$rc"
    n=$(grep -c '^\[0x004000' "$out")
    [ "$n" = 3 ] || fail "expected 3 instructions in the text segment, got $n"
    grep -q '^\[0x00400008\].*addiu \$t0, \$r0, 7' "$out" || fail "the . line is not at 0x00400008"
    ;;
  missing_flag_value)
    # A flag that takes a value, given last: report it and print the usage,
    # like any other bad flag.  -sdata read atoi(NULL) and crashed;
    # -exception_file silently loaded no handler.
    for flag in -sdata -exception_file; do
      timeout 10 "$SPIM" $flag </dev/null >"$out" 2>&1; rc=$?
      [ "$rc" -lt 128 ] || fail "$flag as the last argument died of a signal (exit $rc)"
      grep -q "spimulator: $flag requires a value" "$out" || fail "no message for $flag"
    done
    ;;
  engine_fixes)
    # Self-checking: the engine fixes of tasks/archive/2026/11/10/engine-port-prereq-c-fixes.md.
    timeout 10 "$SPIM" -exception_file "$EF" -f tt.engine_fixes.s >"$out" 2>&1; rc=$?
    expect_exit 0 "$rc"
    expect_sentinel
    ;;
  stderr_split)
    # Banner on stderr, 'ok' on stdout.  Verify stdout is exactly 'ok\n'.
    stdout_only=$("$SPIM" -exception_file "$EF" -f tt.stderr_split.s 2>/dev/null); rc=$?
    expect_exit 0 "$rc"
    [ "$stdout_only" = "ok" ] || fail "stdout was '$stdout_only', expected 'ok'"
    ;;

  ast_parity_all)
    # AST-emit regression golden: for every representative .s file in
    # tests/, assemble it (-dump) and diff the text + data segment dumps
    # against tests/golden.ast_parity_all.txt.  That golden was captured
    # from the SDT parser before it was removed (2026-08-03), so this
    # test proves the AST-only emitter still reproduces the exact bytes
    # the old syntax-directed path produced.  Regenerate the golden with
    # tests/gen-parity-golden.sh if an intentional codegen change lands.
    #
    # The program list and each program's flags: tests/parity-programs.txt.
    # A listed program that writes no dump fails the test.
    . "$TESTS_DIR/parity-capture.sh"
    d=$(mktemp -d); got="$d/got.txt"
    trap 'rm -f "$out"; rm -rf "$d"' EXIT
    capture_all > "$got" 2> "$d/err" || { cat "$d/err" > "$out"; fail "a parity program wrote no dump"; }
    diff -u "$TESTS_DIR/golden.ast_parity_all.txt" "$got" > "$out" 2>&1 \
      || fail "AST emit differs from golden.ast_parity_all.txt"
    ;;
  ast_parity)
    # AST-emit regression golden for tt.core.s (text + data segments),
    # diffed against tests/golden.ast_parity.txt.  Same rationale as
    # ast_parity_all: the golden is the pre-removal SDT output, so this
    # confirms the AST-only emitter is byte-identical to it.
    d=$(mktemp -d); got="$d/got.txt"
    trap 'rm -f "$out"; rm -rf "$d"' EXIT
    ( cd "$d" && "$SPIM" -exception_file "$EF" -dump \
        -f "$TESTS_DIR/tt.core.s" </dev/null >/dev/null 2>&1 )
    : > "$got"
    for seg in text data; do
      [ -f "$d/$seg.asm" ] || continue
      echo "### tt.core.s :: $seg ###" >> "$got"
      sed 's/[[:space:]]*;.*$//' "$d/$seg.asm" >> "$got"
    done
    diff -u "$TESTS_DIR/golden.ast_parity.txt" "$got" > "$out" 2>&1 \
      || fail "AST emit differs from golden.ast_parity.txt"
    ;;
  print_ast)
    # -print-ast: AST gets dumped to stderr, exit 0, no emit.
    "$SPIM" -noexception -print-ast -f tt.listing.s >/dev/null 2>"$out"; rc=$?
    [ "$rc" = "0" ] || fail "expected exit=0, got exit=$rc"
    grep -q "FILE source=" "$out" || fail "AST output missing FILE header"
    grep -q "DATA_BYTE count=1 values=\[66\]" "$out" \
      || fail "AST output missing data byte node"
    grep -q "INST_J op=" "$out" || fail "AST output missing J-type node"
    grep -q "LABEL_DEF name=target" "$out" \
      || fail "AST output missing target label"
    ;;
  show_expansion)
    # -show-expansion: prints each PSEUDO node + its expansion children.
    # Verify we see the expected pseudo wrappers and that each has at
    # least one expansion child.  Exit 0; no emit.
    "$SPIM" -noexception -show-expansion -f tt.pseudo.s >/dev/null 2>"$out"; rc=$?
    [ "$rc" = "0" ] || fail "expected exit=0, got exit=$rc"
    grep -q "PSEUDO mnemonic=li" "$out" \
      || fail "missing PSEUDO wrapper for li"
    grep -q "PSEUDO mnemonic=la" "$out" \
      || fail "missing PSEUDO wrapper for la"
    grep -q "PSEUDO mnemonic=move" "$out" \
      || fail "missing PSEUDO wrapper for move"
    grep -q "PSEUDO mnemonic=neg" "$out" \
      || fail "missing PSEUDO wrapper for neg"
    grep -q "PSEUDO mnemonic=not" "$out" \
      || fail "missing PSEUDO wrapper for not"
    grep -q "PSEUDO mnemonic=bge" "$out" \
      || fail "missing PSEUDO wrapper for bge"
    # bge expands to slt + beq — verify at least two children below
    # the PSEUDO bge line by checking the count of indented expansion
    # entries.
    # Children name their opcode and carry the pseudo-op's own line, even
    # when its last operand read on to the newline.
    grep -q '^  \[line 8\] INST_I op=ori rt=8 rs=0 imm=100000$' "$out" \
      || fail "li's child is not 'ori' on line 8"
    grep -q '^  \[line 9\] INST_I op=ori rt=4 rs=0 imm=msg$' "$out" \
      || fail "la's child is not 'ori' on line 9"
    awk '/PSEUDO mnemonic=bge/{flag=1; next} flag && /^  \[line/{c++; next} flag && /^\[line/{flag=0} END{exit (c >= 2 ? 0 : 1)}' "$out" \
      || fail "bge PSEUDO should have >=2 expansion children"
    ;;
  listing)
    # -listing FILE: produces an assemble-time event trace.  Verify each
    # event kind we instrumented fires for tt.listing.s.  Greps for
    # patterns rather than diffing a golden file because exception_file
    # addresses are environment-dependent.
    lst=$(mktemp); trap 'rm -f "$out" "$lst"' EXIT
    "$SPIM" -exception_file "$EF" -listing "$lst" -f tt.listing.s >"$out" 2>&1; rc=$?
    expect_exit 0 "$rc"
    grep -q "label  b1 .*defined at" "$lst" \
      || fail "missing label def for b1"
    grep -q "label  main .*defined at .*(global)" "$lst" \
      || fail "missing global label def for main"
    grep -qE "data .*  byte    0x42" "$lst" \
      || fail "missing byte event for b1"
    grep -qE "data .*  half    0x1234" "$lst" \
      || fail "missing half event for h1"
    grep -qE "data .*  word    0xdeadbeef" "$lst" \
      || fail "missing word event for w1"
    grep -qE "data .*  string  len=2 \(\+\\\\0\)" "$lst" \
      || fail "missing string event for s1"
    grep -q "fref .*uses unresolved symbol 'target'" "$lst" \
      || fail "missing forward ref for target"
    grep -q "fres .*patched 'target'" "$lst" \
      || fail "missing forward resolved for target"
    ;;
  divide_by_zero)
    # spim's div pseudo-op traps on zero divisor.  The default
    # exception handler reports ExcCode 9 (Bp) and continues, so
    # the program exits 0 from its own exit syscall.  Verify both:
    # the stderr report fired AND the program reached its exit.
    "$SPIM" -exception_file "$EF" -f tt.divide_by_zero.s >"$out" 2>&1; rc=$?
    expect_exit 0 "$rc"
    grep -q 'Exception 9' "$out" \
      || fail "stderr missing 'Exception 9' diagnostic"
    ;;

  explain_parity)
    # The teaching mode must not change what the program does: stdout and
    # the exit status are the same at every -explain level, and the
    # exception the unaligned load raises is still reported.  (Narration
    # goes to stderr.)
    ref=$(mktemp); trap 'rm -f "$out" "$ref"' EXIT
    env -i "$SPIM" -exception_file "$EF" -f tt.explain_parity.s >"$ref" 2>/dev/null; ref_rc=$?
    expect_exit 132 "$ref_rc"
    grep -q 'Exception 4' "$ref" || fail "no Exception 4 report without -explain"
    # Every level also narrates how the program ended.
    err=$(mktemp); trap 'rm -f "$out" "$ref" "$err"' EXIT
    for level in 1 2 3 4; do
      env -i "$SPIM" -exception_file "$EF" -explain=$level -f tt.explain_parity.s >"$out" 2>"$err"; rc=$?
      [ "$rc" = "$ref_rc" ] || fail "-explain=$level exit=$rc, without -explain exit=$ref_rc"
      diff -q "$ref" "$out" >/dev/null || fail "-explain=$level stdout differs from the run without -explain"
      grep -q "The program exited with status $ref_rc " "$err" \
        || fail "-explain=$level does not narrate the exit status $ref_rc"
    done
    ;;
  explain_branch_targets)
    # A taken branch's narration names the address the PC moves to (the
    # "PC: from → to" line), and the step header names the branch's own
    # PC, in both branch modes.  In delayed mode the delay slot runs, and
    # is narrated, before the branch is.
    for mode in "" -delayed_branches; do
      env -i "$SPIM" -exception_file "$EF" $mode -explain=2 -f tt.explain_branches.s \
        2>"$out" >/dev/null; rc=$?
      expect_exit 0 "$rc"
      n=$(awk '
        /^Stepped at PC = /{ at = $5; sub(/:$/, "", at); target = ""; taken = 0; next }
        /would transfer to/{ target = $0; sub(/.*transfer to /, "", target); sub(/ .*/, "", target); next }
        /branch was taken/{ taken = 1; next }
        /^  PC:/ && target != "" {
          if (taken) { n++; if ($2 != at || $4 != target) { print "bad: " $0 " target " target " at " at > "/dev/stderr"; bad = 1 } }
          target = ""
        }
        END { if (bad) exit 1; print n }' "$out") || fail "${mode:-non-delayed}: a taken branch names the wrong target or PC"
      [ "$n" = 3 ] || fail "${mode:-non-delayed}: expected 3 taken branches, saw $n"
    done
    ;;
  explain_pseudo)
    # Pseudo-op headers at -explain=1: a pseudo-op behind a label is named;
    # an expansion's first instruction says how many there are and the
    # rest are continuations; a syscall after a pseudo-op is not one; and
    # a real instruction the assembler expands (lw from a label) is named.
    env -i "$SPIM" -exception_file "$EF" -explain=1 -f tt.explain_pseudo.s >/dev/null 2>"$out"; rc=$?
    expect_exit 0 "$rc"
    for want in \
      'Pseudo-op: `li` (single real instruction)' \
      'Pseudo-op: `la` (first of 2 real instructions)' \
      'Pseudo-op: `la` (continuation, 2 of 2)' \
      'Expanded: `lw` (first of 2 real instructions)' \
      'Expanded: `lw` (continuation, 2 of 2)' \
      'Pseudo-op: `bgt` (first of 2 real instructions)' \
      'Pseudo-op: `bgt` (continuation, 2 of 2)'; do
      grep -qF "$want" "$out" || fail "missing '$want'"
    done
    # main's li (behind the label) is the first of them.
    awk '/^Stepped at PC = 0x00400028:/{f=1; next} /^Stepped at/{f=0} f && /Pseudo-op: `li` \(single/{ok=1} END{exit !ok}' "$out" \
      || fail "the li behind main: gets no pseudo-op line"
    # The syscall at 0x00400034, after the la, gets no pseudo-op line.
    awk '/^Stepped at PC = 0x00400034:/{f=1; next} /^Stepped at/{f=0} f && /Pseudo-op/{exit 1}' "$out" \
      || fail "the syscall is narrated as part of a pseudo-op"
    # At -explain=2 the bgt description names the immediate form it emitted.
    env -i "$SPIM" -exception_file "$EF" -explain=2 -f tt.explain_pseudo.s >/dev/null 2>"$out"
    grep -qF '`slti $at, $rs, imm+1` + `beq $at, $0, label`' "$out" \
      || fail "the bgt description does not name slti + beq"
    ;;
  repl_step_exception)
    # REPL `step` shows each instruction it runs; showing one must not hide
    # the exception it raised, so `step` reports what `run` reports.
    printf 'load "tt.explain_parity.s"\nstep 200\nexit\n' \
      | env -i "$SPIM" -exception_file "$EF" >"$out" 2>&1; rc=$?
    expect_exit 0 "$rc"
    grep -q 'Exception occurred at PC=0x0040003c' "$out" || fail "step hid the unaligned load's exception"
    ;;
  explain)
    # -explain=2 narration: byte-diff against the pinned golden file.
    #
    # The simulated stack-pointer values depend on the host environment
    # size (env vars consume stack space before argv is placed), so run
    # spim under `env -i` with a fixed minimal environment to match the
    # conditions under which the golden file was captured.  Strip the
    # path-dependent "Loaded: ..." line before diffing.
    env -i "$SPIM" -exception_file "$EF" -explain=2 -f tt.explain.s 2>&1 \
      | grep -v '^Loaded: ' > "$out"; rc=${PIPESTATUS[0]}
    expect_exit 0 "$rc"
    expected=$(mktemp)
    trap 'rm -f "$out" "$expected"' EXIT
    grep -v '^Loaded: ' tt.explain.expected > "$expected"
    if ! diff -q "$expected" "$out" > /dev/null; then
      fail "explain output differs from golden; regen with: env -i ./builddir/spimulator -exception_file src/exceptions.s -explain=2 -f tests/tt.explain.s > tests/tt.explain.expected 2>&1"
    fi
    ;;

  *)
    echo "unknown test name: $NAME" >&2
    exit 2
    ;;
esac

echo "PASS: $NAME"
