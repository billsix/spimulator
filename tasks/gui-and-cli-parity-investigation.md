# Investigate a GUI front end with every command-line feature, and an imgui view of memory

**Status:** proposed — investigate first (needs go-ahead)
**Priority:** 7
**Difficulty:** 8
**Created:** 2026-11-14 (William Emerison Six <billsix@gmail.com>)

## BLUF

Investigate how spimulator could be driven either from the command line or from a GUI, with every
command-line feature available in the GUI. The GUI should load an assembly file, step through the
assembler's phases while showing the memory space, then run and step the program. Once the AST
exists, an imgui view should show the memory space graphically and what is added to it when. The
deliverable is an investigation: the options, a recommended architecture, and a follow-on plan.

## Context

- Today the simulator is text only: the REPL in `src/spim.c`, the flags in
  `Documentation/spimulator.1`. The old PCSpim/QtSpim GUIs exist only as PDFs under
  `Documentation/`.
- The assembler already reports its steps as events (`src/assembler-event.c`, `asm_event` with
  label definitions, text instructions and data bytes), which is a natural feed for a "what is added
  when" view. The parse goes through an AST (`tasks/reference/parser-ast-emit.md`).
- Teaching mode (`-explain`, `src/explain.c`) narrates execution; a GUI would want the same facts.
- imgui is used elsewhere by the maintainer (github.com/billsix/towersofhanoi's C++ subproject vendors
  it, with an `update-imgui` target); for C there are cimgui-style bindings.
- Engine ports must match the C engine's behaviour (`port/README.md`); a GUI that only reads state
  should not affect them.

## Questions to answer in the investigation

- Front-end shape: imgui in-process (C, linking cimgui), or a separate process speaking to the engine
  over a protocol, so the ports could be driven too?
- Which engine state needs a read API (segments, symbol table, registers, the event stream) and
  what the REPL already exposes.
- What "step through the phases" means concretely for this assembler (scan, parse, AST, emit,
  relocation) and where each can pause.
