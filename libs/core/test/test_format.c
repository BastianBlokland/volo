#include "core_array.h"
#include "core_float.h"
#include "core_format.h"

#include "check_spec.h"

spec(format) {

  it("can write FormatArg's") {
    struct {
      FormatArg* arg;
      String     expected;
    } const data[] = {
        {&fmt_int(42), string_lit("42")},
        {&fmt_int(-42), string_lit("-42")},
        {&fmt_int(42, .base = 16), string_lit("2A")},
        {&fmt_float(42.42), string_lit("42.42")},
        {&fmt_bool(true), string_lit("true")},
        {&fmt_mem(string_lit("Hello")), string_lit("6F6C6C6548")},
        {&fmt_duration(time_minute), string_lit("1m")},
        {&fmt_size(usize_mebibyte), string_lit("1MiB")},
        {&fmt_text_lit("Hello World"), string_lit("Hello World")},
        {&fmt_char('a'), string_lit("a")},
        {&fmt_path(string_lit("c:\\hello")), string_lit("C:/hello")},
        {&fmt_padding(5), string_lit("     ")},
    };

    DynString string = dynstring_create_over(mem_stack(128));
    for (usize i = 0; i != array_elems(data); ++i) {
      dynstring_clear(&string);
      format_write_arg(&string, data[i].arg);
      check_eq_string(dynstring_view(&string), data[i].expected);
    }
    dynstring_destroy(&string);
  }

  it("can write formatted strings") {
    struct {
      String           format;
      const FormatArg* args;
      String           expected;
    } const data[] = {
        {string_lit("Value {}"), fmt_args(fmt_int(42)), string_lit("Value 42")},
        {string_lit("hello world"), fmt_args(), string_lit("hello world")},
        {string_lit("{} hello world {  }-{ \t }"),
         fmt_args(fmt_bool(false), fmt_int(42), fmt_bool(true)),
         string_lit("false hello world 42-true")},
        {string_lit("{>4}|{<4}|"), fmt_args(fmt_int(1), fmt_int(20)), string_lit("   1|20  |")},
        {string_lit("{ >4 }|{ >4}|{:4}|{:4}|"),
         fmt_args(fmt_int(1), fmt_int(20), fmt_int(1), fmt_int(42)),
         string_lit("   1|  20| 1  | 42 |")},
        {string_lit("{}"),
         fmt_args(fmt_list_lit(fmt_int(1), fmt_int(2), fmt_int(3))),
         string_lit("1, 2, 3")},
        {string_lit("{}"), fmt_args(fmt_list_lit()), string_lit("")},
        {string_lit("{}"), fmt_args(fmt_list_lit(fmt_int(1))), string_lit("1")},
    };

    DynString string = dynstring_create_over(mem_stack(128));
    for (usize i = 0; i != array_elems(data); ++i) {
      dynstring_clear(&string);
      format_write_formatted(&string, data[i].format, data[i].args);
      check_eq_string(dynstring_view(&string), data[i].expected);
    }
    dynstring_destroy(&string);
  }

  it("can write memory as hex") {
    DynString string = dynstring_create_over(mem_stack(128));

    u64 testData = 0x8BADF00DDEADBEEF;
    Mem testMem  = mem_create(&testData, sizeof(testData));

    format_write_mem(&string, testMem);
    check_eq_string(dynstring_view(&string), string_lit("8BADF00DDEADBEEF"));

    dynstring_destroy(&string);
  }

  it("can write u64 integers") {
    struct {
      u64           val;
      FormatOptsInt opts;
      String        expected;
    } const data[] = {
        {0, format_opts_int(), string_lit("0")},
        {0, format_opts_int(.minDigits = 4), string_lit("0000")},
        {1, format_opts_int(), string_lit("1")},
        {42, format_opts_int(), string_lit("42")},
        {42, format_opts_int(.minDigits = 2), string_lit("42")},
        {42, format_opts_int(.minDigits = 4), string_lit("0042")},
        {1337, format_opts_int(), string_lit("1337")},
        {u64_max, format_opts_int(), string_lit("18446744073709551615")},
        {0, format_opts_int(.base = 2), string_lit("0")},
        {1, format_opts_int(.base = 2), string_lit("1")},
        {2, format_opts_int(.base = 2), string_lit("10")},
        {0b010110110, format_opts_int(.base = 2), string_lit("10110110")},
        {255, format_opts_int(.base = 2), string_lit("11111111")},
        {0x0, format_opts_int(.base = 16), string_lit("0")},
        {0x9, format_opts_int(.base = 16), string_lit("9")},
        {0xF, format_opts_int(.base = 16), string_lit("F")},
        {0xDEADBEEF, format_opts_int(.base = 16), string_lit("DEADBEEF")},
        {u64_max, format_opts_int(.base = 16), string_lit("FFFFFFFFFFFFFFFF")},
    };

    DynString string = dynstring_create_over(mem_stack(128));
    for (usize i = 0; i != array_elems(data); ++i) {
      dynstring_clear(&string);
      format_write_u64(&string, data[i].val, &data[i].opts);
      check_eq_string(dynstring_view(&string), data[i].expected);
    }
    dynstring_destroy(&string);
  }

  it("can write i64 integers") {
    struct {
      i64           val;
      FormatOptsInt opts;
      String        expected;
    } const data[] = {
        {0, format_opts_int(), string_lit("0")},
        {-0, format_opts_int(), string_lit("0")},
        {1, format_opts_int(), string_lit("1")},
        {-1, format_opts_int(), string_lit("-1")},
        {-42, format_opts_int(), string_lit("-42")},
        {1337, format_opts_int(), string_lit("1337")},
        {i64_min, format_opts_int(), string_lit("-9223372036854775808")},
        {i64_max, format_opts_int(), string_lit("9223372036854775807")},
    };

    DynString string = dynstring_create_over(mem_stack(128));
    for (usize i = 0; i != array_elems(data); ++i) {
      dynstring_clear(&string);
      format_write_i64(&string, data[i].val, &data[i].opts);
      check_eq_string(dynstring_view(&string), data[i].expected);
    }
    dynstring_destroy(&string);
  }

  it("can write f64 floats") {
    struct {
      f64             val;
      FormatOptsFloat opts;
      String          expected;
    } const data[] = {
        {f64_nan, format_opts_float(), string_lit("nan")},
        {f64_inf, format_opts_float(), string_lit("inf")},
        {-f64_inf, format_opts_float(), string_lit("-inf")},
        {0, format_opts_float(), string_lit("0")},
        {42, format_opts_float(), string_lit("42")},
        {42.0042, format_opts_float(), string_lit("42.0042")},
        {42.42, format_opts_float(), string_lit("42.42")},
        {1337.13371337, format_opts_float(.maxDecDigits = 8), string_lit("1337.13371337")},
        {1337.133713371337, format_opts_float(.maxDecDigits = 12), string_lit("1337.133713371337")},
        {1337133713371337, format_opts_float(), string_lit("1.3371337e15")},
        {1337133713371337,
         format_opts_float(.expThresholdPos = 1e16),
         string_lit("1337133713371337")},
        {.0000000001, format_opts_float(.maxDecDigits = 10), string_lit("1e-10")},
        {.0000000001,
         format_opts_float(.maxDecDigits = 10, .expThresholdNeg = 1e-11),
         string_lit("0.0000000001")},
        {10, format_opts_float(.expThresholdPos = 1), string_lit("1e1")},
        {0, format_opts_float(.minDecDigits = 2), string_lit("0.00")},
        {42, format_opts_float(.minDecDigits = 1), string_lit("42.0")},
        {42.0042, format_opts_float(.minDecDigits = 5), string_lit("42.00420")},
        {42.0042, format_opts_float(.maxDecDigits = 2), string_lit("42")},
        {42.005, format_opts_float(.maxDecDigits = 2), string_lit("42.01")},
        {42.005, format_opts_float(.maxDecDigits = 0), string_lit("42")},
        {f64_min, format_opts_float(), string_lit("-1.7976931e308")},
        {f64_max, format_opts_float(), string_lit("1.7976931e308")},
        {1e255, format_opts_float(), string_lit("1e255")},
        {1e-255, format_opts_float(), string_lit("1e-255")},
        {f64_epsilon, format_opts_float(), string_lit("4.9406565e-324")},
    };

    DynString string = dynstring_create_over(mem_stack(128));
    for (usize i = 0; i != array_elems(data); ++i) {
      dynstring_clear(&string);
      format_write_f64(&string, data[i].val, &data[i].opts);
      check_eq_string(dynstring_view(&string), data[i].expected);
    }
    dynstring_destroy(&string);
  }

  it("can write booleans") {
    struct {
      bool   val;
      String expected;
    } const data[] = {
        {true, string_lit("true")},
        {false, string_lit("false")},
    };

    DynString string = dynstring_create_over(mem_stack(128));
    for (usize i = 0; i != array_elems(data); ++i) {
      dynstring_clear(&string);
      format_write_bool(&string, data[i].val);
      check_eq_string(dynstring_view(&string), data[i].expected);
    }
    dynstring_destroy(&string);
  }

  it("can write bitsets") {
    struct {
      BitSet val;
      String expected;
    } const data[] = {
        {bitset_from_var((u8){0}), string_lit("00000000")},
        {bitset_from_var((u8){0b01011101}), string_lit("01011101")},
        {bitset_from_var((u16){0b0101110101011101}), string_lit("0101110101011101")},
    };

    DynString string = dynstring_create_over(mem_stack(128));
    for (usize i = 0; i != array_elems(data); ++i) {
      dynstring_clear(&string);
      format_write_bitset(&string, data[i].val);
      check_eq_string(dynstring_view(&string), data[i].expected);
    }
    dynstring_destroy(&string);
  }

  it("can write time durations in pretty format") {
    struct {
      TimeDuration val;
      String       expected;
    } const data[] = {
        {time_nanosecond, string_lit("1ns")},
        {-time_nanosecond, string_lit("-1ns")},
        {time_nanoseconds(42), string_lit("42ns")},
        {time_microsecond, string_lit("1us")},
        {time_microseconds(42), string_lit("42us")},
        {time_millisecond, string_lit("1ms")},
        {time_milliseconds(42), string_lit("42ms")},
        {time_second, string_lit("1s")},
        {time_seconds(42), string_lit("42s")},
        {time_minute, string_lit("1m")},
        {time_minutes(42), string_lit("42m")},
        {time_hour, string_lit("1h")},
        {time_hours(13), string_lit("13h")},
        {time_day, string_lit("1d")},
        {time_days(42), string_lit("42d")},
        {-time_days(42), string_lit("-42d")},
        {time_days(-42), string_lit("-42d")},
        {time_millisecond + time_microseconds(300), string_lit("1.3ms")},
    };

    DynString string = dynstring_create_over(mem_stack(128));
    for (usize i = 0; i != array_elems(data); ++i) {
      dynstring_clear(&string);
      format_write_time_duration_pretty(&string, data[i].val);
      check_eq_string(dynstring_view(&string), data[i].expected);
    }
    dynstring_destroy(&string);
  }

  it("can write time in iso8601 format") {
    struct {
      TimeReal val;
      String   expected;
    } const data[] = {
        {time_real_epoch, string_lit("1970-01-01T00:00:00.000Z")},
        {time_real_offset(time_real_epoch, time_days(13)), string_lit("1970-01-14T00:00:00.000Z")},
        {time_real_offset(time_real_epoch, time_hours(13) + time_milliseconds(42)),
         string_lit("1970-01-01T13:00:00.042Z")},
        {time_real_offset(time_real_epoch, time_days(40) + time_hours(13) + time_milliseconds(42)),
         string_lit("1970-02-10T13:00:00.042Z")},
    };

    DynString string = dynstring_create_over(mem_stack(128));
    for (usize i = 0; i != array_elems(data); ++i) {
      dynstring_clear(&string);
      format_write_time_iso8601(&string, data[i].val, &format_opts_time());
      check_eq_string(dynstring_view(&string), data[i].expected);
    }
    dynstring_destroy(&string);
  }

  it("can write time in iso8601 format without seperators") {
    const TimeReal time =
        time_real_offset(time_real_epoch, time_days(40) + time_hours(13) + time_milliseconds(42));
    const String str = format_write_arg_scratch(&fmt_time(time, .flags = FormatTimeFlags_None));
    check_eq_string(str, string_lit("19700210T130000042Z"));
  }

  it("can write byte-sizes in pretty format") {
    struct {
      usize  val;
      String expected;
    } const data[] = {
      {42, string_lit("42B")},
      {42 * usize_kibibyte, string_lit("42KiB")},
      {42 * usize_mebibyte, string_lit("42MiB")},
      {3 * usize_gibibyte, string_lit("3GiB")},
#if uptr_max == u64_max // 64 bit only sizes.
      {42 * usize_gibibyte, string_lit("42GiB")},
      {42 * usize_tebibyte, string_lit("42TiB")},
      {42 * usize_pebibyte, string_lit("42PiB")},
      {42 * usize_mebibyte + 200 * usize_kibibyte, string_lit("42.2MiB")},
      {2048 * usize_pebibyte, string_lit("2048PiB")},
#endif
    };

    DynString string = dynstring_create_over(mem_stack(128));
    for (usize i = 0; i != array_elems(data); ++i) {
      dynstring_clear(&string);
      format_write_size_pretty(&string, data[i].val);
      check_eq_string(dynstring_view(&string), data[i].expected);
    }
    dynstring_destroy(&string);
  }

  it("can write text") {
    struct {
      String val;
      String expected;
    } const data[] = {
        {string_lit(""), string_lit("")},
        {string_lit("\fHello\nWorld\\b"), string_lit("\\fHello\\nWorld\\b")},
        {string_lit("Hello\0World"), string_lit("Hello\\0World")},
        {string_lit("\xFFHello\xFBWorld\xFA"), string_lit("\\FFHello\\FBWorld\\FA")},
    };

    DynString string = dynstring_create_over(mem_stack(128));
    for (usize i = 0; i != array_elems(data); ++i) {
      dynstring_clear(&string);
      format_write_text(
          &string, data[i].val, &format_opts_text(.flags = FormatTextFlags_EscapeNonPrintAscii));
      check_eq_string(dynstring_view(&string), data[i].expected);
    }
    dynstring_destroy(&string);
  }

  it("can write wrapped text") {
    struct {
      String linePrefix;
      usize  maxWidth;
      String val;
      String expected;
    } const data[] = {
        {
            .linePrefix = string_lit(""),
            .maxWidth   = 1,
            .val        = string_lit(""),
            .expected   = string_lit(""),
        },
        {
            .linePrefix = string_lit(""),
            .maxWidth   = 1,
            .val        = string_lit("Hello"),
            .expected   = string_lit("H\ne\nl\nl\no"),
        },
        {
            .linePrefix = string_lit("> "),
            .maxWidth   = 30,
            .val        = string_lit("pulvinar pellentesque habitant"),
            .expected   = string_lit("> pulvinar pellentesque habitant"),
        },
        {
            .linePrefix = string_lit("> "),
            .maxWidth   = 30,
            .val        = string_lit("pulvinar\tpellentesque\thabitant"),
            .expected   = string_lit("> pulvinar pellentesque habitant"),
        },
        {
            .linePrefix = string_lit(""),
            .maxWidth   = 30,
            .val        = string_lit("nisl condimentum\r\n\r\nid venenatis a condimentum vitae"),
            .expected   = string_lit("nisl condimentum\n\n"
                                   "id venenatis a condimentum \n"
                                   "vitae"),
        },
        {
            .linePrefix = string_lit("> "),
            .maxWidth   = 30,
            .val        = string_lit("nisl condimentum\r\n\r\nid venenatis a condimentum vitae"),
            .expected   = string_lit("> nisl condimentum\n"
                                   "> \n"
                                   "> id venenatis a condimentum \n"
                                   "> vitae"),
        },
        {
            .linePrefix = string_lit("> "),
            .maxWidth   = 30,
            .val        = string_lit("cursuseuismodquisviverranibhcraspulvinar "
                              "cursuseuismodquisviverranibhcraspulvinar"),
            .expected   = string_lit("> cursuseuismodquisviverranibhcr\n"
                                   "> aspulvinar \n"
                                   "> cursuseuismodquisviverranibhcr\n"
                                   "> aspulvinar"),
        },
        {
            .linePrefix = string_lit("> "),
            .maxWidth   = 30,
            .val =
                string_lit("porttitor lacus luctus accumsan tortor posuere ac ut consequat semper "
                           "viverra nam libero justo laoreet sit amet cursus sit amet"),
            .expected = string_lit("> porttitor lacus luctus \n"
                                   "> accumsan tortor posuere ac ut \n"
                                   "> consequat semper viverra nam \n"
                                   "> libero justo laoreet sit amet \n"
                                   "> cursus sit amet"),
        },

    };

    DynString string = dynstring_create(g_alloc_scratch, 1024);
    for (usize i = 0; i != array_elems(data); ++i) {
      dynstring_clear(&string);
      dynstring_append(&string, data[i].linePrefix);
      format_write_text_wrapped(&string, data[i].val, data[i].maxWidth, data[i].linePrefix);
      check_eq_string(dynstring_view(&string), data[i].expected);
    }
    dynstring_destroy(&string);
  }

  it("can read whitespace") {
    struct {
      String val;
      String expected;
      String expectedRemaining;
    } const data[] = {
        {string_empty, string_empty, string_empty},
        {string_lit(" \t \n"), string_lit(" \t \n"), string_empty},
        {string_lit(" \t \nHello"), string_lit(" \t \n"), string_lit("Hello")},
    };

    for (usize i = 0; i != array_elems(data); ++i) {
      String       out;
      const String rem = format_read_whitespace(data[i].val, &out);
      check_eq_string(out, data[i].expected);
      check_eq_string(rem, data[i].expectedRemaining);
    }
  }

  it("can read u64 integers") {
    struct {
      String val;
      u8     base;
      u64    expected;
      String expectedRemaining;
    } const data[] = {
        {string_empty, 10, 0, string_empty},
        {string_lit("1"), 10, 1, string_empty},
        {string_lit("1337"), 10, 1337, string_empty},
        {string_lit("18446744073709551615"), 10, u64_lit(18446744073709551615), string_empty},
        {string_lit("1337-hello"), 10, 1337, string_lit("-hello")},
        {string_lit("42abc"), 10, 42, string_lit("abc")},
        {string_lit("Hello"), 10, 0, string_lit("Hello")},
        {string_lit("abcdef"), 16, 0xABCDEF, string_empty},
        {string_lit("123abcdef"), 16, 0x123ABCDEF, string_empty},
        {string_lit("123abcdef-hello"), 16, 0x123ABCDEF, string_lit("-hello")},
    };

    for (usize i = 0; i != array_elems(data); ++i) {
      u64          out;
      const String rem = format_read_u64(data[i].val, &out, data[i].base);
      check_eq_int(out, data[i].expected);
      check_eq_string(rem, data[i].expectedRemaining);
    }
  }

  it("can read i64 integers") {
    struct {
      String val;
      u8     base;
      i64    expected;
      String expectedRemaining;
    } const data[] = {
        {string_empty, 10, 0, string_empty},
        {string_lit("-42"), 10, -42, string_empty},
        {string_lit("+42"), 10, 42, string_empty},
        {string_lit("42"), 10, 42, string_empty},
        {string_lit("9223372036854775807"), 10, 9223372036854775807ll, string_empty},
        {string_lit("+9223372036854775807"), 10, 9223372036854775807ll, string_empty},
        {string_lit("-9223372036854775807"), 10, -9223372036854775807ll, string_empty},
        {string_lit("-123abcdef-hello"), 16, -0x123ABCDEF, string_lit("-hello")},
    };

    for (usize i = 0; i != array_elems(data); ++i) {
      i64          out;
      const String rem = format_read_i64(data[i].val, &out, data[i].base);
      check_eq_int(out, data[i].expected);
      check_eq_string(rem, data[i].expectedRemaining);
    }
  }

  it("can read f64 floating point numbers") {
    struct {
      String val;
      f64    expected;
      String expectedRemaining;
    } const data[] = {
        {string_empty, 0.0, string_empty},
        {string_lit("-42"), -42.0, string_empty},
        {string_lit("+42"), 42.0, string_empty},
        {string_lit("42"), 42.0, string_empty},
        {string_lit("-42.1337"), -42.1337, string_empty},
        {string_lit("+42.1337"), 42.1337, string_empty},
        {string_lit("42.1337"), 42.1337, string_empty},
        {string_lit("0.421337"), .421337, string_empty},
        {string_lit(".421337"), .421337, string_empty},
        {string_lit("421337.421337"), 421337.421337, string_empty},
        {string_lit("1.0e+3"), 1e+3, string_empty},
        {string_lit("1E+6"), 1e+6, string_empty},
        {string_lit("1e-14"), 1e-14, string_empty},
        {string_lit("1E-18"), 1e-18, string_empty},
        {string_lit("1E-20"), 1e-19, string_empty},
        {string_lit("1E+20"), 1e+19, string_empty},
        {string_lit("-1e+7"), -1e+7, string_empty},
        {string_lit("-1e-0"), -1e-0, string_empty},
        {string_lit("-1e+0"), -1e+0, string_empty},
        {string_lit("0.17976931348623157"), 0.17976931348623157, string_empty},
        {string_lit("17976931348623157"), 17976931348623157.0, string_empty},
        {string_lit("1797693.1348623157"), 1797693.1348623157, string_empty},
        {string_lit("-0.17976931348623157"), -0.17976931348623157, string_empty},
        {string_lit("-17976931348623157"), -17976931348623157.0, string_empty},
        {string_lit("-1797693.1348623157"), -1797693.1348623157, string_empty},
        {string_lit("0.00000000000000000000000000000001"), 1e-32, string_empty},
        {string_lit("100000000000000000000000000000.0"), 1e+29, string_empty},
        {string_lit("100000000000000000000000000.00000000000000000000000000"), 1e+26, string_empty},
        {string_lit("1Hello"), 1.0, string_lit("Hello")},
        {string_lit("1.0Hello"), 1.0, string_lit("Hello")},
        {string_lit(".0Hello"), .0, string_lit("Hello")},
        {string_lit("1e+10Hello"), 1.0e+10, string_lit("Hello")},
        {string_lit("1a"), 1.0, string_lit("a")},
        {string_lit("1.a"), 1.0, string_lit("a")},
        {string_lit("1.."), 1.0, string_lit(".")},
    };

    for (usize i = 0; i != array_elems(data); ++i) {
      f64          out;
      const String rem = format_read_f64(data[i].val, &out);
      check_eq_float(out, data[i].expected, 1e-32);
      check_eq_string(rem, data[i].expectedRemaining);
    }
  }
}
