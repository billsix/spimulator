; tree-sitter highlights query for MIPS assembly, spim flavour.
;
; Capture names follow the conventions documented at
; https://tree-sitter.github.io/tree-sitter/3-syntax-highlighting.html
; so editors that consume tree-sitter queries (Helix, Neovim, Zed,
; Emacs treesit) pick up sensible defaults without per-editor
; per-language config.

; --- Lexical ---

(comment) @comment
(string)  @string
(integer) @number
(fp_literal) @number.float

; --- Identifiers and labels ---

(label_def name: (identifier) @function)

; Address operands that name a label use the same highlight as
; label definitions, so jumps "look like" they're going somewhere
; you can navigate to.
(label_ref (identifier) @function.call)

(constant_def name: (identifier) @constant)

; Other identifiers in expressions (label uses, symbol references).
(identifier) @variable

; --- Registers ---

(register)    @variable.parameter
(fp_register) @variable.parameter

; --- Opcodes and directives ---

(opcode) @keyword.function

(directive
  [ ".alias" ".align" ".ascii" ".asciiz" ".asm0" ".bgnb" ".byte"
    ".comm" ".data" ".double" ".end" ".endb" ".endr" ".ent" ".err"
    ".extern" ".file" ".float" ".fmask" ".frame" ".global" ".globl"
    ".half" ".kdata" ".ktext" ".label" ".lcomm" ".livereg" ".loc"
    ".mask" ".noalias" ".option" ".rdata" ".repeat" ".sdata" ".set"
    ".space" ".struct" ".text" ".verstamp" ".vreg" ".word" ] @keyword.directive)

; --- Punctuation ---

[ "," ":" "(" ")" "+" "-" "=" ] @punctuation.delimiter
