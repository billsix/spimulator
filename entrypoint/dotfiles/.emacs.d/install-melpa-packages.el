(require 'package)
(add-to-list 'package-archives '("melpa" . "https://melpa.org/packages/") t)
(package-initialize)

(setq package-selected-packages
      '(lsp-mode
        yasnippet
        helm-lsp
        projectile
        hydra
        flycheck
        company
        which-key
        helm-xref

        material-theme
        dracula-theme
        monokai-theme
        zenburn-theme

        lsp-ui
        org-modern
        discover
        yafolding
        fold-this
        better-defaults
        magit
        markdown-mode
        markdown-preview-mode
        ))

(when (cl-find-if-not #'package-installed-p package-selected-packages)
  (package-refresh-contents)
  (mapc #'package-install package-selected-packages))
