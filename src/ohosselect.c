/* OHOS selection backend.

   HarmonyOS applications reach the system clipboard through the
   Pasteboard service, which is only available from ArkTS.  Emacs
   therefore asks the ArkUI page for the clipboard contents over the
   host request/response channel and sets them through the existing
   base64 clipboard sink.

   There is no PRIMARY selection on OHOS; the Lisp glue in
   `term/ohos-win.el' maps both PRIMARY and CLIPBOARD here.  */

#include <config.h>
#include <string.h>

#include "lisp.h"
#include "frame.h"
#include "window.h"
#include "termhooks.h"
#include "keyboard.h"
#include "coding.h"
#include "ohosgui.h"

extern Lisp_Object Fbase64_encode_string (Lisp_Object, Lisp_Object);

/* Whether Emacs last placed the clipboard contents.  HarmonyOS has no
   ownership query, so this is the best available approximation.  */
static bool ohos_clipboard_owned;

/* IDs for clipboard requests; disjoint from the menu ID range used by
   ohosmenu.c.  */
static int next_selection_id = 0x40000000;

DEFUN ("ohos-get-selection", Fohos_get_selection, Sohos_get_selection,
       0, 1, 0,
       doc: /* Return the text in the OHOS system clipboard.
SELECTION is a string naming the selection (\"CLIPBOARD\" or
\"PRIMARY\"); both name the system pasteboard.  Return nil when the
clipboard is empty or the ArkUI host did not answer in time.  */)
  (Lisp_Object selection)
{
  char text[OHOS_HOST_RESULT_TEXT];
  int id;
  Lisp_Object decoded;

  (void) selection;
  if (!ohos_gui_available ())
    return Qnil;

  id = next_selection_id++;
  if (next_selection_id < 0)
    next_selection_id = 0x40000000;
  text[0] = '\0';
  ohos_host_command ("clipboard-get %d", id);
  if (!ohos_host_wait_result (id, 10000, NULL, text, sizeof text))
    return Qnil;
  if (text[0] == '\0')
    return Qnil;

  decoded = code_convert_string_norecord
    (make_unibyte_string (text, strlen (text)), Qutf_8, false);
  return decoded;
}

DEFUN ("ohos-set-selection", Fohos_set_selection, Sohos_set_selection,
       1, 2, 0,
       doc: /* Put STRING in the OHOS system clipboard.
SELECTION is a string naming the selection; it is accepted for
compatibility with the selection API and ignored.  STRING nil clears
the clipboard.  */)
  (Lisp_Object selection, Lisp_Object string)
{
  Lisp_Object encoded, b64;

  (void) selection;
  if (NILP (string))
    {
      ohos_set_clipboard ("");
      ohos_clipboard_owned = false;
      return Qnil;
    }
  CHECK_STRING (string);
  encoded = ENCODE_UTF_8 (string);
  b64 = Fbase64_encode_string (encoded, Qt);
  ohos_set_clipboard (SSDATA (b64));
  ohos_clipboard_owned = true;
  return Qnil;
}

DEFUN ("ohos-selection-owner-p", Fohos_selection_owner_p,
       Sohos_selection_owner_p, 0, 1, 0,
       doc: /* Return non-nil if Emacs last set the OHOS clipboard.
SELECTION is a string naming the selection and is ignored.  */)
  (Lisp_Object selection)
{
  (void) selection;
  return ohos_clipboard_owned ? Qt : Qnil;
}

DEFUN ("ohos-selection-exists-p", Fohos_selection_exists_p,
       Sohos_selection_exists_p, 0, 1, 0,
       doc: /* Return non-nil: the OHOS pasteboard may always hold text.
There is no synchronous way to query the pasteboard, so this always
returns t when the GUI backend is active.  */)
  (Lisp_Object selection)
{
  (void) selection;
  return ohos_gui_available () ? Qt : Qnil;
}

void
syms_of_ohosselect (void)
{
  ohos_clipboard_owned = false;
  defsubr (&Sohos_get_selection);
  defsubr (&Sohos_set_selection);
  defsubr (&Sohos_selection_owner_p);
  defsubr (&Sohos_selection_exists_p);
}
