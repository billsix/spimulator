# C23 modernization plan

Audit + plan for upgrading the spimulator source to take fuller
advantage of C23.  The project is already on `c_std=gnu23` (GCC
16.1.1), so this is about consuming the features we've already
opted into — not raising the standard.

## What's already done

A scan of the tree before writing this plan showed:

- `nullptr` is the universal null pointer (~250 sites; only 4
  surviving `NULL` references are in comments or macro
  parameter names, none in code).
- `bool`/`true`/`false` are used as keywords; no `<stdbool.h>`
  inclusion anywhere.
- `<stdint.h>` types are pervasive (`int32_t`, `uint32_t`,
  `mem_addr` typedef'd to `uint32_t`).  The `int32`/`uint32`
  typedefs are gone (Phase 3 portability cleanup, May 2026).
- `static_assert` is used as a keyword once (`include/mem.h:19`,
  guarding `sizeof(mem_word) == BYTES_PER_WORD`).
- `[[nodiscard]]` attaches to three functions:
  `read_assembly_file`, `parse_file`, `lookup_label`.
- The portability cleanup also removed all `_MSC_VER` / Windows
  / IRIX shims, so the codebase is purely Unix + modern GCC.

The remaining surface is C23 features we haven't yet pulled in
where they'd improve type safety, expressiveness, or readability.

## High-value categories (rough order of leverage)

### 1. `[[noreturn]]` for `_Noreturn`

**What changes:** `_Noreturn void run_error(char* fmt, ...);`
becomes `[[noreturn]] void run_error(char* fmt, ...);`.

**Why it's better:** `_Noreturn` is the C11 keyword form;
`[[noreturn]]` is the C23 attribute form.  Functionally
identical — both tell the compiler "this function never
returns to its caller," which lets the compiler (a) skip
return-path cleanup at the call site, (b) suppress "control
reaches end of non-void function" warnings after a call,
(c) detect dead code following the call.  The keyword is
deprecated in C23; the attribute matches the style we already
use for `[[nodiscard]]`.

Sites:

- `include/spim.h:180` — `fatal_error`
- `src/spim.c:667` — `top_level`
- `src/spim.c:793` — `control_c_seen`
- `src/spim.c:1439` — `run_error`

Also: `run_error`'s declaration in `spim.h:184` is missing
`_Noreturn` (the definition has it).  Add `[[noreturn]]` to
the declaration too — having the attribute only on the
definition means the compiler doesn't know about no-return at
call sites in other TUs.

**Effort:** trivial.  **Risk:** none.  Pure mechanical rename.

### 2. `enum E : T` — give enums explicit underlying types

**What changes:** `typedef enum { AST_INST_R, ... } ast_kind;`
becomes `typedef enum : uint8_t { AST_INST_R, ... } ast_kind;`
(width picked per-enum — see the table below).

**Why it's better:** In C without an explicit underlying type,
the compiler picks an integer type large enough to hold all
the enumerator values — `int`, `unsigned`, `long`, possibly
something else.  Two compilers (or two compilation flags) can
pick differently, which means `sizeof(ast_kind)` and the
signedness of its values can drift across builds.  Pinning
the underlying type makes both deterministic.  Side benefit:
explicitly typed enums can be forward-declared (`enum ast_kind
: uint8_t;`), useful if a future refactor wants to break a
header cycle.

The existing typedef enums all benefit.  Use sized types
from `<stdint.h>` (matches the Phase 3 portability convention
where `int32`/`uint32` got swapped for stdint types) — pick
the smallest width that fits *and* matches how the value is
stored downstream:

| File / line | Enum | Suggested underlying type | Reason |
|---|---|---|---|
| `include/parser.h:29` | `parse_mode_t` | `uint8_t` | 2 values |
| `include/ast.h:43` | `ast_kind` | `uint8_t` | ~23 values |
| `include/ast.h:85` | `ast_label_kind` | `uint8_t` | 2 values |
| `include/asm_event.h:20` | `asm_event_kind` | `uint8_t` | 11 values |
| `include/asm_event.h:34` | `asm_segment` | `uint8_t` | 4 values |

Picking `uint8_t` here shrinks the discriminator field in
each AST node and asm_event struct from 4 bytes to 1 byte
— meaningful since both types are heap-allocated in
quantity.

Plus: there's a tag-less file-scope enum in
`include/tokens.h` (~389 token names, populated by X-macro
from `op.h`).  Give this one `int32_t` rather than a narrower
type: token values start at 256 and tokens flow through
`int`-typed variables everywhere (scanner return values,
parser state).  A narrower underlying type would force
sign-extension noise at every use site for no real win.

```c
typedef enum : uint8_t { AST_INST_R, AST_INST_R_SHIFT, ... } ast_kind;
typedef enum : int32_t { TOK_EOF = 256, TOK_NL, ... } /*anonymous*/;
```

**Effort:** small.  **Risk:** none — ABI of these enums isn't
exposed across compilation boundaries we care about.

### 3. Bare `#define` integer macros → `enum E : T { ... }`

**What changes:** A cluster like
```c
#define BC_TYPE_INST   10
#define B1_TYPE_INST   11
#define I1s_TYPE_INST  12
// ... 20 more
```
becomes
```c
typedef enum op_type : uint8_t {
    BC_TYPE_INST  = 10,
    B1_TYPE_INST  = 11,
    I1s_TYPE_INST = 12,
    // ...
} op_type;
```
plus the consumers (struct fields, function parameters) get
typed as `op_type` instead of `int`.

Underlying-type choice: `uint8_t` for `op_type` (values 0-42,
fits in a byte) and `mips_exc_code` (values 0-30).  Use sized
stdint types throughout the enum sweep — see §2 for the
rationale.

**Why it's better:** Three concrete wins:

1. **`-Wswitch` coverage.**  Once `inst->type` is `op_type`
   rather than `int`, GCC warns if a `switch (inst->type)`
   forgets a case for any enumerator.  Today, adding a new
   instruction type without a corresponding case is a silent
   fall-through bug; with the enum, it's a compile error.
2. **Debugger visibility.**  `print inst->type` shows
   `BC_TYPE_INST` instead of `10`.  Macro names are gone by
   the time the debugger sees the binary; enum names survive.
3. **Symbol existence.**  Enumerators are real entries in the
   compiler's symbol table — visible to nm, objdump, IDE
   "find references," etc.  `#define` names exist only during
   preprocessing.

**Criterion for enum vs. constexpr** (see §4 for the other side):
prefer `enum E : T` for *disjoint sets of named values that get
switched over*, where you'd benefit from `-Wswitch` coverage.
Prefer `constexpr` for *individually-meaningful typed constants*
even if they happen to come in a family — especially if the set
has aliased values (e.g. `REG_V0 == REG_RES`), or if the values
flow through plain-int APIs and an enum type would force casts
at every boundary.

- **`include/op.h:18-49`** — the type-tag block: `ASM_DIR`,
  `PSEUDO_OP`, `BC_TYPE_INST` … `NOARG_TYPE_INST` (~25 values
  scattered across 10-49).  Used in the parser/explain dispatch
  switches.  Make this `enum op_type : uint8_t { ... }`.  These
  values are matched in switches in inst.c, explain.c — making
  them an enum lights up `-Wswitch` coverage warnings, which
  is genuinely useful for a project where new instructions get
  added.

- **`include/inst.h:156-175`** — `ExcCode_Int` ... `ExcCode_CacheErr`
  (15 named values, mostly contiguous 0-30).  Promote to
  `enum mips_exc_code : uint8_t { ... }`.  Used in
  `RAISE_EXCEPTION(EXCODE, ...)` and as an argument to
  `raise_exception(int excode)` — change the signature too
  (`void raise_exception(mips_exc_code excode);`).

Some `#define`s should **stay** as macros because they're
preprocessor-sensitive — e.g. `EXCEPTION_ADDR` selects between
`#ifdef MIPS1` branches at `include/spim.h:131-137`.

Pure flag-bit families (`COND_UN/EQ/LT/IN`, `CP0_Status_*`,
`CP0_Cause_*`, `FCSR_*`) might look like enum candidates but
are only ever bitwise-OR'd and AND-masked, never switched —
they belong in §4 with the other typed constants.

**Effort:** moderate.  **Risk:** the type-tag block is hit by
many switches; converting it changes some warning surfaces.
That's the *point*, but expect to fix a handful of legitimately
missing-`default:` warnings while doing it.

### 4. `constexpr` for typed integer/address constants

**What changes:** `#define TEXT_BOT ((mem_addr)0x400000)`
becomes `constexpr mem_addr TEXT_BOT = 0x400000;`.

**Why it's better:** `#define` does *textual* substitution
during preprocessing — there's no type information attached
to the name.  Wherever `TEXT_BOT` appears, the preprocessor
just splices in `((mem_addr)0x400000)`, and the cast inside
the macro body is the only thing keeping the type sensible.
With `constexpr`:

- **Type is declared and enforced.**  The compiler knows
  `TEXT_BOT` is `mem_addr` (i.e. `uint32_t`).  Passing it to
  a function expecting `int` warns.  Passing the macro
  version silently coerces.
- **It's an integer constant expression.**  Usable as an
  array size (`int buf[R_LENGTH]`), case label, static_assert
  argument, bitfield width — every place `#define` worked.
- **The name exists at runtime for tools.**  Debuggers,
  symbol-table inspectors, IDEs all see `TEXT_BOT`.  Macros
  vanish at preprocessing.
- **No macro hygiene pitfalls.**  `#define K 1024` followed
  by someone writing `int K = 5;` in a function gives a
  bizarre error message because `K` got substituted before
  the compiler ever saw the declaration.  Constexpr objects
  follow normal scope rules.

At file scope, a `constexpr` object has internal linkage and
static storage duration in C23 (similar to a `static const`
in C++).  That means putting `constexpr int R_LENGTH = 32;`
in a header is one-definition-per-TU; safe.

The trade-off: `constexpr` doesn't participate in `#if` /
`#ifdef`.  Anything tested at preprocessing time (e.g.
`EXCEPTION_ADDR` which is conditional on `#ifdef MIPS1`) has
to stay a macro.

High-value targets:

- **`include/spim.h`** — `K`, `BYTES_PER_WORD`, `TEXT_SIZE`,
  `K_TEXT_SIZE`, `DATA_SIZE`, `DATA_LIMIT`, `K_DATA_SIZE`,
  `K_DATA_LIMIT`, `STACK_SIZE`, `STACK_LIMIT`,
  `SMALL_DATA_SEG_MAX_SIZE`, `DEFAULT_RUN_STEPS`,
  `IO_INTERVAL`, `RECV_INTERVAL`, `TRANS_LATENCY`,
  `TIMER_TICK_MS`, `DIRECT_MAPPED`, `TWO_WAY_SET`.

  Caveat: each of these is wrapped in `#ifndef X / #define X /
  #endif`, allowing override from the meson build.  C23
  constexpr can't be conditionally redefined the same way.
  Decide per-knob whether the override path is actually used
  — if not (most likely), drop the guard and convert.

- **`include/mem.h`** — `TEXT_BOT`, `DATA_BOT`, `STACK_TOP`,
  `K_TEXT_BOT`, `K_DATA_BOT`, `MM_IO_BOT`, `MM_IO_TOP`,
  `RECV_CTRL_ADDR`, `RECV_BUFFER_ADDR`, `TRANS_CTRL_ADDR`,
  `TRANS_BUFFER_ADDR`, `RECV_READY`, `RECV_INT_ENABLE`,
  `RECV_INT_LEVEL`, `TRANS_READY`, `TRANS_INT_ENABLE`,
  `TRANS_INT_LEVEL`, `TEXT_CHUNK_SIZE`.

  These are all clear `constexpr mem_addr X = ...;` —
  type-checked addresses instead of `(mem_addr)0x80000000`
  casts in the macro body.

- **`include/inst.h`** — `IMM_MIN`, `IMM_MAX`, `UIMM_MIN`,
  `UIMM_MAX`.  These currently use mixed bare-int and
  `(unsigned)` casts; constexpr fixes the type story
  (`constexpr int32_t IMM_MIN = -0x8000;` is much cleaner
  than `#define IMM_MIN 0xffff8000`).

- **`include/reg.h`** — `R_LENGTH = 32`, `FGR_LENGTH = 32`,
  `FPR_LENGTH = 16`.  Used as array sizes
  (`extern reg_word R[R_LENGTH];`) — `constexpr` objects are
  ICEs, so this works.

- **`include/reg.h:27-43`** — register-number constants:
  `REG_V0`, `REG_A0`...`REG_FA0`, `REG_SP`, `REG_GP`, `REG_RES`,
  `REG_FRES`.  `constexpr int REG_SP = 29;` and friends.
  Looks like a clustered named set that wants an enum, but
  constexpr is the better fit here for two reasons: (a) the set
  isn't disjoint — `REG_V0 = REG_RES = 2`, `REG_FRES = 0`
  collides with `$zero`, so an enum loses its "named partition
  of a domain" benefit; (b) register numbers flow through
  int-typed APIs everywhere (`r_type_inst(int rs, int rt, ...)`,
  `R[RS(inst)]`), so wrapping them in `enum mips_reg` either
  forces casts at every use or relies on implicit conversion
  that gains nothing.

- **`include/reg.h:CP0_*_Reg` / `FIR_REG` / `FCSR_REG`** —
  coprocessor-0 and FP control register numeric selectors
  (`CP0_BadVAddr_Reg = 8`, `CP0_Count_Reg = 9`, etc.).  Same
  reasoning as `REG_SP` — used as `CPR[0][CP0_BadVAddr_Reg]`
  indices.  `constexpr int`.

- **`include/inst.h:119-122`** — `COND_UN = 0x1`,
  `COND_EQ = 0x2`, `COND_LT = 0x4`, `COND_IN = 0x8`.  FP
  comparison flag bits, used via OR-to-combine and AND-mask
  to test.  Never switched.  Looks like an enum (4 named
  flag values from one domain) but constexpr is more honest
  given the bitwise usage pattern — `enum fp_cond_flags`
  wouldn't catch the "OR two flags → not an enumerator"
  case that's the *correct* use anyway.

- **`include/reg.h:CP0_Status_*` / `CP0_Cause_*` / `FCSR_*`** —
  the larger version of the same story.  ~30 bitmask values
  (`CP0_Status_CU = 0xf0000000`, `CP0_Cause_BD = 0x80000000`,
  `FCSR_FCC = 0xfe800000`, ...).  Used exclusively via
  `(CP0_Status & CP0_Status_IE)`-style masking.  Choose
  `constexpr uint32_t` so the bit-31 values fit unsigned.

  Bonus: the composite masks (`CP0_Status_Mask`, `CP0_Cause_Mask`,
  `FIR_MASK`, `FCSR_MASK`) currently `#define` themselves as
  the OR of earlier `#define`s.  Constexpr handles this
  cleanly — a constexpr's initializer can reference earlier
  constexpr values:

  ```c
  constexpr uint32_t CP0_Status_CU  = 0xf0000000;
  constexpr uint32_t CP0_Status_UM  = 0x00000010;
  // ...
  constexpr uint32_t CP0_Status_Mask =
      CP0_Status_CU | CP0_Status_UM | CP0_Status_IM |
      CP0_Status_EXL | CP0_Status_IE;
  ```

**Effort:** larger.  **Risk:** main risk is the `#ifndef X`
override guards; need to check meson.build and the Dockerfile
to see if anyone actually overrides these.  If not, drop the
guards.  If so, find a different mechanism (or keep those as
`#define`).

### 5. Function-like macros → `static inline` functions

**What changes:** `#define MIN(A, B) ((A) < (B) ? (A) : (B))`
becomes either a `static inline` function in the header, or
a `typeof`-based macro that captures arguments first.

**The bug this fixes:** Macros expand by textual substitution.
`MIN(rand(), x)` becomes `((rand()) < (x) ? (rand()) : (x))`.
The compiler then evaluates `rand()` *twice* — once for the
comparison, once for the result.  Each call returns a
different value, so you get back something that wasn't even
in the original input.  Same hazard for `MIN(i++, j)`,
`MIN(*p++, x)`, anything with side effects.

Before / after:

```c
// BEFORE — macro
#define MIN(A, B) ((A) < (B) ? (A) : (B))
int x = MIN(rand(), 100);   // rand() called twice!

// AFTER, option 1 — static inline function (type-specific)
static inline int min_i(int a, int b) { return a < b ? a : b; }
int x = min_i(rand(), 100); // rand() called once

// AFTER, option 2 — typeof macro (polymorphic, C23-standardized)
#define MIN(A, B) ({ typeof(A) _a = (A); \
                     typeof(B) _b = (B); \
                     _a < _b ? _a : _b; })
int x = MIN(rand(), 100);   // expands to { int _a = rand(); int _b = 100; ... }
                            // rand() called once; works for any type
```

**Other wins (besides the double-eval fix):**

- **Type checking.**  A function signature constrains
  arguments.  `min_i("hello", 3)` is a compile error;
  `MIN("hello", 3)` is not.
- **Debugger steps in.**  You can step into an inline
  function and inspect locals.  Macros expand inline and are
  invisible to the debugger.
- **Error messages point at the right place.**  A type error
  inside a `static inline` reports the function's source
  line.  A macro error reports the call site with the full
  expanded gibberish.

Targets:

- `MIN(A, B)`, `MAX(A, B)` in `include/spim.h:46-47` — use
  the `typeof` form to keep them polymorphic across `int` /
  `mem_addr` / `size_t`.
- `ROUND_UP(V, B)`, `ROUND_DOWN(V, B)` in `include/spim.h:33-34`
  — these multi-evaluate `B`; same fix.
- `SIGN_EX(X)` in `include/spim.h:38` — already a single cast
  expression, no multi-eval, but still worth wrapping for the
  type-checking win.
- `streq(s1, s2)` in `include/spim.h:30` — currently
  `!strcmp(...)`, which reads backwards.  An inline `static
  inline bool streq(const char* a, const char* b) { return
  strcmp(a, b) == 0; }` is clearer.

There are 65 use sites across `src/*.c`.  A spot-check
suggests arguments are almost always simple `int` variables,
so the double-eval hazard is mostly hypothetical *in current
code* — but future code added to a project that uses
side-effect-bearing arguments would hit it.  Fix once.

Why these can't be `constexpr` instead: C23's `constexpr`
applies to objects, not functions.  There's no way to make
`MIN(3, 5)` an integer constant expression in a generic way.
Where you need ICE-ness with no arguments (e.g. an array
size), use a `constexpr` object directly.

**Effort:** small per-macro, moderate aggregate.
**Risk:** if any current call site relies on the macro
accepting two different types (e.g. `MIN(int, mem_addr)`),
the inline-fn signature has to pick one; the `typeof`-macro
preserves the existing behavior.

### 6. Control-flow macros — keep, but wrap in `do { ... } while (0)`

**Why these can't be functions:** `RAISE_EXCEPTION`,
`RAISE_INTERRUPT`, `CLEAR_INTERRUPT`, `BRANCH_INST`,
`JUMP_INST`, `LOAD_INST`, `LOAD_INST_BASE`,
`DO_DELAYED_UPDATE` all embed `break`, `return`, or `if`
statements in their body that target the enclosing loop /
switch / function.  A function can only `return` from
itself, not break out of its caller's loop.  These genuinely
need to stay macros.

**The cosmetic fix:** the macros in `include/inst.h:136-152`
currently expand to a bare brace block:

```c
#define RAISE_EXCEPTION(EXCODE, MISC) \
  {                                   \
    raise_exception(EXCODE);          \
    MISC;                             \
  }
```

That's a foot-gun in if/else chains:

```c
if (overflow) RAISE_EXCEPTION(ExcCode_Ov, break);
else continue;
```

expands to:

```c
if (overflow) { raise_exception(ExcCode_Ov); break; };  // <-- stray ;
else continue;                                          // <-- error: 'else' without matching 'if'
```

The trailing semicolon at the call site becomes a null
statement after the `}`, which terminates the `if`'s
controlled statement.  The `else` is now orphaned.

`do { ... } while (0)` consumes the trailing semicolon as
part of its own syntax and behaves like a single statement
in every context.  Costs zero at runtime — the compiler
eliminates the loop — and the if/else chain stays intact.
Standard C idiom since the 1980s; not C23-specific, just
overdue.

**Important exception (found during Phase 4 work):**
`RAISE_EXCEPTION(EXCODE, MISC)` *cannot* be wrapped in
`do { } while (0)`.  Its `MISC` parameter is a statement
injected at the call site's enclosing scope, often
`break;` (to terminate a switch case in `run_spim`) or
`return true;` (early return).  If the macro becomes a
do-while, `break` would scope to the do-while loop instead
of the enclosing switch, silently leaving the case body
running.  That's exactly the kind of bug the wrap is
supposed to prevent — but for this macro the bare-brace
design *is* correct.  Recorded in a comment at the macro
definition.

The wrap-safe macros are:

- `include/inst.h`: `RAISE_INTERRUPT`, `CLEAR_INTERRUPT`
- `src/run.c`: `BRANCH_INST`, `JUMP_INST`, `LOAD_INST`,
  `LOAD_INST_BASE`, `DO_DELAYED_UPDATE`

`DO_DELAYED_UPDATE` was previously a bare `if () { }`
without even the surrounding braces — the most dangerous
form, and the one that benefits most from the wrap.

A future cleanup worth considering: eliminate
`RAISE_EXCEPTION` entirely and have call sites write
`raise_exception(X); break;` directly.  The macro adds no
abstraction beyond textual brevity, and the explicit form
reads cleaner in modern C.  21 call sites in `run.c`;
mechanical replacement.  Defer until after Phase 6
(enum-tag sweep) so the type-checking on the exception
code argument lands at the same time.

### 7. `[[fallthrough]]` annotation in switches

**The bug class this fixes:** C's switch statement falls
through from one case to the next unless a `break` (or
`return`, `continue`, `goto`) breaks the chain.  Forgetting
the `break` is one of C's classic bug shapes — the code
silently flows into the next case's body, executing both.

```c
case TOK_SLLV_OP:
  R[RD(inst)] = R[RT(inst)] << (R[RS(inst)] & 0x1f);
  // <-- did the author mean to break here?  hard to tell.
case TOK_SRA_OP:
  R[RD(inst)] = ((int32_t)R[RT(inst)]) >> SHAMT(inst);
  break;
```

The compiler can't distinguish "intentional fallthrough" from
"forgotten break" without help.

**What changes:** Enable `-Wimplicit-fallthrough` in
`meson.build`.  Then for every case that *intentionally*
flows into the next, add a `[[fallthrough]];` annotation:

```c
case TOK_SLLV_OP:
  R[RD(inst)] = R[RT(inst)] << (R[RS(inst)] & 0x1f);
  [[fallthrough]];   // explicit: we mean to flow into TOK_SRA_OP
case TOK_SRA_OP:
  R[RD(inst)] = ((int32_t)R[RT(inst)]) >> SHAMT(inst);
  break;
```

For cases that were *unintentionally* missing a `break`, add
the `break` (this is the bug we just found).

Empty case-label stacking is unaffected — it doesn't count
as fallthrough:

```c
case TOK_BC2F_OP:           // <-- no statements, no warning
case TOK_BC2FL_OP:          // <-- no statements, no warning
case TOK_BC2T_OP:
case TOK_BC2TL_OP:
  RAISE_EXCEPTION(ExcCode_CpU, {});
  break;
```

**Why it's better:** Two outcomes from the sweep, both wins:

- Annotated intentional fallthroughs → future readers see the
  intent without having to reason about whether the missing
  `break` is a bug.
- Discovered unintentional fallthroughs → real bug fixes.

Plus a permanent benefit: with the warning on, any *future*
forgotten-break is a compile-time error instead of a
runtime surprise.

There are 43 switch statements in the tree; grepping for
existing fallthrough comments finds only two.  Reading each
switch case carefully is unavoidable, but most are trivially
either "breaks at end" or "no statements."

**Effort:** small, but requires reading every switch
carefully.  **Risk:** could surface real bugs (good); no
behavior change in already-correct code.

### 8. `unreachable()` for impossible default cases

**What it is:** C23 adds `unreachable()` (declared in
`<stddef.h>`) — a macro you call to tell the compiler
"control will never reach this point."  If execution somehow
does reach it anyway, behavior is undefined.

**What changes:** A `default:` case that handles a
"this can never happen" state currently looks something like:

```c
switch (ast->kind) {
  case AST_INST_R:    /* handle */ break;
  case AST_INST_I:    /* handle */ break;
  // ... all 23 enumerators ...
  default:
    fprintf(stderr, "ast_print: unknown kind %d\n", ast->kind);
    break;
}
```

Becomes either (a) drop the default entirely, relying on
`-Wswitch` to verify coverage at compile time:

```c
switch (ast->kind) {
  case AST_INST_R: /* handle */ break;
  // ... all 23 enumerators ...
}
// if a future enumerator gets added and isn't handled, the
// compiler warns now — no need for a default branch
```

Or (b) keep the default and mark it impossible:

```c
default:
  unreachable();   // promises the compiler this branch is dead
```

**Why it's better:** Three concrete things `unreachable()`
buys you:

1. **Smaller code.**  The compiler skips generating any
   instructions for the unreachable path — no fallback
   handler code, no setup for unused locals.
2. **Better optimization downstream.**  If a value flows
   into the switch and 23 of 24 possible values are handled,
   `unreachable()` on the 24th promises the compiler that
   only the 23 are real — which can elide range checks,
   simplify control flow, and improve register allocation
   in code after the switch.
3. **Contract documentation.**  `unreachable()` reads as
   "I, the human, assert this is impossible" — which is
   exactly the invariant that's otherwise implicit and
   un-checked.  Combined with `-Wswitch` coverage, the enum
   type system actually enforces it.

**Why it's dangerous:** It's a *lie* if you're wrong.  If
execution actually reaches `unreachable()`, the compiler
has already optimized away the surrounding code based on
"this can't happen," and you can get arbitrarily bad
behavior — code paths the source doesn't even describe,
variables that should alias suddenly diverging, etc.

The current 19 `default:` cases break down roughly:

- Some handle "unrecognized opcode from a binary file" —
  legitimately reachable for adversarial input; leave alone.
- Some are "all enum values covered, just here to silence
  `-Wswitch-default`" — these are candidates for
  `unreachable()` *after* the §3 enum-tag sweep makes
  `-Wswitch` coverage real.
- Some print a diagnostic and continue — depends on whether
  the input domain is internal (impossible) or external
  (possible); audit each.

Conservative bias: only annotate cases where the type
system already guarantees impossibility (an exhaustive
switch on a `enum E : uint8_t` (or similar sized type) with
`-Werror=switch` enabled).
Anywhere the input domain is wider than the type, leave
the existing handler.

**Effort:** small.  **Risk:** misjudging "impossible"
leaves a UB landmine.  Pair with §3 (the enum-tag sweep) so
the type system carries the weight.

### 9. Wider `[[nodiscard]]` adoption

**What changes:** Functions where ignoring the return value
is almost certainly a bug get marked `[[nodiscard]]`:

```c
[[nodiscard]] ast_node* ast_make_inst_r(int op, int rd,
                                        int rs, int rt);
```

If a caller writes `ast_make_inst_r(...)` and throws away
the result, the compiler warns — and since `ast_make_*`
returns a freshly heap-allocated node, throwing it away is
a guaranteed memory leak.

**Why it's better:** The attribute encodes the contract
("you must use this") in the type system instead of in a
comment or convention.  Three existing `[[nodiscard]]`
markers already in the tree show the pattern works; this is
a sweep to find every function that deserves the same.

Audit candidates (not exhaustive):

- `make_imm_expr`, `const_imm_expr`, `incr_expr_offset`,
  `copy_inst`, `inst_decode`, `make_addr_expr`,
  `set_breakpoint`, `make_label`, `lookup_label`, `record_label`
- AST constructors `ast_make_*` (40+ of them in `include/ast.h`)
  — discarding the result leaks the node.

Discoverable systematically: any function that returns a
heap-allocated pointer.

**Effort:** medium.  **Risk:** wave of warnings on commit; fix
them all in the same pass, don't merge a partial sweep.

### 10. `<stdbit.h>` for count-leading-zeros/ones

**What changes:** `src/run.c:332-348` implements `TOK_CLO_OP`
and `TOK_CLZ_OP` (the MIPS count-leading-ones /
count-leading-zeros instructions) with a hand-rolled loop:

```c
// BEFORE
case TOK_CLZ_OP: {
  reg_word val = R[RS(inst)];
  int i;
  for (i = 31; 0 <= i; i -= 1)
    if (((val >> i) & 0x1) == 1) break;
  R[RD(inst)] = 31 - i;
  break;
}
```

C23's `<stdbit.h>` adds `stdc_leading_zeros_ui32` and
`stdc_leading_ones_ui32`:

```c
// AFTER
#include <stdbit.h>
case TOK_CLZ_OP:
  R[RD(inst)] = stdc_leading_zeros_ui32(R[RS(inst)]);
  break;
```

**Why it's better:** Reads as a one-liner that names the
operation.  Compiles to a single `lzcnt` (x86-64) or `clz`
(ARM64) instruction — the loop version is 8-10 instructions
even with good optimization, because the early-exit
control flow stops the compiler from recognizing the
idiom.  Same architectural behavior; faster and clearer.

**Effort:** trivial (2 cases).  **Risk:** none.

### 11. `<stdckdint.h>` for overflow checks

**What changes:** `src/run.c` currently detects signed
overflow on MIPS `add` / `addi` / `sub` / `subi` like this:

```c
// BEFORE  (src/run.c:41 and case TOK_ADD_OP around :202)
#define ARITH_OVFL(RESULT, OP1, OP2) \
  (SIGN_BIT(OP1) == SIGN_BIT(OP2) && SIGN_BIT(OP1) != SIGN_BIT(RESULT))

case TOK_ADD_OP: {
  reg_word vs = R[RS(inst)], vt = R[RT(inst)];
  reg_word sum = vs + vt;   // <-- wrapping add; UB on signed
  if (ARITH_OVFL(sum, vs, vt)) RAISE_EXCEPTION(ExcCode_Ov, break);
  R[RD(inst)] = sum;
  break;
}
```

C23's `<stdckdint.h>` provides `ckd_add` / `ckd_sub` /
`ckd_mul`, which perform the operation and report overflow
together:

```c
// AFTER
#include <stdckdint.h>
case TOK_ADD_OP: {
  reg_word vs = R[RS(inst)], vt = R[RT(inst)], sum;
  if (ckd_add(&sum, vs, vt)) RAISE_EXCEPTION(ExcCode_Ov, break);
  R[RD(inst)] = sum;
  break;
}
```

**Why it's better:**

1. **Closes a UB hole.**  Signed integer overflow is
   undefined behavior in C.  The current macro performs the
   wrapping add *first*, then checks the sign bits — but at
   `-O2`, GCC is allowed to assume overflow didn't happen
   and optimize the check away.  `ckd_add` is *defined* to
   detect overflow without invoking UB.
2. **One concept, one call.**  "Add and tell me if it
   overflowed" is what the operation actually is.  The
   sign-bit-comparison macro is an implementation trick.
3. **No reliance on `SIGN_BIT`.**  One less macro for a
   future maintainer to discover.

Sites: `TOK_ADD_OP` at run.c:202, `TOK_ADDI_OP` at run.c:211,
`TOK_SUB_OP` and `TOK_SUBI_OP` analogs further down. ~6 use
sites total.

**Effort:** small.  **Risk:** the careful part is verifying
that `ckd_add` matches MIPS `add` overflow semantics exactly
(signed overflow is the trap condition; unsigned wrap is
not).  MIPS `addu` / `subu` are *unsigned*-named but
actually mean "don't trap on overflow"; they should keep
the bare `+` / `-` operators with no overflow check.

### 12. `#embed` for the default exceptions.s

Speculative.  `src/exceptions.s` (the default exception
handler) is installed alongside the binary and located at
runtime via a compiled-in path (`DEFAULT_EXCEPTION_HANDLER`
in config.h).  C23 `#embed` would let us baked it into the
binary itself:

```c
static const char default_exceptions_s[] = {
#embed "exceptions.s"
};
```

Removes the runtime file-find dance and the install-path
bootstrapping (which has bitten people in container builds —
see SESSION_NOTES handoff about `-exception_file` being
required when running uninstalled).

**Effort:** small to medium.  **Risk:** changes user-visible
behavior; `-exception_file FOO` still works (and overrides),
but the default no longer hits the filesystem.  Probably want
to retain filesystem fallback as an environment-variable
override for the "I edited exceptions.s, please pick up my
edit" workflow.

Not pre-authorized — surface for discussion before doing.

### 13. Cosmetic — empty initializers, `alignas`/`alignof` keywords

**What changes:** `T x = {0};` becomes `T x = {};`.

**Why it's better:** Both forms zero-initialize every member
of `x`.  The C23 empty-initializer form makes the intent
explicit ("default-initialize everything") without the
slightly-misleading `0` literal — which especially helps
when the first member isn't a scalar where `0` is a sensible
value (e.g. a struct with a pointer first member: the `{0}`
form was always relying on C's "remaining members are
zero-initialized" rule, not literally setting the first
member to integer 0).

~10 sites, mostly snapshot structs in explain.c and zeroing
buffers in spim.c.

**Separately:** C23 makes `alignas` and `alignof` real
keywords (was `_Alignas`, `_Alignof`).  No current uses in
the tree, but worth preferring the keyword form for any
future alignment work.

**Effort:** trivial.

## Things C23 brings that I'd skip

- **`auto` for type deduction** — niche in C; readability harm
  in a teaching simulator outweighs the saved keystrokes.
- **`_BitInt(N)`** — `int32_t`/`uint32_t` already cover us;
  not worth the disruption.
- **Digit separators (`1'000'000`)** — cosmetic; can introduce
  on a case-by-case basis without a sweep.
- **`u8` / `char8_t`** — no Unicode work in the simulator core.
- **`[[reproducible]]` / `[[unsequenced]]`** — hint attributes;
  experimental enough that fitting them in carefully is more
  work than the optimizer hint is worth.
- **Argument-less function declarations now mean `(void)`** —
  every declaration in the tree already writes `(void)`
  explicitly; no migration needed.

## Suggested ordering

If approved, do one pass per category as a separate commit so
the regression suite can blame any breakage on a specific
change:

1. `[[noreturn]]` — 5 sites, mechanical. (cat 1)
2. `[[fallthrough]]` annotations + enable
   `-Wimplicit-fallthrough`. (cat 7)
3. `[[nodiscard]]` sweep. (cat 9)
4. `do { ... } while (0)` wrap on control-flow macros. (cat 6
   cosmetic)
5. `enum E : T` underlying types on existing typedef enums. (cat 2)
6. `#define` clusters → `enum` (op type tags, ExcCode_*). (cat 3)
7. `constexpr` migration of typed constants. (cat 4)
8. `static inline` for MIN/MAX/ROUND/streq/SIGN_EX. (cat 5)
9. `<stdbit.h>` for CLO/CLZ. (cat 10)
10. `<stdckdint.h>` for arithmetic overflow. (cat 11)
11. Empty initializers / alignas / alignof. (cat 13)
12. `#embed exceptions.s` — *only if approved*. (cat 12)

Each step keeps all 22 regression tests green
(`meson test -C builddir`).  Most steps touch ≤ 20 files; the
constexpr and enum-tag sweeps are the largest.

## Build configuration changes

In `meson.build`, consider adding to `add_project_arguments`:

- `-Wimplicit-fallthrough` — surfaces unannotated fallthroughs.
- `-Wenum-conversion` — surfaces enum/int misuse after the
  `enum E : T` sweep.
- `-Wswitch-enum` — already implied by warning_level=3; verify.
- `-Wmissing-declarations` — ensures `[[nodiscard]]` attributes
  on declarations and definitions stay in sync.

Don't add `-Wconversion` blanket — it's a 1000-warning hose
on this codebase and most are harmless.

## Verification approach

For each commit:

```sh
ninja -C builddir
meson test -C builddir
```

Plus a smoke run to confirm the explain mode still emits
the same line counts:

```sh
./builddir/spimulator -ef src/exceptions.s -explain=2 \
    -f tests/tt.explain.s | wc -l
# expect: ~2972 lines
```

Drift of more than ±5 lines means something in the explain
template chain changed shape unintentionally.

## What's intentionally out of scope

- Switching `c_std` from `gnu23` to plain `c23`.  `gnu23`
  keeps `typeof`-as-statement-expression and a few other GNU
  extensions we already rely on (statement expressions in
  the BRANCH_INST family).
- Touching the AST branch's files (`include/ast.h`,
  `src/ast.c`, `src/asm_event.c`, `src/parser.c`'s AST
  half) — that work is mid-flight in another branch.  When
  the AST branch merges, fold the same C23 sweep into the
  same files.
- Refactoring the X-macro pattern in `op.h`.  It works; it's
  re-includable; leave it alone.
- Removing `intptr_union` from `spim.h` (kept in Phase 5
  cleanup for legitimate reasons — 70 use sites model a sum
  type).
