;;; ohos-win.el --- native OHOS window system setup -*- lexical-binding: t -*-

;;; Commentary:

;; OHOS native GUI window-system initialization.  This mirrors
;; term/android-win.el: the terminal/display is created by C, while the
;; first frame is created through the standard Lisp `frame-initialize'
;; path.

;;; Code:

(require 'frame)
(require 'term/common-win)
(require 'fontset)

(declare-function ohos-create-frame "ohosfns.c" (&optional parms))
(declare-function x-handle-args "term/common-win" (args))

(cl-defmethod window-system-initialization (&context (window-system ohos)
                                                     &optional _display)
  "Set up the OHOS window system."
  (create-default-fontset))

(cl-defmethod handle-args-function (args &context (window-system ohos))
  "Process OHOS command-line arguments."
  (x-handle-args args))

(cl-defmethod frame-creation-function (params &context (window-system ohos))
  (ohos-create-frame params))

(provide 'ohos-win)
;;; ohos-win.el ends here
