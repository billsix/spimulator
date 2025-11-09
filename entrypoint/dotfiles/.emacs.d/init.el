(package-initialize)

(load-file "~/.emacs.d/helm.el")
(load-file "~/.emacs.d/preferences.el")

;; theme
(load-theme 'modus-vivendi t)
;(load-theme 'material t)
;(load-theme 'dracula t)
;(load-theme 'monokai t)
;(load-theme 'zenburn t)

(global-auto-revert-mode)
(setq auto-revert-avoid-polling t)

;; Disable eglot hooks for python-mode
(remove-hook 'python-mode-hook 'eglot-ensure)

(require 'lsp-mode)

(setq lsp-auto-guess-root nil)

(defun my-lsp-root (&rest _)
  "/spimulator/")

(advice-add 'lsp--calculate-root :override #'my-lsp-root)

(add-hook 'prog-mode-hook #'lsp-deferred)


;; LSP setup
(use-package lsp-mode
  :hook ((c-mode c++-mode) . lsp)
  :commands lsp
  :init
  (setq lsp-clients-clangd-executable "clangd"
        lsp-enable-snippet nil
        lsp-prefer-capf t))

(use-package lsp-ui :commands lsp-ui-mode)

(use-package company
  :hook (after-init . global-company-mode)
  :config
  (setq company-minimum-prefix-length 1
        company-idle-delay 0.0))

(use-package clang-format
  :bind (:map c-mode-base-map
              ("C-c f" . clang-format-buffer)))
