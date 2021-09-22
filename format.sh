#/bin/bash
find . -iname '*.c' | grep -v lex | grep -v yacc  | xargs clang-format -i
