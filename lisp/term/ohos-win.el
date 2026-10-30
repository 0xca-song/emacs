;;; ohos-win.el --- native OHOS window system setup -*- lexical-binding: t -*-

(require 'frame)
(require 'term/common-win)

(declare-function ohos-create-frame "ohosgui.c" (&optional parms))

(cl-defmethod frame-creation-function (params &context (window-system ohos))
  (ohos-create-frame params))

(provide 'ohos-win)
;;; ohos-win.el ends here
