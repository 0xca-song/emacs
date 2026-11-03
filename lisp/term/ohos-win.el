;;; ohos-win.el --- native OHOS window system setup -*- lexical-binding: t -*-

;;; Commentary:

;; OHOS native GUI window-system initialization.  This mirrors
;; term/android-win.el: the terminal/display is created by C, while the
;; first frame is created through the standard Lisp `frame-initialize'
;; path.
;;
;; Selection handling uses the HarmonyOS pasteboard, which is only
;; reachable from ArkTS.  The C primitives in ohosselect.c forward every
;; request to the ArkUI host over the host command channel, so the
;; generic selection plumbing (`gui-set-selection', `gui-get-selection',
;; interprogram cut/paste) works unchanged.

;;; Code:

(require 'frame)
(require 'term/common-win)
(require 'fontset)
(require 'select)

(declare-function ohos-create-frame "ohosfns.c" (&optional parms))
(declare-function ohos-set-title "ohosfns.c" (title))
(declare-function ohos-notify "ohosfns.c" (title body &optional extra))
(declare-function ohos-battery "ohosfns.c" ())
(declare-function ohos-frame-host-operation "ohosfns.c" (command))
(declare-function ohos-get-selection "ohosselect.c" (&optional selection))
(declare-function ohos-set-selection "ohosselect.c" (selection string))
(declare-function ohos-selection-owner-p "ohosselect.c"
                  (&optional selection))
(declare-function ohos-selection-exists-p "ohosselect.c"
                  (&optional selection))
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

;;; Selections.

(defun ohos-selection-bounds (value)
  "Return the buffer bounds of selection VALUE, or nil.
VALUE is a string, a cons of two markers, or an overlay."
  (cond ((consp value)
         (when (and (markerp (car value))
                    (markerp (cdr value))
                    (marker-buffer (car value))
                    (eq (marker-buffer (car value))
                        (marker-buffer (cdr value))))
           (list (marker-position (car value))
                 (marker-position (cdr value))
                 (marker-buffer (car value)))))
        ((overlayp value)
         (when (overlay-buffer value)
           (list (overlay-start value)
                 (overlay-end value)
                 (overlay-buffer value))))))

(defun ohos-encode-select-string (value)
  "Turn VALUE into a string suitable for the OHOS clipboard."
  (unless (stringp value)
    (when-let* ((bounds (ohos-selection-bounds value)))
      (setq value (ignore-errors
                    (with-current-buffer (nth 2 bounds)
                      (buffer-substring (nth 0 bounds)
                                        (nth 1 bounds)))))))
  value)

(cl-defmethod gui-backend-get-selection (type _data-type
                                         &context (window-system ohos))
  "Return the OHOS pasteboard text for selection TYPE."
  (when (memq type '(CLIPBOARD PRIMARY SECONDARY))
    (ohos-get-selection (symbol-name type))))

(cl-defmethod gui-backend-set-selection (type value
                                         &context (window-system ohos))
  "Put VALUE on the OHOS pasteboard as selection TYPE."
  (when-let* ((string (ohos-encode-select-string value)))
    (ohos-set-selection (symbol-name type) string)))

(cl-defmethod gui-backend-selection-exists-p (selection
                                              &context (window-system ohos))
  "Return non-nil: the OHOS pasteboard may hold text for SELECTION."
  (ohos-selection-exists-p (symbol-name selection)))

(cl-defmethod gui-backend-selection-owner-p (selection
                                             &context (window-system ohos))
  "Return non-nil if Emacs last set the OHOS clipboard for SELECTION."
  (ohos-selection-owner-p (symbol-name selection)))

;;; Frame title.

;; Redisplay calls the `implicit_set_name_hook' of the frame's terminal
;; when `frame-title-format' changes, so the native window title tracks
;; the selected buffer without any Lisp hook.  `frame-title-format' keeps
;; its default value; only make sure the interactive title command also
;; reaches the ArkUI window.

(defun ohos-set-frame-title (title)
  "Set the native window TITLE of the selected frame.
Interactively, prompt for the new title."
  (interactive "sNew title: ")
  (ohos-set-title title))

;;; System services.

(defun ohos-post-notification (title body)
  "Post a HarmonyOS notification with TITLE and BODY."
  (interactive "sTitle: \nsBody: ")
  (ohos-notify title body))

(defun ohos-battery-status ()
  "Return a `battery-status-function' style alist for HarmonyOS.
The values come from the ArkUI host, which reads BatteryInfo."
  (let* ((json (ohos-battery))
         (data (and json (> (length json) 0)
                    (ignore-errors
                      (json-parse-string json :object-type 'alist)))))
    (when data
      (let* ((capacity (cdr (assq 'capacity data)))
             (plugged (cdr (assq 'plugged data)))
             (status (cdr (assq 'status data)))
             (temperature (cdr (assq 'temperature data)))
             (charging (and (numberp status) (= status 1)))
             (full (and (numberp status) (= status 3)))
             (on-ac (and (numberp plugged) (/= plugged 0))))
        (append
         (when (numberp capacity)
           `((?p . ,(number-to-string capacity))
             (?c . ,(number-to-string capacity))))
         `((?L . ,(if on-ac "AC" "Battery"))
           (?B . ,(cond (full "Full")
                        (charging "Charging")
                        (t "Discharging")))
           (?b . ,(cond (full "=")
                        (charging "+")
                        ((and (numberp capacity) (< capacity 10)) "!")
                        ((and (numberp capacity) (< capacity 30)) "-")
                        (t ""))))
         (when (numberp temperature)
           `((?d . ,(number-to-string (/ temperature 10.0))))))))) )

(defun ohos-battery-enable ()
  "Display battery information in the mode line using `battery-mode'."
  (interactive)
  (setq battery-status-function #'ohos-battery-status)
  (battery-mode 1))

(defun ohos-pick-host-command (command)
  "Send COMMAND to the ArkUI host.
COMMAND is a raw host command string, for example \"resize 640 480\"."
  (ohos-frame-host-operation command))

;;; Menus.

;; Without the menu bar mode there is no menu-bar-map, so F10 and the menu
;; bar have nothing to show.
(when (fboundp 'menu-bar-mode)
  (menu-bar-mode 1))

;; The backend turns a touch long press into mouse-3, and right click is
;; the natural way to reach the context menu on both PC and phone.  Enable
;; it here (rather than in a user init file) so it also works with the
;; stock runtime and without ~/.emacs.
;; context-menu-mode would install its own [down-mouse-3] binding through a
;; minor-mode map, which shadows the global binding below; the backend opens
;; the menu itself, so leave the mode alone.

;; context-menu-mode installs its binding through its minor-mode map; when
;; the event position cannot be resolved to a window Emacs falls back to the
;; global map, so bind the command there as well.  This is what makes a long
;; press (delivered as down-mouse-3) reliably open a menu.
;; Right click (or a long press, which the backend turns into mouse-3) opens
;; the context menu.  context-menu-mode cannot be used for this: it installs
;; [down-mouse-3] in its minor-mode map, which shadows this binding, and its
;; menu-item filter needs machinery the plain event path does not have.
;; Building the map and popping it up ourselves keeps one code path.
(defun ohos-write-backtrace (&rest args)
  "Write ARGS and the current backtrace to a file, then let the error signal."
  (ignore-errors
    (with-temp-file (expand-file-name "menu-error.txt" (getenv "HOME"))
      (insert (format "%S\n\n" args))
      (insert (if (fboundp 'backtrace-to-string)
                  (backtrace-to-string)
                (format "%S" (backtrace))))))
  nil)

(defun ohos-context-menu (&optional event)
  "Open the context menu for EVENT (a mouse click)."
  (interactive "e")
  (let ((click (or event last-command-event)))
    (condition-case err
        (let ((map (context-menu-map click)))
          (when (keymapp map)
            (x-popup-menu click map)))
      (error (ohos-frame-host-operation
              (format "cursor err-%s" (car err)))))))

(defun ohos-menu-bar-open ()
  "Pop up the menu bar (F10, S-F10 or the menu key)."
  (interactive)
  ;; This Emacs has no `menu-bar-map'; the menu bar is built on demand by
  ;; `mouse-menu-bar-map'.
  (condition-case err
      (x-popup-menu t (mouse-menu-bar-map))
    (error (ohos-frame-host-operation
            (format "cursor err-%s" (car err))))))

;; F10 opens the menu bar, like other GUI backends do.  Some window
;; managers reserve F10, so bind the alternatives as well.
(global-set-key [f10] #'ohos-menu-bar-open)
(global-set-key [S-f10] #'ohos-menu-bar-open)
(global-set-key [menu] #'ohos-menu-bar-open)

;;; Development hook.

;; A writable file that is reloaded whenever it changes, so Lisp experiments
;; cost one `hdc file send' instead of a rebuild + reinstall of the runtime.
;; The file is created here because the application sandbox only allows
;; overwriting existing files from outside.
(defvar ohos-dev-file (expand-file-name "ohos-dev.el" (getenv "HOME")))
(defvar ohos-dev-stamp nil)

(defun ohos-dev-poll ()
  "Load the development file when it changed since the last poll."
  (when (file-readable-p ohos-dev-file)
    (let ((stamp (file-attribute-modification-time
                  (file-attributes ohos-dev-file))))
      (unless (equal stamp ohos-dev-stamp)
        (setq ohos-dev-stamp stamp)
        (condition-case err
            (load ohos-dev-file t t)
          (error
           (ohos-frame-host-operation
            (format "cursor dev-err-%s" (car err)))))))))

(defun ohos-dev-watch ()
  "Start polling the development file."
  (unless (file-exists-p ohos-dev-file)
    (ignore-errors
      (write-region ";; ohos-dev.el: reloaded automatically when changed.\n"
                    nil ohos-dev-file nil 'silent)))
  (setq ohos-dev-stamp nil)
  (ohos-dev-poll)
  (run-with-timer 0.5 0.5 #'ohos-dev-poll))

;; Start right away: in this CANNOT_DUMP build after-init-hook is not a
;; reliable trigger, but timers run once the command loop is up.
(condition-case err
    (ohos-dev-watch)
  (error (message "ohos-dev-watch: %S" err)))

(provide 'ohos-win)
;;; ohos-win.el ends here
