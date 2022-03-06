#include "core_array.h"
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

static bool ui_escape_check_min_chars(String input, usize minChars, UiEscape* out) {
  if (input.size < minChars) {
    if (out) {
      *out = ui_escape_invalid;
    }
    return false;
  }
  return true;
}

static String ui_escape_read_reset(String input, UiEscape* out) {
  if (out) {
    *out = (UiEscape){.type = UiEscape_Reset};
  }
  return input;
}

static String ui_escape_read_color(String input, UiEscape* out) {
  if (!ui_escape_check_min_chars(input, 8, out)) {
    return input;
  }
  if (!out) {
    return string_consume(input, 8); // Fast path in case the output is not needed.
  }

  UiColor color;
  input = ui_escape_read_byte_hex(input, &color.r);
  input = ui_escape_read_byte_hex(input, &color.g);
  input = ui_escape_read_byte_hex(input, &color.b);
  input = ui_escape_read_byte_hex(input, &color.a);

  *out = (UiEscape){.type = UiEscape_Color, .escColor = {.value = color}};
  return input;
}

static String ui_escape_read_color_named(const String input, UiEscape* out) {
  struct UiNamedColor {
    String  name;
    UiColor value;
  };
  static const struct UiNamedColor g_namedColors[] = {
      {string_static("white"), {0xFF, 0xFF, 0xFF, 0xFF}},
      {string_static("black"), {0x00, 0x00, 0x00, 0xFF}},
      {string_static("clear"), {0x00, 0x00, 0x00, 0x00}},
      {string_static("silver"), {0xC0, 0xC0, 0xC0, 0xFF}},
      {string_static("gray"), {0x80, 0x80, 0x80, 0xFF}},
      {string_static("red"), {0xFF, 0x00, 0x00, 0xFF}},
      {string_static("maroon"), {0x80, 0x00, 0x00, 0xFF}},
      {string_static("yellow"), {0xFF, 0xFF, 0x00, 0xFF}},
      {string_static("olive"), {0x80, 0x80, 0x00, 0xFF}},
      {string_static("lime"), {0x00, 0xFF, 0x00, 0xFF}},
      {string_static("green"), {0x00, 0x80, 0x00, 0xFF}},
      {string_static("aqua"), {0x00, 0xFF, 0xFF, 0xFF}},
      {string_static("teal"), {0x00, 0x80, 0x80, 0xFF}},
      {string_static("blue"), {0x00, 0x00, 0xFF, 0xFF}},
      {string_static("navy"), {0x00, 0x00, 0x80, 0xFF}},
      {string_static("fuchsia"), {0xFF, 0x00, 0xFF, 0xFF}},
      {string_static("purple"), {0x80, 0x00, 0x80, 0xFF}},
  };

  array_for_t(g_namedColors, struct UiNamedColor, namedColor) {
    if (string_starts_with(input, namedColor->name)) {
      if (out) {
        *out = (UiEscape){.type = UiEscape_Color, .escColor = {.value = namedColor->value}};
      }
      return string_consume(input, namedColor->name.size);
    }
  }
  if (out) {
    *out = ui_escape_invalid;
  }
  return input;
}

static String ui_escape_read_outline(String input, UiEscape* out) {
  if (!ui_escape_check_min_chars(input, 2, out)) {
    return input;
  }
  if (!out) {
    return string_consume(input, 2); // Fast path in case the output is not needed.
  }
  u8 outlineWidth;
  input = ui_escape_read_byte_hex(input, &outlineWidth);

  *out = (UiEscape){.type = UiEscape_Outline, .escOutline = {.value = outlineWidth}};
  return input;
}

String ui_escape_read(String input, UiEscape* out) {
  u8 ch;
  input = format_read_char(input, &ch);
  switch (ch) {
  case 'r':
    return ui_escape_read_reset(input, out);
  case '#':
    return ui_escape_read_color(input, out);
  case '~':
    return ui_escape_read_color_named(input, out);
  case '|':
    return ui_escape_read_outline(input, out);
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

String ui_escape_outline_scratch(const u8 outline) {
  return fmt_write_scratch("\33|{}", fmt_int(outline, .base = 16, .minDigits = 2));
}
