#include "core_sentinel.h"
#include "core_winutils.h"

#ifdef VOLO_WIN32

#include <Windows.h>

usize winutils_to_widestr_size(String input) {
  const int wideChars = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, (const char*)input.ptr, input.size, null, 0);
  if (wideChars <= 0) {
    return sentinel_usize;
  }
  return wideChars * sizeof(wchar_t) + 1; // +1 for the null-terminator.
}

usize winutils_to_widestr(Mem output, String input) {
  if (output.size < sizeof(wchar_t) + 1) { // +1 for the null-terminator.
    return sentinel_usize;
  }
  const int wideChars = MultiByteToWideChar(
      CP_UTF8,
      MB_ERR_INVALID_CHARS,
      (const char*)input.ptr,
      input.size,
      (wchar_t*)output.ptr,
      output.size / sizeof(wchar_t));
  if (wideChars <= 0) {
    return sentinel_usize;
  }
  *mem_at_u8(output, wideChars * sizeof(wchar_t)) = '\0'; // Null terminate.
  return wideChars;
}

usize winutils_from_widestr_size(void* input, usize inputCharCount) {
  const int chars = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, (const wchar_t*)input, inputCharCount, null, 0, null, null);
  if (chars <= 0) {
    return sentinel_usize;
  }
  return chars;
}

usize winutils_from_widestr(String output, void* input, usize inputCharCount) {
  const int chars = WideCharToMultiByte(
      CP_UTF8,
      WC_ERR_INVALID_CHARS,
      (const wchar_t*)input,
      inputCharCount,
      (char*)output.ptr,
      output.size,
      null,
      null);
  if (chars <= 0) {
    return sentinel_usize;
  }
  return chars;
}

#endif
