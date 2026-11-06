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

(declare-function ohos-menu-bar-item "ohosmenu.c" (index))

;; Choosing an item in the native menu bar must *run* its command.  The
;; tty backend gets that from `popup-menu': `x-popup-menu' returns the
;; key sequence of the selected item, `lookup-key' turns it into the
;; binding, and `call-interactively' runs it.  The OHOS backend had the
;; C side call `x-popup-menu' and hand the returned binding to a timer
;; that threw it away, so clicking a menu item never did anything
;; (task book M-2).  Do the same three steps here instead.
(defun ohos-menu-bar-command (event map)
  "Return the command bound to menu EVENT in keymap MAP, or nil."
  (cond
   ((keymapp map)
    (lookup-key map event))
   ((and (listp map) (keymapp (car map)))
    (catch 'ohos-menu-item
      (dolist (m map)
        (let ((binding (lookup-key m event)))
          (when binding (throw 'ohos-menu-item binding))))
      nil))
   (t
    (ignore-errors (key-binding event)))))

(defun ohos-menu-bar-open-index (index)
  "Pop up menu bar item INDEX of the selected frame and run its command.
Return the chosen command when it was executed."
  (let* ((def (ohos-menu-bar-item index))
         (map (and def (indirect-function def)))
         (value (and def (x-popup-menu t def))))
    (when value
      (let* ((event (if (vectorp value) value (apply #'vector value)))
             (cmd (ohos-menu-bar-command event map)))
        (when (and (> (length event) 0) (commandp cmd))
          ;; Several menu commands (mouse-yank-at-click, isearch) read
          ;; `last-command-event'.
          (setq last-command-event (aref event (1- (length event))))
          (let ((from--tty-menu-p t))
            (call-interactively cmd))
          cmd)))))

;; Use Emacs' own menu machinery.  `menu-bar-mode' gives the frame a menu
;; bar row, and `menu-bar-open' (already bound to [f10] in menu-bar.el) has
;; a generic branch for non-toolkit frames: it looks the item up in the
;; global map and calls `popup-menu' with a position, which ends up in the
;; backend's menu_show_hook.  `context-menu-mode' likewise provides the
;; right-click / long-press context menu through `context-menu-entry'.
;; Nothing here re-binds keys: doing that shadowed the standard bindings and
;; hid the fact that the menu bar row was empty.
;;; Display capabilities.

;; The OHOS backend has C stubs for `xw-display-color-p' and
;; `x-display-grayscale-p' (ohosgui_stubs.c) but never registers them as
;; Lisp subrs, so `display-color-p' could not classify the frame.  Without
;; that classification every `(class color)' / `(min-colors 88)' /
;; `(background light)' face spec is skipped and faces such as `highlight'
;; or `mode-line-highlight' stay unspecified -- no hover highlight, no
;; coloured mode line (task book B-15).
;; Startup contract: after a frame exists the standard init runs
;; (startup.el: frame-notice-user-settings, frame-set-background-mode,
;; face-set-after-frame-default) to classify the frame and realize every
;; face from its spec.  On this port the initial frame is created outside
;; that path, so the parameters `display-type'/`background-mode' stayed
;; nil and the mouse-feedback faces kept the mono fallback (B-17).  Run the
;; same two initializers as soon as the frame exists -- before the user's
;; init file touches the faces.
(defun ohos-init-frame-faces (frame)
  "Run the standard face initializers for FRAME."
  (when (and (frame-live-p frame) (display-graphic-p frame))
    (with-selected-frame frame
      (condition-case nil
          (frame-set-background-mode frame)
        (error nil))
      (condition-case nil
          (face-set-after-frame-default frame)
        (error nil)))))

(add-hook 'after-make-frame-functions #'ohos-init-frame-faces)
(condition-case nil
    (progn
      (ohos-init-frame-faces (selected-frame))
      (when (fboundp 'frame-notice-user-settings)
        (frame-notice-user-settings)))
  (error nil))

;; The shipped init file (`~/.emacs') runs `internal-make-lisp-face' for
;; every face on the frame, and that call resets all the frame-local face
;; attributes to `unspecified' (xfaces.c, internal-make-lisp-face).  The
;; faces therefore lose what the startup initialization had just realized,
;; which is why `highlight' and `mode-line-highlight' fell back to
;; `(t :inverse-video t)': no colour on hover, no mode-line box (B-17).
;; Re-run the standard initializer after the init file, instead of editing
;; the init file itself.
(defun ohos-refresh-frame-faces (&rest _)
  "Realize every face of the OHOS frames from its spec."
  (dolist (frame (frame-list))
    (when (and (frame-live-p frame) (display-graphic-p frame))
      (condition-case nil
          (with-selected-frame frame
            (face-set-after-frame-default frame))
        (error nil)))))

(add-hook 'window-setup-hook #'ohos-refresh-frame-faces)

;; Image support: PNG through Emacs' own png_load (libpng, built with the
;; OHOS NDK), SVG through the port's nanosvg loader (librsvg is not
;; available).  Both decoders write into the backend pixmap, which the
;; IMAGE_GLYPH path blits, and that pixmap path is generic: every type
;; image.c registers can be drawn the same way.  So the list is Emacs'
;; own plus svg, not a hand-picked subset (a subset makes create-image
;; reject perfectly drawable types with "Invalid image type").
(setq image-types (delete-dups (cons 'svg image-types)))


(when (fboundp 'menu-bar-mode)
  (menu-bar-mode 1))

(when (fboundp 'context-menu-mode)
  (context-menu-mode 1))

;; The host's native menu bar asks for item N to be opened; the request is
;; picked up here so the popup runs from a Lisp context (the backend only
;; stores the index).
(defun ohos-menu-bar-poll ()
  "Open the menu bar item the host asked for, if any."
  (let ((index (ohos-menu-bar-take-request)))
    (when (integerp index)
      (condition-case nil
          (ohos-menu-bar-open-index index)
        (error nil)))))

(run-with-timer 0.5 0.5 #'ohos-menu-bar-poll)

;; The menu bar content depends on the buffer and its modes.  Polling it
;; once a second keeps the native bar in sync without depending on when
;; update_menu_bar decides to run; the backend only talks to the host when
;; the item list actually changed.
(run-with-timer 5 5 #'ohos-menu-bar-refresh)

(defun ohos-menu-bar-open (&optional frame)
  "Open the menu bar of FRAME, the ArkUI bar counterpart of menu-bar-open."
  (interactive)
  (with-selected-frame (or frame (selected-frame))
    (ohos-menu-bar-open-index 0)))

;;; Custom Themes widget compatibility.

;; `widget-button-click' waits for the release event and cancels the
;; press when it sees motion.  With the OHOS pointer event stream the
;; release is a separate `mouse-1' event, but the down/up pair can be
;; consumed before the widget tracking loop sees it.  Invoke the widget
;; at the release position directly, which is what a GUI mouse click is
;; expected to do.
(defun ohos-widget-button-press (event)
  "Press the widget at the release position of EVENT."
  (interactive "e")
  (let ((pos (posn-point (event-start event))))
    (when (and pos (get-char-property pos 'button))
      (widget-button-press pos event))))

(with-eval-after-load 'cus-theme
  (define-key custom-theme-choose-mode-map [mouse-1]
    #'ohos-widget-button-press))

;; Native wheel events are delivered by the OHOS backend as WHEEL_EVENT.
(when (fboundp 'mouse-wheel-mode)
  (mouse-wheel-mode 1))
(when (fboundp 'xterm-mouse-mode)
  (xterm-mouse-mode -1))

;; Use Eww for in-program links instead of handing them to another app.
(condition-case nil
    (progn
      (require 'eww)
      (setq browse-url-browser-function #'eww-browse-url))
  (error nil))


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
      ;; The cookie matters: Emacs 30 warns for every .el loaded without
      ;; `lexical-binding', and this file is reloaded on every change.
      (write-region
       ";;; ohos-dev.el --- development overlay  -*- lexical-binding: t; -*-\n;; Reloaded automatically when changed.\n"
       nil ohos-dev-file nil 'silent)))
  (setq ohos-dev-stamp nil)
  (ohos-dev-poll)
  (run-with-timer 0.4 0.4 #'ohos-dev-poll))

;; Start right away: in this CANNOT_DUMP build after-init-hook is not a
;; reliable trigger, but timers run once the command loop is up.
(condition-case err
    (ohos-dev-watch)
  (error (message "ohos-dev-watch: %S" err)))

(provide 'ohos-win)
;;; ohos-win.el ends here
