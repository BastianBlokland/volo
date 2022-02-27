#include "core_ascii.h"
#include "core_diag.h"

#include "escape_internal.h"

#define ui_escape_invalid                                                                          \
  (UiEscape) { .type = UiEscape_Invalid }

static String ui_escape_read_byte_hex(String input, u8* out) {
  diag_assert(input.size >= 2);
  const u8 c1 = string_begin(input)[0];
  const u8 c2 = string_begin(input)[1];
  *out        = (ascii_to_integer(c1) << 4) | ascii_to_integer(c2);
  return string_consume(input, 2);
}

static String ui_escape_read_color(String input, UiEscape* out) {
  if (input.size < 8) {
    if (out) {
      *out = ui_escape_invalid;
    }
    return input;
  }
  if (!out) {
    // Fast path in case the output is not needed.
    return string_consume(input, 8);
  }

  UiColor color;
  input = ui_escape_read_byte_hex(input, &color.r);
  input = ui_escape_read_byte_hex(input, &color.g);
  input = ui_escape_read_byte_hex(input, &color.b);
  input = ui_escape_read_byte_hex(input, &color.a);

  *out = (UiEscape){.type = UiEscape_Color, .escColor = {.value = color}};
  return input;
}

String ui_escape_read(String input, UiEscape* out) {
  u8 ch;
  input = format_read_char(input, &ch);
  switch (ch) {
  case '#':
    return ui_escape_read_color(input, out);
  default:
    if (out) {
      *out = ui_escape_invalid;
    }
    return input;
  }
}

String ui_escape_color_scratch(const UiColor color) {
  return fmt_write_scratch(
      "\33#{}{}{}{}",
      fmt_int(color.r, .base = 16, .minDigits = 2),
      fmt_int(color.g, .base = 16, .minDigits = 2),
      fmt_int(color.b, .base = 16, .minDigits = 2),
      fmt_int(color.a, .base = 16, .minDigits = 2));
}
