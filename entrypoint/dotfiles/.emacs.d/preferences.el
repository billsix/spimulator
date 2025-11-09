(set-background-color "black")
(set-foreground-color "white")
(show-paren-mode)
(tool-bar-mode -1)
(menu-bar-mode -1)
(setq inhibit-startup-message t)        ; Disable startup message
(setq-default indent-tabs-mode nil)
(set-face-attribute 'default nil :height 180)

(add-hook 'before-save-hook 'delete-trailing-whitespace)


(visual-line-mode t)
(global-visual-line-mode t)
(global-display-line-numbers-mode t)
;;(setq display-line-numbers-type 'relative)
(set-buffer-file-coding-system 'unix)

(setq make-backup-files nil)

(defun region-generate-sha1 ()
  (interactive)
  (kill-new (sha1 (buffer-substring (region-beginning) (region-end)))))

(use-package eglot
  :ensure t
  :defer t
  :hook ((c-mode c++-mode) . eglot-ensure))

(use-package eglot
  :ensure t
  :defer t
  :hook (emacs-lisp-mode . eglot-ensure))

(global-company-mode)
(setq company-idle-delay 0)
(setq company-minimum-prefix-length 1)


; force transparancy for streaming
(if (eq (framep (selected-frame)) 't)
    (custom-set-faces
     '(default ((t (:background "unspecified-bg"))))))
