;;; ohos-bundled-elf-selftest.el --- on-device acceptance test  -*- lexical-binding: t; -*-

;; Copyright (C) 2026 Free Software Foundation, Inc.

;; This file is part of GNU Emacs.

;; GNU Emacs is free software: you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation, either version 3 of the License, or
;; (at your option) any later version.

;;; Commentary:

;; Acceptance test for bundled ELF execution on HarmonyOS.  The HAP
;; ships small ELF programs in <runtime>/libexec; the application
;; sandbox refuses to execve them, so Emacs runs them with its own
;; userspace loader (src/ohosproc.c).  This test exercises that path
;; end to end and writes a report to $HOME/ohos-bundled-elf-selftest.txt.
;;
;; Start it without a terminal by launching the ability with the demo
;; parameter:
;;
;;   hdc shell aa start -b emacs.hmos.org -a EntryAbility \
;;       --ps demo selftest
;;
;; Every check is reported as PASS or FAIL; a summary line is written
;; last so a script can read the result.

;;; Code:

(defconst ohos-bundled-elf-selftest-file
  (expand-file-name "ohos-bundled-elf-selftest.txt" (getenv "HOME"))
  "Report file written by the bundled ELF acceptance test.")

(defvar ohos-bundled-elf-selftest--pass 0)
(defvar ohos-bundled-elf-selftest--fail 0)

(defun ohos-bundled-elf-selftest--write (fmt &rest args)
  (write-region (apply #'format fmt args) nil
                ohos-bundled-elf-selftest-file 'append))

(defmacro ohos-bundled-elf-selftest--check (name expected form)
  "Run FORM and record PASS/FAIL for NAME against EXPECTED."
  (declare (indent 2))
  `(let* ((expected ,expected)
          (actual (condition-case err
                      ,form
                    (error (format "ERROR %S" err))))
          (ok (equal actual expected)))
     (if ok
         (setq ohos-bundled-elf-selftest--pass
               (1+ ohos-bundled-elf-selftest--pass))
       (setq ohos-bundled-elf-selftest--fail
             (1+ ohos-bundled-elf-selftest--fail)))
     (ohos-bundled-elf-selftest--write
      "%s %-26s expected=%S actual=%S\n"
      (if ok "PASS" "FAIL") ,name expected actual)))

(defun ohos-bundled-elf-selftest--call (program args &optional env)
  "Run PROGRAM synchronously and return (RC . OUTPUT)."
  (with-temp-buffer
    (let ((process-environment (append env process-environment)))
      (cons (apply #'call-process program nil t nil args)
            (buffer-string)))))

(defun ohos-bundled-elf-selftest--wait (proc &optional seconds)
  "Wait until PROC exits, at most SECONDS."
  (let ((limit (truncate (* (or seconds 10) 20)))
        (n 0))
    (while (and (process-live-p proc) (< n limit))
      (accept-process-output nil 0.05)
      (setq n (1+ n))))
  proc)

(defun ohos-bundled-elf-selftest--elf (name)
  "Return the libexec path of bundled program NAME."
  (expand-file-name name
                    (concat (file-name-directory
                             (directory-file-name (getenv "HOME")))
                            "libexec/")))

;;;###autoload
(defun ohos-bundled-elf-selftest ()
  "Run the bundled ELF acceptance test and write its report."
  (interactive)
  (setq ohos-bundled-elf-selftest--pass 0
        ohos-bundled-elf-selftest--fail 0)
  (delete-file ohos-bundled-elf-selftest-file)
  (ohos-bundled-elf-selftest--write
   "emacs=%S\nlibexec=%S\nexec-path=%S\n"
   (emacs-version) (ohos-bundled-elf-selftest--elf "hello") exec-path)

  ;; Dynamic, static and static-PIE images.
  (ohos-bundled-elf-selftest--check "hello-argv" 0
    (car (ohos-bundled-elf-selftest--call
          "hello" '("arg one" "中文"))))
  (ohos-bundled-elf-selftest--check "hello-argv-text" t
    (not (null (string-match-p
                "argv\\[1\\]=arg one"
                (cdr (ohos-bundled-elf-selftest--call
                      "hello" '("arg one" "中文")))))))
  (ohos-bundled-elf-selftest--check "hello-static" 0
    (car (ohos-bundled-elf-selftest--call "hello-static" '("static"))))
  (ohos-bundled-elf-selftest--check "hello-pie" 0
    (car (ohos-bundled-elf-selftest--call "hello-pie" '("pie"))))
  (ohos-bundled-elf-selftest--check "cpphello" 0
    (car (ohos-bundled-elf-selftest--call "cpphello" nil)))
  (ohos-bundled-elf-selftest--check "cjk-file-name" 0
    (car (ohos-bundled-elf-selftest--call "hello 中文" '("x"))))
  (ohos-bundled-elf-selftest--check "many-args" 0
    (car (ohos-bundled-elf-selftest--call
          "hello" (append (make-list 60 "arg with space") '("last")))))

  ;; Environment and working directory.
  (ohos-bundled-elf-selftest--check "environment" t
    (not (null (string-match-p
                "env=value with space"
                (cdr (ohos-bundled-elf-selftest--call
                      "hello" nil '("OHOS_BUNDLED_TEST=value with space")))))))
  (ohos-bundled-elf-selftest--check "cwd" t
    (let* ((runtime (file-name-directory
                     (directory-file-name (getenv "HOME"))))
           (default-directory runtime))
      (not (null
            (string-match-p
             (concat "cwd=" (regexp-quote (directory-file-name runtime)))
             (cdr (ohos-bundled-elf-selftest--call "hello" nil)))))))

  ;; Standard input, standard output and standard error.
  (ohos-bundled-elf-selftest--check "stdin-pipe" "line-1\nline-2\n"
    (with-temp-buffer
      (insert "line-1\nline-2\n")
      (let ((rc (call-process-region (point-min) (point-max) "cat" t t)))
        (if (eq rc 0) (buffer-string) (format "rc=%S" rc)))))
  (ohos-bundled-elf-selftest--check "stderr-split" "err-line\n"
    (let* ((errfile (expand-file-name "selftest-stderr.txt" (getenv "HOME")))
           (out (generate-new-buffer " *selftest-stdout*")))
      (unwind-protect
          (progn
            (delete-file errfile)
            (call-process "err" nil (list out errfile))
            (with-temp-buffer
              (insert-file-contents errfile)
              (buffer-string)))
        (kill-buffer out))))

  ;; Exit status and signals.
  (ohos-bundled-elf-selftest--check "exit-code" 7
    (car (ohos-bundled-elf-selftest--call "exit" '("7"))))
  (ohos-bundled-elf-selftest--check "signal" '(signal 15)
    (let* ((proc (start-process "selftest-term" nil "term"))
           (pid (process-id proc)))
      (accept-process-output nil 0.5)
      (signal-process pid 'SIGTERM)
      (ohos-bundled-elf-selftest--wait proc 5)
      (list (process-status proc) (process-exit-status proc))))

  ;; Asynchronous and concurrent processes.
  (ohos-bundled-elf-selftest--check "async-delete" 0
    (let* ((procs (mapcar (lambda (i)
                            (start-process (format "selftest-par-%d" i) nil
                                           "sleep" "20"))
                          (number-sequence 1 5))))
      (mapc #'delete-process procs)
      (length (seq-filter (lambda (p) (process-live-p p)) procs))))
  (ohos-bundled-elf-selftest--check "pty" 0
    (let ((proc (make-process :name "selftest-pty" :command '("hello" "pty")
                              :connection-type 'pty :noquery t)))
      (ohos-bundled-elf-selftest--wait proc 10)
      (process-exit-status proc)))
  (ohos-bundled-elf-selftest--check "make-process-pipe" 0
    (let ((proc (make-process :name "selftest-pipe" :command '("hello" "pipe")
                              :connection-type 'pipe :noquery t)))
      (ohos-bundled-elf-selftest--wait proc 10)
      (process-exit-status proc)))

  ;; A bundled program can fork and exec system programs itself.
  (ohos-bundled-elf-selftest--check "child-exec" t
    (not (null (string-match-p
                "child-exec-ok"
                (cdr (ohos-bundled-elf-selftest--call "spawn" nil))))))

  ;; #! scripts in the app data directory.
  (ohos-bundled-elf-selftest--check "script" 0
    (let ((script (expand-file-name "selftest-script.sh" (getenv "HOME"))))
      (with-temp-file script
        (insert "#!/system/bin/sh\necho script-ok\n"))
      (set-file-modes script #o755)
      (car (ohos-bundled-elf-selftest--call script nil))))

  ;; A non-ELF file must fail with a clear error instead of hanging.
  (ohos-bundled-elf-selftest--check "not-an-elf" 126
    (let ((file (expand-file-name "selftest-not-elf" (getenv "HOME"))))
      (with-temp-file file (insert "just text\n"))
      (set-file-modes file #o755)
      (car (ohos-bundled-elf-selftest--call file nil))))

  ;; Regression: system programs still go through execve untouched.
  (ohos-bundled-elf-selftest--check "system-sh" 0
    (car (ohos-bundled-elf-selftest--call
          "/system/bin/sh" '("-c" "echo sh-ok"))))
  (ohos-bundled-elf-selftest--check "system-echo" 0
    (car (ohos-bundled-elf-selftest--call "/system/bin/echo" '("echo-ok"))))
  (ohos-bundled-elf-selftest--check "shell-command" 0
    (shell-command "exit 0"))
  (ohos-bundled-elf-selftest--check "process-file" 4
    (process-file "/system/bin/sh" nil nil nil "-c" "exit 4"))

  ;; Nothing left running.
  (ohos-bundled-elf-selftest--check "no-zombies" 0
    (length (seq-filter #'process-live-p (process-list))))

  (ohos-bundled-elf-selftest--write
   "SUMMARY pass=%d fail=%d\n"
   ohos-bundled-elf-selftest--pass ohos-bundled-elf-selftest--fail)
  (message "bundled-elf selftest: %d passed, %d failed"
           ohos-bundled-elf-selftest--pass ohos-bundled-elf-selftest--fail))

(provide 'ohos-bundled-elf-selftest)
;;; ohos-bundled-elf-selftest.el ends here
