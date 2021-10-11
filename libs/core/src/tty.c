#include "core_diag.h"
#include "core_file.h"
#include "core_format.h"

#include "init_internal.h"
#include "tty_internal.h"

#define tty_esc "\33"

void tty_init() { tty_pal_init(); }
void tty_teardown() { tty_pal_teardown(); }

bool tty_isatty(File* file) { return tty_pal_isatty(file); }
u16  tty_width(File* file) { return tty_pal_width(file); }
u16  tty_height(File* file) { return tty_pal_height(file); }
void tty_opts_set(File* file, const TtyOpts opts) { tty_pal_opts_set(file, opts); }
bool tty_read(File* file, DynString* dynstr, const TtyReadFlags flags) {
  return tty_pal_read(file, dynstr, flags);
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
