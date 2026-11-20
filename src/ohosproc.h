/* Bundled ELF execution for HarmonyOS.

   Copyright (C) 2026 Free Software Foundation, Inc.

   This file is part of GNU Emacs.

   GNU Emacs is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.  */

#ifndef OHOSPROC_H
#define OHOSPROC_H

/* Set by emacs_spawn before forking when ARGV[0] is a bundled ELF or
   script that the sandbox will not exec directly.  The forked child
   reads it in child_setup.  */
extern int ohos_child_loader;

/* True when FILE is an app-private ELF or #! script.  */
int ohos_bundled_program_p (const char *file, const char *cwd);

/* Execute ARGV/ENVP in the forked child, using the userspace loader
   when execve is denied.  Returns an errno value on failure.  */
int ohos_exec_bundled (char *const argv[], char *const envp[]);

#endif /* OHOSPROC_H */
