#include "core.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_ascii.h"
#include "core_dynstring.h"
#include "core_format.h"
#include "core_path.h"
#include "core_rng.h"
#include "core_sentinel.h"
#include "core_string.h"
#include "core_time.h"

#include "init_internal.h"
#include "path_internal.h"

static String g_pathSeparators = string_static("/\\");

static bool path_ends_with_separator(const String str) {
  return mem_contains(g_pathSeparators, *string_last(str));
}

static bool path_starts_with_posix_root(const String path) {
  return !string_is_empty(path) && *string_begin(path) == '/';
}

static bool path_starts_with_win32_root(const String path) {
  if (path.size < 3) {
    return false;
  }
  if (!ascii_is_letter(*string_begin(path))) {
    return false;
  }
  const String postDriveLetter = string_slice(path, 1, 2);
  return string_eq(postDriveLetter, string_lit(":/")) ||
         string_eq(postDriveLetter, string_lit(":\\"));
}

static u8 g_pathWorkingDir_buffer[path_pal_max_size];
String    g_pathWorkingDir = {0};

static u8 g_pathExecutable_buffer[path_pal_max_size];
String    g_pathExecutable = {0};

static u8 g_pathTempDir_buffer[path_pal_max_size];
String    g_pathTempDir = {0};

void path_init(void) {
  g_pathWorkingDir = path_pal_workingdir(array_mem(g_pathWorkingDir_buffer));
  g_pathExecutable = path_pal_executable(array_mem(g_pathExecutable_buffer));
  g_pathTempDir    = path_pal_tempdir(array_mem(g_pathTempDir_buffer));
}

bool path_is_absolute(const String path) {
  return path_starts_with_posix_root(path) || path_starts_with_win32_root(path);
}

bool path_is_root(const String path) {
  return (path.size == 1 && path_starts_with_posix_root(path)) ||
         (path.size == 3 && path_starts_with_win32_root(path));
}

String path_filename(const String path) {
  const usize lastSegStart = string_find_last_any(path, g_pathSeparators);
  return sentinel_check(lastSegStart)
             ? path
             : string_slice(path, lastSegStart + 1, path.size - lastSegStart - 1);
}

String path_extension(const String path) {
  const String fileName       = path_filename(path);
  const usize  extensionStart = string_find_last_any(fileName, string_lit("."));
  return sentinel_check(extensionStart)
             ? string_empty
             : string_slice(fileName, extensionStart + 1, fileName.size - extensionStart - 1);
}

String path_stem(const String path) {
  const String fileName       = path_filename(path);
  const usize  extensionStart = string_find_first_any(fileName, string_lit("."));
  return sentinel_check(extensionStart) ? fileName : string_slice(fileName, 0, extensionStart);
}

String path_parent(const String path) {
  const usize lastSegStart = string_find_last_any(path, g_pathSeparators);
  if (sentinel_check(lastSegStart)) {
    return string_empty;
  }

  // For the root directory we preserve the separator, for any other directory we do not.
  const String parentWithSep = string_slice(path, 0, lastSegStart + 1);
  return path_is_root(parentWithSep) ? parentWithSep : string_slice(path, 0, lastSegStart);
}

bool path_canonize(DynString* str, String path) {

  /**
   * Canonize the root in case of an absolute path.
   * NOTE: Windows drive letters are canonized to uppercase.
   */

  if (path_starts_with_posix_root(path)) {
    dynstring_append_char(str, '/');
    path = string_consume(path, 1);
  } else if (path_starts_with_win32_root(path)) {
    dynstring_append_char(str, ascii_to_upper(*string_begin(path)));
    dynstring_append(str, string_lit(":/"));
    path = string_consume(path, 3);
  }

  /**
   * Canonize the segments of the path. Keep a array of the starting position of each segment in the
   * output string. This way we can erase a segment if we encounter a '..' entry.
   */

  usize segStarts[64];
  u32   segCount = 0;

  segStarts[segCount++] = str->size; // Start of the first segment.

  bool success = true;
  while (path.size) {
    const usize segEnd = string_find_first_any(path, g_pathSeparators);
    String      seg;
    if (sentinel_check(segEnd)) {
      seg  = path;
      path = string_empty;
    } else {
      seg  = string_slice(path, 0, segEnd);
      path = string_consume(path, segEnd + 1);
    }
    if (string_is_empty(seg) || string_eq(seg, string_lit("."))) {
      continue;
    }
    if (string_eq(seg, string_lit(".."))) {
      if (segCount > 1) {
        // Erase the last written segment.
        str->size = segStarts[segCount - 1];
        --segCount;
      }
      continue;
    }

    if (segCount > 1 && !path_ends_with_separator(dynstring_view(str))) {
      dynstring_append_char(str, '/');
    }
    segStarts[segCount++] = str->size; // Remember where this segment starts.
    if (UNLIKELY(segCount == array_elems(segStarts))) {
      success = false;
      break;
    }
    dynstring_append(str, seg); // Write the segment to the output.
  }

  return success;
}

String path_canonize_scratch(const String path) {
  Mem       scratchMem = alloc_alloc(g_allocScratch, path_pal_max_size, 1);
  DynString str        = dynstring_create_over(scratchMem);

  path_canonize(&str, path);

  String res = dynstring_view(&str);
  dynstring_destroy(&str);
  return res;
}

void path_append(DynString* str, const String path) {
  if (str->size && !path_ends_with_separator(dynstring_view(str))) {
    dynstring_append_char(str, '/');
  }
  dynstring_append(str, path);
}

void path_build_raw(DynString* str, const String* segments) {
  DynString tmpWriter = dynstring_create_over(mem_stack(path_pal_max_size));

  const bool prependWorkingDir = !segments->ptr || !path_is_absolute(*segments);
  if (prependWorkingDir) {
    dynstring_append(&tmpWriter, g_pathWorkingDir);
  }
  for (; segments->ptr && !string_is_empty(*segments); ++segments) {
    path_append(&tmpWriter, *segments);
  }

  path_canonize(str, dynstring_view(&tmpWriter));
  dynstring_destroy(&tmpWriter);
}

String path_build_scratch_raw(const String* segments) {
  Mem       scratchMem = alloc_alloc(g_allocScratch, path_pal_max_size, 1);
  DynString str        = dynstring_create_over(scratchMem);

  path_build_raw(&str, segments);

  String res = dynstring_view(&str);
  dynstring_destroy(&str);
  return res;
}

void path_name_random(DynString* str, Rng* rng, const String prefix, const String extension) {
  static const u8 g_chars[] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
                               'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
                               'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
                               'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
                               '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};

  if (!string_is_empty(prefix)) {
    dynstring_append(str, prefix);
    dynstring_append_char(str, '_');
  }

  static const usize g_nameSize = 16; // Note: Only multiples of 4 are supported atm.
  for (usize i = 0; i < g_nameSize; i += 4) {
    const u32 rngVal = rng_sample_u32(rng);
    dynstring_append_char(str, g_chars[((rngVal >> 0) & 255) % array_elems(g_chars)]);
    dynstring_append_char(str, g_chars[((rngVal >> 1) & 255) % array_elems(g_chars)]);
    dynstring_append_char(str, g_chars[((rngVal >> 2) & 255) % array_elems(g_chars)]);
    dynstring_append_char(str, g_chars[((rngVal >> 3) & 255) % array_elems(g_chars)]);
  }

  if (!string_is_empty(extension)) {
    dynstring_append_char(str, '.');
    dynstring_append(str, extension);
  }
}

String path_name_random_scratch(Rng* rng, const String prefix, const String extension) {
  Mem       scratchMem = alloc_alloc(g_allocScratch, prefix.size + 32 + extension.size, 1);
  DynString str        = dynstring_create_over(scratchMem);

  path_name_random(&str, rng, prefix, extension);

  String res = dynstring_view(&str);
  dynstring_destroy(&str);
  return res;
}

void path_name_timestamp(DynString* str, const String prefix, const String extension) {
  if (!string_is_empty(prefix)) {
    dynstring_append(str, prefix);
    dynstring_append_char(str, '_');
  }

  format_write_time_iso8601(
      str,
      time_real_clock(),
      &format_opts_time(
              .terms = FormatTimeTerms_Date | FormatTimeTerms_Time, .flags = FormatTimeFlags_None));

  if (!string_is_empty(extension)) {
    dynstring_append_char(str, '.');
    dynstring_append(str, extension);
  }
}

String path_name_timestamp_scratch(const String prefix, const String extension) {
  Mem       scratchMem = alloc_alloc(g_allocScratch, prefix.size + 32 + extension.size, 1);
  DynString str        = dynstring_create_over(scratchMem);

  path_name_timestamp(&str, prefix, extension);

  String res = dynstring_view(&str);
  dynstring_destroy(&str);
  return res;
}
