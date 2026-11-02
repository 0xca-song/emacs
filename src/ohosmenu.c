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
#include "coding.h"
#include "ohosgui.h"

/* How long to wait for the ArkUI page to answer a menu request.  */
#define OHOS_MENU_TIMEOUT_MS 600000

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

/* Append STRING as a JSON string literal.  STRING is UTF-8.  */

static void
ohos_json_string (struct ohos_json *json, Lisp_Object string)
{
  static const char hex[] = "0123456789abcdef";
  const unsigned char *p;
  ptrdiff_t n;

  if (NILP (string) || !STRINGP (string))
    {
      ohos_json_puts (json, "\"\"");
      return;
    }
  string = ENCODE_UTF_8 (string);
  p = SDATA (string);
  n = SBYTES (string);
  ohos_json_put (json, "\"", 1);
  for (ptrdiff_t i = 0; i < n; i++)
    {
      unsigned char c = p[i];

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

int
popup_activated (void)
{
  return popup_activated_flag;
}

/* Serial number for the pending menu request.  Menu IDs live in a
   range disjoint from the selection request IDs.  */
static int next_menu_id = 1;

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
  ohos_json_string (&json, title);
  ohos_json_puts (&json, ",\"items\":[");
  ohos_menu_json_items (&json, &i, 0, menuflags);
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
  specpdl_ref count;
  Lisp_Object value = Qnil;
  char *json;
  int id, index;
  bool answered;

  (void) f; (void) x; (void) y;
  if (error_name)
    *error_name = NULL;
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
  json = ohos_menu_json (title, menuflags, false);
  record_unwind_protect_ptr (xfree, json);
  record_unwind_protect_int (ohos_clear_popup_flag, 0);

  popup_activated_flag = 1;
  ohos_host_command ("menu %d %s", id, json);

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
  return Fx_popup_menu (newpos, list2 (prompt, contents));
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

/* Recompute the menu bar contents.  */

void
set_frame_menubar (struct frame *f, bool deep_p)
{
  (void) deep_p;
  ohos_activate_menubar (f);
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
}
