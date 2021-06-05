#include "core_math.h"
#include "core_string.h"
#include <string.h>

i8 string_cmp(String a, String b) {
  return math_sign(strncmp((const char*)a.ptr, (const char*)b.ptr, math_min(a.size, b.size)));
}

bool string_eq(String a, String b) { return mem_eq(a, b); }

String string_slice(String str, usize offset, usize size) { return mem_slice(str, offset, size); }
