#include "core_ascii.h"
#include "core_diag.h"
#include "core_file.h"
#include "core_format.h"
#include "core_utf8.h"

#include "init_internal.h"
#include "tty_internal.h"

void tty_init(void) { tty_pal_init(); }
void tty_teardown(void) { tty_pal_teardown(); }

bool tty_isatty(File* file) { return tty_pal_isatty(file); }
u16  tty_width(File* file) { return tty_pal_width(file); }
u16  tty_height(File* file) { return tty_pal_height(file); }
void tty_opts_set(File* file, const TtyOpts opts) { tty_pal_opts_set(file, opts); }
bool tty_read(File* file, DynString* dynstr, const TtyReadFlags flags) {
  return tty_pal_read(file, dynstr, flags);
}

static String tty_input_lex_escape(String str, TtyInputToken* out) {
  enum { TtyInputEscapeModifiersMax = 16 };
  i64 modifiers[TtyInputEscapeModifiersMax];
  u32 modifierCount = 0;
  for (;;) {
    if (UNLIKELY(string_is_empty(str))) {
      return out->type = TtyInputType_Unsupported, str;
    }
    const u8 ch = string_begin(str)[0];
    switch (ch) {
    case '-':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      if (UNLIKELY(modifierCount == TtyInputEscapeModifiersMax)) {
        return out->type = TtyInputType_Unsupported, string_consume(str, 1);
      }
      str = format_read_i64(str, &modifiers[modifierCount++], 10);
      continue;
    case ';':
      str = string_consume(str, 1);
      continue; // Modifier separator.
    case 'A':
      return out->type = TtyInputType_KeyUp, string_consume(str, 1);
    case 'B':
      return out->type = TtyInputType_KeyDown, string_consume(str, 1);
    case 'C':
      return out->type = TtyInputType_KeyRight, string_consume(str, 1);
    case 'D':
      return out->type = TtyInputType_KeyLeft, string_consume(str, 1);
    case 'F':
      return out->type = TtyInputType_KeyEnd, string_consume(str, 1);
    case 'H':
      return out->type = TtyInputType_KeyHome, string_consume(str, 1);
    case '~':
      if (!modifierCount) {
        return out->type = TtyInputType_Unsupported, string_consume(str, 1);
      }
      switch (modifiers[0]) {
      case 1:
        return out->type = TtyInputType_KeyHome, string_consume(str, 1);
      case 3:
        return out->type = TtyInputType_KeyDelete, string_consume(str, 1);
      case 4:
        return out->type = TtyInputType_KeyEnd, string_consume(str, 1);
      case 7:
        return out->type = TtyInputType_KeyHome, string_consume(str, 1);
      case 8:
        return out->type = TtyInputType_KeyEnd, string_consume(str, 1);
      }
      return out->type = TtyInputType_Unsupported, string_consume(str, 1);
    }
    return out->type = TtyInputType_Unsupported, string_consume(str, 1);
  }
}

String tty_input_lex(String str, TtyInputToken* out) {
  for (;;) {
    Unicode cp;
    str = utf8_cp_read(str, &cp);
    switch (cp) {
    case Unicode_Invalid:
      return out->type = TtyInputType_End, string_empty;
    case Unicode_Escape:
      if (!string_is_empty(str) && string_begin(str)[0] == '[') {
        return tty_input_lex_escape(string_consume(str, 1), out);
      }
      return out->type = TtyInputType_KeyEscape, str;
    case Unicode_EndOfText:
      return out->type = TtyInputType_Interrupt, str;
    case Unicode_Backspace:
      return out->type = TtyInputType_KeyDelete, str;
    case Unicode_Delete:
      return out->type = TtyInputType_KeyBackspace, str;
    case Unicode_Newline:
    case Unicode_CarriageReturn:
      return out->type = TtyInputType_Accept, str;
    default:
      if (!unicode_is_ascii(cp) || ascii_is_printable((u8)cp)) {
        // Either a printable ascii character or a non-ascii character.
        return out->type = TtyInputType_Text, out->val_text = cp, str;
      }
      return out->type = TtyInputType_Unsupported, str;
    }
  }
}

void tty_write_style_sequence(DynString* str, TtyStyle style) {
  /**
   * Write a 'CSI' sequence.
   * More info: https://en.wikipedia.org/wiki/ANSI_escape_code.
   */
  dynstring_append(str, string_lit(tty_esc "["));
  if (style.fgColor) {
    format_write_int(str, style.fgColor);
    dynstring_append_char(str, ';');
  }
  if (style.bgColor) {
    format_write_int(str, style.bgColor);
    dynstring_append_char(str, ';');
  }
  if (style.flags & TtyStyleFlags_Bold) {
    format_write_int(str, 1);
    dynstring_append_char(str, ';');
  }
  if (style.flags & TtyStyleFlags_Faint) {
    format_write_int(str, 2);
    dynstring_append_char(str, ';');
  }
  if (style.flags & TtyStyleFlags_Italic) {
    format_write_int(str, 3);
    dynstring_append_char(str, ';');
  }
  if (style.flags & TtyStyleFlags_Underline) {
    format_write_int(str, 4);
    dynstring_append_char(str, ';');
  }
  if (style.flags & TtyStyleFlags_Blink) {
    format_write_int(str, 5);
    dynstring_append_char(str, ';');
  }
  if (style.flags & TtyStyleFlags_Reversed) {
    format_write_int(str, 7);
    dynstring_append_char(str, ';');
  }
  u8* last = string_last(dynstring_view(str));
  if (*last == ';') {
    *last = 'm';
  } else {
    dynstring_append_char(str, 'm');
  }
}

void tty_write_window_title_sequence(DynString* str, String title) {
  /**
   * Private 'CSI' sequence.
   * xterm extension for setting the window title.
   */
  dynstring_append(str, string_lit(tty_esc "]0;"));
  format_write_text(str, title, &format_opts_text(.flags = FormatTextFlags_EscapeNonPrintAscii));
  dynstring_append_char(str, '\a');
}

void tty_write_set_cursor_sequence(DynString* str, const u32 row, const u32 col) {
  /**
   * 'CSI' sequence: 'Cursor Position'.
   */
  dynstring_append(str, string_lit(tty_esc "["));
  format_write_int(str, row);
  dynstring_append_char(str, ';');
  format_write_int(str, col);
  dynstring_append_char(str, 'H');
}

void tty_write_set_cursor_hor_sequence(DynString* str, const u32 col) {
  /**
   * 'CSI' sequence: 'Cursor Horizontal Absolute'.
   */
  dynstring_append(str, string_lit(tty_esc "["));
  format_write_int(str, col);
  dynstring_append_char(str, 'G');
}

void tty_write_cursor_show_sequence(DynString* str, const bool show) {
  /**
   * Private 'CSI' sequence.
   * VT220 sequence for hiding / showing the cursor, broadly supported.
   */
  dynstring_append(str, string_lit(tty_esc "[?25"));
  dynstring_append_char(str, show ? 'h' : 'l');
}

void tty_write_clear_sequence(DynString* str, const TtyClearMode mode) {
  /**
   * 'CSI' sequence: 'Erase in Display'.
   */
  dynstring_append(str, string_lit(tty_esc "["));
  format_write_int(str, mode);
  dynstring_append_char(str, 'J');
}

void tty_write_clear_line_sequence(DynString* str, const TtyClearMode mode) {
  /**
   * 'CSI' sequence: 'Erase in Line'.
   */
  dynstring_append(str, string_lit(tty_esc "["));
  format_write_int(str, mode);
  dynstring_append_char(str, 'K');
}

void tty_write_alt_screen_sequence(DynString* str, const bool enable) {
  /**
   * Private 'CSI' sequence.
   * xterm extension for enabling / disabling the alternative screen buffer.
   */
  dynstring_append(str, string_lit(tty_esc "[?1049"));
  dynstring_append_char(str, enable ? 'h' : 'l');
}

void tty_write_line_wrap_sequence(DynString* str, bool enable) {
  /**
   * 'CSI' sequence: 'Enable Line Wrap' / 'Disable Line Wrap'.
   */
  dynstring_append(str, string_lit(tty_esc "[?7"));
  dynstring_append_char(str, enable ? 'h' : 'l');
}
