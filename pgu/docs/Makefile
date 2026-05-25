# Minimal makefile for Sphinx documentation
#

# You can set these variables from the command line, and also
# from the environment for the first two.
SPHINXOPTS    ?=
SPHINXBUILD   ?= sphinx-build
SOURCEDIR     = source
BUILDDIR      = build

# Put it first so that "make" without argument is like "make help".
help:
	@$(SPHINXBUILD) -M help "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)

.PHONY: help Makefile spellcheck

PERSONAL_DICT := ./pgu.pws

spellcheck:
	for file in $(shell ls source/*.rst); do aspell --personal=$(PERSONAL_DICT) check $$file; done

# Catch-all target: route all unknown targets to Sphinx using the new
# "make mode" option.  $(O) is meant as a shortcut for $(SPHINXOPTS).
%: Makefile spellcheck
	inkscape source/_static/bigendian.svg --export-filename=source/_static/bigendian.png
	inkscape source/_static/littleendian.svg --export-filename=source/_static/littleendian.png
	inkscape source/_static/mailbox.svg --export-filename=source/_static/mailbox.png
	inkscape source/_static/memorypages.svg --export-filename=source/_static/memorypages.png
	inkscape source/_static/memory.svg --export-filename=source/_static/memory.png
	inkscape source/_static/PGUFrontCover.svg --export-filename=source/_static/PGUFrontCover.png
	inkscape source/_static/registerdescription.svg --export-filename=source/_static/registerdescription.png
	inkscape inkscape ndcSpace.svg  --export-filename=inkscape ndcSpace.spng
	@$(SPHINXBUILD) -M $@ "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)
