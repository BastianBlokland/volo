#include "check_spec.h"
#include "core_array.h"
#include "core_time.h"
#include "script_val.h"

#include "utils_internal.h"

spec(val) {
  it("can type-erase values") {
    check_eq_int(script_type(script_null()), ScriptType_Null);

    check_eq_int(script_type(script_number(42)), ScriptType_Number);
    check_eq_int(script_get_number(script_number(42), 0), 42);

    check_eq_int(script_type(script_bool(true)), ScriptType_Bool);
    check(script_get_bool(script_bool(true), false) == true);

    check_eq_int(script_type(script_vector3(geo_vector(1, 2, 3))), ScriptType_Vector3);
    check_eq_int(script_get_vector3(script_vector3(geo_vector(1, 2, 3)), geo_vector(0)).z, 3);

    check_eq_int(script_type(script_entity(0x42)), ScriptType_Entity);
    check_eq_int(script_get_entity(script_entity(0x42), 0), 0x42);

    check_eq_int(script_type(script_time(time_seconds(2))), ScriptType_Number);
    check_eq_int(script_get_time(script_time(time_seconds(2)), 0), time_seconds(2));
  }

  it("clears the w component of vector3's") {
    const ScriptVal val = script_vector3(geo_vector(1, 2, 3, 4));
    check_eq_float(script_get_vector3(val, geo_vector(0)).x, 1, 1e-6f);
    check_eq_float(script_get_vector3(val, geo_vector(0)).y, 2, 1e-6f);
    check_eq_float(script_get_vector3(val, geo_vector(0)).z, 3, 1e-6f);
    check_eq_float(script_get_vector3(val, geo_vector(0)).w, 0, 1e-6f);
  }

  it("can extract specific types from values") {
    check_eq_float(script_get_number(script_number(42), 1337), 42, 1e-6);
    check_eq_float(script_get_number(script_null(), 1337), 1337, 1e-6);
    check_eq_float(script_get_number(script_bool(false), 1337), 1337, 1e-6);

    check(script_get_bool(script_bool(true), false) == true);
    check(script_get_bool(script_null(), false) == false);

    check(geo_vector_equal(
        script_get_vector3(script_vector3(geo_vector(1, 2, 3)), geo_vector(4, 5, 6)),
        geo_vector(1, 2, 3),
        1e-6f));
    check(geo_vector_equal(
        script_get_vector3(script_null(), geo_vector(4, 5, 6)), geo_vector(4, 5, 6), 1e-6f));

    check(script_get_time(script_time(time_seconds(1)), time_seconds(2)) == time_seconds(1));
    check(script_get_time(script_null(), time_seconds(2)) == time_seconds(2));

    check(script_get_entity(script_entity(0x1), 0x2) == 0x1);
    check(script_get_entity(script_null(), 0x2) == 0x2);
  }

  it("can test if a value is not null") {
    check(script_val_has(script_number(42)));
    check(!script_val_has(script_null()));
  }

  it("can return a default if the value is null") {
    check_eq_val(script_val_or(script_number(42), script_number(1337)), script_number(42));
    check_eq_val(script_val_or(script_number(42), script_null()), script_number(42));
    check_eq_val(script_val_or(script_null(), script_number(1337)), script_number(1337));
    check_eq_val(script_val_or(script_null(), script_null()), script_null());
  }

  it("can produce a textual representation for a type") {
    check_eq_string(script_val_type_str(ScriptType_Null), string_lit("null"));
    check_eq_string(script_val_type_str(ScriptType_Number), string_lit("number"));
    check_eq_string(script_val_type_str(ScriptType_Bool), string_lit("bool"));
    check_eq_string(script_val_type_str(ScriptType_Vector3), string_lit("vector3"));
    check_eq_string(script_val_type_str(ScriptType_Entity), string_lit("entity"));
  }

  it("can create a textual representation of a value") {
    const struct {
      ScriptVal value;
      String    expected;
    } testData[] = {
        {script_null(), string_lit("null")},
        {script_number(42), string_lit("42")},
        {script_number(42.1), string_lit("42.1")},
        {script_bool(true), string_lit("true")},
        {script_bool(false), string_lit("false")},
        {script_vector3(geo_vector(1, 2, 3)), string_lit("1, 2, 3")},
        {script_entity(0x1337), string_lit("1337")},
        {script_time(time_seconds(42)), string_lit("42")},
        {script_time(time_hour), string_lit("3600")},
        {script_time(time_milliseconds(500)), string_lit("0.5")},
        {script_time(time_milliseconds(42)), string_lit("0.042")},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      check_eq_string(script_val_str_scratch(testData[i].value), testData[i].expected);
    }
  }

  it("can test if values are equal") {
    const struct {
      ScriptVal a, b;
      bool      expected;
    } testData[] = {
        {script_null(), script_null(), .expected = true},
        {script_null(), script_number(42), .expected = false},
        {script_number(42), script_null(), .expected = false},

        {script_number(42), script_number(42), .expected = true},
        {script_number(42), script_number(42.1), .expected = false},
        {script_number(42), script_number(42.000001), .expected = false},
        {script_number(42), script_number(42.0000001), .expected = true},

        {script_bool(true), script_bool(true), .expected = true},
        {script_bool(false), script_bool(false), .expected = true},
        {script_bool(false), script_bool(true), .expected = false},

        {script_vector3(geo_vector(1, 2)), script_vector3(geo_vector(1, 2)), .expected = true},
        {script_vector3(geo_vector(1, 2)), script_vector3(geo_vector(1, 3)), .expected = false},

        {script_time(time_seconds(1)), script_time(time_seconds(1)), .expected = true},
        {script_time(time_seconds(1)), script_time(time_seconds(2)), .expected = false},

        {script_entity(1), script_entity(1), .expected = true},
        {script_entity(1), script_entity(2), .expected = false},

        {script_number(1), script_bool(true), .expected = false},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      if (testData[i].expected) {
        check_eq_val(testData[i].a, testData[i].b);
      } else {
        check_neq_val(testData[i].a, testData[i].b);
      }
    }
  }

  it("can test if values are less") {
    const struct {
      ScriptVal a, b;
      bool      expected;
    } testData[] = {
        {script_null(), script_null(), .expected = false},
        {script_null(), script_number(42), .expected = false},
        {script_number(42), script_null(), .expected = false},

        {script_number(1), script_number(2), .expected = true},
        {script_number(2), script_number(1), .expected = false},
        {script_number(1), script_number(1), .expected = false},

        {script_bool(true), script_bool(true), .expected = false},
        {script_bool(false), script_bool(false), .expected = false},
        {script_bool(true), script_bool(false), .expected = false},
        {script_bool(false), script_bool(true), .expected = true},

        {script_vector3(geo_vector(1, 2)), script_vector3(geo_vector(1, 2)), .expected = false},
        {script_vector3(geo_vector(1, 3)), script_vector3(geo_vector(1, 2)), .expected = false},
        {script_vector3(geo_vector(1, 2)), script_vector3(geo_vector(1, 3)), .expected = true},

        {script_time(time_seconds(1)), script_time(time_seconds(2)), .expected = true},
        {script_time(time_seconds(2)), script_time(time_seconds(1)), .expected = false},
        {script_time(time_seconds(1)), script_time(time_seconds(1)), .expected = false},

        {script_number(1), script_bool(true), .expected = false},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      if (testData[i].expected) {
        check_less_val(testData[i].a, testData[i].b);
      } else {
        check_msg(
            !script_val_less(testData[i].a, testData[i].b),
            "{} >= {}",
            fmt_text(script_val_str_scratch(testData[i].a)),
            fmt_text(script_val_str_scratch(testData[i].b)));
      }
    }
  }

  it("can test if values are greater") {
    const struct {
      ScriptVal a, b;
      bool      expected;
    } testData[] = {
        {script_null(), script_null(), .expected = false},
        {script_null(), script_number(42), .expected = false},
        {script_number(42), script_null(), .expected = false},

        {script_number(2), script_number(1), .expected = true},
        {script_number(1), script_number(2), .expected = false},
        {script_number(1), script_number(1), .expected = false},

        {script_bool(true), script_bool(false), .expected = true},
        {script_bool(true), script_bool(true), .expected = false},
        {script_bool(false), script_bool(false), .expected = false},
        {script_bool(false), script_bool(true), .expected = false},

        {script_vector3(geo_vector(1, 3)), script_vector3(geo_vector(1, 2)), .expected = true},
        {script_vector3(geo_vector(1, 2)), script_vector3(geo_vector(1, 2)), .expected = false},
        {script_vector3(geo_vector(1, 2)), script_vector3(geo_vector(1, 3)), .expected = false},

        {script_time(time_seconds(2)), script_time(time_seconds(1)), .expected = true},
        {script_time(time_seconds(1)), script_time(time_seconds(2)), .expected = false},
        {script_time(time_seconds(1)), script_time(time_seconds(1)), .expected = false},

        {script_number(1), script_bool(true), .expected = false},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      if (testData[i].expected) {
        check_greater_val(testData[i].a, testData[i].b);
      } else {
        check_msg(
            !script_val_greater(testData[i].a, testData[i].b),
            "{} <= {}",
            fmt_text(script_val_str_scratch(testData[i].a)),
            fmt_text(script_val_str_scratch(testData[i].b)));
      }
    }
  }

  it("can add values") {
    const struct {
      ScriptVal a, b;
      ScriptVal expected;
    } testData[] = {
        {script_null(), script_null(), .expected = script_null()},
        {script_null(), script_number(42), .expected = script_number(42)},
        {script_number(42), script_null(), .expected = script_number(42)},
        {script_number(42), script_bool(false), .expected = script_number(42)},

        {script_number(42), script_number(1), .expected = script_number(43)},
        {script_number(42), script_number(1337), .expected = script_number(1379)},

        {script_bool(true), script_bool(false), .expected = script_bool(true)},
        {script_bool(true), script_bool(true), .expected = script_bool(true)},
        {script_bool(false), script_bool(false), .expected = script_bool(false)},
        {script_bool(false), script_bool(true), .expected = script_bool(false)},

        {.a        = script_vector3(geo_vector(1, 2, 3)),
         .b        = script_vector3(geo_vector(4, 5, 6)),
         .expected = script_vector3(geo_vector(5, 7, 9))},

        {.a        = script_vector3(geo_vector(1, 2, 3)),
         .b        = script_number(42),
         .expected = script_vector3(geo_vector(1, 2, 3))},

        {.a        = script_time(time_seconds(1)),
         .b        = script_null(),
         .expected = script_time(time_seconds(1))},

        {.a = script_entity(0x1), .b = script_entity(0x2), .expected = script_entity(0x1)},

    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      const ScriptVal actual = script_val_add(testData[i].a, testData[i].b);
      check_eq_val(actual, testData[i].expected);
    }
  }

  it("can subtract values") {
    const struct {
      ScriptVal a, b;
      ScriptVal expected;
    } testData[] = {
        {script_null(), script_null(), .expected = script_null()},
        {script_null(), script_number(42), .expected = script_number(42)},
        {script_number(42), script_null(), .expected = script_number(42)},
        {script_number(42), script_bool(false), .expected = script_number(42)},

        {script_number(42), script_number(1), .expected = script_number(41)},
        {script_number(42), script_number(1337), .expected = script_number(-1295)},

        {script_bool(true), script_bool(false), .expected = script_bool(true)},
        {script_bool(true), script_bool(true), .expected = script_bool(true)},
        {script_bool(false), script_bool(false), .expected = script_bool(false)},
        {script_bool(false), script_bool(true), .expected = script_bool(false)},

        {.a        = script_vector3(geo_vector(1, 2, 3)),
         .b        = script_vector3(geo_vector(4, 5, 6)),
         .expected = script_vector3(geo_vector(-3, -3, -3))},

        {.a        = script_vector3(geo_vector(1, 2, 3)),
         .b        = script_number(42),
         .expected = script_vector3(geo_vector(1, 2, 3))},

        {.a        = script_time(time_seconds(1)),
         .b        = script_null(),
         .expected = script_time(time_seconds(1))},

        {.a = script_entity(0x1), .b = script_entity(0x2), .expected = script_entity(0x1)},

    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      const ScriptVal actual = script_val_sub(testData[i].a, testData[i].b);
      check_eq_val(actual, testData[i].expected);
    }
  }
}
