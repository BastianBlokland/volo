#include "core_string.h"

i8 string_cmp(String a, String b) { return mem_cmp(a, b); }

bool string_eq(String a, String b) { return mem_eq(a, b); }
