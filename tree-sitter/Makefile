# The keyword lists (scripts/keyword-lists.js) are generated from spim's
# opcode table, ../include/opcodes.tsv, by ../tools/gen_opcodes.py
# (`make generate` at the repo root) and committed; this Makefile only
# builds and tests the parser.

# Prefer a locally `npm install`ed tree-sitter-cli (lands in
# node_modules/.bin) over any global install.  Works either way.
export PATH := $(CURDIR)/node_modules/.bin:$(PATH)

.PHONY: all generate test clean

all: generate test

# Regenerate the parser from grammar.js and scripts/keyword-lists.js.
generate:
	tree-sitter generate

test:
	tree-sitter test

clean:
	rm -rf src/parser.c src/grammar.json src/node-types.json src/tree_sitter
