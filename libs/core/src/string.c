#include "core_math.h"
#include "core_sentinel.h"
#include "core_string.h"
#include <string.h>

i8 string_cmp(String a, String b) {
  return math_sign(strncmp((const char*)a.ptr, (const char*)b.ptr, math_min(a.size, b.size)));
}

bool string_eq(String a, String b) { return mem_eq(a, b); }

bool string_starts_with(String str, String start) {
  return str.size >= start.size && string_eq(string_slice(str, 0, start.size), start);
}

bool string_ends_with(String str, String end) {
  return str.size >= end.size && string_eq(string_slice(str, str.size - end.size, end.size), end);
}

String string_slice(String str, usize offset, usize size) { return mem_slice(str, offset, size); }

usize string_find_first_any(String string, String chars) {
  mem_for_u8(string, c, {
    if (mem_contains(chars, c)) {
      return c_itr - string_begin(string);
    }
  });
  return sentinel_usize;
}

usize string_find_last_any(String str, String chars) {
  for (u8* itr = mem_end(str); itr-- != mem_begin(str);) {
    if (mem_contains(chars, *itr)) {
      return itr - string_begin(str);
    }
  }
  return sentinel_usize;
}
