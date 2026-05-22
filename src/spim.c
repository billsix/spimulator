/* SPIM S20 MIPS simulator.
   Terminal interface for SPIM simulator.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#include "config.h"

#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <setjmp.h>
#include <signal.h>
#include <arpa/inet.h>

#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>

#include <stdarg.h>

#include "spim.h"
#include "string-stream.h"
#include "spim-utils.h"
#include "inst.h"
#include "reg.h"
#include "mem.h"
#include "parser.h"
#include "sym-tbl.h"
#include "scanner.h"
#include "tokens.h"
#include "data.h"
#include "explain.h"

#ifdef HAVE_LIBEDIT
#include <editline/readline.h>
#include <stdlib.h>
#include <string.h>
static char history_path[1024] = {0};
static void save_history_at_exit(void) {
  if (history_path[0] != '\0') write_history(history_path);
}

/* Tab-completion at the (spim) prompt.
 *
 * Two completion sources, dispatched by where the cursor is when the
 * student presses Tab:
 *   - Cursor at column 0  -> REPL command names (load, breakpoint, ...)
 *   - Cursor past column 0 -> "Try it yourself" suggestions cached by
 *                              explain.c for the most recent instruction
 *
 * spim_commands must stay in sync with read_assembly_command()'s
 * str_prefix table further below. Alphabetical order so the listing
 * libedit prints on Tab-Tab reads cleanly. */
static const char* spim_commands[] = {
    "args",          "breakpoint", "continue", "delete",
    "dump",          "dumpnative", "exit",     "help",
    "list",          "load",       "print",    "print_all_regs",
    "print_symbols", "quit",       "read",     "reinitialize",
    "run",           "step",       nullptr};

static char* command_generator(const char* text, int state) {
  static size_t idx;
  static size_t text_len;
  if (state == 0) {
    idx = 0;
    text_len = strlen(text);
  }
  while (spim_commands[idx] != nullptr) {
    const char* s = spim_commands[idx++];
    if (strncmp(s, text, text_len) == 0) return str_copy(s);
  }
  return nullptr;
}

static char* suggestion_generator(const char* text, int state) {
  static size_t idx;
  static size_t text_len;
  if (state == 0) {
    idx = 0;
    text_len = strlen(text);
  }
  size_t n = explain_suggestion_count();
  while (idx < n) {
    const char* s = explain_suggestion(idx++);
    if (s == nullptr) continue;
    /* Suggestions are stored as full commands ("print $a0"). When the
       student has already typed `print ` and hits Tab, libedit passes
       us just `$a0` (or the prefix thereof) as `text`. Strip a leading
       space-separated word from the candidate before the prefix check
       so the operand portion still matches. Degrades gracefully if
       there's no space: tail == s, prefix check runs on the whole. */
    const char* tail = s;
    const char* space = strchr(s, ' ');
    if (space != nullptr) tail = space + 1;
    if (strncmp(tail, text, text_len) == 0) return str_copy(tail);
  }
  return nullptr;
}

/* Helper used by line_is_file_command / line_is_label_command. True if
   the current line starts with any of the given command names followed
   by whitespace. */
static bool line_starts_with_cmd(const char* const* cmds) {
  const char* line = rl_line_buffer;
  if (line == nullptr) return false;
  while (*line == ' ' || *line == '\t') line++;
  for (size_t i = 0; cmds[i] != nullptr; i++) {
    size_t len = strlen(cmds[i]);
    if (strncmp(line, cmds[i], len) == 0 &&
        (line[len] == ' ' || line[len] == '\t')) {
      return true;
    }
  }
  return false;
}

/* True if the current line begins with a file-taking REPL command
   followed by whitespace — those are the prompts where the student
   expects libedit's default filename completion to kick in. */
static bool line_is_file_command(void) {
  static const char* cmds[] = {"load", "read", "dump", "dumpnative", nullptr};
  return line_starts_with_cmd(cmds);
}

/* True if the current line begins with a REPL command whose argument
   is a label/address — `breakpoint` and `delete` accept either a hex
   address (TOK_INT) or a symbol name (TOK_ID); tab-completion offers the
   symbol names defined in the loaded program. */
static bool line_is_label_command(void) {
  static const char* cmds[] = {"breakpoint", "delete", nullptr};
  return line_starts_with_cmd(cmds);
}

/* Snapshot of label names taken on each Tab. The pointers point into
   the symbol table's own storage (label->name), so we don't str_copy —
   but we re-collect on every state==0 call in case the symbol table
   changed (e.g. after `reinit` + `load`). */
static const char** label_names_cache = nullptr;
static size_t label_names_cache_n = 0;
static size_t label_names_cache_cap = 0;

static void label_collect_cb(const label* l, void* ctx) {
  (void)ctx;
  if (l == nullptr || l->name == nullptr) return;
  if (label_names_cache_n >= label_names_cache_cap) {
    size_t new_cap =
        label_names_cache_cap == 0 ? 32 : label_names_cache_cap * 2;
    const char** new_arr =
        realloc(label_names_cache, new_cap * sizeof(*label_names_cache));
    if (new_arr == nullptr) return;
    label_names_cache = new_arr;
    label_names_cache_cap = new_cap;
  }
  label_names_cache[label_names_cache_n++] = l->name;
}

static char* label_generator(const char* text, int state) {
  static size_t idx;
  static size_t text_len;
  if (state == 0) {
    label_names_cache_n = 0;
    for_each_label(label_collect_cb, nullptr);
    idx = 0;
    text_len = strlen(text);
  }
  while (idx < label_names_cache_n) {
    const char* s = label_names_cache[idx++];
    if (strncmp(s, text, text_len) == 0) return str_copy(s);
  }
  return nullptr;
}

static char** spim_completion(const char* text, int start, int end) {
  (void)end;
  /* Reset append-character to the default (space) per call — we'll
     override below for the quoted-filename case so the closing `"` is
     auto-inserted on a unique match. */
  rl_completion_append_character = ' ';

  if (start == 0) {
    /* First word of the line: REPL command names. */
    rl_attempted_completion_over = 1;
    return rl_completion_matches(text, command_generator);
  }
  if (line_is_file_command()) {
    /* `load `, `read `, `dump `, `dumpnative ` — filename completion.
       libedit doesn't auto-fall-back to filename completion when we
       return nullptr the way GNU readline does, so we drive
       rl_filename_completion_function explicitly. Two cases: */
    rl_attempted_completion_over = 1;
    if (text[0] == '"') {
      /* Quoted form (`load "...`). Strip the leading `"` before passing
         to the filename completer, then prepend it back to each match
         so libedit's insertion-into-line still aligns with `text`. The
         closing `"` is appended on a unique match via
         rl_completion_append_character. */
      char** m =
          rl_completion_matches(text + 1, rl_filename_completion_function);
      if (m != nullptr) {
        for (int i = 0; m[i] != nullptr; i++) {
          size_t l = strlen(m[i]);
          char* re = malloc(l + 2);
          if (re == nullptr) continue;
          re[0] = '"';
          memcpy(re + 1, m[i], l + 1);
          free(m[i]);
          m[i] = re;
        }
      }
      rl_completion_append_character = '"';
      return m;
    }
    /* Unquoted form (`load tt.s` style). The simulator's parser
       requires quotes around the filename, but offering the names
       still helps the student see what's available — they can wrap
       it in quotes themselves. */
    return rl_completion_matches(text, rl_filename_completion_function);
  }
  if (line_is_label_command()) {
    /* `breakpoint ` / `delete ` — argument is a label or hex address.
       Offer label completion from the loaded program's symbol table. */
    rl_attempted_completion_over = 1;
    return rl_completion_matches(text, label_generator);
  }
  /* Past the first word, not a file/label command: offer the cached
     "Try it yourself" suggestions from the most recent instruction. */
  rl_attempted_completion_over = 1;
  return rl_completion_matches(text, suggestion_generator);
}
#endif

/* Internal functions: */

static void console_to_program(void);
static void console_to_spim(void);
static void control_c_seen(int arg);
static void flush_to_newline(void);
static int get_opt_int(void);
static bool parse_spim_command(bool redo);
static void print_reg(int reg_no);

/* Scanner / parser surface used by the REPL.  Defined in
   src/scanner.c and src/parser.c. */
extern int scanner_advance(void);
extern int parse_file(void);
extern void scanner_push_source(FILE* in_file);
extern void scanner_pop_source(void);
extern void scanner_init(FILE* in_file);
extern void set_input_file_name(char* file_name);
static int print_fp_reg(int reg_no);
static int print_reg_from_string(char* reg);
static void print_all_regs(int hex_flag);
static int read_assembly_command(void);
static int str_prefix(char* s1, char* s2, int min_match);
static void top_level(void);
static int read_token(void);
static bool write_assembled_code(char* program_name);
static void dump_data_seg(bool kernel_also);
static void dump_text_seg(bool kernel_also);

/* Exported Variables: */

/* Not local, but not export so all files don't need setjmp.h */
static jmp_buf spim_top_level_env; /* For ^C */

bool bare_machine;        /* => simulate bare machine */
bool delayed_branches;    /* => simulate delayed branches */
bool delayed_loads;       /* => simulate delayed loads */
bool accept_pseudo_insts; /* => parse pseudo instructions  */
bool quiet;               /* => no warning messages */
static bool assemble;     /* => assemble, disassemble to file and exit */
char* exception_file_name = DEFAULT_EXCEPTION_HANDLER;
port message_out, console_out, console_in;
bool mapped_io;        /* => activate memory-mapped IO */
int spim_return_value; /* Value returned when spim exits */

/* Local variables: */

/* => load standard exception handler */
static bool load_exception_handler = true;
static int console_state_saved;

static struct termios saved_console_state;
static int program_argc;
static char** program_argv;
static bool dump_user_segments = false;
static bool dump_all_segments = false;

int main(int argc, char** argv) {
  int i;
  bool assembly_file_loaded = false;
  bool assembly_file_attempted =
      false; /* -file was given (even if load failed) */
  int print_usage_msg = 0;

  console_out.f = stdout;
  message_out.f = stderr;

  bare_machine = false;
  delayed_branches = false;
  delayed_loads = false;
  accept_pseudo_insts = true;
  quiet = false;
  assemble = false;
  spim_return_value = 0;
  first_bad_exception = -1;

  /* Input comes directly (not through stdio): */
  console_in.i = 0;
  mapped_io = false;

  if (getenv("SPIM_EXCEPTION_HANDLER") != nullptr)
    exception_file_name = getenv("SPIM_EXCEPTION_HANDLER");

  for (i = 1; i < argc; i++) {
    if (streq(argv[i], "-asm") || streq(argv[i], "-a")) {
      bare_machine = false;
      delayed_branches = false;
      delayed_loads = false;
    } else if (streq(argv[i], "-bare") || streq(argv[i], "-b")) {
      bare_machine = true;
      delayed_branches = true;
      delayed_loads = true;
      quiet = true;
    } else if (streq(argv[i], "-delayed_branches") || streq(argv[i], "-db")) {
      delayed_branches = true;
    } else if (streq(argv[i], "-delayed_loads") || streq(argv[i], "-dl")) {
      delayed_loads = true;
    } else if (streq(argv[i], "-exception") || streq(argv[i], "-e")) {
      load_exception_handler = true;
    } else if (streq(argv[i], "-noexception") || streq(argv[i], "-ne")) {
      load_exception_handler = false;
    } else if (streq(argv[i], "-exception_file") || streq(argv[i], "-ef")) {
      exception_file_name = argv[++i];
      load_exception_handler = true;
    } else if (streq(argv[i], "-mapped_io") || streq(argv[i], "-mio")) {
      mapped_io = true;
    } else if (streq(argv[i], "-nomapped_io") || streq(argv[i], "-nmio")) {
      mapped_io = false;
    } else if (streq(argv[i], "-pseudo") || streq(argv[i], "-p")) {
      accept_pseudo_insts = true;
    } else if (streq(argv[i], "-nopseudo") || streq(argv[i], "-np")) {
      accept_pseudo_insts = false;
    } else if (streq(argv[i], "-quiet") || streq(argv[i], "-q")) {
      quiet = true;
    } else if (streq(argv[i], "-noquiet") || streq(argv[i], "-nq")) {
      quiet = false;
    } else if (streq(argv[i], "-noexplain") || streq(argv[i], "-nx")) {
      explain_level = 0;
    } else if (strncmp(argv[i], "-explain=", 9) == 0 ||
               strncmp(argv[i], "-x=", 3) == 0) {
      /* -explain=N / -x=N form. N must be a single digit 0..4. */
      const char* v = strchr(argv[i], '=') + 1;
      if (v[0] >= '0' && v[0] <= '4' && v[1] == '\0') {
        explain_level = v[0] - '0';
      } else {
        fprintf(stderr, "spimulator: %s requires a level in 0..4\n", argv[i]);
        print_usage_msg = 1;
      }
    } else if (streq(argv[i], "-explain") || streq(argv[i], "-x")) {
      /* Bare -explain / -x defaults to level 3 (full output — the
         historical -explain behavior). An optional next arg can set
         a different level: `-explain 2` shows L2 detail. */
      if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '4' &&
          argv[i + 1][1] == '\0') {
        explain_level = argv[i + 1][0] - '0';
        i++;
      } else {
        explain_level = 3;
      }
    } else if (streq(argv[i], "-trap") || streq(argv[i], "-t")) {
      load_exception_handler = true;
    } else if (streq(argv[i], "-notrap") || streq(argv[i], "-nt")) {
      load_exception_handler = false;
    } else if (streq(argv[i], "-trap_file") || streq(argv[i], "-tf")) {
      exception_file_name = argv[++i];
      load_exception_handler = true;
    } else if (streq(argv[i], "-stext") || streq(argv[i], "-st")) {
      initial_text_size = atoi(argv[++i]);
    } else if (streq(argv[i], "-sdata") || streq(argv[i], "-sd")) {
      initial_data_size = atoi(argv[++i]);
    } else if (streq(argv[i], "-ldata") || streq(argv[i], "-ld")) {
      initial_data_limit = (mem_addr)atoi(argv[++i]);
    } else if (streq(argv[i], "-sstack") || streq(argv[i], "-ss")) {
      initial_stack_size = atoi(argv[++i]);
    } else if (streq(argv[i], "-lstack") || streq(argv[i], "-ls")) {
      initial_stack_limit = (mem_addr)atoi(argv[++i]);
    } else if (streq(argv[i], "-sktext") || streq(argv[i], "-skt")) {
      initial_k_text_size = atoi(argv[++i]);
    } else if (streq(argv[i], "-skdata") || streq(argv[i], "-skd")) {
      initial_k_data_size = atoi(argv[++i]);
    } else if (streq(argv[i], "-lkdata") || streq(argv[i], "-lkd")) {
      initial_k_data_limit = (mem_addr)atoi(argv[++i]);
    } else if (((streq(argv[i], "-file") || streq(argv[i], "-f")) &&
                (i + 1 < argc))
               /* Assume this argument is a program's file name and everything
                  following are arguments to the program */
               || (argv[i][0] != '-')) {
      /* Without this guard, every program arg after the .asm filename
         re-enters this branch (via the `|| (argv[i][0] != '-')`
         disjunct) and overwrites program_argc/argv to a progressively
         smaller slice — leaving the runtime with argc=1 and
         argv=["<last>"].  See tasks/argv-command-line-handling.md. */
      if (assembly_file_loaded) continue;

      int program_i = (argv[i][0] == '-') ? (i + 1) : i;
      program_argc = argc - program_i;
      program_argv = &argv[program_i];

      initialize_world(load_exception_handler ? exception_file_name : nullptr,
                       !quiet);
      initialize_run_stack(program_argc, program_argv);
      assembly_file_attempted = true;
      assembly_file_loaded = read_assembly_file(argv[program_i]);

      /* Remaining tokens belong to the program (they're now in
         program_argv).  Don't let them keep matching spim flags. */
      break;
    } else if (streq(argv[i], "-assemble")) {
      assemble = true;
    } else if (streq(argv[i], "-dump")) {
      dump_user_segments = true;
    } else if (streq(argv[i], "-full_dump")) {
      dump_all_segments = true;
    } else {
      error("\nUnknown argument: %s (ignored)\n", argv[i]);
      print_usage_msg = 1;
    }
  }

  if (print_usage_msg) {
    error(
        "Usage: spim\n\
	-bare			Bare machine (no pseudo-ops, delayed branches and loads)\n\
	-asm			Extended machine (pseudo-ops, no delayed branches and loads) (default)\n\
	-delayed_branches	Execute delayed branches\n\
	-delayed_loads		Execute delayed loads\n\
	-exception		Load exception handler (default)\n\
	-noexception		Do not load exception handler\n\
	-exception_file <file>	Specify exception handler in place of default\n\
	-quiet			Do not print warnings\n\
	-noquiet		Print warnings (default)\n\
	-explain [N]		Narrate every instruction (teaching mode).\n\
				Level N selects detail: 1 = semantic, 2 = + interactive,\n\
				3 = + bit-layout (default when N omitted; historical behavior),\n\
				4 = + progressive decoder walkthrough.\n\
				Also accepts -explain=N. Short form: -x.\n\
	-noexplain		Disable teaching mode (default)\n\
	-mapped_io		Enable memory-mapped IO\n\
	-nomapped_io		Do not enable memory-mapped IO (default)\n\
	-file <file> <args>	Assembly code file and arguments to program\n\
	-assemble		Write assembled code to <file>.out\n\
	-dump			Write user data and text segments into files\n\
	-full_dump		Write user and kernel data and text into files.\n");
  }

  if (!assembly_file_loaded) {
    /* -file was given but the file couldn't be opened: don't fall through to
       the REPL — a non-interactive pipeline would hang on stdin.  Exit with a
       non-zero shell status so a Makefile/CI can detect the failure. */
    if (assembly_file_attempted) {
      return spim_return_value != 0 ? spim_return_value : 2;
    }

    initialize_world(load_exception_handler ? exception_file_name : nullptr,
                     !quiet);
    initialize_run_stack(program_argc, program_argv);
    top_level();
  } else /* assembly_file_loaded */
  {
    /* If the assembler reported any error during parse, the program is not in
       a runnable state — refuse to run it and exit non-zero so the shell can
       tell that the build failed. */
    if (parse_errors_seen > 0) {
      return spim_return_value != 0 ? spim_return_value : 2;
    }

    if (assemble) {
      return write_assembled_code(program_argv[0]);
    } else if (dump_user_segments) {
      dump_data_seg(false);
      dump_text_seg(false);
    } else if (dump_all_segments) {
      dump_data_seg(true);
      dump_text_seg(true);
    } else {
      bool continuable;
      console_to_program();
      initialize_run_stack(program_argc, program_argv);
      if (!setjmp(spim_top_level_env)) {
        char* undefs = undefined_symbol_string();
        if (undefs != nullptr) {
          write_output(message_out, "The following symbols are undefined:\n");
          write_output(message_out, undefs);
          write_output(message_out, "\n");
          free(undefs);
          /* Linker-equivalent failure.  Skip the run; jumping into undefined
             memory would just emit a runtime-error message and exit 0. */
          console_to_spim();
          return spim_return_value != 0 ? spim_return_value : 1;
        }
        run_program(find_symbol_address(DEFAULT_RUN_LOCATION),
                    DEFAULT_RUN_STEPS, false, false, &continuable);
      }
      console_to_spim();

      /* If any runtime fault fired (address/bus/alignment/overflow/etc.) and
         the program didn't otherwise set its own exit status via exit2, lift
         the fault into a shell-visible non-zero exit code.  Convention:
         128 + ExcCode, mirroring the shell's "128 + signum" form for
         processes killed by a signal.  An explicit exit2(N) where N != 0
         takes precedence — the program said what it meant. */
      if (first_bad_exception != -1 && spim_return_value == 0) {
        spim_return_value = 128 + first_bad_exception;
      }
    }
  }

  return (spim_return_value);
}

/* Top-level read-eval-print loop for SPIM. */

_Noreturn static void top_level(void) {
  bool redo = false; /* => reexecute last command */

  (void)signal(SIGINT, control_c_seen);
  scanner_init(stdin);
  set_input_file_name("<standard input>");

#ifdef HAVE_LIBEDIT
  /* Persistent REPL history at ~/.spimulator_history, the same idiom gdb,
     sqlite3, and python use. atexit() catches EXIT_CMD's exit(0) too. */
  using_history();
  const char* home = getenv("HOME");
  if (home != nullptr) {
    snprintf(history_path, sizeof(history_path), "%s/.spimulator_history",
             home);
    read_history(history_path);
    atexit(save_history_at_exit);
  }

  /* Tab completion: dispatch to commands / suggestions based on cursor
     column. Trim the word-break set to whitespace only — the libedit
     defaults include `$` (among other punctuation), which would split
     `print $a0` so the cursor word is just the empty string after the
     `$`. That makes Tab on "print $" append the common prefix "$" to an
     empty word, producing "print $$" instead of offering the register
     names. Both variables matter: rl_completer_word_break_characters is
     what the completer consults, but libedit also falls back to / mixes
     with rl_basic_word_break_characters in some code paths. Override
     both to whitespace-only. */
  rl_attempted_completion_function = spim_completion;
  rl_completer_word_break_characters = (char*)" \t\n";
  rl_basic_word_break_characters = " \t\n";

  /* Per-iteration scratch — declared outside so the setjmp landing pad can
     unwind them if a SIGINT longjmps us out of parse_spim_command mid-line. */
  char* repl_line = nullptr;
  char* repl_buf = nullptr;
  FILE* repl_fp = nullptr;
  bool scanner_pushed = false;
#endif

  while (1) {
#ifdef HAVE_LIBEDIT
    if (setjmp(spim_top_level_env)) {
      /* Returned via control_c_seen longjmp. Unwind any in-flight
         scanner state so the flex buffer stack doesn't grow each Ctrl-C. */
      if (scanner_pushed) {
        scanner_pop_source();
        scanner_pushed = false;
      }
      if (repl_fp) {
        fclose(repl_fp);
        repl_fp = nullptr;
      }
      free(repl_buf);
      repl_buf = nullptr;
      free(repl_line);
      repl_line = nullptr;
      redo = false;
      fflush(stdout);
      fflush(stderr);
      continue;
    }

    if (!redo) {
      repl_line = readline("(spim) ");
      if (repl_line == nullptr) {
        /* Ctrl-D / EOF — exit cleanly. atexit handler writes history. */
        write_output(message_out, "\n");
        console_to_spim();
        exit(0);
      }
      if (*repl_line != '\0') add_history(repl_line);

      /* Append '\n' so flex sees TOK_NL exactly once per command, matching
         the stdin path. Wrap the buffer in a FILE* via fmemopen and reuse
         the existing scanner_push_source mechanism — flex sees a normal input
         source, parse_spim_command is unaware of libedit. */
      size_t len = strlen(repl_line);
      repl_buf = malloc(len + 2);
      if (repl_buf == nullptr) {
        free(repl_line);
        repl_line = nullptr;
        continue;
      }
      memcpy(repl_buf, repl_line, len);
      repl_buf[len] = '\n';
      repl_buf[len + 1] = '\0';
      repl_fp = fmemopen(repl_buf, len + 1, "r");
      if (repl_fp == nullptr) {
        free(repl_buf);
        repl_buf = nullptr;
        free(repl_line);
        repl_line = nullptr;
        continue;
      }
      scanner_push_source(repl_fp);
      scanner_pushed = true;
    }

    redo = parse_spim_command(redo);

    if (scanner_pushed) {
      scanner_pop_source();
      scanner_pushed = false;
    }
    if (repl_fp) {
      fclose(repl_fp);
      repl_fp = nullptr;
    }
    free(repl_buf);
    repl_buf = nullptr;
    free(repl_line);
    repl_line = nullptr;
#else
    if (!redo) write_output(message_out, "(spim) ");
    if (!setjmp(spim_top_level_env))
      redo = parse_spim_command(redo);
    else
      redo = false;
#endif
    fflush(stdout);
    fflush(stderr);
  }
}

_Noreturn static void control_c_seen(int arg) {
  (void)arg;  // this line is to suppress compiler warnings
  console_to_spim();
  write_output(message_out, "\nExecution interrupted\n");
  longjmp(spim_top_level_env, 1);
}

/* SPIM commands */

enum {
  UNKNOWN_CMD = 0,
  EXIT_CMD,
  READ_CMD,
  RUN_CMD,
  STEP_CMD,
  PRINT_CMD,
  PRINT_SYM_CMD,
  PRINT_ALL_REGS_CMD,
  REINITIALIZE_CMD,
  ASM_CMD,
  REDO_CMD,
  NOP_CMD,
  HELP_CMD,
  CONTINUE_CMD,
  SET_BKPT_CMD,
  DELETE_BKPT_CMD,
  LIST_BKPT_CMD,
  DUMPNATIVE_TEXT_CMD,
  DUMP_TEXT_CMD,
  ARGS_CMD
};

/* Parse a SPIM command from the currently open file and execute it.
   If REDO is true, don't read a new command; just rexecute the
   previous one.  Return true if the command was to redo the previous
   command. */

static bool parse_spim_command(bool redo) {
  static int prev_cmd = NOP_CMD; /* Default redo */
  static int prev_token;
  int cmd;

  switch (cmd = (redo ? prev_cmd : read_assembly_command())) {
    case EXIT_CMD:
      console_to_spim();
      exit(0);

    case READ_CMD: {
      int token = (redo ? prev_token : read_token());

      if (!redo) flush_to_newline();
      if (token == TOK_STR) {
        (void)read_assembly_file((char*)scan_value.p);
        scanner_pop_source();
      } else
        error("Must supply a filename to read\n");
      prev_cmd = READ_CMD;
      return (0);
    }

    case RUN_CMD: {
      static mem_addr addr;
      bool continuable;

      addr = (redo ? addr : ((mem_addr)get_opt_int()));
      if (addr == 0) addr = starting_address();

      initialize_run_stack(program_argc, program_argv);
      console_to_program();
      if (addr != 0) {
        char* undefs = undefined_symbol_string();
        if (undefs != nullptr) {
          write_output(message_out, "The following symbols are undefined:\n");
          write_output(message_out, undefs);
          write_output(message_out, "\n");
          free(undefs);
        }

        if (run_program(addr, DEFAULT_RUN_STEPS, false, false, &continuable))
          write_output(message_out, "Breakpoint encountered at 0x%08x\n", PC);
      }
      console_to_spim();

      prev_cmd = RUN_CMD;
      return (0);
    }

    case CONTINUE_CMD: {
      if (PC != 0) {
        bool continuable;
        console_to_program();
        if (run_program(PC, DEFAULT_RUN_STEPS, false, true, &continuable))
          write_output(message_out, "Breakpoint encountered at 0x%08x\n", PC);
        console_to_spim();
      }
      prev_cmd = CONTINUE_CMD;
      return (0);
    }

    case STEP_CMD: {
      static int steps;
      mem_addr addr;

      steps = (redo ? steps : get_opt_int());
      addr = PC == 0 ? starting_address() : PC;

      if (steps == 0) steps = 1;
      if (addr != 0) {
        bool continuable;
        console_to_program();
        if (run_program(addr, steps, true, true, &continuable))
          write_output(message_out, "Breakpoint encountered at 0x%08x\n", PC);
        console_to_spim();
      }

      prev_cmd = STEP_CMD;
      return (0);
    }

    case PRINT_CMD: {
      int token = (redo ? prev_token : read_token());
      static int loc;
      /* Set true if we've already consumed TOK_NL while parsing a base+offset
       * suffix, so the trailing flush_to_newline doesn't gobble the next
       * command's tokens. */
      bool consumed_nl = false;

      if (token == TOK_REG) {
        if (redo)
          loc += 1;
        else
          loc = scan_value.i;
        print_reg(loc);
      } else if (token == TOK_FP_REG) {
        if (redo)
          loc += 2;
        else
          loc = scan_value.i;
        print_fp_reg(loc);
      } else if (token == TOK_INT) {
        if (redo) {
          loc += 4;
          print_mem((mem_addr)loc);
        } else {
          int int_val = scan_value.i;
          /* Peek for the N($reg) base+offset form so students can inspect
           * memory using the same syntax they wrote in load/store ops. */
          int next = read_token();
          if (next == '(') {
            int reg_tok = read_token();
            int reg = (reg_tok == TOK_REG) ? scan_value.i : -1;
            int close = (reg_tok == TOK_REG) ? read_token() : 0;
            if (reg_tok == TOK_NL || close == TOK_NL) consumed_nl = true;
            if (reg < 0 || close != ')') {
              error("expected $reg) after '(' in print N($reg) form\n");
            } else {
              loc = R[reg] + int_val;
              print_mem((mem_addr)loc);
            }
          } else {
            /* Not the base+offset form — treat int_val as an absolute
             * address (the standard form). */
            loc = int_val;
            print_mem((mem_addr)loc);
            if (next == TOK_NL) consumed_nl = true;
          }
        }
      } else if (token == TOK_ID) {
        if (!print_reg_from_string((char*)scan_value.p)) {
          if (redo)
            loc += 4;
          else
            loc = (int)find_symbol_address((char*)scan_value.p);

          if (loc != 0)
            print_mem((mem_addr)loc);
          else
            error("Unknown label: %s\n", scan_value.p);
        }
      } else
        error("Print what?\n");
      if (!redo && !consumed_nl) flush_to_newline();
      prev_cmd = PRINT_CMD;
      prev_token = token;
      return (0);
    }

    case PRINT_SYM_CMD:
      print_symbols();
      if (!redo) flush_to_newline();
      prev_cmd = NOP_CMD;
      return (0);

    case PRINT_ALL_REGS_CMD: {
      int hex_flag = 0;
      int token = (redo ? prev_token : read_token());
      if (token == TOK_ID && streq((char*)scan_value.p, "hex")) hex_flag = 1;
      print_all_regs(hex_flag);
      if (!redo) flush_to_newline();
      prev_cmd = NOP_CMD;
      return (0);
    }

    case REINITIALIZE_CMD:
      flush_to_newline();
      initialize_world(load_exception_handler ? exception_file_name : nullptr,
                       !quiet);
      initialize_run_stack(program_argc, program_argv);
      write_startup_message();
      /* Drop stale Tab-completion suggestions from the prior program — the
         memory addresses they reference don't survive the reinit. */
      explain_clear_suggestions();
      prev_cmd = NOP_CMD;
      return (0);

    case ASM_CMD:
      (void)parse_file();
      prev_cmd = ASM_CMD;
      return (0);

    case REDO_CMD:
      return (1);

    case NOP_CMD:
      prev_cmd = NOP_CMD;
      return (0);

    case ARGS_CMD: {
      /* gdb-style `set args`: replace the args the *next* `run` (or
         reinitialize) will pass to the program.  argv[0] is preserved
         as whatever the command-line gave us, so demos that print
         their own program name still work. */
      static char** owned_argv = nullptr;
      static char** owned_strs = nullptr;
      static int owned_strs_len = 0;

      /* Stash argv[0] before freeing the prior vector — program_argv
         may point into the prior owned_argv. */
      char* argv0 = (program_argv != nullptr && program_argc >= 1)
                        ? program_argv[0]
                        : (char*)"<repl>";

      if (owned_strs != nullptr) {
        for (int i = 0; i < owned_strs_len; i++) free(owned_strs[i]);
        free(owned_strs);
        owned_strs = nullptr;
        owned_strs_len = 0;
      }
      free(owned_argv);
      owned_argv = nullptr;

      int t;
      int cap = 0;
      while ((t = read_token()) != TOK_NL && t != 0) {
        char* s = nullptr;
        if (t == TOK_STR || t == TOK_ID) {
          s = str_copy((char*)scan_value.p);
        } else if (t == TOK_INT) {
          char buf[32];
          snprintf(buf, sizeof(buf), "%d", scan_value.i);
          s = str_copy(buf);
        } else {
          continue;
        }
        if (owned_strs_len + 1 > cap) {
          cap = cap == 0 ? 4 : cap * 2;
          owned_strs = realloc(owned_strs, cap * sizeof(char*));
        }
        owned_strs[owned_strs_len++] = s;
      }

      owned_argv = malloc((owned_strs_len + 1) * sizeof(char*));
      owned_argv[0] = argv0;
      for (int i = 0; i < owned_strs_len; i++)
        owned_argv[1 + i] = owned_strs[i];
      program_argc = 1 + owned_strs_len;
      program_argv = owned_argv;

      prev_cmd = ARGS_CMD;
      return (0);
    }

    case HELP_CMD:
      if (!redo) flush_to_newline();
      write_output(message_out, "\nSPIM is a MIPS32 simulator.\n");
      write_output(message_out, "Its top-level commands are:\n");
      write_output(message_out, "exit  -- Exit the simulator\n");
      write_output(message_out, "quit  -- Exit the simulator\n");
      write_output(
          message_out,
          "read \"FILE\" -- Read FILE containing assembly code into memory\n");
      write_output(message_out, "load \"FILE\" -- Same as read\n");
      write_output(message_out,
                   "run <ADDR> -- Start the program at (optional) ADDRESS\n");
      write_output(message_out,
                   "args <ARG ...> -- Set the args the next `run` will pass "
                   "to the program;\n"
                   "                  argv[0] stays as the loaded program "
                   "path.  `args`\n"
                   "                  with no operands clears the args.\n");
      write_output(
          message_out,
          "step <N> -- Step the program for N instructions (default 1)\n");
      write_output(message_out,
                   "continue -- Continue program execution without stepping\n");
      write_output(message_out, "print $N -- Print register N\n");
      write_output(message_out,
                   "print $fN -- Print floating point register N\n");
      write_output(message_out,
                   "print ADDR -- Print contents of memory at ADDRESS\n");
      write_output(message_out, "print_symbols -- Print all global symbols\n");
      write_output(message_out, "print_all_regs -- Print all MIPS registers\n");
      write_output(message_out,
                   "print_all_regs hex -- Print all MIPS registers in hex\n");
      write_output(message_out,
                   "reinitialize -- Clear the memory and registers\n");
      write_output(message_out,
                   "breakpoint <ADDR> -- Set a breakpoint at address ADDR\n");
      write_output(message_out,
                   "delete <ADDR> -- Delete breakpoint at address ADDR\n");
      write_output(message_out, "list -- List all breakpoints\n");
      write_output(message_out,
                   "dump [ \"FILE\" ] -- Dump binary code to spim.dump or FILE "
                   "in network byte order\n");
      write_output(message_out,
                   "dumpnative [ \"FILE\" ] -- Dump binary code to spim.dump "
                   "or FILE in host byte order\n");
      write_output(message_out,
                   ". -- Rest of line is assembly instruction to execute\n");
      write_output(message_out,
                   "<cr> -- Newline reexecutes previous command\n");
      write_output(message_out, "? -- Print this message\n");

      write_output(
          message_out,
          "\nMost commands can be abbreviated to their unique prefix\n");
      write_output(message_out,
                   "e.g., ex(it), re(ad), l(oad), ru(n), s(tep), p(rint)\n\n");
      prev_cmd = HELP_CMD;
      return (0);

    case SET_BKPT_CMD:
    case DELETE_BKPT_CMD: {
      int token = (redo ? prev_token : read_token());
      static mem_addr addr;

      if (!redo) flush_to_newline();
      if (token == TOK_INT)
        addr = redo ? addr + 4 : (mem_addr)scan_value.i;
      else if (token == TOK_ID)
        addr = redo ? addr + 4 : find_symbol_address((char*)scan_value.p);
      else
        error("Must supply an address for breakpoint\n");
      if (cmd == SET_BKPT_CMD)
        add_breakpoint(addr);
      else
        delete_breakpoint(addr);
      prev_cmd = cmd;

      return (0);
    }

    case LIST_BKPT_CMD:
      if (!redo) flush_to_newline();
      list_breakpoints();
      prev_cmd = LIST_BKPT_CMD;
      return (0);

    case DUMPNATIVE_TEXT_CMD:
    case DUMP_TEXT_CMD: {
      int token = (redo ? prev_token : read_token());

      FILE* fp = nullptr;
      char* filename = nullptr;

      int words = 0;
      mem_addr addr;
      mem_addr dump_start;
      mem_addr dump_end;

      if (token == TOK_STR)
        filename = (char*)scan_value.p;
      else if (token == TOK_NL)
        filename = "spim.dump";
      else {
        fprintf(stderr, "usage: %s [ \"filename\" ]\n",
                (cmd == DUMP_TEXT_CMD ? "dump" : "dumpnative"));
        return (0);
      }

      fp = fopen(filename, "wbt");
      if (fp == nullptr) {
        perror(filename);
        return (0);
      }

      user_kernel_text_segment(false);
      dump_start = find_symbol_address(END_OF_TRAP_HANDLER_SYMBOL);
      dump_end = current_text_pc();

      for (addr = dump_start; addr < dump_end; addr += BYTES_PER_WORD) {
        int32_t code = inst_encode(mem_read_inst(addr));
        if (cmd == DUMP_TEXT_CMD)
          code = (int32_t)htonl(
              (unsigned long)code); /* dump in network byte order */
        (void)fwrite(&code, 1, sizeof(code), fp);
        words += 1;
      }

      fclose(fp);
      fprintf(stderr, "Dumped %d words starting at 0x%08x to file %s\n", words,
              (unsigned int)dump_start, filename);

      prev_cmd = cmd;
      return (0);
    }

    default:
      while (read_token() != TOK_NL);
      error("Unknown spim command\n");
      return (0);
  }
}

/* Read a SPIM command with the scanner and return its ennuemerated
   value. */

static int read_assembly_command(void) {
  int token = read_token();

  if (token == TOK_NL) /* Blank line means redo */
    return (REDO_CMD);
  else if (token != TOK_ID) /* Better be a string */
    return (UNKNOWN_CMD);
  else if (str_prefix((char*)scan_value.p, "exit", 2))
    return (EXIT_CMD);
  else if (str_prefix((char*)scan_value.p, "quit", 2))
    return (EXIT_CMD);
  else if (str_prefix((char*)scan_value.p, "print", 1))
    return (PRINT_CMD);
  else if (str_prefix((char*)scan_value.p, "print_symbols", 7))
    return (PRINT_SYM_CMD);
  else if (str_prefix((char*)scan_value.p, "print_all_regs", 7))
    return (PRINT_ALL_REGS_CMD);
  else if (str_prefix((char*)scan_value.p, "run", 2))
    return (RUN_CMD);
  else if (str_prefix((char*)scan_value.p, "args", 2))
    return (ARGS_CMD);
  else if (str_prefix((char*)scan_value.p, "read", 2))
    return (READ_CMD);
  else if (str_prefix((char*)scan_value.p, "load", 2))
    return (READ_CMD);
  else if (str_prefix((char*)scan_value.p, "reinitialize", 6))
    return (REINITIALIZE_CMD);
  else if (str_prefix((char*)scan_value.p, "step", 1))
    return (STEP_CMD);
  else if (str_prefix((char*)scan_value.p, "help", 1))
    return (HELP_CMD);
  else if (str_prefix((char*)scan_value.p, "continue", 1))
    return (CONTINUE_CMD);
  else if (str_prefix((char*)scan_value.p, "breakpoint", 2))
    return (SET_BKPT_CMD);
  else if (str_prefix((char*)scan_value.p, "delete", 1))
    return (DELETE_BKPT_CMD);
  else if (str_prefix((char*)scan_value.p, "list", 2))
    return (LIST_BKPT_CMD);
  else if (str_prefix((char*)scan_value.p, "dumpnative", 5))
    return (DUMPNATIVE_TEXT_CMD);
  else if (str_prefix((char*)scan_value.p, "dump", 4))
    return (DUMP_TEXT_CMD);
  else if (*(char*)scan_value.p == '?')
    return (HELP_CMD);
  else if (*(char*)scan_value.p == '.')
    return (ASM_CMD);
  else
    return (UNKNOWN_CMD);
}

/* Return non-nil if STRING1 is a (proper) prefix of STRING2. */

static int str_prefix(char* s1, char* s2, int min_match) {
  for (; *s1 == *s2 && *s1 != '\0'; s1++, s2++) min_match--;
  return (*s1 == '\0' && min_match <= 0);
}

/* Read and return an integer from the current line of input.  If the
   line doesn't contain an integer, return 0.  In either case, flush the
   rest of the line, including the newline. */

static int get_opt_int(void) {
  int token;

  if ((token = read_token()) == TOK_INT) {
    flush_to_newline();
    return (scan_value.i);
  } else if (token == TOK_NL)
    return (0);
  else {
    flush_to_newline();
    return (0);
  }
}

/* Flush the rest of the input line up to and including the next newline. */

static void flush_to_newline(void) { while (read_token() != TOK_NL); }

/* Print register number N. */

static void print_reg(int reg_no) {
  write_output(message_out, "Reg %d = 0x%08x (%d)\n", reg_no, R[reg_no],
               R[reg_no]);
}

static int print_fp_reg(int reg_no) {
  if ((reg_no & 1) == 0)
    write_output(message_out, "FP reg %d = %g (double)\n", reg_no,
                 FPR_D(reg_no));
  write_output(message_out, "FP reg %d = %g (single)\n", reg_no, FPR_S(reg_no));
  return (1);
}

static int print_reg_from_string(char* reg_num) {
  char s[100];
  char* s1 = s;

  /* Conver to lower case */
  while (*reg_num != '\0' && s1 - s < 100) *s1++ = tolower(*reg_num++);
  *s1 = '\0';
  /* Drop leading $ */
  if (s[0] == '$')
    s1 = s + 1;
  else
    s1 = s;

  if (streq(s1, "pc"))
    write_output(message_out, "PC = 0x%08x (%d)\n", PC, PC);
  else if (streq(s1, "hi"))
    write_output(message_out, "HI = 0x%08x (%d)\n", HI, HI);
  else if (streq(s1, "lo"))
    write_output(message_out, "LO = 0x%08x (%d)\n", LO, LO);
  else if (streq(s1, "fpcond"))
    write_output(message_out, "FCSR = 0x%08x (%d)\n", FCSR, FCSR);
  else if (streq(s1, "cause"))
    write_output(message_out, "Cause = 0x%08x (%d)\n", CP0_Cause, CP0_Cause);
  else if (streq(s1, "epc"))
    write_output(message_out, "EPC = 0x%08x (%d)\n", CP0_EPC, CP0_EPC);
  else if (streq(s1, "status"))
    write_output(message_out, "Status = 0x%08x (%d)\n", CP0_Status, CP0_Status);
  else if (streq(s1, "badvaddr"))
    write_output(message_out, "BadVAddr = 0x%08x (%d)\n", CP0_BadVAddr,
                 CP0_BadVAddr);
  else
    return (0);

  return (1);
}

static void print_all_regs(int hex_flag) {
  static str_stream ss;

  ss_clear(&ss);
  format_registers(&ss, hex_flag, hex_flag);
  write_output(message_out, "%s\n", ss_to_string(&ss));
}

static bool write_assembled_code(char* program_name) {
  if (parse_error_occurred) {
    return (parse_error_occurred);
  }

  FILE* fp = nullptr;
  {
    char* filename = nullptr;
    const unsigned long filename_len = strlen(program_name) + 5;
    filename = (char*)xmalloc(filename_len);
    strlcpy(filename, program_name, filename_len);
    strlcat(filename, ".out", filename_len);

    fp = fopen(filename, "wt");
    if (fp == nullptr) {
      perror(filename);
      free(filename);
      return (true);
    }
    free(filename);
  }

  /* dump text segment */
  user_kernel_text_segment(false);
  mem_addr dump_start = find_symbol_address(END_OF_TRAP_HANDLER_SYMBOL);
  mem_addr dump_end = current_text_pc();

  (void)fprintf(fp, ".text # 0x%x .. 0x%x\n.word ", dump_start, dump_end);
  for (mem_addr addr = dump_start; addr < dump_end; addr += BYTES_PER_WORD) {
    int32_t code = inst_encode(mem_read_inst(addr));
    (void)fprintf(fp, "0x%x%s", code,
                  addr != (dump_end - BYTES_PER_WORD) ? ", " : "");
  }
  (void)fprintf(fp, "\n");

  /* dump data segment */
  user_kernel_data_segment(false);
  if (bare_machine) {
    dump_start = 0;
  } else {
    dump_start = DATA_BOT;
  }
  dump_end = current_data_pc();

  if (dump_end > dump_start) {
    (void)fprintf(fp, ".data # 0x%x .. 0x%x\n.word ", dump_start, dump_end);
    for (mem_addr addr = dump_start; addr < dump_end; addr += BYTES_PER_WORD) {
      int32_t code = mem_read_word(addr);
      (void)fprintf(fp, "0x%x%s", code,
                    addr != (dump_end - BYTES_PER_WORD) ? ", " : "");
    }
    (void)fprintf(fp, "\n");
  }

  fclose(fp);
  return (false);
}

/* Print an error message. */

void error(char* fmt, ...) {
  va_list args;

  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
}

/* Print the error message then exit. */

void fatal_error(char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  fmt = va_arg(args, char*);
  vfprintf(stderr, fmt, args);
  exit(-1);
}

/* Print an error message and return to top level. */

_Noreturn void run_error(char* fmt, ...) {
  va_list args;

  va_start(args, fmt);

  console_to_spim();

  vfprintf(stderr, fmt, args);
  va_end(args);
  longjmp(spim_top_level_env, 1);
}

/* IO facilities: */

void write_output(port fp, char* fmt, ...) {
  va_list args;
  FILE* f;
  int restore_console_to_program = 0;

  va_start(args, fmt);
  f = fp.f;

  if (console_state_saved) {
    restore_console_to_program = 1;
    console_to_spim();
  }

  if (f != 0) {
    vfprintf(f, fmt, args);
    fflush(f);
  } else {
    vfprintf(stdout, fmt, args);
    fflush(stdout);
  }
  va_end(args);

  if (restore_console_to_program) console_to_program();
}

/* Simulate the semantics of fgets (not gets) on Unix file.
 *
 * Returns the number of bytes consumed from the host input (NOT
 * including the NUL terminator written into `str`).  A return of
 * 0 means the host's read(2) signaled EOF before any byte was
 * read — callers use this to set $a3 / return -1 to user code.
 */

int read_input(char* str, int str_size) {
  char* ptr;
  int restore_console_to_program = 0;
  int count = 0;

  if (console_state_saved) {
    restore_console_to_program = 1;
    console_to_spim();
  }

  ptr = str;

  while (1 < str_size) /* Reserve space for null */
  {
    char buf[1];
    if (read((int)console_in.i, buf, 1) <= 0) /* Not in raw mode! */
      break;

    *ptr++ = buf[0];
    str_size -= 1;
    count += 1;

    if (buf[0] == '\n') break;
  }

  if (0 < str_size) *ptr = '\0'; /* Null terminate input */

  if (restore_console_to_program) console_to_program();
  return count;
}

/* Give the console to the program for IO. */

static void console_to_program(void) {
  if (mapped_io && !console_state_saved) {
    struct termios params;

    tcgetattr(console_in.i, &saved_console_state);
    params = saved_console_state;
    params.c_iflag &= ~(ISTRIP | INLCR | ICRNL | IGNCR | IXON | IXOFF | INPCK |
                        BRKINT | PARMRK);

    /* Translate CR -> NL to canonicalize input. */
    params.c_iflag |= IGNBRK | IGNPAR | ICRNL;
    params.c_oflag = OPOST | ONLCR;
    params.c_cflag &= ~PARENB;
    params.c_cflag |= CREAD | CS8;
    params.c_lflag = 0;
    params.c_cc[VMIN] = 1;
    params.c_cc[VTIME] = 1;

    tcsetattr(console_in.i, TCSANOW, &params);
    console_state_saved = 1;
  }
}

/* Return the console to SPIM. */

static void console_to_spim(void) {
  if (mapped_io && console_state_saved)
    tcsetattr(console_in.i, TCSANOW, &saved_console_state);
  console_state_saved = 0;
}

int console_input_available(void) {
  fd_set fdset;
  struct timeval timeout;

  if (mapped_io) {
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    FD_ZERO(&fdset);
    FD_SET((int)console_in.i, &fdset);
    return (select(sizeof(fdset) * 8, &fdset, nullptr, nullptr, &timeout));
  } else
    return (0);
}

char get_console_char(void) {
  char buf;

  read((int)console_in.i, &buf, 1);

  if (buf == 3) /* ^C */
    control_c_seen(0);
  return (buf);
}

void put_console_char(char c) {
  putc(c, console_out.f);
  fflush(console_out.f);
}

static int read_token(void) {
  int token = scanner_advance();

  if (token == TOK_EOF) {
    console_to_spim();
    exit(0);
  } else {
    return (token);
  }
}

/*
 * Writes the contents of the (user and optionally kernel) data segment into
 * data.asm file. If data.asm already exists, it's replaced.
 */

static void dump_data_seg(bool kernel_also) {
  static str_stream ss;
  ss_clear(&ss);

  if (kernel_also) {
    format_data_segs(&ss);
  } else {
    ss_printf(&ss, "\tDATA\n");
    format_mem(&ss, DATA_BOT, data_top);
  }

  {
    FILE* fp = fopen("data.asm", "w");
    fprintf(fp, "%s", ss_to_string(&ss));
    fclose(fp);
  }
}

/*
 * Writes the contents of the (user and optionally kernel) text segment in
 * text.asm file. If data.asm already exists, it's replaced.
 */

static void dump_text_seg(bool kernel_also) {
  static str_stream ss;
  ss_clear(&ss);

  if (kernel_also) {
    format_insts(&ss, TEXT_BOT, text_top);
    ss_printf(&ss, "\n\tKERNEL\n");
    format_insts(&ss, K_TEXT_BOT, k_text_top);
  } else {
    ss_printf(&ss, "\n\tUSER TEXT SEGMENT\n");
    format_insts(&ss, TEXT_BOT, text_top);
  }

  FILE* fp;
  fp = fopen("text.asm", "w");
  fprintf(fp, "%s", ss_to_string(&ss));
  fclose(fp);
}
