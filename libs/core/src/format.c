#include "core_array.h"
#include "core_ascii.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_format.h"
#include "core_math.h"

void format_write_u64(DynString* str, u64 val, const FormatOptsInt* opts) {
  diag_assert(opts->base > 1 && opts->base <= 16);

  Mem buffer = mem_stack(64);
  u8* ptr    = mem_end(buffer);

  const char* chars         = "0123456789ABCDEF";
  u8          digitsWritten = 0;
  do {
    *--ptr = chars[val % opts->base];
    val /= opts->base;
  } while (++digitsWritten < opts->minDigits || val);

  const u8 numDigits = (u8)(mem_end(buffer) - ptr);
  dynstring_append(str, mem_create(ptr, numDigits));
}

void format_write_i64(DynString* str, i64 val, const FormatOptsInt* opts) {
  if (val < 0) {
    dynstring_append_char(str, '-');
    val = -val;
  }
  format_write_u64(str, val, opts);
}

struct FormatF64Exp {
  i16 exp;
  f64 remaining;
};

/**
 * Calculate the exponent (for scientific notation) for the given float.
 */
static struct FormatF64Exp format_f64_decompose_exp(const f64 val, const FormatOptsFloat* opts) {

  /**
   * Uses binary jumps in the exponentiation, this is a reasonable compromize between the highly
   * inaccurate: 'just loop and keep dividing by 10' and the expensive 'log()' calculation.
   *
   * More info: https://blog.benoitblanchon.fr/lightweight-float-to-string/
   */

  static f64 binPow10[]           = {1e1, 1e2, 1e4, 1e8, 1e16, 1e32, 1e64, 1e128, 1e256};
  static f64 negBinPow10[]        = {1e-1, 1e-2, 1e-4, 1e-8, 1e-16, 1e-32, 1e-64, 1e-128, 1e-256};
  static f64 negBinPow10PlusOne[] = {1e0, 1e-1, 1e-3, 1e-7, 1e-15, 1e-31, 1e-63, 1e-127, 1e-255};

  struct FormatF64Exp res;
  res.exp       = 0;
  res.remaining = val;

  i32 i   = array_elems(binPow10) - 1;
  i32 bit = 1 << i;

  if (val >= opts->expThresholdPos) {
    // Calculate the positive exponent.
    for (; i >= 0; --i) {
      if (res.remaining >= binPow10[i]) {
        res.remaining *= negBinPow10[i];
        res.exp = (i16)(res.exp + bit);
      }
      bit >>= 1;
    }
  } else if (val > 0 && val <= opts->expThresholdNeg) {
    // Calculate the negative exponent.
    for (; i >= 0; --i) {
      if (res.remaining < negBinPow10PlusOne[i]) {
        res.remaining *= binPow10[i];
        res.exp = (i16)(res.exp - bit);
      }
      bit >>= 1;
    }
  }

  return res;
}

struct FormatF64Parts {
  u64 intPart;
  u64 decPart;
  u8  decDigits;
  i16 expPart;
};

static struct FormatF64Parts format_f64_decompose(const f64 val, const FormatOptsFloat* opts) {
  diag_assert(val >= 0.0); // Negative values should be handled earlier.
  diag_assert(opts->minDecDigits <= opts->maxDecDigits);

  const struct FormatF64Exp exp = format_f64_decompose_exp(val, opts);

  struct FormatF64Parts res;
  res.expPart   = exp.exp;
  res.decDigits = opts->maxDecDigits;
  res.intPart   = (u64)exp.remaining;

  const u64 maxDecPart = math_pow10_u64(res.decDigits);
  f64       remainder  = (exp.remaining - (f64)res.intPart) * (f64)maxDecPart;
  res.decPart          = (u64)remainder;

  // Apply rounding.
  remainder -= res.decPart;
  if (remainder >= 0.5) {
    ++res.decPart;
    if (res.decPart >= maxDecPart) {
      res.decPart = 0;
      ++res.intPart;
      if (res.expPart && res.intPart >= 10) {
        res.expPart++;
        res.intPart = 1;
      }
    }
  }

  // Remove trailing zeroes in the decimal part.
  while (res.decPart % 10 == 0 && res.decDigits > opts->minDecDigits) {
    res.decPart /= 10;
    --res.decDigits;
  }

  return res;
}

void format_write_f64(DynString* str, f64 val, const FormatOptsFloat* opts) {
  /**
   * Simple routine for formatting floating-point numbers with reasonable accuracy.
   * Implementation based on: https://blog.benoitblanchon.fr/lightweight-float-to-string/
   */

  if (float_isnan(val)) {
    dynstring_append(str, string_lit("nan"));
    return;
  }
  if (val < 0.0) {
    dynstring_append_char(str, '-');
    val = -val;
  }
  if (float_isinf(val)) {
    dynstring_append(str, string_lit("inf"));
    return;
  }

  const struct FormatF64Parts parts = format_f64_decompose(val, opts);

  format_write_int(str, parts.intPart);
  if (parts.decDigits) {
    dynstring_append_char(str, '.');
    format_write_int(str, parts.decPart, .minDigits = parts.decDigits);
  }
  if (parts.expPart) {
    dynstring_append_char(str, 'e');
    format_write_int(str, parts.expPart);
  }
}

void format_write_bool(DynString* str, const bool val) {
  dynstring_append(str, val ? string_lit("true") : string_lit("false"));
}

void format_write_bitset(DynString* str, const BitSet val) {
  for (usize i = bitset_size(val); i-- > 0;) {
    dynstring_append_char(str, bitset_test(val, i) ? '1' : '0');
  }
}

void format_write_mem(DynString* str, const Mem val) {
  for (usize i = val.size; i-- > 0;) {
    format_write_int(str, *mem_at_u8(val, i), .minDigits = 2, .base = 16);
  }
}

void format_write_time_duration_pretty(DynString* str, const TimeDuration val) {
  static struct {
    TimeDuration val;
    String       str;
  } units[] = {
      {time_nanosecond, string_static("ns")},
      {time_microsecond, string_static("us")},
      {time_millisecond, string_static("ms")},
      {time_second, string_static("s")},
      {time_minute, string_static("m")},
      {time_hour, string_static("h")},
      {time_day, string_static("d")},
  };
  const TimeDuration absVal = math_abs(val);
  size_t             i      = 0;
  for (; (i + 1) != array_elems(units) && absVal >= units[i + 1].val; ++i)
    ;
  format_write_float(str, (f64)val / (f64)units[i].val, .maxDecDigits = 1);
  dynstring_append(str, units[i].str);
}

void format_write_time_iso8601(DynString* str, const TimeReal val, const FormatOptsTime* opts) {

  const TimeReal localTime = time_real_offset(val, time_zone_to_duration(opts->timezone));
  const TimeDate date      = time_real_to_date(localTime);
  const u8       hours     = (localTime / (time_hour / time_microsecond)) % 24;
  const u8       minutes   = (localTime / (time_minute / time_microsecond)) % 60;
  const u8       seconds   = (localTime / (time_second / time_microsecond)) % 60;

  // Date.
  if (opts->terms & FormatTimeTerms_Date) {
    format_write_int(str, date.year, .minDigits = 4);
    dynstring_append_char(str, '-');
    format_write_int(str, date.month, .minDigits = 2);
    dynstring_append_char(str, '-');
    format_write_int(str, date.day, .minDigits = 2);
  }

  // Time.
  if (opts->terms & FormatTimeTerms_Time) {
    dynstring_append_char(str, 'T');
    format_write_int(str, hours, .minDigits = 2);
    dynstring_append_char(str, ':');
    format_write_int(str, minutes, .minDigits = 2);
    dynstring_append_char(str, ':');
    format_write_int(str, seconds, .minDigits = 2);
  }
  if (opts->terms & FormatTimeTerms_Milliseconds) {
    const u16 milliseconds = (localTime / (time_millisecond / time_microsecond)) % 1000;
    dynstring_append_char(str, '.');
    format_write_int(str, milliseconds, .minDigits = 3);
  }

  // Timezone.
  if (opts->terms & FormatTimeTerms_Timezone) {
    if (opts->timezone == time_zone_utc) {
      dynstring_append_char(str, 'Z');
    } else {
      if (opts->timezone > 0) {
        dynstring_append_char(str, '+');
      }
      format_write_int(str, opts->timezone / 60, .minDigits = 2);
      dynstring_append_char(str, ':');
      format_write_int(str, opts->timezone % 60, .minDigits = 2);
    }
  }
}

void format_write_size_pretty(DynString* str, const usize val) {
  static String units[] = {
      string_static("B"),
      string_static("KiB"),
      string_static("MiB"),
      string_static("GiB"),
      string_static("TiB"),
      string_static("PiB"),
  };

  u8  unit       = 0;
  f64 scaledSize = val;
  for (; scaledSize >= 1024.0 && unit != array_elems(units) - 1; ++unit) {
    scaledSize /= 1024.0;
  }
  format_write_float(str, scaledSize, .maxDecDigits = 1);
  dynstring_append(str, units[unit]);
}

void format_write_text(DynString* str, String val, const FormatOptsText* opts) {

  static struct {
    u8     byte;
    String escapeSeq;
  } escapes[] = {
      {'"', string_static("\\\"")},
      {'\\', string_static("\\\\")},
      {'\r', string_static("\\r")},
      {'\n', string_static("\\n")},
      {'\t', string_static("\\t")},
      {'\b', string_static("\\b")},
      {'\f', string_static("\\f")},
      {'\0', string_static("\\0")},
  };

  mem_for_u8(val, byte, {
    if (opts->flags & FormatTextFlags_EscapeNonPrintAscii && !ascii_is_printable(byte)) {
      // If we have a well-known sequence for this byte we apply it.
      for (size_t i = 0; i != array_elems(escapes); ++i) {
        if (escapes[i].byte == byte) {
          dynstring_append(str, escapes[i].escapeSeq);
          goto byte_end;
        }
      }
      // Otherwise escape it as \hex.
      dynstring_append_char(str, '\\');
      format_write_int(str, byte, .base = 16, .minDigits = 2);
      goto byte_end;
    }
    // No escape needed: write verbatim.
    dynstring_append_char(str, byte);
  byte_end:
    continue;
  });
}
