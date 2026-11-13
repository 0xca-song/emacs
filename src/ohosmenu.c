/* OHOS menu backend, mirroring androidmenu.c.

   OHOS has no toolkit menu attached to a native window: the ArkUI page
   owns all menus.  Emacs therefore serialises the menu built by
   `x-popup-menu' into a small JSON document, queues it as a host
   command, and waits for the ArkUI page to report which item the user
   picked through the host result channel.

   The return value follows the `menu_show_hook' contract: the value
   stored in the selected item (plus the keymap prefix chain when the
   caller asked for MENU_KEYMAPS).  */

#include <config.h>
#include <string.h>
#include <stdio.h>

#include "lisp.h"
#include "frame.h"
#include "window.h"
#include "dispextern.h"
#include "termhooks.h"
#include "keyboard.h"
#include "blockinput.h"
#include "menu.h"
#include "buffer.h"
#include "window.h"
#include "coding.h"
#include "ohosgui.h"

/* The host always answers (selection, cancel, error) or sends
   menu-close, so the wait is unbounded, like the Android and X11
   backends.  A negative timeout means "wait forever".  */
#define OHOS_MENU_TIMEOUT_MS (-1)

/* A growable JSON buffer.  */

struct ohos_json
{
  char *data;
  size_t len;
  size_t cap;
};

static void
ohos_json_init (struct ohos_json *json)
{
  json->cap = 1024;
  json->len = 0;
  json->data = xmalloc (json->cap);
  json->data[0] = '\0';
}

static void
ohos_json_put (struct ohos_json *json, const char *text, size_t len)
{
  if (json->len + len + 1 > json->cap)
    {
      while (json->len + len + 1 > json->cap)
	json->cap *= 2;
      json->data = xrealloc (json->data, json->cap);
    }
  memcpy (json->data + json->len, text, len);
  json->len += len;
  json->data[json->len] = '\0';
}

static void
ohos_json_puts (struct ohos_json *json, const char *text)
{
  ohos_json_put (json, text, strlen (text));
}

/* Append STRING as a JSON string literal.

   This must never signal.  Menu titles and item names can contain raw
   bytes (a multibyte string is not guaranteed to hold valid UTF-8), and
   ENCODE_UTF_8 raises a coding-system error on those; that used to abort
   the whole popup before the host was told to open anything.  Bytes that
   do not form a valid UTF-8 sequence are replaced with '?'.  */

static void
ohos_json_string (struct ohos_json *json, Lisp_Object string)
{
  static const char hex[] = "0123456789abcdef";
  const unsigned char *p;
  ptrdiff_t n, i;

  if (!STRINGP (string))
    {
      ohos_json_puts (json, "\"\"");
      return;
    }
  p = SDATA (string);
  n = SBYTES (string);
  ohos_json_put (json, "\"", 1);
  for (i = 0; i < n; i++)
    {
      unsigned char c = p[i];
      int len = 0;

      if (c < 0x80)
	{
	  switch (c)
	    {
	    case '"':
	      ohos_json_puts (json, "\\\"");
	      break;
	    case '\\':
	      ohos_json_puts (json, "\\\\");
	      break;
	    case '\n':
	      ohos_json_puts (json, "\\n");
	      break;
	    case '\r':
	      ohos_json_puts (json, "\\r");
	      break;
	    case '\t':
	      ohos_json_puts (json, "\\t");
	      break;
	    default:
	      if (c < 0x20)
		{
		  char buf[8] = { '\\', 'u', '0', '0', hex[c >> 4],
				  hex[c & 0xf], '\0' };
		  ohos_json_puts (json, buf);
		}
	      else
		ohos_json_put (json, (const char *) &c, 1);
	      break;
	    }
	  continue;
	}

      if ((c & 0xe0) == 0xc0)
	len = 2;
      else if ((c & 0xf0) == 0xe0)
	len = 3;
      else if ((c & 0xf8) == 0xf0)
	len = 4;

      if (len > 0 && i + len <= n)
	{
	  int k, ok = 1;

	  for (k = 1; k < len; k++)
	    if ((p[i + k] & 0xc0) != 0x80)
	      ok = 0;
	  if (ok)
	    {
	      ohos_json_put (json, (const char *) &p[i], len);
	      i += len - 1;
	      continue;
	    }
	}
      ohos_json_puts (json, "?");
    }
  ohos_json_put (json, "\"", 1);
}

static void
ohos_json_bool (struct ohos_json *json, bool value)
{
  ohos_json_puts (json, value ? "true" : "false");
}

/* ------------------------------------------------------------------ */
/* Menu panel state.                                                   */

static int popup_activated_flag;

extern Lisp_Object Fx_hide_tip (Lisp_Object);

static void
ohos_clear_popup_flag (int value)
{
  popup_activated_flag = value;
}

static uint32_t
ohos_frame_id (struct frame *f)
{
  struct ohos_output *o = f ? FRAME_OUTPUT_DATA (f) : NULL;
  return o ? o->frame_id : 1;
}

static uint32_t popup_frame_id = 1;

/* Unwind-protect handler: if the menu is still up when Emacs leaves
   (C-g, quit, error), ask the host to dismiss the panel.  */
static void
ohos_cancel_menu (int id)
{
  if (!popup_activated_flag)
    return;
  popup_activated_flag = 0;
  ohos_host_menu_close (popup_frame_id, id);
}

int
popup_activated (void)
{
  return popup_activated_flag;
}

/* Serial number for the pending menu request.  Menu IDs live in a
   range disjoint from the selection request IDs.  */
static int next_menu_id = 1;

/* Set while a dialog is being turned into a menu by x-popup-menu, so
   ohos_menu_show tells the host to render a dialog instead of a menu.  */
static bool pending_dialog;

/* Write the menu described by the global `menu_items' vector into
   JSON.  *I is the current index, DEPTH the submenu nesting depth.  */

static void
ohos_menu_json_items (struct ohos_json *json, size_t *i, int depth,
		      int menuflags)
{
  Lisp_Object item_name, enable, def, type, selected, help;
  bool first = true;

  (void) menuflags;
  while (*i < menu_items_used)
    {
      if (NILP (AREF (menu_items, *i)))
	{
	  /* Start of a submenu's contents; the caller opened the
	     array already.  */
	  *i += 1;
	  continue;
	}
      if (EQ (AREF (menu_items, *i), Qlambda))
	{
	  /* End of this submenu.  */
	  *i += 1;
	  if (depth > 0)
	    return;
	  continue;
	}
      if (EQ (AREF (menu_items, *i), Qquote))
	{
	  *i += 1;
	  continue;
	}
      if (EQ (AREF (menu_items, *i), Qt))
	{
	  /* A pane separator.  With a single pane the pane name is the
	     menu title and is already shown, so skip it.  */
	  if (menu_items_n_panes < 2)
	    {
	      *i += MENU_ITEMS_PANE_LENGTH;
	      continue;
	    }
	  if (!first)
	    ohos_json_puts (json, ",");
	  first = false;
	  ohos_json_puts (json, "{\"kind\":\"pane\",\"label\":");
	  ohos_json_string (json, AREF (menu_items,
					*i + MENU_ITEMS_PANE_NAME));
	  ohos_json_puts (json, "}");
	  *i += MENU_ITEMS_PANE_LENGTH;
	  continue;
	}

      item_name = AREF (menu_items, *i + MENU_ITEMS_ITEM_NAME);
      enable = AREF (menu_items, *i + MENU_ITEMS_ITEM_ENABLE);
      def = AREF (menu_items, *i + MENU_ITEMS_ITEM_DEFINITION);
      type = AREF (menu_items, *i + MENU_ITEMS_ITEM_TYPE);
      selected = AREF (menu_items, *i + MENU_ITEMS_ITEM_SELECTED);
      help = AREF (menu_items, *i + MENU_ITEMS_ITEM_HELP);

      if (*i + MENU_ITEMS_ITEM_LENGTH < menu_items_used
	  && NILP (AREF (menu_items, *i + MENU_ITEMS_ITEM_LENGTH)))
	{
	  /* A submenu.  */
	  if (!first)
	    ohos_json_puts (json, ",");
	  first = false;
	  ohos_json_puts (json, "{\"kind\":\"submenu\",\"label\":");
	  ohos_json_string (json, item_name);
	  ohos_json_puts (json, ",\"enabled\":");
	  ohos_json_bool (json, !NILP (enable));
	  if (STRINGP (help))
	    {
	      ohos_json_puts (json, ",\"help\":");
	      ohos_json_string (json, help);
	    }
	  ohos_json_puts (json, ",\"items\":[");

	  *i += MENU_ITEMS_ITEM_LENGTH;
	  if (*i < menu_items_used && NILP (AREF (menu_items, *i)))
	    *i += 1;
	  ohos_menu_json_items (json, i, depth + 1, menuflags);
	  ohos_json_puts (json, "]}");
	  continue;
	}

      if (NILP (def) && STRINGP (item_name)
	  && menu_separator_name_p (SSDATA (item_name)))
	{
	  if (!first)
	    ohos_json_puts (json, ",");
	  first = false;
	  ohos_json_puts (json, "{\"kind\":\"separator\"}");
	  *i += MENU_ITEMS_ITEM_LENGTH;
	  continue;
	}

      if (!first)
	ohos_json_puts (json, ",");
      first = false;
      ohos_json_puts (json, "{\"kind\":\"item\",\"id\":");
      {
	char buf[32];
	snprintf (buf, sizeof buf, "%ld",
		  (long) (*i + MENU_ITEMS_ITEM_VALUE));
	ohos_json_puts (json, buf);
      }
      ohos_json_puts (json, ",\"label\":");
      ohos_json_string (json, item_name);
      ohos_json_puts (json, ",\"enabled\":");
      ohos_json_bool (json, !NILP (enable));
      if (STRINGP (AREF (menu_items, *i + MENU_ITEMS_ITEM_EQUIV_KEY)))
	{
	  ohos_json_puts (json, ",\"keys\":");
	  ohos_json_string (json,
			    AREF (menu_items,
				  *i + MENU_ITEMS_ITEM_EQUIV_KEY));
	}
      ohos_json_puts (json, ",\"selected\":");
      ohos_json_bool (json, !NILP (selected));
      ohos_json_puts (json, ",\"toggle\":");
      ohos_json_bool (json, EQ (type, QCtoggle));
      ohos_json_puts (json, ",\"radio\":");
      ohos_json_bool (json, EQ (type, QCradio));
      if (STRINGP (help))
	{
	  ohos_json_puts (json, ",\"help\":");
	  ohos_json_string (json, help);
	}
      ohos_json_puts (json, "}");
      *i += MENU_ITEMS_ITEM_LENGTH;
    }
}

/* Build the panel description for the current `menu_items'.  */

static char *
ohos_menu_json (Lisp_Object title, int menuflags, bool dialog)
{
  struct ohos_json json;
  size_t i = 0;

  ohos_json_init (&json);
  ohos_json_puts (&json, "{\"dialog\":");
  ohos_json_bool (&json, dialog);
  ohos_json_puts (&json, ",\"title\":");
  ohos_log ("menu_json title start");
  ohos_json_string (&json, title);
  ohos_log ("menu_json title ok");
  ohos_json_puts (&json, ",\"items\":[");
  ohos_menu_json_items (&json, &i, 0, menuflags);
  ohos_log ("menu_json items ok");
  ohos_json_puts (&json, "]}");
  return json.data;
}

/* Convert the numeric menu item ID reported by the host back into the
   Lisp value the caller expects.  */

struct ohos_subprefix
{
  struct ohos_subprefix *last;
  Lisp_Object subprefix;
};

static Lisp_Object
ohos_menu_item_value (long id, int menuflags)
{
  Lisp_Object entry, prefix, tem = Qnil;
  struct ohos_subprefix *subprefix = NULL;
  struct ohos_subprefix *temp;
  size_t i = 0;

  if (id < 0 || id >= ASIZE (menu_items))
    return Qnil;

  prefix = entry = Qnil;
  while (i < menu_items_used)
    {
      if (NILP (AREF (menu_items, i)))
	{
	  temp = xmalloc (sizeof *temp);
	  temp->last = subprefix;
	  subprefix = temp;
	  subprefix->subprefix = prefix;
	  prefix = entry;
	  i++;
	}
      else if (EQ (AREF (menu_items, i), Qlambda))
	{
	  prefix = subprefix->subprefix;
	  temp = subprefix->last;
	  xfree (subprefix);
	  subprefix = temp;
	  i++;
	}
      else if (EQ (AREF (menu_items, i), Qt))
	{
	  prefix = AREF (menu_items, i + MENU_ITEMS_PANE_PREFIX);
	  i += MENU_ITEMS_PANE_LENGTH;
	}
      else if (EQ (AREF (menu_items, i), Qquote))
	i += 1;
      else
	{
	  entry = AREF (menu_items, i + MENU_ITEMS_ITEM_VALUE);

	  if (i + MENU_ITEMS_ITEM_VALUE == (size_t) id)
	    {
	      if (menuflags & MENU_KEYMAPS)
		{
		  entry = list1 (entry);
		  if (!NILP (prefix))
		    entry = Fcons (prefix, entry);
		  for (struct ohos_subprefix *s = subprefix; s; s = s->last)
		    if (!NILP (s->subprefix))
		      entry = Fcons (s->subprefix, entry);
		}
	      tem = entry;
	    }
	  i += MENU_ITEMS_ITEM_LENGTH;
	}
    }

  while (subprefix)
    {
      temp = subprefix->last;
      xfree (subprefix);
      subprefix = temp;
    }
  return tem;
}

Lisp_Object
ohos_menu_show (struct frame *f, int x, int y, int menuflags,
		Lisp_Object title, const char **error_name)
{
  ohos_log ("ohos_menu_show enter");
  specpdl_ref count;
  Lisp_Object value = Qnil;
  char *json;
  int id, index;
  bool answered, dialog = pending_dialog;
  uint32_t frame_id = ohos_frame_id (f);

  pending_dialog = false;

  (void) x; (void) y;
  if (error_name)
    *error_name = NULL;
  /* Plain ASCII only: no string conversion, so this cannot itself fail
     with a coding-system error.  */
  ohos_log ("menu_show enter");
  if (menu_items_used == 0)
    {
      if (error_name)
	*error_name = "Empty menu";
      return Qnil;
    }

  count = SPECPDL_INDEX ();
  block_input ();

  id = next_menu_id++;
  if (next_menu_id <= 0)
    next_menu_id = 1;
  json = ohos_menu_json (title, menuflags, dialog);
  record_unwind_protect_ptr (xfree, json);

  if (ohos_host_menu_show (frame_id, id, menuflags, dialog ? 1 : 0,
			  json, strlen (json)) != 0)
    {
      ohos_log ("menu_show rejected");
      unblock_input ();
      return unbind_to (count, Qnil);
    }

  /* Dismiss the panel if Emacs leaves this function abnormally.  */
  record_unwind_protect_int (ohos_cancel_menu, id);

  ohos_log ("menu_show json built");
  popup_frame_id = frame_id;
  popup_activated_flag = 1;
  answered = ohos_host_wait_result (id, OHOS_MENU_TIMEOUT_MS, &index,
				    NULL, 0);
  popup_activated_flag = 0;

  if (answered && index >= 0)
    value = ohos_menu_item_value (index, menuflags);

  unblock_input ();
  return unbind_to (count, value);
}

/* Dialogs use the same panel: Emacs turns a dialog into a single-pane
   menu, so all that is needed here is the plumbing.  */

Lisp_Object
ohos_popup_dialog (struct frame *f, Lisp_Object header, Lisp_Object contents)
{
  Lisp_Object x, y, frame, newpos, prompt = Fcar (contents);
  int x_coord, y_coord;

  (void) header;
  if (FRAME_WINDOW_P (f))
    {
      x_coord = FRAME_PIXEL_WIDTH (f);
      y_coord = FRAME_PIXEL_HEIGHT (f);
    }
  else
    {
      x_coord = FRAME_COLS (f);
      y_coord = FRAME_TOTAL_LINES (f);
    }

  XSETFRAME (frame, f);
  XSETINT (x, x_coord / 2);
  XSETINT (y, y_coord / 2);
  newpos = list2 (list2 (x, y), frame);
  /* x-popup-menu still builds the item list for us; the flag makes
     ohos_menu_show ask the host for a dialog instead of a menu.  */
  {
    specpdl_ref count = SPECPDL_INDEX ();
    record_unwind_protect_int (ohos_clear_popup_flag, 0);
    pending_dialog = true;
    return unbind_to (count, Fx_popup_menu (newpos,
					    list2 (prompt, contents)));
  }
}

/* The menu bar is a glyph row drawn by redisplay, so activating it only
   has to force an update.  */

void
ohos_activate_menubar (struct frame *f)
{
  if (f)
    {
      fset_redisplay (f);
      SET_FRAME_GARBAGED (f);
    }
}

/* Recompute the menu bar contents.

   The menu bar is a glyph row drawn by redisplay, but redisplay only has
   something to draw once FRAME_MENU_BAR_ITEMS is filled: the toolkit
   backends do that from their set_frame_menubar, and without it the row
   stays blank.  */

/* Rebuild the menu bar content for F.  Called by redisplay through
   update_menu_bar (HAVE_EXT_MENU_BAR is defined for HAVE_OHOS_GUI in
   lisp.h), never from set_menu_bar_lines: at frame creation the selected
   window and buffer are not ready yet.  */

/* Publish the menu bar labels to the host.  Pure C, no Lisp evaluation:
   this is the only work done on the redisplay path (deep_p == false).  */

static void
ohos_publish_menu_bar (struct frame *f)
{
  struct ohos_json json;
  Lisp_Object items = FRAME_MENU_BAR_ITEMS (f);
  int n = 0;

  ohos_json_init (&json);
  ohos_json_puts (&json, "{\"bar\":[");
  for (size_t i = 0; i + 3 < ASIZE (items); i += 4)
    {
      Lisp_Object name = AREF (items, i + 1);

      if (!STRINGP (name))
	continue;
      if (n++)
	ohos_json_puts (&json, ",");
      {
	char buf[24];

	snprintf (buf, sizeof buf, "{\"index\":%d,\"label\":", (int) (i / 4));
	ohos_json_puts (&json, buf);
      }
      ohos_json_string (&json, name);
      ohos_json_puts (&json, "}");
    }
  ohos_json_puts (&json, "]}");

  /* Publish only on change: redisplay calls this for every frame update and
     the host does not need to rebuild the bar for identical contents.  */
  {
    static char last[64][4096];
    static bool valid[64];
    uint32_t slot = ohos_frame_id (f) % 64;

    if (!valid[slot] || strcmp (last[slot], json.data) != 0)
      {
	snprintf (last[slot], sizeof last[slot], "%s", json.data);
	valid[slot] = true;
	{
	  int len = (int) strlen (json.data);
	  if (ohos_host_menu_bar (ohos_frame_id (f), json.data, len) != 0)
	    ohos_log ("menubar publish rejected");
	}
      }
  }
  xfree (json.data);
}

/* Rebuild the menu bar content for F.

   Mirrors the toolkit ports (pgtkmenu.c:233): the redisplay calls this with
   deep_p == false and must not evaluate Lisp, while the menu-show path and
   the initial build use deep_p == true.  Doing the Lisp part on the
   redisplay path crashed this CANNOT_DUMP build in set_match_data.  */

void
set_frame_menubar (struct frame *f, bool deep_p)
{
  if (!f || !FRAME_LIVE_P (f) || !FRAME_OHOS_P (f)
      || !WINDOWP (FRAME_SELECTED_WINDOW (f)))
    return;

  /* The recomputation redisplay itself performs for a glyph menu bar
     (xdisp.c: fset_menu_bar_items + menu_bar_items) and nothing more:
     specbind, buffer switching and the menu hooks are what crashed here.  */
  if (deep_p)
    fset_menu_bar_items (f, menu_bar_items (FRAME_MENU_BAR_ITEMS (f)));

  /* No fset_redisplay here: this runs inside redisplay, and asking for
     another redisplay from there recursed and corrupted the specpdl
     unwinding (the crash landed in set_match_data).  */
  ohos_publish_menu_bar (f);
}

DEFUN ("ohos-menu-bar-refresh", Fohos_menu_bar_refresh,
       Sohos_menu_bar_refresh, 0, 0, 0,
       doc: /* Recompute and publish the menu bar of the selected frame.  */)
  (void)
{
  struct frame *f = SELECTED_FRAME ();

  if (f && FRAME_LIVE_P (f) && FRAME_OHOS_P (f))
    set_frame_menubar (f, true);
  return Qnil;
}

/* The host asks for menu bar item INDEX to be opened.  The request is
   stored here and picked up by a Lisp timer, so the popup runs from a Lisp
   context rather than from the ArkUI thread.  */

static int ohos_menu_bar_pending = -1;

void
ohos_menu_bar_request (int index)
{
  ohos_menu_bar_pending = index;
}

DEFUN ("ohos-menu-bar-take-request", Fohos_menu_bar_take_request,
       Sohos_menu_bar_take_request, 0, 0, 0,
       doc: /* Return the menu bar item the host asked to open, or nil.  */)
  (void)
{
  int index = ohos_menu_bar_pending;

  ohos_menu_bar_pending = -1;
  return index < 0 ? Qnil : make_fixnum (index);
}

/* Return the definition (menu keymap) of menu bar item INDEX.

   The popup itself is driven from Lisp (`ohos-menu-bar-open-index' in
   term/ohos-win.el) because choosing an item must also *run* the command
   it is bound to: `x-popup-menu' only returns the key sequence.  That
   means: x-popup-menu -> lookup-key in the item's keymap -> call
   interactively, which is what `popup-menu' does for tty menus.  */

DEFUN ("ohos-menu-bar-item", Fohos_menu_bar_item,
       Sohos_menu_bar_item, 1, 1, 0,
       doc: /* Return the menu definition of menu bar item INDEX, or nil.  */)
  (Lisp_Object index)
{
  struct frame *f = SELECTED_FRAME ();
  Lisp_Object items;

  CHECK_FIXNUM (index);
  if (!f || !FRAME_LIVE_P (f) || !FRAME_OHOS_P (f))
    return Qnil;
  items = FRAME_MENU_BAR_ITEMS (f);
  if (XFIXNUM (index) < 0
      || (size_t) (XFIXNUM (index) * 4 + 2) >= ASIZE (items))
    return Qnil;
  return AREF (items, XFIXNUM (index) * 4 + 2);
}

/* Initial build, called once the frame is complete (pgtkfns.c:1648).  */

void
ohos_initialize_frame_menubar (struct frame *f)
{
  if (f && FRAME_LIVE_P (f) && FRAME_OHOS_P (f)
      && FRAME_EXTERNAL_MENU_BAR (f))
    set_frame_menubar (f, true);
}

Lisp_Object
Fx_hide_tip (Lisp_Object tip)
{
  (void) tip;
  return Qnil;
}

void
syms_of_ohosmenu (void)
{
  defsubr (&Sohos_menu_bar_refresh);
  defsubr (&Sohos_menu_bar_take_request);
  defsubr (&Sohos_menu_bar_item);
}
