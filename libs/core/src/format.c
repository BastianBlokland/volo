#include "core_array.h"
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

  const u8 numDigits = mem_end(buffer) - ptr;
  dynstring_append(str, mem_create(ptr, numDigits));
}

void format_write_i64(DynString* str, i64 val, const FormatOptsInt* opts) {
  if (val < 0) {
    dynstring_append_char(str, '-');
    val = -val;
  }
  return format_write_u64(str, val, opts);
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

void format_write_duration_pretty(DynString* str, Duration val) {
  static struct {
    Duration val;
    String   str;
  } units[] = {
      {time_nanosecond, string_lit("ns")},
      {time_microsecond, string_lit("us")},
      {time_millisecond, string_lit("ms")},
      {time_second, string_lit("s")},
      {time_minute, string_lit("m")},
      {time_hour, string_lit("h")},
      {time_day, string_lit("d")},
  };
  size_t i = 0;
  for (; (i + 1) != array_elems(units) && val >= units[i + 1].val; ++i)
    ;
  format_write_float(str, (f64)val / (f64)units[i].val, .maxDecDigits = 1);
  dynstring_append(str, units[i].str);
}
