/* OHOS native GUI backend contract.
   Copyright (C) 2026 Free Software Foundation, Inc.

This file is part of GNU Emacs.

GNU Emacs is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

GNU Emacs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Emacs.  If not, see <https://www.gnu.org/licenses/>.  */

/* This header is the C-side contract described by
   docs/adr/0007-ohos-native-backend.md.  It is deliberately separate
   from ohosgui.h: ohosgui.h contains the existing implementation
   details, while this file states the lifecycle and ownership that a
   complete backend must provide.  */

#ifndef EMACS_OHOSBACKEND_H
#define EMACS_OHOSBACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Backend result codes.  A backend failure must be reported to the
   caller; it must not call abort() from a presentation callback.  */
enum ohos_backend_status
{
  OHOS_BACKEND_OK = 0,
  OHOS_BACKEND_NOT_READY,
  OHOS_BACKEND_INVALID_STATE,
  OHOS_BACKEND_NO_SURFACE,
  OHOS_BACKEND_NO_MEMORY,
  OHOS_BACKEND_IO_ERROR,
  OHOS_BACKEND_UNSUPPORTED
};

/* A cell color is 0x00RRGGBB.  */
typedef uint32_t ohos_color;

struct ohos_frame_geometry
{
  int rows;
  int cols;
  int cell_width;
  int cell_height;
  int pixel_width;
  int pixel_height;
};

struct ohos_frame_cell
{
  unsigned int code;
  ohos_color fg;
  ohos_color bg;
  unsigned char width;
  unsigned char flags;
};

/* Frame output is frame-scoped.  A producer must bracket all cell
   writes with begin/end; consumers must not observe a partially
   updated frame.  */
struct ohos_frame_output
{
  void *userdata;
  enum ohos_backend_status (*begin) (void *userdata,
                                     const struct ohos_frame_geometry *g);
  enum ohos_backend_status (*clear) (void *userdata, ohos_color bg);
  enum ohos_backend_status (*put_text) (void *userdata, int row, int col,
                                        const struct ohos_frame_cell *cells,
                                        size_t n);
  enum ohos_backend_status (*set_cursor) (void *userdata, int row, int col,
                                          int style);
  enum ohos_backend_status (*scroll) (void *userdata, int top, int bottom,
                                      int delta);
  enum ohos_backend_status (*resize) (void *userdata,
                                      const struct ohos_frame_geometry *g);
  enum ohos_backend_status (*end) (void *userdata);
  /* Present is separate from end: a frame may be complete before a
     surface is available.  */
  enum ohos_backend_status (*present) (void *userdata);
};

enum ohos_input_kind
{
  OHOS_INPUT_KEY,
  OHOS_INPUT_TEXT,
  OHOS_INPUT_POINTER,
  OHOS_INPUT_FOCUS,
  OHOS_INPUT_RESIZE
};

struct ohos_input_event
{
  enum ohos_input_kind kind;
  /* KEY: keysym/modifiers; TEXT: UTF-8; POINTER: x/y/button/state;
     FOCUS: focused; RESIZE: rows/cols/pixels.  */
  unsigned int keysym;
  unsigned int modifiers;
  const char *text;
  size_t text_len;
  int x;
  int y;
  int button;
  int state;
  int focused;
  int rows;
  int cols;
  int pixel_width;
  int pixel_height;
};

/* Terminal lifecycle.  ohos_term_init creates terminal/display_info
   only; it must not create the initial frame.  */
enum ohos_backend_status ohos_term_init_backend (void);
void ohos_term_deinit_backend (void);

/* Frame lifecycle.  The Lisp frame-creation-function calls this exactly
   once for the initial frame.  It must:
     make_frame
     set output_method/terminal/parameters
     initialize faces/fontset/glyph matrices
     register the frame in Vframe_list
     select the frame
   It must not be called from the command loop as a recovery action.  */
enum ohos_backend_status ohos_frame_create_backend (void *frame_params,
                                                    void **frame_out);
void ohos_frame_delete_backend (void *frame);

/* Output/input registration.  The surface layer installs an output
   sink; the input layer pushes events into a native ring consumed by
   read_socket_hook.  Neither callback may use a napi_env directly from
   the Emacs thread.  */
void ohos_frame_set_output (const struct ohos_frame_output *output);
void ohos_input_set_queue (void *queue);
enum ohos_backend_status ohos_input_push (const struct ohos_input_event *event);

/* Presentation is driven by Emacs redisplay, not by the UI thread.  */
enum ohos_backend_status ohos_present (void);

#endif /* EMACS_OHOSBACKEND_H */
