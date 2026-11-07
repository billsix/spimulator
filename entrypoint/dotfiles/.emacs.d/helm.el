;; sample `helm' configuration use https://github.com/emacs-helm/helm/ for details
(helm-mode)
(require 'helm-xref)
(define-key global-map [remap find-file] #'helm-find-files)
;; C-x C-f on a file name prefills it using Emacs's own find-file-at-point
;; guesser, which also looks in parent folders.  Docs cite files relative to the
;; repository root (tasks/reference/x.md written inside tasks/foo.md); without
;; this, helm prefills tasks/tasks/reference/x.md, which does not exist.
(setq helm-ff-guess-ffap-filenames t)
(define-key global-map [remap execute-extended-command] #'helm-M-x)
(define-key global-map [remap switch-to-buffer] #'helm-mini)
