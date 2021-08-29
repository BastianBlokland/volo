#include "core_diag.h"
#include "core_sentinel.h"
#include "core_winutils.h"

#ifdef VOLO_WIN32

#include <Windows.h>

usize winutils_to_widestr_size(String input) {
  diag_assert_msg(!string_is_empty(input), "Empty input provided to winutils_to_widestr_size");

  const int wideChars = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, (const char*)input.ptr, (int)input.size, null, 0);
  if (wideChars <= 0) {
    return sentinel_usize;
  }
  return (wideChars + 1) * sizeof(wchar_t); // +1 for the null-terminator.
}

usize winutils_to_widestr(Mem output, String input) {
  diag_assert_msg(!string_is_empty(input), "Empty input provided to winutils_to_widestr");

  if (output.size < sizeof(wchar_t) * 2) { // +1 for the null-terminator.
    return sentinel_usize;
  }
  const int wideChars = MultiByteToWideChar(
      CP_UTF8,
      MB_ERR_INVALID_CHARS,
      (const char*)input.ptr,
      (int)input.size,
      (wchar_t*)output.ptr,
      (int)(output.size / sizeof(wchar_t)) - 1);
  if (wideChars <= 0) {
    return sentinel_usize;
  }
  mem_set(mem_slice(output, wideChars * sizeof(wchar_t), sizeof(wchar_t)), 0); // Null terminate.
  return wideChars;
}

usize winutils_from_widestr_size(void* input, usize inputCharCount) {
  diag_assert_msg(inputCharCount, "Zero characters provided to winutils_from_widestr_size");

  const int chars = WideCharToMultiByte(
      CP_UTF8,
      WC_ERR_INVALID_CHARS,
      (const wchar_t*)input,
      (int)inputCharCount,
      null,
      0,
      null,
      null);
  if (chars <= 0) {
    return sentinel_usize;
  }
  return chars;
}

usize winutils_from_widestr(String output, void* input, usize inputCharCount) {
  diag_assert_msg(inputCharCount, "Zero characters provided to winutils_from_widestr");

  const int chars = WideCharToMultiByte(
      CP_UTF8,
      WC_ERR_INVALID_CHARS,
      (const wchar_t*)input,
      (int)inputCharCount,
      (char*)output.ptr,
      (int)output.size,
      null,
      null);
  if (chars <= 0) {
    return sentinel_usize;
  }
  return chars;
}

#endif
