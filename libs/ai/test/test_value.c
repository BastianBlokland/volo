#include "ai_value.h"
#include "check_spec.h"
#include "core_array.h"
#include "core_time.h"

#include "utils_internal.h"

spec(value) {
  it("can type-erase values") {
    check_eq_int(ai_value_type(ai_value_null()), AiValueType_Null);

    check_eq_int(ai_value_type(ai_value_f64(42)), AiValueType_f64);
    check_eq_int(ai_value_get_f64(ai_value_f64(42), 0), 42);

    check_eq_int(ai_value_type(ai_value_bool(true)), AiValueType_Bool);
    check(ai_value_get_bool(ai_value_bool(true), false) == true);

    check_eq_int(ai_value_type(ai_value_vector3(geo_vector(1, 2, 3))), AiValueType_Vector3);
    check_eq_int(ai_value_get_vector3(ai_value_vector3(geo_vector(1, 2, 3)), geo_vector(0)).z, 3);

    check_eq_int(ai_value_type(ai_value_entity(0x42)), AiValueType_Entity);
    check_eq_int(ai_value_get_entity(ai_value_entity(0x42), 0), 0x42);

    check_eq_int(ai_value_type(ai_value_time(time_seconds(2))), AiValueType_f64);
    check_eq_int(ai_value_get_time(ai_value_time(time_seconds(2)), 0), time_seconds(2));
  }

  it("clears the w component of vector3's") {
    const AiValue val = ai_value_vector3(geo_vector(1, 2, 3, 4));
    check_eq_float(ai_value_get_vector3(val, geo_vector(0)).x, 1, 1e-6f);
    check_eq_float(ai_value_get_vector3(val, geo_vector(0)).y, 2, 1e-6f);
    check_eq_float(ai_value_get_vector3(val, geo_vector(0)).z, 3, 1e-6f);
    check_eq_float(ai_value_get_vector3(val, geo_vector(0)).w, 0, 1e-6f);
  }

  it("can extract specific types from values") {
    check_eq_float(ai_value_get_f64(ai_value_f64(42), 1337), 42, 1e-6);
    check_eq_float(ai_value_get_f64(ai_value_null(), 1337), 1337, 1e-6);
    check_eq_float(ai_value_get_f64(ai_value_bool(false), 1337), 1337, 1e-6);

    check(ai_value_get_bool(ai_value_bool(true), false) == true);
    check(ai_value_get_bool(ai_value_null(), false) == false);

    check(geo_vector_equal(
        ai_value_get_vector3(ai_value_vector3(geo_vector(1, 2, 3)), geo_vector(4, 5, 6)),
        geo_vector(1, 2, 3),
        1e-6f));
    check(geo_vector_equal(
        ai_value_get_vector3(ai_value_null(), geo_vector(4, 5, 6)), geo_vector(4, 5, 6), 1e-6f));

    check(ai_value_get_time(ai_value_time(time_seconds(1)), time_seconds(2)) == time_seconds(1));
    check(ai_value_get_time(ai_value_null(), time_seconds(2)) == time_seconds(2));

    check(ai_value_get_entity(ai_value_entity(0x1), 0x2) == 0x1);
    check(ai_value_get_entity(ai_value_null(), 0x2) == 0x2);
  }

  it("can test if a value is not null") {
    check(ai_value_has(ai_value_f64(42)));
    check(!ai_value_has(ai_value_null()));
  }

  it("can return a default if the value is null") {
    check_eq_value(ai_value_or(ai_value_f64(42), ai_value_f64(1337)), ai_value_f64(42));
    check_eq_value(ai_value_or(ai_value_f64(42), ai_value_null()), ai_value_f64(42));
    check_eq_value(ai_value_or(ai_value_null(), ai_value_f64(1337)), ai_value_f64(1337));
    check_eq_value(ai_value_or(ai_value_null(), ai_value_null()), ai_value_null());
  }

  it("can produce a textual representation for a type") {
    check_eq_string(ai_value_type_str(AiValueType_Null), string_lit("null"));
    check_eq_string(ai_value_type_str(AiValueType_f64), string_lit("f64"));
    check_eq_string(ai_value_type_str(AiValueType_Bool), string_lit("bool"));
    check_eq_string(ai_value_type_str(AiValueType_Vector3), string_lit("vector3"));
    check_eq_string(ai_value_type_str(AiValueType_Entity), string_lit("entity"));
  }

  it("can create a textual representation of a value") {
    const struct {
      AiValue value;
      String  expected;
    } testData[] = {
        {ai_value_null(), string_lit("null")},
        {ai_value_f64(42), string_lit("42")},
        {ai_value_f64(42.1), string_lit("42.1")},
        {ai_value_bool(true), string_lit("true")},
        {ai_value_bool(false), string_lit("false")},
        {ai_value_vector3(geo_vector(1, 2, 3)), string_lit("1, 2, 3")},
        {ai_value_entity(0x1337), string_lit("1337")},
        {ai_value_time(time_seconds(42)), string_lit("42")},
        {ai_value_time(time_hour), string_lit("3600")},
        {ai_value_time(time_milliseconds(500)), string_lit("0.5")},
        {ai_value_time(time_milliseconds(42)), string_lit("0.042")},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      check_eq_string(ai_value_str_scratch(testData[i].value), testData[i].expected);
    }
  }

  it("can test if values are equal") {
    const struct {
      AiValue a, b;
      bool    expected;
    } testData[] = {
        {ai_value_null(), ai_value_null(), .expected = true},
        {ai_value_null(), ai_value_f64(42), .expected = false},
        {ai_value_f64(42), ai_value_null(), .expected = false},

        {ai_value_f64(42), ai_value_f64(42), .expected = true},
        {ai_value_f64(42), ai_value_f64(42.1), .expected = false},
        {ai_value_f64(42), ai_value_f64(42.000001), .expected = false},
        {ai_value_f64(42), ai_value_f64(42.0000001), .expected = true},

        {ai_value_bool(true), ai_value_bool(true), .expected = true},
        {ai_value_bool(false), ai_value_bool(false), .expected = true},
        {ai_value_bool(false), ai_value_bool(true), .expected = false},

        {ai_value_vector3(geo_vector(1, 2)), ai_value_vector3(geo_vector(1, 2)), .expected = true},
        {ai_value_vector3(geo_vector(1, 2)), ai_value_vector3(geo_vector(1, 3)), .expected = false},

        {ai_value_time(time_seconds(1)), ai_value_time(time_seconds(1)), .expected = true},
        {ai_value_time(time_seconds(1)), ai_value_time(time_seconds(2)), .expected = false},

        {ai_value_entity(1), ai_value_entity(1), .expected = true},
        {ai_value_entity(1), ai_value_entity(2), .expected = false},

        {ai_value_f64(1), ai_value_bool(true), .expected = false},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      if (testData[i].expected) {
        check_eq_value(testData[i].a, testData[i].b);
      } else {
        check_neq_value(testData[i].a, testData[i].b);
      }
    }
  }

  it("can test if values are less") {
    const struct {
      AiValue a, b;
      bool    expected;
    } testData[] = {
        {ai_value_null(), ai_value_null(), .expected = false},
        {ai_value_null(), ai_value_f64(42), .expected = false},
        {ai_value_f64(42), ai_value_null(), .expected = false},

        {ai_value_f64(1), ai_value_f64(2), .expected = true},
        {ai_value_f64(2), ai_value_f64(1), .expected = false},
        {ai_value_f64(1), ai_value_f64(1), .expected = false},

        {ai_value_bool(true), ai_value_bool(true), .expected = false},
        {ai_value_bool(false), ai_value_bool(false), .expected = false},
        {ai_value_bool(true), ai_value_bool(false), .expected = false},
        {ai_value_bool(false), ai_value_bool(true), .expected = true},

        {ai_value_vector3(geo_vector(1, 2)), ai_value_vector3(geo_vector(1, 2)), .expected = false},
        {ai_value_vector3(geo_vector(1, 3)), ai_value_vector3(geo_vector(1, 2)), .expected = false},
        {ai_value_vector3(geo_vector(1, 2)), ai_value_vector3(geo_vector(1, 3)), .expected = true},

        {ai_value_time(time_seconds(1)), ai_value_time(time_seconds(2)), .expected = true},
        {ai_value_time(time_seconds(2)), ai_value_time(time_seconds(1)), .expected = false},
        {ai_value_time(time_seconds(1)), ai_value_time(time_seconds(1)), .expected = false},

        {ai_value_f64(1), ai_value_bool(true), .expected = false},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      if (testData[i].expected) {
        check_less_value(testData[i].a, testData[i].b);
      } else {
        check_msg(
            !ai_value_less(testData[i].a, testData[i].b),
            "{} >= {}",
            fmt_text(ai_value_str_scratch(testData[i].a)),
            fmt_text(ai_value_str_scratch(testData[i].b)));
      }
    }
  }

  it("can test if values are greater") {
    const struct {
      AiValue a, b;
      bool    expected;
    } testData[] = {
        {ai_value_null(), ai_value_null(), .expected = false},
        {ai_value_null(), ai_value_f64(42), .expected = false},
        {ai_value_f64(42), ai_value_null(), .expected = false},

        {ai_value_f64(2), ai_value_f64(1), .expected = true},
        {ai_value_f64(1), ai_value_f64(2), .expected = false},
        {ai_value_f64(1), ai_value_f64(1), .expected = false},

        {ai_value_bool(true), ai_value_bool(false), .expected = true},
        {ai_value_bool(true), ai_value_bool(true), .expected = false},
        {ai_value_bool(false), ai_value_bool(false), .expected = false},
        {ai_value_bool(false), ai_value_bool(true), .expected = false},

        {ai_value_vector3(geo_vector(1, 3)), ai_value_vector3(geo_vector(1, 2)), .expected = true},
        {ai_value_vector3(geo_vector(1, 2)), ai_value_vector3(geo_vector(1, 2)), .expected = false},
        {ai_value_vector3(geo_vector(1, 2)), ai_value_vector3(geo_vector(1, 3)), .expected = false},

        {ai_value_time(time_seconds(2)), ai_value_time(time_seconds(1)), .expected = true},
        {ai_value_time(time_seconds(1)), ai_value_time(time_seconds(2)), .expected = false},
        {ai_value_time(time_seconds(1)), ai_value_time(time_seconds(1)), .expected = false},

        {ai_value_f64(1), ai_value_bool(true), .expected = false},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      if (testData[i].expected) {
        check_greater_value(testData[i].a, testData[i].b);
      } else {
        check_msg(
            !ai_value_greater(testData[i].a, testData[i].b),
            "{} <= {}",
            fmt_text(ai_value_str_scratch(testData[i].a)),
            fmt_text(ai_value_str_scratch(testData[i].b)));
      }
    }
  }

  it("can add values") {
    const struct {
      AiValue a, b;
      AiValue expected;
    } testData[] = {
        {ai_value_null(), ai_value_null(), .expected = ai_value_null()},
        {ai_value_null(), ai_value_f64(42), .expected = ai_value_f64(42)},
        {ai_value_f64(42), ai_value_null(), .expected = ai_value_f64(42)},
        {ai_value_f64(42), ai_value_bool(false), .expected = ai_value_f64(42)},

        {ai_value_f64(42), ai_value_f64(1), .expected = ai_value_f64(43)},
        {ai_value_f64(42), ai_value_f64(1337), .expected = ai_value_f64(1379)},

        {ai_value_bool(true), ai_value_bool(false), .expected = ai_value_bool(true)},
        {ai_value_bool(true), ai_value_bool(true), .expected = ai_value_bool(true)},
        {ai_value_bool(false), ai_value_bool(false), .expected = ai_value_bool(false)},
        {ai_value_bool(false), ai_value_bool(true), .expected = ai_value_bool(false)},

        {.a        = ai_value_vector3(geo_vector(1, 2, 3)),
         .b        = ai_value_vector3(geo_vector(4, 5, 6)),
         .expected = ai_value_vector3(geo_vector(5, 7, 9))},

        {.a        = ai_value_vector3(geo_vector(1, 2, 3)),
         .b        = ai_value_f64(42),
         .expected = ai_value_vector3(geo_vector(1, 2, 3))},

        {.a        = ai_value_time(time_seconds(1)),
         .b        = ai_value_null(),
         .expected = ai_value_time(time_seconds(1))},

        {.a = ai_value_entity(0x1), .b = ai_value_entity(0x2), .expected = ai_value_entity(0x1)},

    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      const AiValue actual = ai_value_add(testData[i].a, testData[i].b);
      check_eq_value(actual, testData[i].expected);
    }
  }

  it("can subtract values") {
    const struct {
      AiValue a, b;
      AiValue expected;
    } testData[] = {
        {ai_value_null(), ai_value_null(), .expected = ai_value_null()},
        {ai_value_null(), ai_value_f64(42), .expected = ai_value_f64(42)},
        {ai_value_f64(42), ai_value_null(), .expected = ai_value_f64(42)},
        {ai_value_f64(42), ai_value_bool(false), .expected = ai_value_f64(42)},

        {ai_value_f64(42), ai_value_f64(1), .expected = ai_value_f64(41)},
        {ai_value_f64(42), ai_value_f64(1337), .expected = ai_value_f64(-1295)},

        {ai_value_bool(true), ai_value_bool(false), .expected = ai_value_bool(true)},
        {ai_value_bool(true), ai_value_bool(true), .expected = ai_value_bool(true)},
        {ai_value_bool(false), ai_value_bool(false), .expected = ai_value_bool(false)},
        {ai_value_bool(false), ai_value_bool(true), .expected = ai_value_bool(false)},

        {.a        = ai_value_vector3(geo_vector(1, 2, 3)),
         .b        = ai_value_vector3(geo_vector(4, 5, 6)),
         .expected = ai_value_vector3(geo_vector(-3, -3, -3))},

        {.a        = ai_value_vector3(geo_vector(1, 2, 3)),
         .b        = ai_value_f64(42),
         .expected = ai_value_vector3(geo_vector(1, 2, 3))},

        {.a        = ai_value_time(time_seconds(1)),
         .b        = ai_value_null(),
         .expected = ai_value_time(time_seconds(1))},

        {.a = ai_value_entity(0x1), .b = ai_value_entity(0x2), .expected = ai_value_entity(0x1)},

    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      const AiValue actual = ai_value_sub(testData[i].a, testData[i].b);
      check_eq_value(actual, testData[i].expected);
    }
  }
}
