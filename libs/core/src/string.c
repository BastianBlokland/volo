#include "core_alloc.h"
#include "core_ascii.h"
#include "core_bits.h"
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

StringHash string_hash(const String str) { return bits_hash_32(str); }

StringHash string_maybe_hash(const String str) { return str.size ? bits_hash_32(str) : 0; }

String string_from_null_term(const char* ptr) {
  return (String){
      .ptr  = (void*)ptr,
      .size = strlen(ptr),
  };
}

String string_dup(Allocator* alloc, const String str) { return alloc_dup(alloc, str, 1); }

String string_maybe_dup(Allocator* alloc, const String str) {
  return string_is_empty(str) ? string_empty : alloc_dup(alloc, str, 1);
}

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

void string_free(Allocator* alloc, const String str) { alloc_free(alloc, str); }

void string_maybe_free(Allocator* alloc, const String str) {
  if (!string_is_empty(str)) {
    alloc_free(alloc, str);
  }
}

i8 string_cmp(const String a, const String b) { return mem_cmp(a, b); }

bool string_eq(const String a, const String b) { return mem_eq(a, b); }

bool string_starts_with(const String str, const String start) {
  return str.size >= start.size && string_eq(string_slice(str, 0, start.size), start);
}

bool string_ends_with(const String str, const String end) {
  return str.size >= end.size && string_eq(string_slice(str, str.size - end.size, end.size), end);
}

String string_slice(const String str, const usize offset, const usize size) {
  return mem_slice(str, offset, size);
}

String string_consume(const String str, const usize amount) { return mem_consume(str, amount); }

usize string_find_first(const String str, const String subStr) {
  for (u8* itr = mem_begin(str); itr <= string_end(str) - subStr.size; ++itr) {
    if (mem_eq(mem_create(itr, subStr.size), subStr)) {
      return itr - string_begin(str);
    }
  }
  return sentinel_usize;
}

usize string_find_first_char(const String str, const u8 subChar) {
  mem_for_u8(str, itr) {
    if (*itr == subChar) {
      return itr - string_begin(str);
    }
  }
  return sentinel_usize;
}

usize string_find_first_any(const String str, const String chars) {
  mem_for_u8(str, itr) {
    if (mem_contains(chars, *itr)) {
      return itr - string_begin(str);
    }
  }
  return sentinel_usize;
}

usize string_find_last(const String str, const String subStr) {
  for (u8* itr = mem_end(str) - subStr.size + 1; itr-- > string_begin(str);) {
    if (mem_eq(mem_create(itr, subStr.size), subStr)) {
      return itr - string_begin(str);
    }
  }
  return sentinel_usize;
}

usize string_find_last_any(const String str, const String chars) {
  for (u8* itr = mem_end(str); itr-- != mem_begin(str);) {
    if (mem_contains(chars, *itr)) {
      return itr - string_begin(str);
    }
  }
  return sentinel_usize;
}

bool string_match_glob(const String str, const String pattern, const StringMatchFlags flags) {
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
