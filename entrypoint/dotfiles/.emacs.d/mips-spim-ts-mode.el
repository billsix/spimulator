;;; mips-spim-ts-mode.el --- Tree-sitter major mode for spim MIPS assembly -*- lexical-binding: t -*-

;; SPDX-License-Identifier: BSD-3-Clause

;;; Commentary:
;;
;; Provides `mips-spim-ts-mode', a tree-sitter-based major mode for
;; MIPS assembly in the spim dialect.  Uses the grammar shipped
;; alongside the spimulator codebase at /spimulator/tree-sitter/.
;;
;; Requires Emacs 30+ (technically 29+ for treesit, but tested
;; against the Fedora 44 build of 30.2 that ships in the
;; spimulator dev container).
;;
;; Registration: the mode auto-binds to .s / .asm / .mips files.
;; If the grammar shared library isn't present at load time, the
;; mode definition still exists but `mips-spim-ts-mode` will error
;; with a clear "grammar not installed" message.

;;; Code:

(require 'treesit)

(defcustom mips-spim-ts-grammar-name 'mips_spim
  "Tree-sitter language name for the spim MIPS grammar.
Must match the `name:` field in grammar.js."
  :type 'symbol
  :group 'mips-spim)

;; ---------------------------------------------------------------
;; Font-lock rules.
;;
;; Each rule maps tree-sitter node types from grammar.js to Emacs
;; font-lock faces.  Capture names mirror queries/highlights.scm
;; where practical; Emacs's `treesit-font-lock-rules' is a
;; thin sexp wrapper over the query DSL.
;; ---------------------------------------------------------------

(defvar mips-spim-ts--font-lock-settings
  (treesit-font-lock-rules
   :language 'mips_spim
   :feature 'comment
   '((comment) @font-lock-comment-face)

   :language 'mips_spim
   :feature 'string
   '((string) @font-lock-string-face)

   :language 'mips_spim
   :feature 'number
   '((integer) @font-lock-number-face
     (fp_literal) @font-lock-number-face)

   :language 'mips_spim
   :feature 'definition
   '((label_def name: (identifier) @font-lock-function-name-face)
     (constant_def name: (identifier) @font-lock-constant-face))

   :language 'mips_spim
   :feature 'function
   '((label_ref (identifier) @font-lock-function-call-face))

   :language 'mips_spim
   :feature 'keyword
   '((opcode) @font-lock-keyword-face)

   :language 'mips_spim
   :feature 'preprocessor
   `((directive
      [".alias" ".align" ".ascii" ".asciiz" ".asm0" ".bgnb" ".byte"
       ".comm" ".data" ".double" ".end" ".endb" ".endr" ".ent" ".err"
       ".extern" ".file" ".float" ".fmask" ".frame" ".global" ".globl"
       ".half" ".kdata" ".ktext" ".label" ".lcomm" ".livereg" ".loc"
       ".mask" ".noalias" ".option" ".rdata" ".repeat" ".sdata" ".set"
       ".space" ".struct" ".text" ".verstamp" ".vreg" ".word"]
      @font-lock-preprocessor-face))

   :language 'mips_spim
   :feature 'variable
   '((register) @font-lock-variable-use-face
     (fp_register) @font-lock-variable-use-face))
  "Tree-sitter font-lock settings for `mips-spim-ts-mode'.")

;; ---------------------------------------------------------------
;; Completion-at-point: closed sets that the grammar knows about.
;;
;; - Registers (after `$`): ~50 names including $zero, $at, $v0/$v1,
;;   $a0-$a3, $t0-$t9, $s0-$s8, $sp/$gp/$fp/$ra, $k0/$k1, plus the
;;   raw $0-$31 forms and the FP $f0-$f31.  Closed set, no dabbrev
;;   backend can guess these from buffer content.
;; - Directives (after `.`): the 41 ASM_DIRs from opcodes.h.
;; - Opcodes (otherwise at start of statement): ~300 mnemonics.
;;
;; The grammar's keyword surface and the scanner's register table
;; are the source of truth in spim; we mirror them here.  When
;; spim grows new opcodes, update the corresponding list below.
;; ---------------------------------------------------------------

(defconst mips-spim-ts--registers
  (append
   ;; Numeric forms: $0..$31
   (mapcar (lambda (n) (format "$%d" n)) (number-sequence 0 31))
   ;; FP forms: $f0..$f31
   (mapcar (lambda (n) (format "$f%d" n)) (number-sequence 0 31))
   ;; ABI names: matches src/scanner.c register_tbl[].
   '("$zero" "$at" "$v0" "$v1"
     "$a0" "$a1" "$a2" "$a3"
     "$t0" "$t1" "$t2" "$t3" "$t4" "$t5" "$t6" "$t7" "$t8" "$t9"
     "$s0" "$s1" "$s2" "$s3" "$s4" "$s5" "$s6" "$s7" "$s8"
     "$sp" "$gp" "$fp" "$ra"
     "$k0" "$k1" "$kt0" "$kt1"))
  "Complete set of register names spim's scanner recognises.")

(defconst mips-spim-ts--directives
  '(".alias" ".align" ".ascii" ".asciiz" ".asm0" ".bgnb" ".byte"
    ".comm" ".data" ".double" ".end" ".endb" ".endr" ".ent" ".err"
    ".extern" ".file" ".float" ".fmask" ".frame" ".global" ".globl"
    ".half" ".kdata" ".ktext" ".label" ".lcomm" ".livereg" ".loc"
    ".mask" ".noalias" ".option" ".rdata" ".repeat" ".sdata" ".set"
    ".space" ".struct" ".text" ".verstamp" ".vreg" ".word")
  "Assembler directives spim accepts (matches opcodes.h ASM_DIR entries).")

(defconst mips-spim-ts--opcodes
  ;; Real instructions + pseudo-ops from opcodes.h, excluding directives.
  ;; Loosely sorted by frequency-of-use in a teaching context, then
  ;; alphabetical within each tier so the completion popup orders
  ;; useful things first.
  '(;; Common 3-arg arithmetic + logical
    "add" "addu" "sub" "subu" "and" "or" "xor" "nor" "slt" "sltu"
    "addi" "addiu" "andi" "ori" "xori" "slti" "sltiu"
    "mul" "mulo" "mulou" "mult" "multu" "div" "divu" "rem" "remu"
    ;; Shifts
    "sll" "srl" "sra" "sllv" "srlv" "srav" "rol" "ror"
    ;; Loads / stores
    "lw" "lh" "lhu" "lb" "lbu" "lwl" "lwr" "ll"
    "sw" "sh" "sb" "swl" "swr" "sc"
    "ulw" "ulh" "ulhu" "usw" "ush"
    "ld" "sd" "la" "li"
    "lui"
    ;; Branches
    "beq" "bne" "blt" "bltu" "ble" "bleu" "bgt" "bgtu" "bge" "bgeu"
    "beqz" "bnez" "bgez" "bgtz" "blez" "bltz"
    "bgezal" "bltzal" "b" "bal"
    ;; Jumps / calls
    "j" "jal" "jr" "jalr"
    ;; Hi/Lo and moves
    "mfhi" "mflo" "mthi" "mtlo" "move" "movz" "movn" "movf" "movt"
    ;; Compare-and-set pseudos
    "seq" "sne" "sle" "sleu" "sgt" "sgtu" "sge" "sgeu"
    ;; Negation / not
    "neg" "negu" "not" "abs"
    ;; FP arithmetic
    "add.s" "add.d" "sub.s" "sub.d" "mul.s" "mul.d" "div.s" "div.d"
    "abs.s" "abs.d" "neg.s" "neg.d" "mov.s" "mov.d"
    "sqrt.s" "sqrt.d"
    ;; FP compare + branch
    "c.eq.s" "c.eq.d" "c.lt.s" "c.lt.d" "c.le.s" "c.le.d"
    "bc1t" "bc1f" "bc1tl" "bc1fl"
    ;; FP load / store / convert
    "lwc1" "swc1" "ldc1" "sdc1" "l.s" "l.d" "s.s" "s.d" "li.s" "li.d"
    "cvt.s.w" "cvt.d.w" "cvt.s.d" "cvt.d.s" "cvt.w.s" "cvt.w.d"
    "trunc.w.s" "trunc.w.d" "ceil.w.s" "ceil.w.d"
    "floor.w.s" "floor.w.d" "round.w.s" "round.w.d"
    "mfc1" "mtc1" "cfc1" "ctc1" "mfc1.d" "mtc1.d"
    ;; Coprocessor + privileged
    "mfc0" "mtc0" "cfc0" "ctc0" "syscall" "break" "eret" "rfe"
    "tlbp" "tlbr" "tlbwi" "tlbwr" "wait"
    ;; Traps
    "teq" "tne" "tlt" "tltu" "tge" "tgeu"
    "teqi" "tnei" "tlti" "tltiu" "tgei" "tgeiu"
    ;; Count leading
    "clo" "clz"
    ;; nop / ssnop
    "nop" "ssnop")
  "Opcodes + pseudo-ops spim accepts.")

(defun mips-spim-ts--capf ()
  "Completion-at-point for MIPS-spim buffers.
Offers registers after `$', directives after `.', and opcodes
otherwise — closed sets from the grammar / scanner."
  (let* ((line-start (line-beginning-position))
         (bol-to-point (buffer-substring-no-properties line-start (point))))
    (cond
     ;; After `$`: register completion
     ((looking-back "\\$[A-Za-z0-9]*" line-start)
      (let ((start (match-beginning 0)))
        (list start (point) mips-spim-ts--registers
              :annotation-function (lambda (_) " register")
              :exclusive 'no)))
     ;; After `.` at start of line (allowing whitespace): directive completion
     ((and (looking-back "[ \t]*\\.[A-Za-z0-9]*" line-start)
           (string-match-p "\\`[ \t]*\\.[A-Za-z0-9]*\\'" bol-to-point))
      (let ((start (save-excursion (search-backward "."))))
        (list start (point) mips-spim-ts--directives
              :annotation-function (lambda (_) " directive")
              :exclusive 'no)))
     ;; Otherwise, if point is at the start of a word at the beginning
     ;; of a statement (line-start + optional indent), offer opcodes.
     ((looking-back "^[ \t]*[A-Za-z][A-Za-z0-9._]*" line-start)
      (let ((start (save-excursion
                     (skip-chars-backward "A-Za-z0-9._")
                     (point))))
        (list start (point) mips-spim-ts--opcodes
              :annotation-function (lambda (_) " opcode")
              :exclusive 'no))))))

;; ---------------------------------------------------------------
;; Imenu — surface label definitions in the buffer menu.
;; ---------------------------------------------------------------

(defvar mips-spim-ts--imenu-settings
  '(("Label" "\\`label_def\\'" nil nil)
    ("Constant" "\\`constant_def\\'" nil nil))
  "Imenu categories for `mips-spim-ts-mode'.")

;; ---------------------------------------------------------------
;; The mode itself.
;; ---------------------------------------------------------------

;;;###autoload
(define-derived-mode mips-spim-ts-mode prog-mode "MIPS-spim"
  "Major mode for editing MIPS assembly (spim dialect) using tree-sitter."
  :group 'mips-spim

  (unless (treesit-ready-p 'mips_spim)
    (error "Tree-sitter grammar `mips_spim' not available.
Build it from /spimulator/tree-sitter (run `make` there, then place
libtree-sitter-mips_spim.so on Emacs's `treesit-extra-load-path')"))

  (treesit-parser-create 'mips_spim)

  ;; Comments: # to end of line.
  (setq-local comment-start "# ")
  (setq-local comment-end "")
  (setq-local comment-start-skip "#+[ \t]*")

  ;; Font-lock — group features by what's least vs. most expensive
  ;; to render, so users can dial it down with `treesit-font-lock-level'.
  (setq-local treesit-font-lock-settings mips-spim-ts--font-lock-settings)
  (setq-local treesit-font-lock-feature-list
              '((comment string)                   ; level 1
                (keyword preprocessor)             ; level 2
                (definition number)                ; level 3
                (function variable)))              ; level 4

  ;; Imenu — jump to any label in the buffer.
  (setq-local treesit-simple-imenu-settings mips-spim-ts--imenu-settings)

  ;; Completion-at-point: closed sets the grammar knows about.
  (add-hook 'completion-at-point-functions
            #'mips-spim-ts--capf nil 'local)

  (treesit-major-mode-setup))

;;;###autoload
(progn
  (add-to-list 'auto-mode-alist '("\\.s\\'"    . mips-spim-ts-mode))
  (add-to-list 'auto-mode-alist '("\\.asm\\'"  . mips-spim-ts-mode))
  (add-to-list 'auto-mode-alist '("\\.mips\\'" . mips-spim-ts-mode)))

(provide 'mips-spim-ts-mode)
;;; mips-spim-ts-mode.el ends here
