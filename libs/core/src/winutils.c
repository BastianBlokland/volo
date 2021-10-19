#include "core_alloc.h"
#include "core_diag.h"
#include "core_sentinel.h"
#include "core_winutils.h"

#if defined(VOLO_WIN32)

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

Mem winutils_to_widestr_scratch(String input) {
  const usize size = winutils_to_widestr_size(input);
  if (UNLIKELY(sentinel_check(size))) {
    diag_crash_msg("winutils_to_widestr_scratch: Input is not valid utf8");
  }
  Mem result = alloc_alloc(g_alloc_scratch, size, 1);
  winutils_to_widestr(result, input);
  return result;
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

String winutils_from_widestr_scratch(void* input, usize inputCharCount) {
  const usize size = winutils_from_widestr_size(input, inputCharCount);
  if (UNLIKELY(sentinel_check(size))) {
    diag_crash_msg("winutils_from_widestr_scratch: Input cannot be represented as utf8");
  }
  String result = alloc_alloc(g_alloc_scratch, size, 1);
  winutils_from_widestr(result, input, inputCharCount);
  return result;
}

String winutils_error_msg_scratch(unsigned long errCode) {
  Mem buffer = alloc_alloc(g_alloc_scratch, 2 * usize_kibibyte, 1);

  const DWORD chars = FormatMessage(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      null,
      errCode,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      (wchar_t*)buffer.ptr,
      buffer.size / sizeof(wchar_t),
      null);
  if (UNLIKELY(chars == 0)) {
    diag_crash_msg("Failed to format win32 error-code: {}", fmt_int((u64)errCode));
  }
  return winutils_from_widestr_scratch(buffer.ptr, chars);
}

#endif
