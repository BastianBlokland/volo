#include "core_format.h"
#include "init_internal.h"
#include "tty_internal.h"

#define tty_esc "\e"

void tty_init() { tty_pal_init(); }
void tty_teardown() { tty_pal_teardown(); }

bool tty_isatty(File* file) { return tty_pal_isatty(file); }
u16  tty_width(File* file) { return tty_pal_width(file); }
u16  tty_height(File* file) { return tty_pal_height(file); }

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
