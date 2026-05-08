;; -*- no-byte-compile: t; lexical-binding: nil -*-
(define-package "markdown-preview-mode" "20230707.803"
  "Markdown realtime preview minor mode."
  '((emacs         "24.4")
    (websocket     "1.6")
    (markdown-mode "2.0")
    (cl-lib        "0.5")
    (web-server    "0.1.1"))
  :url "https://github.com/ancane/markdown-preview-mode"
  :commit "68242b3907dc065aa35412bfd928b43d8052d321"
  :revdesc "68242b3907dc"
  :keywords '("markdown" "gfm" "convenience")
  :authors '(("Igor Shymko" . "igor.shimko@gmail.com"))
  :maintainers '(("Igor Shymko" . "igor.shimko@gmail.com")))
