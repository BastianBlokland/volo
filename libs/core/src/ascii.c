#include "core_ascii.h"
#include "core_sentinel.h"

bool ascii_is_valid(const u8 c) { return (c & 0b10000000) == 0; }

bool ascii_is_digit(const u8 c) { return c >= '0' && c <= '9'; }

bool ascii_is_hex_digit(const u8 c) {
  return ascii_is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool ascii_is_letter(const u8 c) { return ascii_is_lower(c) || ascii_is_upper(c); }

bool ascii_is_lower(const u8 c) { return c >= 'a' && c <= 'z'; }

bool ascii_is_upper(const u8 c) { return c >= 'A' && c <= 'Z'; }

bool ascii_is_control(const u8 c) { return c <= 0x1f || c == 0x7f; }

bool ascii_is_whitespace(const u8 c) { return c == ' ' || c == '\t' || (c >= 0x0A && c <= 0x0D); }

bool ascii_is_newline(const u8 c) { return c == '\n' || c == '\r'; }

bool ascii_is_printable(const u8 c) { return !ascii_is_control(c) && c < 127; }

u8 ascii_toggle_case(const u8 c) { return c ^ 0x20; }

u8 ascii_to_upper(const u8 c) { return ascii_is_lower(c) ? ascii_toggle_case(c) : c; }

u8 ascii_to_lower(const u8 c) { return ascii_is_upper(c) ? ascii_toggle_case(c) : c; }

u8 ascii_to_integer(const u8 c) {
  if (ascii_is_digit(c)) {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - ('a' - 10);
  }
  if (c >= 'A' && c <= 'F') {
    return c - ('A' - 10);
  }
  return sentinel_u8;
}
