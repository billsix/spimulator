# Fix left-shift overflow in hex-literal scanner

## Status — not started

Filed during the post-C23-sweep ASan/UBSan audit (May 2026).
Pre-existing from the hand-written parser migration.

## Bug

`src/scanner.c:237-241`:

```c
while (isxdigit(peek_char())) {
  int c = next_char();
  value = (value << 4) |
          (c <= '9' ? c - '0' : (c <= 'F' ? c - 'A' + 10 : c - 'a' + 10));
}
```

`value` is declared `int`.  Once `value` has accumulated 7 hex
digits (i.e. is in the range `0x0fffffff`+ with the top nibble
set), `value << 4` overflows a signed 32-bit int.  Left-shifting
a signed value past its sign bit is undefined behavior in C.

UBSan finding on any program that uses an 8-character hex
constant with the high nibble bit set (very common —
`0x80000000` for `STACK_TOP`, `0xfffe0000` masks, etc.):

```
src/scanner.c:239:22: runtime error: left shift of 134217752 by
   4 places cannot be represented in type 'int'
```

## Root cause

`value` should accumulate in `unsigned` (or `uint32_t`) where
the shift is defined to wrap, then convert to whatever signed
type the caller expects.  MIPS 32-bit immediates and addresses
are naturally unsigned bit patterns; the signed interpretation
happens at use time.

Code was written by Bill, May 2026 during the hand-written
parser migration that replaced flex+bison.

## Fix

Change the local `value` from `int` to `uint32_t`:

```c
uint32_t value = 0;
if (peek_char() == '0' && (peek_char2() == 'x' || peek_char2() == 'X')) {
  next_char();
  next_char(); /* consume "0x" */
  while (isxdigit(peek_char())) {
    int c = next_char();
    value = (value << 4) |
            (c <= '9' ? c - '0' : (c <= 'F' ? c - 'A' + 10 : c - 'a' + 10));
  }
} else {
  while (isdigit(peek_char())) {
    value = value * 10 + (next_char() - '0');
  }
}
out->type = TOK_INT;
out->scan_value.i = (int32_t)value;     /* preserve bit pattern */
```

The signed-decimal branch (the `else`) doesn't overflow in
practice (decimal can only reach `2147483648` which would itself
trip UBSan, but the parser rejects it via a different path).
Still worth doing the same `uint32_t` accumulator for symmetry
— and to handle the historical `li $reg, 4294967295` style of
loading a 32-bit pattern.

If `scan_value.i` is also `int` and the bit pattern needs to
flow as the same 32 bits, the final cast `(int32_t)value` does
the right thing (implementation-defined but universally a
no-op bit-cast on two's-complement platforms — which is every
platform GCC supports).

## Verification

1. Rebuild under ASan+UBSan.
2. Run a small program that uses `li $reg, 0x80000000` or
   similar high-bit constants:
   ```asm
   li $a0, 0x80000000
   li $a1, 0xffff0000
   ```
3. Confirm no `left shift` UBSan finding from `scanner.c`.
4. Spot-check that the values parsed are still correct
   bit-for-bit (`0x80000000` should give `R[$a0] = INT_MIN`,
   not 0).
5. 22/22 regression tests still pass.

The existing `tests/tt.le.s` and `tt.be.s` both include
`li $t5 0x7fffffff` — would catch a wrong-value regression but
not the UB (since `0x7fffffff` doesn't trip the overflow; it's
the high bit being set that does).  Worth adding a regression
test with a value like `0xfedcba98` to exercise the path.

## Why this matters

Same UB-hazard story as `sym-tbl.c:76`: works fine under `-O0`
because GCC is conservative there; could miscompile at `-O2`
if the optimizer uses "no signed overflow" assumptions.  Not
currently causing user-visible breakage.

Has the added wrinkle that hex constants in test programs are
common (MIPS address-space layout is full of high-bit values),
so this UB site is exercised by basically every realistic
program.

## Effort

Trivial — change the type of one local variable, possibly cast
once at the output.
