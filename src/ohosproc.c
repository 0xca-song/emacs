/* Bundled ELF loader for HarmonyOS NEXT.

   The OHOS application sandbox denies execve of files outside the
   system image, including the application's own data and native
   library directories, and it denies mmap(PROT_EXEC) of app-data
   files.  It does allow PROT_EXEC on anonymous memory, so a forked
   child can reproduce what the kernel's ELF loader does:

     - read the target image from the app data directory;
     - copy its PT_LOAD segments into anonymous memory;
     - apply the final segment protections;
     - build an initial stack with argv/envp/auxv;
     - either jump to the target entry (static) or to the dynamic
       interpreter's entry (dynamic), with AT_BASE/AT_ENTRY/AT_PHDR
       set the way the kernel would set them.

   Everything here runs after fork() in the child of a multi-threaded
   Emacs, so it must not take libc locks.  Only raw syscalls and
   async-signal-safe helpers are used.

   Copyright (C) 2026 Free Software Foundation, Inc.

   This file is part of GNU Emacs.

   GNU Emacs is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.  */

#include <config.h>

#include "ohosproc.h"

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/auxv.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <elf.h>

#ifndef AT_HWCAP2
# define AT_HWCAP2 26
#endif
#ifndef AT_SYSINFO_EHDR
# define AT_SYSINFO_EHDR 33
#endif
#ifndef AT_MINSIGSTKSZ
# define AT_MINSIGSTKSZ 51
#endif

/* Set by emacs_spawn before fork; read by child_setup after it.  */
int ohos_child_loader;

#define OHOS_PAGE_SIZE 4096UL

/* Native library directory of this HAP, where libc++_shared.so lives.
   Bundled programs are given it on LD_LIBRARY_PATH, the way the
   application's own loader resolves its libraries.  */
#if defined __aarch64__
# define OHOS_BUNDLE_LIBS "/data/storage/el1/bundle/libs/arm64"
#elif defined __x86_64__
# define OHOS_BUNDLE_LIBS "/data/storage/el1/bundle/libs/x86_64"
#else
# define OHOS_BUNDLE_LIBS ""
#endif
#define OHOS_MAX_PHDR 64
#define OHOS_MAX_INTERP_PATH 256
#define OHOS_MAX_PATH 512
#define OHOS_STACK_SLACK (1024UL * 1024)

/* Diagnostics go to fd 2 and are best effort.  */
static void
ohos_msg (const char *what, const char *detail, int err)
{
  char buf[512];
  size_t n = 0;
  const char *s;
  for (s = "ohos-exec: "; *s && n < sizeof buf - 1; s++) buf[n++] = *s;
  for (s = what; s && *s && n < sizeof buf - 1; s++) buf[n++] = *s;
  if (detail)
    {
      for (s = " "; *s && n < sizeof buf - 1; s++) buf[n++] = *s;
      for (s = detail; *s && n < sizeof buf - 1; s++) buf[n++] = *s;
    }
  if (err)
    {
      buf[n++] = ':';
      buf[n++] = ' ';
      int v = err;
      char tmp[12];
      int i = 0;
      if (v < 0) v = -v;
      do { tmp[i++] = '0' + v % 10; v /= 10; } while (v);
      while (i && n < sizeof buf - 1) buf[n++] = tmp[--i];
    }
  buf[n++] = '\n';
  (void) !write (2, buf, n);
}

struct ohos_elf
{
  int fd;
  Elf64_Ehdr eh;
  Elf64_Phdr ph[OHOS_MAX_PHDR];
  unsigned phnum;
  unsigned long long min_page, max_page;
  uintptr_t bias;               /* vaddr -> runtime address */
  uintptr_t entry;
  uintptr_t phdr;               /* runtime address of e_phoff */
  const char *interp;           /* static storage, NULL when static */
  char interp_buf[OHOS_MAX_INTERP_PATH];
  int is_dynamic;
};

static int ohos_load_bundled (const char *path, char *const argv[],
			      char *const envp[]);

static int
ohos_read_full (int fd, void *buf, size_t len, unsigned long long off)
{
  char *p = buf;
  size_t done = 0;
  while (done < len)
    {
      long r = syscall (SYS_pread64, fd, p + done, len - done, off + done);
      if (r <= 0)
	return -1;
      done += (size_t) r;
    }
  return 0;
}

/* Read and validate the ELF header and program headers of PATH.  */
static int
ohos_elf_open (struct ohos_elf *img, const char *path)
{
  memset (img, 0, sizeof *img);
  img->fd = syscall (SYS_openat, AT_FDCWD, path, O_RDONLY | O_CLOEXEC, 0);
  if (img->fd < 0)
    {
      ohos_msg ("cannot open", path, errno);
      return -1;
    }
  if (ohos_read_full (img->fd, &img->eh, sizeof img->eh, 0))
    {
      ohos_msg ("short read", path, 0);
      return -1;
    }
  if (memcmp (img->eh.e_ident, ELFMAG, SELFMAG)
      || img->eh.e_ident[EI_CLASS] != ELFCLASS64
      || img->eh.e_ident[EI_DATA] != ELFDATA2LSB
      || img->eh.e_machine != EM_AARCH64)
    {
      ohos_msg ("not an aarch64 ELF", path, 0);
      return -1;
    }
  if (img->eh.e_type != ET_EXEC && img->eh.e_type != ET_DYN)
    {
      ohos_msg ("unsupported ELF type", path, 0);
      return -1;
    }
  if (img->eh.e_phnum == 0 || img->eh.e_phnum > OHOS_MAX_PHDR
      || img->eh.e_phentsize != sizeof (Elf64_Phdr))
    {
      ohos_msg ("bad program header table", path, 0);
      return -1;
    }
  img->phnum = img->eh.e_phnum;
  if (ohos_read_full (img->fd, img->ph, img->phnum * sizeof (Elf64_Phdr),
		      img->eh.e_phoff))
    {
      ohos_msg ("cannot read program headers", path, 0);
      return -1;
    }

  img->min_page = ~0ULL;
  img->max_page = 0;
  for (unsigned i = 0; i < img->phnum; i++)
    {
      Elf64_Phdr *ph = &img->ph[i];
      if (ph->p_type == PT_LOAD)
	{
	  unsigned long long s = ph->p_vaddr & ~(OHOS_PAGE_SIZE - 1);
	  unsigned long long e = (ph->p_vaddr + ph->p_memsz
				  + OHOS_PAGE_SIZE - 1)
	    & ~(OHOS_PAGE_SIZE - 1);
	  if (s < img->min_page) img->min_page = s;
	  if (e > img->max_page) img->max_page = e;
	}
      else if (ph->p_type == PT_INTERP)
	{
	  if (ph->p_filesz == 0 || ph->p_filesz >= OHOS_MAX_INTERP_PATH)
	    {
	      ohos_msg ("bad PT_INTERP", path, 0);
	      return -1;
	    }
	  if (ohos_read_full (img->fd, img->interp_buf, ph->p_filesz,
			      ph->p_offset))
	    {
	      ohos_msg ("cannot read PT_INTERP", path, 0);
	      return -1;
	    }
	  img->interp_buf[ph->p_filesz] = '\0';
	  img->interp = img->interp_buf;
	  img->is_dynamic = 1;
	}
    }
  if (img->min_page >= img->max_page)
    {
      ohos_msg ("no PT_LOAD segment", path, 0);
      return -1;
    }
  return 0;
}

/* Reserve the whole image span.  For ET_DYN the kernel picks the base;
   for ET_EXEC the linked addresses are used and any inherited mapping
   in the way is replaced (the child never returns to Emacs).  */
static int
ohos_elf_reserve (struct ohos_elf *img, const char *path)
{
  size_t span = img->max_page - img->min_page;
  void *want = NULL;
  int fixed = 0;
  if (img->eh.e_type == ET_EXEC)
    {
      /* Fixed-address images replace whatever mapping is in the way;
	 refuse the (pathological) case where that would erase the
	 loader itself or the stack we are running on.  */
      uintptr_t self = (uintptr_t) &ohos_load_bundled;
      uintptr_t here = (uintptr_t) __builtin_frame_address (0);
      if ((self >= img->min_page && self < img->max_page)
	  || (here >= img->min_page && here < img->max_page))
	{
	  ohos_msg ("image overlaps the loader", path, 0);
	  return ENOEXEC;
	}
      want = (void *) (uintptr_t) img->min_page;
      fixed = MAP_FIXED;
    }
  void *got = (void *) syscall (SYS_mmap, want, span,
				PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS | fixed, -1, 0);
  if ((long) got < 0 && got != MAP_FAILED)
    {
      ohos_msg ("mmap failed", path, (int) -(long) got);
      return -1;
    }
  if (got == MAP_FAILED)
    {
      ohos_msg ("mmap failed", path, errno);
      return -1;
    }
  if (fixed && (uintptr_t) got != img->min_page)
    {
      ohos_msg ("MAP_FIXED moved", path, 0);
      return -1;
    }
  img->bias = (uintptr_t) got - img->min_page;
  img->entry = img->bias + img->eh.e_entry;
  /* e_phoff is a file offset: translate it through the PT_LOAD that
     contains it, which is not necessarily at virtual address zero
     (ET_EXEC images usually start at 0x200000).  */
  img->phdr = 0;
  for (unsigned i = 0; i < img->phnum; i++)
    {
      Elf64_Phdr *ph = &img->ph[i];
      if (ph->p_type != PT_LOAD)
	continue;
      if (img->eh.e_phoff >= ph->p_offset
	  && img->eh.e_phoff < ph->p_offset + ph->p_filesz)
	{
	  img->phdr = (img->bias + ph->p_vaddr
		       + (img->eh.e_phoff - ph->p_offset));
	  break;
	}
    }
  if (!img->phdr)
    {
      ohos_msg ("cannot locate program headers", path, 0);
      return -1;
    }
  return 0;
}

/* Copy PT_LOAD contents and apply segment protections.  */
static int
ohos_elf_load (struct ohos_elf *img, const char *path)
{
  for (unsigned i = 0; i < img->phnum; i++)
    {
      Elf64_Phdr *ph = &img->ph[i];
      if (ph->p_type != PT_LOAD || ph->p_filesz == 0)
	continue;
      char *dst = (char *) (img->bias + ph->p_vaddr);
      unsigned long long left = ph->p_filesz;
      unsigned long long off = ph->p_offset;
      while (left)
	{
	  long r = syscall (SYS_pread64, img->fd, dst, left, off);
	  if (r <= 0)
	    {
	      ohos_msg ("segment read failed", path, errno);
	      return -1;
	    }
	  dst += r;
	  off += (unsigned long long) r;
	  left -= (unsigned long long) r;
	}
    }
  for (unsigned i = 0; i < img->phnum; i++)
    {
      Elf64_Phdr *ph = &img->ph[i];
      if (ph->p_type != PT_LOAD || ph->p_memsz <= ph->p_filesz)
	continue;
      char *zero = (char *) (img->bias + ph->p_vaddr + ph->p_filesz);
      memset (zero, 0, ph->p_memsz - ph->p_filesz);
    }

  /* Per-page protection.  Segments normally do not share pages; when
     they do, take the union so both halves stay usable.  */
  unsigned long long pages = (img->max_page - img->min_page) / OHOS_PAGE_SIZE;
  for (unsigned long long pg = 0; pg < pages; pg++)
    {
      unsigned long long page = img->min_page + pg * OHOS_PAGE_SIZE;
      int prot = 0;
      for (unsigned i = 0; i < img->phnum; i++)
	{
	  Elf64_Phdr *ph = &img->ph[i];
	  if (ph->p_type != PT_LOAD)
	    continue;
	  unsigned long long s = ph->p_vaddr & ~(OHOS_PAGE_SIZE - 1);
	  unsigned long long e = (ph->p_vaddr + ph->p_memsz
				  + OHOS_PAGE_SIZE - 1)
	    & ~(OHOS_PAGE_SIZE - 1);
	  if (page < s || page >= e)
	    continue;
	  if (ph->p_flags & PF_R) prot |= PROT_READ;
	  if (ph->p_flags & PF_W) prot |= PROT_WRITE;
	  if (ph->p_flags & PF_X) prot |= PROT_EXEC;
	}
      if (!prot)
	continue;
      void *addr = (void *) (img->bias + page);
      long r = syscall (SYS_mprotect, addr, OHOS_PAGE_SIZE, prot);
      if (r < 0)
	{
	  ohos_msg ("mprotect failed", path, (int) -r);
	  return -1;
	}
    }
  return 0;
}

static int
ohos_elf_close (struct ohos_elf *img)
{
  if (img->fd >= 0)
    syscall (SYS_close, img->fd);
  img->fd = -1;
  return 0;
}

/* ---- Signed bundle library mapping -------------------------------- */

static const char *
ohos_bundle_dir (void)
{
  const char *env = getenv ("EMACS_OHOS_BUNDLE_LIBS");
  if (env && env[0] == '/')
    return env;
  return OHOS_BUNDLE_LIBS;
}

/* A bundle library name is a leaf file name, never a path.  */
static int
ohos_valid_lib_name (const char *name)
{
  if (!name || strncmp (name, "libohos_exec_", 13) != 0)
    return 0;
  size_t n = strlen (name);
  if (n < 17 || strcmp (name + n - 3, ".so") != 0)
    return 0;
  for (size_t i = 0; i < n; i++)
    {
      char c = name[i];
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
	    || (c >= '0' && c <= '9') || c == '_' || c == '.'
	    || c == '-'))
	return 0;
    }
  return 1;
}

/* Read the runtime/libexec stub at PATH.  Value 1 when it is a stub,
   with the target library name copied into LIBNAME.  */
static int
ohos_read_stub (const char *path, char *libname, size_t size)
{
  char buf[192];
  static const char magic[] = "OHOS-BUNDLE/1\n";
  long n;
  int fd = syscall (SYS_openat, AT_FDCWD, path, O_RDONLY | O_CLOEXEC, 0);
  if (fd < 0)
    return 0;
  n = syscall (SYS_read, fd, buf, sizeof buf - 1);
  syscall (SYS_close, fd);
  if (n <= (long) (sizeof magic - 1))
    return 0;
  buf[n] = '\0';
  if (strncmp (buf, magic, sizeof magic - 1) != 0)
    return 0;
  char *start = buf + sizeof magic - 1;
  char *nl = strchr (start, '\n');
  if (nl)
    *nl = '\0';
  size_t len = strlen (start);
  if (len == 0 || len >= size || !ohos_valid_lib_name (start))
    return 0;
  memcpy (libname, start, len + 1);
  return 1;
}

static unsigned long long
ohos_align_up (unsigned long long v, unsigned long long a)
{
  return (v + a - 1) & ~(a - 1);
}

/* Fill ENTRY and PHDR from the mapped image.  */
static int
ohos_set_entry (struct ohos_elf *img, const char *path)
{
  img->entry = img->bias + img->eh.e_entry;
  img->phdr = 0;
  for (unsigned i = 0; i < img->phnum; i++)
    {
      Elf64_Phdr *ph = &img->ph[i];
      if (ph->p_type != PT_LOAD)
	continue;
      if (img->eh.e_phoff >= ph->p_offset
	  && img->eh.e_phoff < ph->p_offset + ph->p_filesz)
	{
	  img->phdr = img->bias + ph->p_vaddr
	    + (img->eh.e_phoff - ph->p_offset);
	  break;
	}
    }
  if (!img->phdr)
    {
      ohos_msg ("cannot locate program headers", path, 0);
      return -1;
    }
  return 0;
}

/* Map an image directly from a signed file.  All executable pages are
   file-backed; only .bss and the reservation use anonymous memory, and
   never with PROT_EXEC.  */
static int
ohos_map_file_image (struct ohos_elf *img, const char *path)
{
  size_t span = img->max_page - img->min_page;
  unsigned long long align = OHOS_PAGE_SIZE;

  for (unsigned i = 0; i < img->phnum; i++)
    if (img->ph[i].p_type == PT_LOAD
	&& img->ph[i].p_align > align
	&& img->ph[i].p_align <= (1UL << 20))
      align = img->ph[i].p_align;

  /* Reject W+X segments and overlapping segments with different
     protections; such images cannot be mapped without RWX pages.  */
  for (unsigned i = 0; i < img->phnum; i++)
    {
      Elf64_Phdr *a = &img->ph[i];
      if (a->p_type != PT_LOAD)
	continue;
      if ((a->p_flags & PF_W) && (a->p_flags & PF_X))
	{
	  ohos_msg ("RWX segment", path, 0);
	  return ENOEXEC;
	}
      unsigned long long as = a->p_vaddr & ~(OHOS_PAGE_SIZE - 1);
      unsigned long long ae = ohos_align_up (a->p_vaddr + a->p_memsz,
					     OHOS_PAGE_SIZE);
      for (unsigned j = i + 1; j < img->phnum; j++)
	{
	  Elf64_Phdr *b = &img->ph[j];
	  if (b->p_type != PT_LOAD)
	    continue;
	  unsigned long long bs = b->p_vaddr & ~(OHOS_PAGE_SIZE - 1);
	  unsigned long long be = ohos_align_up (b->p_vaddr + b->p_memsz,
						 OHOS_PAGE_SIZE);
	  if (as < be && bs < ae && a->p_flags != b->p_flags)
	    {
	      ohos_msg ("overlapping segments with different protections",
			path, 0);
	      return ENOEXEC;
	    }
	}
    }

  uintptr_t base;
  if (img->eh.e_type == ET_EXEC)
    {
      uintptr_t self = (uintptr_t) &ohos_map_file_image;
      uintptr_t here = (uintptr_t) __builtin_frame_address (0);
      if ((self >= img->min_page && self < img->max_page)
	  || (here >= img->min_page && here < img->max_page))
	{
	  ohos_msg ("image overlaps the loader", path, 0);
	  return ENOEXEC;
	}
      base = (uintptr_t) img->min_page;
    }
  else
    {
      size_t rlen = span + align;
      void *res = (void *) syscall (SYS_mmap, NULL, rlen, PROT_NONE,
				    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if ((long) res < 0 && res != MAP_FAILED)
	{
	  ohos_msg ("reserve failed", path, (int) -(long) res);
	  return ENOMEM;
	}
      if (res == MAP_FAILED)
	{
	  ohos_msg ("reserve failed", path, errno);
	  return ENOMEM;
	}
      base = ((uintptr_t) res + align - 1) & ~(uintptr_t) (align - 1);
      if (base > (uintptr_t) res)
	syscall (SYS_munmap, res, base - (uintptr_t) res);
      uintptr_t tail = base + span;
      uintptr_t end = (uintptr_t) res + rlen;
      if (end > tail)
	syscall (SYS_munmap, (void *) tail, end - tail);
    }
  img->bias = base - img->min_page;

  for (unsigned i = 0; i < img->phnum; i++)
    {
      Elf64_Phdr *ph = &img->ph[i];
      if (ph->p_type != PT_LOAD || ph->p_filesz == 0)
	continue;
      int prot = PROT_READ;
      if (ph->p_flags & PF_W) prot |= PROT_WRITE;
      if (ph->p_flags & PF_X) prot |= PROT_EXEC;
      unsigned long long vstart = ph->p_vaddr & ~(OHOS_PAGE_SIZE - 1);
      unsigned long long fstart = ph->p_offset & ~(OHOS_PAGE_SIZE - 1);
      unsigned long long page_end = ohos_align_up (ph->p_vaddr + ph->p_filesz,
						   OHOS_PAGE_SIZE);
      if (page_end > vstart)
	{
	  long r = syscall (SYS_mmap, (void *) (img->bias + vstart),
			    page_end - vstart, prot,
			    MAP_PRIVATE | MAP_FIXED, img->fd, fstart);
	  if (r < 0)
	    {
	      ohos_msg ("file mmap failed", path, (int) -r);
	      return ENOEXEC;
	    }
	}
    }

  /* Zero the tail of the last file page and map .bss anonymously.  */
  for (unsigned i = 0; i < img->phnum; i++)
    {
      Elf64_Phdr *ph = &img->ph[i];
      if (ph->p_type != PT_LOAD || ph->p_memsz <= ph->p_filesz)
	continue;
      int prot = PROT_READ;
      if (ph->p_flags & PF_W) prot |= PROT_WRITE;
      if (ph->p_flags & PF_X) prot |= PROT_EXEC;

      unsigned long long bss_start;
      if (ph->p_filesz == 0)
	{
	  bss_start = ph->p_vaddr & ~(OHOS_PAGE_SIZE - 1);
	}
      else
	{
	  unsigned long long pad = ph->p_vaddr + ph->p_filesz;
	  unsigned long long pad_page_end = ohos_align_up (pad, OHOS_PAGE_SIZE);
	  if (pad < pad_page_end)
	    {
	      uintptr_t page = img->bias + (pad & ~(OHOS_PAGE_SIZE - 1));
	      if (!(prot & PROT_WRITE))
		syscall (SYS_mprotect, (void *) page, OHOS_PAGE_SIZE,
			 PROT_READ | PROT_WRITE);
	      memset ((void *) (img->bias + pad), 0, pad_page_end - pad);
	      if (!(prot & PROT_WRITE))
		syscall (SYS_mprotect, (void *) page, OHOS_PAGE_SIZE, prot);
	    }
	  bss_start = pad_page_end;
	}
      unsigned long long bss_end = ohos_align_up (ph->p_vaddr + ph->p_memsz,
						  OHOS_PAGE_SIZE);
      if (bss_end > bss_start)
	{
	  long r = syscall (SYS_mmap, (void *) (img->bias + bss_start),
			    bss_end - bss_start, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);
	  if (r < 0)
	    {
	      ohos_msg ("bss mmap failed", path, (int) -r);
	      return ENOMEM;
	    }
	}
    }

  return ohos_set_entry (img, path) ? ENOEXEC : 0;
}

/* Turn off every inherited signal disposition so the target sees the
   post-exec state.  */
static void
ohos_reset_signals (void)
{
  struct sigaction sa;
  memset (&sa, 0, sizeof sa);
  sa.sa_handler = SIG_DFL;
  for (int sig = 1; sig < NSIG; sig++)
    sigaction (sig, &sa, NULL);
  sigset_t empty;
  sigemptyset (&empty);
  sigprocmask (SIG_SETMASK, &empty, NULL);
  /* A stale alternate signal stack inherited from Emacs must not be
     reused by the target's own handlers.  */
  stack_t ss;
  memset (&ss, 0, sizeof ss);
  ss.ss_flags = SS_DISABLE;
  sigaltstack (&ss, NULL);
}

static unsigned long
ohos_auxv (unsigned long type)
{
  return getauxval (type);
}

/* Build the initial process stack and return its SP.

   Layout (high to low): string area, auxiliary vector, envp NULL and
   pointers, argv NULL and pointers, argc.  This is the layout the
   kernel produces; the target's C library expects the arrays to be
   contiguous starting at SP.  */

struct ohos_stack_strings
{
  char *argv[200];
  char *envp[200];
  int argc;
  int envc;
};

static uintptr_t
ohos_build_stack (struct ohos_elf *main_img, struct ohos_elf *interp,
		  const char *path, char *const argv[], char *const envp[])
{
  struct ohos_stack_strings strs;
  int argc = 0, envc = 0;
  while (argv && argv[argc])
    {
      if (argc >= 199)
	return 0;
      argc++;
    }
  while (envp && envp[envc])
    {
      if (envc >= 199)
	return 0;
      envc++;
    }
  strs.argc = argc;
  strs.envc = envc;

  /* Library search path for the bundled program: its own directory
     (where a tool may ship private libraries) plus the HAP's native
     library directory, followed by whatever the caller configured.  */
  char libdir[OHOS_MAX_PATH];
  size_t plen = strlen (path);
  if (plen >= sizeof libdir)
    return 0;
  memcpy (libdir, path, plen + 1);
  char *slash = strrchr (libdir, '/');
  if (slash)
    *slash = '\0';
  else
    strcpy (libdir, ".");
  const char *old_ldpath = NULL;
  for (int i = 0; i < envc; i++)
    if (strncmp (envp[i], "LD_LIBRARY_PATH=", 16) == 0)
      old_ldpath = envp[i] + 16;
  char ldpath[OHOS_MAX_PATH * 2 + 64];
  size_t ldlen = 0;
  const char *pieces[] = {
    "LD_LIBRARY_PATH=", libdir, ":", OHOS_BUNDLE_LIBS,
    (old_ldpath && *old_ldpath) ? ":" : "",
    old_ldpath ? old_ldpath : ""
  };
  for (unsigned piece = 0; piece < sizeof pieces / sizeof pieces[0]; piece++)
    {
      size_t n = strlen (pieces[piece]);
      if (ldlen + n >= sizeof ldpath)
	return 0;
      memcpy (ldpath + ldlen, pieces[piece], n);
      ldlen += n;
    }
  ldpath[ldlen] = '\0';

  /* The passed environment minus a stale LD_LIBRARY_PATH, plus ours.  */
  char *env2[202];
  int envc2 = 0;
  for (int i = 0; i < envc; i++)
    if (strncmp (envp[i], "LD_LIBRARY_PATH=", 16) != 0)
      env2[envc2++] = envp[i];
  env2[envc2++] = ldpath;

  size_t strings = strlen (path) + 1 + 16 + 16 + strlen (ldpath) + 1;
  for (int i = 0; i < argc; i++) strings += strlen (argv[i]) + 1;
  for (int i = 0; i < envc2; i++) strings += strlen (env2[i]) + 1;

  size_t naux = 38;		/* pairs, generous */
  size_t arrays = 16 + (argc + 1) * 8 + (envc2 + 1) * 8 + naux * 8 + 16;
  /* Leave room for the target's stack to grow down towards a guard
     page: [guard][slack][argv/auxv][strings].  */
  size_t slack = OHOS_STACK_SLACK;
  size_t need = strings + arrays + slack + 2 * OHOS_PAGE_SIZE;
  need = (need + OHOS_PAGE_SIZE - 1) & ~(OHOS_PAGE_SIZE - 1);

  char *base = (char *) syscall (SYS_mmap, NULL, need,
				 PROT_READ | PROT_WRITE,
				 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if ((long) base < 0 && base != MAP_FAILED)
    {
      ohos_msg ("stack mmap", NULL, (int) -(long) base);
      return 0;
    }
  if (base == MAP_FAILED)
    return 0;

  /* First page is a guard page; the stack cannot grow past it.  */
  if (syscall (SYS_mprotect, base, OHOS_PAGE_SIZE, PROT_NONE) < 0)
    {
      ohos_msg ("stack guard", NULL, errno);
    }

  char *base_pointers = base + OHOS_PAGE_SIZE + slack;

  /* String area grows down from the top of the mapping.  */
  char *p = (char *) (((uintptr_t) base + need) & ~(uintptr_t) 15);
  p -= 16;
  char *random_bytes = p;
  if (syscall (SYS_getrandom, p, 16, 0) != 16)
    for (int i = 0; i < 16; i++) p[i] = (char) (i * 7 + 1);
  p -= strlen (path) + 1;
  char *execfn = p;
  memcpy (p, path, strlen (path) + 1);
  for (int i = argc - 1; i >= 0; i--)
    {
      size_t n = strlen (argv[i]) + 1;
      p -= n;
      memcpy (p, argv[i], n);
      strs.argv[i] = p;
    }
  for (int i = envc2 - 1; i >= 0; i--)
    {
      size_t n = strlen (env2[i]) + 1;
      p -= n;
      memcpy (p, env2[i], n);
      strs.envp[i] = p;
    }

  /* Pointer array area below the strings and above the stack slack.  */
  uintptr_t sp = ((uintptr_t) base_pointers + 15) & ~(uintptr_t) 15;
  *(long *) sp = argc;
  char **av = (char **) (sp + 8);
  for (int i = 0; i < argc; i++) av[i] = strs.argv[i];
  av[argc] = NULL;
  char **ev = av + argc + 1;
  for (int i = 0; i < envc2; i++) ev[i] = strs.envp[i];
  ev[envc2] = NULL;
  unsigned long *aux = (unsigned long *) (ev + envc2 + 1);
  if ((char *) (aux) + naux * 8 >= p)
    {
      ohos_msg ("stack overflow", NULL, 0);
      return 0;
    }

  size_t n = 0;
#define AUX(t, v) do { aux[n++] = (unsigned long) (t); aux[n++] = (unsigned long) (v); } while (0)
  AUX (AT_PAGESZ, OHOS_PAGE_SIZE);
  AUX (AT_PHDR, main_img->phdr);
  AUX (AT_PHENT, sizeof (Elf64_Phdr));
  AUX (AT_PHNUM, main_img->phnum);
  AUX (AT_ENTRY, main_img->entry);
  AUX (AT_BASE, interp ? interp->bias : 0);
  AUX (AT_FLAGS, 0);
  AUX (AT_UID, syscall (SYS_getuid));
  AUX (AT_EUID, syscall (SYS_geteuid));
  AUX (AT_GID, syscall (SYS_getgid));
  AUX (AT_EGID, syscall (SYS_getegid));
  AUX (AT_SECURE, 0);
  AUX (AT_RANDOM, (uintptr_t) random_bytes);
  AUX (AT_EXECFN, (uintptr_t) execfn);
  AUX (AT_HWCAP, ohos_auxv (AT_HWCAP));
  AUX (AT_HWCAP2, ohos_auxv (AT_HWCAP2));
  AUX (AT_SYSINFO_EHDR, ohos_auxv (AT_SYSINFO_EHDR));
  AUX (AT_NULL, 0);
#undef AUX
  return sp;
}

static void __attribute__ ((noreturn))
ohos_jump (uintptr_t sp, uintptr_t entry)
{
  __asm__ __volatile__ ("mov sp, %0\n\tbr %1\n\t"
			: : "r" (sp), "r" (entry) : "memory");
  __builtin_unreachable ();
}


/* Map LIBNAME from the signed HAP native library directory and run it.
   Executable pages are file-backed; no anonymous executable memory is
   created.  Value is an errno value on failure.  */
static int
ohos_load_bundle_image (const char *libname, char *const argv[],
			char *const envp[])
{
  char path[OHOS_MAX_PATH];
  const char *dir = ohos_bundle_dir ();
  size_t dl = strlen (dir);
  size_t nl = strlen (libname);

  if (!ohos_valid_lib_name (libname) || dl == 0)
    return ENOEXEC;
  if (dl + nl + 2 >= sizeof path)
    return ENAMETOOLONG;
  memcpy (path, dir, dl);
  path[dl] = '/';
  memcpy (path + dl + 1, libname, nl + 1);

  struct ohos_elf main_img, interp_img;
  struct ohos_elf *interp = NULL;

  ohos_reset_signals ();
  {
    struct rlimit rl;
    long maxfd = 1024;
    if (syscall (SYS_getrlimit, RLIMIT_NOFILE, &rl) == 0
	&& rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur < 65536)
      maxfd = (long) rl.rlim_cur;
    for (long fd = 3; fd < maxfd; fd++)
      syscall (SYS_close, fd);
  }

  if (ohos_elf_open (&main_img, path))
    return ENOEXEC;
  int rc = ohos_map_file_image (&main_img, path);
  if (rc)
    {
      ohos_elf_close (&main_img);
      return rc;
    }
  ohos_elf_close (&main_img);

  if (main_img.is_dynamic)
    {
      const char *interp_path = main_img.interp;
      if (syscall (SYS_faccessat, AT_FDCWD, interp_path, R_OK, 0) != 0)
	{
	  static const char fallback[] = "/system/lib/ld-musl-aarch64.so.1";
	  if (syscall (SYS_faccessat, AT_FDCWD, fallback, R_OK, 0) != 0)
	    {
	      ohos_msg ("no dynamic linker", interp_path, errno);
	      return ENOENT;
	    }
	  interp_path = fallback;
	}
      if (ohos_elf_open (&interp_img, interp_path))
	return ENOEXEC;
      rc = ohos_map_file_image (&interp_img, interp_path);
      if (rc)
	{
	  ohos_elf_close (&interp_img);
	  return rc;
	}
      ohos_elf_close (&interp_img);
      interp = &interp_img;
    }

  uintptr_t sp = ohos_build_stack (&main_img, interp, path, argv, envp);
  if (!sp)
    {
      ohos_msg ("cannot build initial stack", path, 0);
      return ENOMEM;
    }
  uintptr_t entry = interp ? interp->entry : main_img.entry;
  ohos_jump (sp, entry);
  return ENOEXEC;		/* not reached */
}

/* Read a #! line from PATH into INTERP and LINEARG.  Value is 1 when
   PATH is a script, 0 when it is not, -1 on error.  */
static int
ohos_read_shebang (const char *path, char *interp, size_t interp_size,
		   char *linearg, size_t linearg_size)
{
  char line[512];
  int fd = syscall (SYS_openat, AT_FDCWD, path, O_RDONLY | O_CLOEXEC, 0);
  if (fd < 0)
    return -1;
  long n = syscall (SYS_read, fd, line, sizeof line - 1);
  syscall (SYS_close, fd);
  if (n < 3 || line[0] != '#' || line[1] != '!')
    return 0;
  line[n] = '\0';
  char *p = line + 2;
  while (*p == ' ' || *p == '\t')
    p++;
  char *start = p;
  while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
    p++;
  size_t len = p - start;
  if (len == 0 || len >= interp_size)
    return -1;
  memcpy (interp, start, len);
  interp[len] = '\0';
  while (*p == ' ' || *p == '\t')
    p++;
  char *astart = p;
  while (*p && *p != '\n' && *p != '\r')
    p++;
  size_t alen = p - astart;
  if (alen >= linearg_size)
    alen = linearg_size - 1;
  memcpy (linearg, astart, alen);
  linearg[alen] = '\0';
  return 1;
}

/* Load PATH and run it in the current (forked) process.  Value is an
   errno value on failure; never returns on success.  */
static int
ohos_load_bundled (const char *path, char *const argv[],
		   char *const envp[])
{
  struct ohos_elf main_img, interp_img;
  struct ohos_elf *interp = NULL;

  ohos_reset_signals ();

  /* The child is a fresh program: do not leak Emacs's descriptors
     (execve only closes FD_CLOEXEC ones, but a bundled program never
     needs any of them).  */
  {
    struct rlimit rl;
    long maxfd = 1024;
    if (syscall (SYS_getrlimit, RLIMIT_NOFILE, &rl) == 0
	&& rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur < 65536)
      maxfd = (long) rl.rlim_cur;
    for (long fd = 3; fd < maxfd; fd++)
      syscall (SYS_close, fd);
  }

  if (ohos_elf_open (&main_img, path))
    return ENOEXEC;
  if (ohos_elf_reserve (&main_img, path))
    return ENOMEM;
  if (ohos_elf_load (&main_img, path))
    return ENOEXEC;
  ohos_elf_close (&main_img);

  if (main_img.is_dynamic)
    {
      const char *interp_path = main_img.interp;
      if (syscall (SYS_faccessat, AT_FDCWD, interp_path, R_OK, 0) != 0)
	{
	  /* The interpreter recorded in the file is the build-time path
	     (/lib/ld-musl-aarch64.so.1); OHOS keeps it under /system.  */
	  static const char fallback[] = "/system/lib/ld-musl-aarch64.so.1";
	  if (syscall (SYS_faccessat, AT_FDCWD, fallback, R_OK, 0) != 0)
	    {
	      ohos_msg ("no dynamic linker", interp_path, errno);
	      return ENOENT;
	    }
	  interp_path = fallback;
	}
      if (ohos_elf_open (&interp_img, interp_path))
	return ENOEXEC;
      if (ohos_elf_reserve (&interp_img, interp_path)
	  || ohos_elf_load (&interp_img, interp_path))
	return ENOMEM;
      ohos_elf_close (&interp_img);
      interp = &interp_img;
    }

  uintptr_t sp = ohos_build_stack (&main_img, interp, path, argv, envp);
  if (!sp)
    {
      ohos_msg ("cannot build initial stack", path, 0);
      return ENOMEM;
    }
  uintptr_t entry = interp ? interp->entry : main_img.entry;
  ohos_jump (sp, entry);
  return ENOEXEC;		/* not reached */
}

int
ohos_bundled_program_p (const char *file, const char *cwd)
{
  char path[OHOS_MAX_PATH];
  if (!file)
    return 0;
  if (file[0] == '/')
    {
      if (strlen (file) >= sizeof path)
	return 0;
      strcpy (path, file);
    }
  else if (strchr (file, '/') && cwd && cwd[0] == '/')
    {
      /* A relative program name is resolved by the child after chdir,
	 so the parent must join it with the child's directory to test
	 whether it is app-private.  */
      if (strlen (cwd) + strlen (file) + 2 >= sizeof path)
	return 0;
      strcpy (path, cwd);
      strcat (path, "/");
      strcat (path, file);
    }
  else
    return 0;
  if (memcmp (path, "/data/storage/", sizeof "/data/storage/" - 1) != 0)
    return 0;
  int fd = open (path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return 0;
  unsigned char buf[16];
  ssize_t n = read (fd, buf, sizeof buf);
  close (fd);
  if (n < 2)
    return 0;
  if (buf[0] == '#' && buf[1] == '!')
    return 1;
  if (n >= 4 && buf[0] == 0x7f && buf[1] == 'E'
      && buf[2] == 'L' && buf[3] == 'F')
    return 1;
  /* runtime/libexec stub pointing at a signed bundle library.  */
  return (n >= 14 && memcmp (buf, "OHOS-BUNDLE/1\n", 14) == 0);
}

/* The anonymous-memory loader is a development aid only: release builds
   must never rely on it.  It is enabled with EMACS_OHOS_ALLOW_ANON_EXEC=1.  */
static int
ohos_anon_exec_allowed (void)
{
  const char *v = getenv ("EMACS_OHOS_ALLOW_ANON_EXEC");
  return v && v[0] == '1';
}

/* Execute the program in ARGV (resolved through exec-path, so ARGV[0]
   is normally absolute) in the forked child.  Value is an errno value
   when nothing could be executed.  */
int
ohos_exec_bundled (char *const argv[], char *const envp[])
{
  execve (argv[0], argv, envp);
  int err = errno;
  if (err != EACCES && err != EPERM && err != ENOEXEC)
    return err;

  char libname[128];
  if (ohos_read_stub (argv[0], libname, sizeof libname))
    return ohos_load_bundle_image (libname, argv, envp);

  char interp[OHOS_MAX_PATH];
  char linearg[256];
  int script = ohos_read_shebang (argv[0], interp, sizeof interp,
				  linearg, sizeof linearg);
  if (script > 0)
    {
      size_t argc = 0;
      while (argv[argc])
	argc++;
      char **newargv = __builtin_alloca ((argc + 4) * sizeof *newargv);
      size_t n = 0;
      newargv[n++] = interp;
      if (linearg[0])
	newargv[n++] = linearg;
      newargv[n++] = argv[0];
      for (size_t i = 1; i < argc; i++)
	newargv[n++] = argv[i];
      newargv[n] = NULL;
      execve (interp, newargv, envp);
      err = errno;
      if (err != EACCES && err != EPERM && err != ENOEXEC)
	return err;
      if (ohos_read_stub (interp, libname, sizeof libname))
	return ohos_load_bundle_image (libname, newargv, envp);
      if (ohos_anon_exec_allowed ())
	return ohos_load_bundled (interp, newargv, envp);
      ohos_msg ("anonymous executable memory disabled", interp, 0);
      return err;
    }

  /* App-data ELF: only the debug opt-in may copy it into anonymous
     executable memory; release builds must use a bundle stub.  */
  if (ohos_anon_exec_allowed ())
    return ohos_load_bundled (argv[0], argv, envp);
  ohos_msg ("not a signed bundle stub", argv[0], 0);
  return ENOEXEC;
}
