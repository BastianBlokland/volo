#include "core_diag.h"
#include "core_math.h"
#include "core_sentinel.h"
#include "core_string.h"
#include <string.h>

String string_from_null_term(const char* ptr) {
  return (String){
      .ptr  = (void*)ptr,
      .size = strlen(ptr),
  };
}

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

String string_consume(String str, usize amount) {
  diag_assert(str.size >= amount);
  return (String){
      .ptr  = (u8*)str.ptr + amount,
      .size = str.size - amount,
  };
}

usize string_find_first(String str, String subStr) {
  for (u8* itr = mem_begin(str); itr <= string_end(str) - subStr.size; ++itr) {
    if (mem_eq(mem_create(itr, subStr.size), subStr)) {
      return itr - string_begin(str);
    }
  }
  return sentinel_usize;
}

usize string_find_first_any(String str, String chars) {
  mem_for_u8(str, c, {
    if (mem_contains(chars, c)) {
      return c_itr - string_begin(str);
    }
  });
  return sentinel_usize;
}

usize string_find_last(String str, String subStr) {
  for (u8* itr = mem_end(str) - subStr.size + 1; itr-- > string_begin(str);) {
    if (mem_eq(mem_create(itr, subStr.size), subStr)) {
      return itr - string_begin(str);
    }
  }
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
