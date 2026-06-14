# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

project = 'Programming From The Ground Up'
copyright = '2025, Jonathan Bartlett'
author = 'Jonathan Bartlett'
release = '0.0.1'
version = release  # required non-empty for EPUB3 builds

# -- General configuration ---------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#general-configuration

extensions = []

templates_path = ['_templates']
exclude_patterns = []

# Pygments' GAS lexer chokes on character-literal apostrophes
# (`'0'`, `'a'`) and on `+` between symbol names — both real GAS
# features the book uses.  It retries in relaxed mode and the
# rendered output is fine, so suppress the noise.
suppress_warnings = ['misc.highlighting_failure']

# Plain-text default for `::` literal blocks.  Without this, Sphinx
# defaults to Python highlighting, which mis-tokenizes apostrophes in
# narrative blocks (e.g. "Customer's name" gets colored as a string).
# Asm code blocks override this with `:language: asm` on their
# literalinclude directive.
highlight_language = 'none'



# -- Options for LaTeX/PDF output --------------------------------------------
# The worked-examples appendix pastes spimulator's -explain output, which
# uses Unicode box-drawing characters (the bit-layout boxes).  pdflatex
# can't typeset those by default and aborts the PDF build, so map each to
# an ASCII box character.  (HTML/EPUB keep the real Unicode boxes; only the
# PDF substitutes.)  No extra LaTeX package needed.
latex_elements = {
    'preamble': r'''
% Box-drawing characters from -explain output -> ASCII for pdflatex
\DeclareUnicodeCharacter{2500}{-}
\DeclareUnicodeCharacter{2502}{|}
\DeclareUnicodeCharacter{250C}{+}
\DeclareUnicodeCharacter{2510}{+}
\DeclareUnicodeCharacter{2514}{+}
\DeclareUnicodeCharacter{2518}{+}
\DeclareUnicodeCharacter{252C}{+}
\DeclareUnicodeCharacter{2534}{+}
''',
}


# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

html_theme = "furo"
html_static_path = ['_static']
