# Built from spim's own opcodes.h — this grammar lives as a subdirectory
# of /spimulator, so the source of truth is one directory up.
SPIM_OPCODES_H = ../include/opcodes.h

# Prefer a locally `npm install`ed tree-sitter-cli (lands in
# node_modules/.bin) over any global install.  Works either way.
export PATH := $(CURDIR)/node_modules/.bin:$(PATH)

.PHONY: all generate test clean

all: generate test

# Re-derive keywords from spim's opcodes.h, then regenerate the parser.
generate: scripts/keyword-lists.js
	tree-sitter generate

scripts/keywords.json: $(SPIM_OPCODES_H) scripts/extract-keywords.py
	scripts/extract-keywords.py $(SPIM_OPCODES_H) > $@

scripts/keyword-lists.js: scripts/keywords.json scripts/build-keyword-lists.py
	scripts/build-keyword-lists.py scripts/keywords.json $@

test:
	tree-sitter test

clean:
	rm -rf src/parser.c src/grammar.json src/node-types.json src/tree_sitter
	rm -f scripts/keywords.json scripts/keyword-lists.js
