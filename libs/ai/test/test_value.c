#include "check_spec.h"
#include "core_array.h"
#include "core_time.h"

#include "utils_internal.h"

spec(value) {
  it("can type-erase values") {
    check_eq_int(ai_value_none().type, AiValueType_None);

    check_eq_int(ai_value_f64(42).type, AiValueType_f64);
    check_eq_int(ai_value_f64(42).data_f64, 42);

    check_eq_int(ai_value_bool(true).type, AiValueType_Bool);
    check(ai_value_bool(true).data_bool == true);

    check_eq_int(ai_value_vector(geo_vector(1, 2, 3)).type, AiValueType_Vector);
    check_eq_int(ai_value_vector(geo_vector(1, 2, 3)).data_vector.z, 3);

    check_eq_int(ai_value_time(time_seconds(2)).type, AiValueType_Time);
    check_eq_int(ai_value_time(time_seconds(2)).data_time, time_seconds(2));

    check_eq_int(ai_value_entity(0x42).type, AiValueType_Entity);
    check_eq_int(ai_value_entity(0x42).data_entity, 0x42);
  }

  it("can produce a textual representation for a type") {
    check_eq_string(ai_value_type_str(AiValueType_None), string_lit("none"));
    check_eq_string(ai_value_type_str(AiValueType_f64), string_lit("f64"));
    check_eq_string(ai_value_type_str(AiValueType_Bool), string_lit("bool"));
    check_eq_string(ai_value_type_str(AiValueType_Vector), string_lit("vector"));
    check_eq_string(ai_value_type_str(AiValueType_Time), string_lit("time"));
    check_eq_string(ai_value_type_str(AiValueType_Entity), string_lit("entity"));
  }

  it("can create a textual representation of a value") {
    const struct {
      AiValue value;
      String  expected;
    } testData[] = {
        {ai_value_none(), string_lit("none")},
        {ai_value_f64(42), string_lit("42")},
        {ai_value_f64(42.1), string_lit("42.1")},
        {ai_value_bool(true), string_lit("true")},
        {ai_value_bool(false), string_lit("false")},
        {ai_value_vector(geo_vector(1, 2, 3)), string_lit("1, 2, 3, 0")},
        {ai_value_time(time_seconds(42)), string_lit("42s")},
        {ai_value_entity(0x1337), string_lit("1337")},
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
        {ai_value_none(), ai_value_none(), .expected = true},
        {ai_value_none(), ai_value_f64(42), .expected = false},
        {ai_value_f64(42), ai_value_none(), .expected = false},

        {ai_value_f64(42), ai_value_f64(42), .expected = true},
        {ai_value_f64(42), ai_value_f64(42.1), .expected = false},
        {ai_value_f64(42), ai_value_f64(42.000001), .expected = false},
        {ai_value_f64(42), ai_value_f64(42.0000001), .expected = true},

        {ai_value_bool(true), ai_value_bool(true), .expected = true},
        {ai_value_bool(false), ai_value_bool(false), .expected = true},
        {ai_value_bool(false), ai_value_bool(true), .expected = false},

        {ai_value_vector(geo_vector(1, 2)), ai_value_vector(geo_vector(1, 2)), .expected = true},
        {ai_value_vector(geo_vector(1, 2)), ai_value_vector(geo_vector(1, 3)), .expected = false},

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
        {ai_value_none(), ai_value_none(), .expected = false},
        {ai_value_none(), ai_value_f64(42), .expected = false},
        {ai_value_f64(42), ai_value_none(), .expected = false},

        {ai_value_f64(1), ai_value_f64(2), .expected = true},
        {ai_value_f64(2), ai_value_f64(1), .expected = false},
        {ai_value_f64(1), ai_value_f64(1), .expected = false},

        {ai_value_bool(true), ai_value_bool(true), .expected = false},
        {ai_value_bool(false), ai_value_bool(false), .expected = false},
        {ai_value_bool(true), ai_value_bool(false), .expected = false},
        {ai_value_bool(false), ai_value_bool(true), .expected = true},

        {ai_value_vector(geo_vector(1, 2)), ai_value_vector(geo_vector(1, 2)), .expected = false},
        {ai_value_vector(geo_vector(1, 3)), ai_value_vector(geo_vector(1, 2)), .expected = false},
        {ai_value_vector(geo_vector(1, 2)), ai_value_vector(geo_vector(1, 3)), .expected = true},

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
        {ai_value_none(), ai_value_none(), .expected = false},
        {ai_value_none(), ai_value_f64(42), .expected = false},
        {ai_value_f64(42), ai_value_none(), .expected = false},

        {ai_value_f64(2), ai_value_f64(1), .expected = true},
        {ai_value_f64(1), ai_value_f64(2), .expected = false},
        {ai_value_f64(1), ai_value_f64(1), .expected = false},

        {ai_value_bool(true), ai_value_bool(false), .expected = true},
        {ai_value_bool(true), ai_value_bool(true), .expected = false},
        {ai_value_bool(false), ai_value_bool(false), .expected = false},
        {ai_value_bool(false), ai_value_bool(true), .expected = false},

        {ai_value_vector(geo_vector(1, 3)), ai_value_vector(geo_vector(1, 2)), .expected = true},
        {ai_value_vector(geo_vector(1, 2)), ai_value_vector(geo_vector(1, 2)), .expected = false},
        {ai_value_vector(geo_vector(1, 2)), ai_value_vector(geo_vector(1, 3)), .expected = false},

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
}
