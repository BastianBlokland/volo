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

void tty_set_window_title(String title) {
  if (!tty_isatty(g_file_stdout)) {
    return;
  }
  static const usize maxTitleLen = 128;
  diag_assert_msg(
      title.size <= maxTitleLen,
      fmt_write_scratch("Tty window-title is too long, maximum is {} chars", fmt_int(maxTitleLen)));

  DynString str = dynstring_create_over(mem_stack(maxTitleLen));
  tty_write_window_title_sequence(&str, title);
  file_write_sync(g_file_stdout, dynstring_view(&str));
  dynstring_destroy(&str);
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
   * Use the xterm extension for setting the window title.
   * Is reasonably widely supported.
   */
  dynstring_append(str, string_lit(tty_esc "]0;"));
  format_write_text(str, title, &format_opts_text(.flags = FormatTextFlags_EscapeNonPrintAscii));
  dynstring_append_char(str, '\a');
}
