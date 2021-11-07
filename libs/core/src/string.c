#include "core_alloc.h"
#include "core_ascii.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_sentinel.h"
#include "core_string.h"

#if defined(VOLO_MSVC)

#include <string.h>
#pragma intrinsic(strlen)

#else

#define strlen __builtin_strlen

#endif

String string_from_null_term(const char* ptr) {
  return (String){
      .ptr  = (void*)ptr,
      .size = strlen(ptr),
  };
}

String string_dup(Allocator* alloc, String str) { return alloc_dup(alloc, str, 1); }

String string_combine_raw(Allocator* alloc, const String* parts) {
  usize size = 0;
  for (const String* itr = parts; itr->ptr; ++itr) {
    size += itr->size;
  }

  if (UNLIKELY(!size)) {
    return string_empty;
  }
  String result = alloc_alloc(alloc, size, 1);

  usize offset = 0;
  for (const String* itr = parts; itr->ptr; ++itr) {
    String part = *itr;
    mem_cpy(mem_consume(result, offset), part);
    offset += part.size;
  }
  return result;
}

void string_free(Allocator* alloc, String str) { alloc_free(alloc, str); }

i8 string_cmp(String a, String b) { return mem_cmp(a, b); }

bool string_eq(String a, String b) { return mem_eq(a, b); }

bool string_starts_with(String str, String start) {
  return str.size >= start.size && string_eq(string_slice(str, 0, start.size), start);
}

bool string_ends_with(String str, String end) {
  return str.size >= end.size && string_eq(string_slice(str, str.size - end.size, end.size), end);
}

String string_slice(String str, usize offset, usize size) { return mem_slice(str, offset, size); }

String string_consume(String str, usize amount) { return mem_consume(str, amount); }

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

bool string_match_glob(String str, String pattern, const StringMatchFlags flags) {
  /**
   * Basic glob matching algorithm.
   * More information on the implementation: https://research.swtch.com/glob.
   */
  usize patternIdx     = 0;
  usize strIdx         = 0;
  usize nextPatternIdx = 0;
  usize nextStrIdx     = 0;
  while (patternIdx < pattern.size || strIdx < str.size) {
    if (patternIdx < pattern.size) {
      const u8 patternChar = *string_at(pattern, patternIdx);
      switch (patternChar) {
      case '?':
        if (strIdx < str.size) {
          ++patternIdx;
          ++strIdx;
          continue;
        }
        break;
      case '*':
        nextPatternIdx = patternIdx++;
        nextStrIdx     = strIdx + 1;
        continue;
      default:
        if (strIdx >= str.size || string_is_empty(str)) {
          break;
        }
        const bool charMatches =
            flags & StringMatchFlags_IgnoreCase
                ? ascii_to_lower(*string_at(str, strIdx)) == ascii_to_lower(patternChar)
                : *string_at(str, strIdx) == patternChar;
        if (!charMatches) {
          break;
        }
        ++patternIdx;
        ++strIdx;
        continue;
      }
    }
    // End of pattern segment; resume the previous segment if any, otherwise fail.
    if (nextStrIdx && nextStrIdx <= str.size) {
      patternIdx = nextPatternIdx;
      strIdx     = nextStrIdx;
      continue;
    }
    return false;
  }
  // Entire pattern matched.
  return true;
}

String string_trim(const String value, const String chars) {
  usize offset = 0;
  for (; offset != value.size && mem_contains(chars, *string_at(value, offset)); ++offset)
    ;
  usize size = value.size;
  for (; size && mem_contains(chars, *string_at(value, size - 1)); --size)
    ;
  return UNLIKELY(offset >= size) ? string_empty : string_slice(value, offset, size - offset);
}

String string_trim_whitespace(const String value) {
  return string_trim(value, string_lit(" \t\r\n\v\f"));
}
