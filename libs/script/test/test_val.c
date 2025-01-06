#include "check_spec.h"
#include "core_array.h"
#include "core_float.h"
#include "core_math.h"
#include "core_stringtable.h"
#include "core_time.h"
#include "geo_color.h"
#include "geo_quat.h"
#include "script_val.h"

#include "utils_internal.h"

static void test_eq_quat(CheckTestContext* ctx, GeoQuat a, const GeoQuat b) {
  if (geo_quat_dot(a, b) < 0) {
    // Compensate for quaternion double-cover (two quaternions representing the same rot).
    a = geo_quat_flip(a);
  }
  for (usize i = 0; i != 4; ++i) {
    if (float_isnan(a.comps[i]) || float_isnan(b.comps[i])) {
      goto Fail;
    }
    if (math_abs(a.comps[i] - b.comps[i]) > 1e-3f) {
      goto Fail;
    }
  }
  return;
Fail:
  check_report_error(
      ctx, fmt_write_scratch("{} == {}", geo_quat_fmt(a), geo_quat_fmt(b)), source_location());
}

spec(val) {
  const EcsEntityId dummyEntity1 = (EcsEntityId)(u64_lit(0) | (u64_lit(1) << 32u));
  const EcsEntityId dummyEntity2 = (EcsEntityId)(u64_lit(1) | (u64_lit(2) << 32u));

  it("can type-erase values") {
    check_eq_int(script_type(script_null()), ScriptType_Null);

    check_eq_int(script_type(script_num(42)), ScriptType_Num);
    check_eq_float(script_get_num(script_num(42), 0), 42, 1e-6);

    check_eq_int(script_type(script_bool(true)), ScriptType_Bool);
    check(script_get_bool(script_bool(true), false) == true);

    check_eq_int(script_type(script_vec3_lit(1, 2, 3)), ScriptType_Vec3);
    check_eq_float(script_get_vec3(script_vec3_lit(1, 2, 3), geo_vector(0)).z, 3, 1e-6);

    check_eq_int(script_type(script_quat(geo_quat_ident)), ScriptType_Quat);
    const ScriptVal quatForwardToDown = script_quat(geo_quat_forward_to_down);
    check_eq_float(script_get_quat(quatForwardToDown, geo_quat_ident).x, 0.7071068f, 1e-6);
    check_eq_float(script_get_quat(quatForwardToDown, geo_quat_ident).y, 0, 1e-6);
    check_eq_float(script_get_quat(quatForwardToDown, geo_quat_ident).z, 0, 1e-6);
    check_eq_float(script_get_quat(quatForwardToDown, geo_quat_ident).w, 0.7071068f, 1e-6);

    check_eq_int(script_type(script_color(geo_color_red)), ScriptType_Color);
    check_eq_float(script_get_color(script_color(geo_color_red), geo_color_clear).r, 1, 1e-6);

    check_eq_int(script_type(script_entity(dummyEntity1)), ScriptType_Entity);
    check_eq_int(script_get_entity(script_entity(dummyEntity1), 0), dummyEntity1);

    check_eq_int(script_type(script_time(time_seconds(2))), ScriptType_Num);
    check_eq_float(script_get_time(script_time(time_seconds(2)), 0), time_seconds(2), 1e-6);

    const ScriptVal str = script_str(string_hash_lit("Hello World"));
    check_eq_int(script_type(str), ScriptType_Str);
    check(script_get_str(str, 0) == string_hash_lit("Hello World"));

    const ScriptVal str2 = script_str_empty();
    check_eq_int(script_type(str2), ScriptType_Str);
    check(script_get_str(str2, 0) == string_hash_lit(""));
  }

  it("clears the w component of vector3's") {
    const ScriptVal val = script_vec3(geo_vector(1, 2, 3, 4));
    check_eq_float(script_get_vec3(val, geo_vector(0)).x, 1, 1e-6f);
    check_eq_float(script_get_vec3(val, geo_vector(0)).y, 2, 1e-6f);
    check_eq_float(script_get_vec3(val, geo_vector(0)).z, 3, 1e-6f);
    check_eq_float(script_get_vec3(val, geo_vector(0)).w, 0, 1e-6f);
  }

  it("can store quaternions") {
    const GeoQuat testData[] = {
        geo_quat_angle_axis(0.0f * math_pi_f32 * 2.0f, geo_up),
        geo_quat_angle_axis(0.25f * math_pi_f32 * 2.0f, geo_up),
        geo_quat_angle_axis(0.5f * math_pi_f32 * 2.0f, geo_up),
        geo_quat_angle_axis(0.75f * math_pi_f32 * 2.0f, geo_up),
        geo_quat_angle_axis(1.0f * math_pi_f32 * 2.0f, geo_up),
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      const ScriptVal val = script_quat(testData[i]);
      test_eq_quat(_testCtx, script_get_quat(val, geo_quat_ident), testData[i]);
    }
  }

  it("normalizes incoming quaternions") {
    const ScriptVal qVal  = script_quat((GeoQuat){1337, 42, -42, 5});
    const GeoQuat   qNorm = script_get_quat(qVal, geo_quat_ident);

    check_eq_float(geo_vector_mag(geo_vector(qNorm.x, qNorm.y, qNorm.z, qNorm.w)), 1, 1e-6);
  }

  it("can extract specific types from values") {
    check_eq_float(script_get_num(script_num(42), 1337), 42, 1e-6);
    check_eq_float(script_get_num(script_null(), 1337), 1337, 1e-6);
    check_eq_float(script_get_num(script_bool(false), 1337), 1337, 1e-6);

    check(script_get_bool(script_bool(true), false) == true);
    check(script_get_bool(script_null(), false) == false);

    check(geo_vector_equal(
        script_get_vec3(script_vec3_lit(1, 2, 3), geo_vector(4, 5, 6)),
        geo_vector(1, 2, 3),
        1e-6f));
    check(geo_vector_equal(
        script_get_vec3(script_null(), geo_vector(4, 5, 6)), geo_vector(4, 5, 6), 1e-6f));

    check_eq_float(
        script_get_quat(script_quat(geo_quat_ident), geo_quat_forward_to_down).w, 1.0, 1e-6f);
    check_eq_float(script_get_quat(script_null(), geo_quat_forward_to_down).w, 0.7071068f, 1e-6f);

    check(geo_color_equal(
        script_get_color(script_color(geo_color_soothing_purple), geo_color_clear),
        geo_color_soothing_purple,
        1e-4f));
    check(geo_color_equal(
        script_get_color(script_null(), geo_color_maroon), geo_color_maroon, 1e-4f));

    check(script_get_time(script_time(time_seconds(1)), time_seconds(2)) == time_seconds(1));
    check(script_get_time(script_null(), time_seconds(2)) == time_seconds(2));

    check(script_get_entity(script_entity(dummyEntity1), dummyEntity1) == dummyEntity1);
    check(script_get_entity(script_null(), 0x2) == 0x2);

    const ScriptVal str = script_str(string_hash_lit("Hello World"));
    check(script_get_str(str, 42) == string_hash_lit("Hello World"));
    check(script_get_str(script_null(), 42) == 42);
  }

  it("can test if a value is truthy") {
    check(!script_truthy(script_null()));

    check(!script_truthy(script_bool(false)));
    check(script_truthy(script_bool(true)));

    check(script_truthy(script_num(0)));
    check(script_truthy(script_num(-0.0)));
    check(script_truthy(script_num(42)));

    check(script_truthy(script_vec3_lit(0, 0, 0)));
    check(script_truthy(script_vec3_lit(1, 2, 0)));

    check(script_truthy(script_quat(geo_quat_ident)));

    check(script_truthy(script_color(geo_color_clear)));
    check(script_truthy(script_color(geo_color_white)));

    check(script_truthy(script_entity(dummyEntity1)));

    check(script_truthy(script_str(0)));
    check(script_truthy(script_str(string_hash_lit("Hello World"))));
  }

  it("can test if a value is falsy") {
    check(script_falsy(script_null()));
    check(script_falsy(script_bool(false)));
    check(!script_falsy(script_bool(true)));

    check(!script_falsy(script_num(0)));
    check(!script_falsy(script_num(42)));

    check(!script_falsy(script_vec3_lit(0, 0, 0)));
    check(!script_falsy(script_vec3_lit(1, 2, 0)));

    check(!script_falsy(script_quat(geo_quat_ident)));

    check(!script_falsy(script_color(geo_color_clear)));
    check(!script_falsy(script_color(geo_color_white)));

    check(!script_falsy(script_entity(dummyEntity1)));

    check(!script_falsy(script_str(0)));
    check(!script_falsy(script_str(string_hash_lit("Hello World"))));
  }

  it("can test if a value is not null") {
    check(script_non_null(script_num(42)));
    check(!script_non_null(script_null()));
  }

  it("can return a default if the value is null") {
    check_eq_val(script_val_or(script_num(42), script_num(1337)), script_num(42));
    check_eq_val(script_val_or(script_num(42), script_null()), script_num(42));
    check_eq_val(script_val_or(script_null(), script_num(1337)), script_num(1337));
    check_eq_val(script_val_or(script_null(), script_null()), script_null());
  }

  it("can produce a textual representation for a type") {
    check_eq_string(script_val_type_str(ScriptType_Null), string_lit("null"));
    check_eq_string(script_val_type_str(ScriptType_Num), string_lit("num"));
    check_eq_string(script_val_type_str(ScriptType_Bool), string_lit("bool"));
    check_eq_string(script_val_type_str(ScriptType_Vec3), string_lit("vec3"));
    check_eq_string(script_val_type_str(ScriptType_Quat), string_lit("quat"));
    check_eq_string(script_val_type_str(ScriptType_Color), string_lit("color"));
    check_eq_string(script_val_type_str(ScriptType_Entity), string_lit("entity"));
    check_eq_string(script_val_type_str(ScriptType_Str), string_lit("str"));
  }

  it("can produce a hash for a value type") {
    check_eq_int(script_val_type_hash(ScriptType_Null), string_hash_lit("null"));
    check_eq_int(script_val_type_hash(ScriptType_Num), string_hash_lit("num"));
    check_eq_int(script_val_type_hash(ScriptType_Bool), string_hash_lit("bool"));
    check_eq_int(script_val_type_hash(ScriptType_Vec3), string_hash_lit("vec3"));
    check_eq_int(script_val_type_hash(ScriptType_Quat), string_hash_lit("quat"));
    check_eq_int(script_val_type_hash(ScriptType_Color), string_hash_lit("color"));
    check_eq_int(script_val_type_hash(ScriptType_Entity), string_hash_lit("entity"));
    check_eq_int(script_val_type_hash(ScriptType_Str), string_hash_lit("str"));
  }

  it("can lookup a type from its string-hash") {
    check_eq_int(script_val_type_from_hash(string_hash_lit("null")), ScriptType_Null);
    check_eq_int(script_val_type_from_hash(string_hash_lit("num")), ScriptType_Num);
    check_eq_int(script_val_type_from_hash(string_hash_lit("bool")), ScriptType_Bool);
    check_eq_int(script_val_type_from_hash(string_hash_lit("vec3")), ScriptType_Vec3);
    check_eq_int(script_val_type_from_hash(string_hash_lit("quat")), ScriptType_Quat);
    check_eq_int(script_val_type_from_hash(string_hash_lit("color")), ScriptType_Color);
    check_eq_int(script_val_type_from_hash(string_hash_lit("entity")), ScriptType_Entity);
    check_eq_int(script_val_type_from_hash(string_hash_lit("str")), ScriptType_Str);

    check_eq_int(script_val_type_from_hash(string_hash_lit("")), ScriptType_Null);
    check_eq_int(script_val_type_from_hash(string_hash_lit("hello-world")), ScriptType_Null);
  }

  it("can create a textual representation of a value") {
    const struct {
      ScriptVal value;
      String    expected;
    } testData[] = {
        {script_null(), string_lit("null")},
        {script_num(42), string_lit("42")},
        {script_num(42.1), string_lit("42.1")},
        {script_num(4294967295), string_lit("4294967295")},
        {script_bool(true), string_lit("true")},
        {script_bool(false), string_lit("false")},
        {script_vec3_lit(1, 2, 3), string_lit("1, 2, 3")},
        {script_quat(geo_quat_ident), string_lit("0, 0, 0, 1")},
        {script_color(geo_color_clear), string_lit("0.00, 0.00, 0.00, 0.00")},
        {script_color(geo_color_red), string_lit("1.00, 0.00, 0.00, 1.00")},
        {script_entity(dummyEntity1), string_lit("0000000100000000")},
        {script_entity(dummyEntity2), string_lit("0000000200000001")},
        {script_time(time_seconds(42)), string_lit("42")},
        {script_time(time_hour), string_lit("3600")},
        {script_time(time_milliseconds(500)), string_lit("0.5")},
        {script_time(time_milliseconds(42)), string_lit("0.042")},
        {script_str(string_hash_lit("Hello World")), string_lit("Hello World")},
    };

    // NOTE: Normally we expect the script lexer to register the strings.
    stringtable_add(g_stringtable, string_lit("Hello World"));

    for (u32 i = 0; i != array_elems(testData); ++i) {
      check_eq_string(script_val_scratch(testData[i].value), testData[i].expected);
    }
  }

  it("can create a textual representation of a mask") {
    const struct {
      ScriptMask mask;
      String     expected;
    } testData[] = {
        {script_mask_none, string_lit("none")},
        {script_mask_any, string_lit("any")},
        {script_mask_null, string_lit("null")},
        {script_mask_num, string_lit("num")},
        {script_mask_bool, string_lit("bool")},
        {script_mask_vec3, string_lit("vec3")},
        {script_mask_quat, string_lit("quat")},
        {script_mask_color, string_lit("color")},
        {script_mask_entity, string_lit("entity")},
        {script_mask_str, string_lit("str")},
        {script_mask_null | script_mask_num, string_lit("num?")},
        {
            script_mask_null | script_mask_num | script_mask_str,
            string_lit("null | num | str"),
        },
        {
            script_mask_null | script_mask_num | script_mask_str | script_mask_vec3,
            string_lit("null | num | vec3 | str"),
        },
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      check_eq_string(script_mask_scratch(testData[i].mask), testData[i].expected);
    }
  }

  it("can test if values are equal") {
    const struct {
      ScriptVal a, b;
      bool      expected;
    } testData[] = {
        {script_null(), script_null(), .expected = true},
        {script_null(), script_num(42), .expected = false},
        {script_num(42), script_null(), .expected = false},

        {script_num(42), script_num(42), .expected = true},
        {script_num(42), script_num(42.1), .expected = false},
        {script_num(42), script_num(42.000001), .expected = false},
        {script_num(42), script_num(42.0000001), .expected = true},

        {script_bool(true), script_bool(true), .expected = true},
        {script_bool(false), script_bool(false), .expected = true},
        {script_bool(false), script_bool(true), .expected = false},

        {script_vec3_lit(1, 2, 0), script_vec3_lit(1, 2, 0), .expected = true},
        {script_vec3_lit(1, 2, 0), script_vec3_lit(1, 3, 0), .expected = false},

        {script_quat(geo_quat_ident), script_quat(geo_quat_ident), .expected = true},
        {script_quat(geo_quat_forward_to_up),
         script_quat(geo_quat_forward_to_up),
         .expected = true},
        {script_quat(geo_quat_ident), script_quat(geo_quat_forward_to_up), .expected = false},
        {script_quat(geo_quat_forward_to_forward),
         script_quat(geo_quat_forward_to_backward),
         .expected = false},

        {script_color(geo_color_red), script_color(geo_color_red), .expected = true},
        {script_color(geo_color_red), script_color(geo_color_blue), .expected = false},

        {script_time(time_seconds(1)), script_time(time_seconds(1)), .expected = true},
        {script_time(time_seconds(1)), script_time(time_seconds(2)), .expected = false},

        {script_entity(dummyEntity1), script_entity(dummyEntity1), .expected = true},
        {script_entity(dummyEntity1), script_entity(dummyEntity2), .expected = false},

        {script_num(1), script_bool(true), .expected = false},

        {script_str(string_hash_lit("A")), script_null(), .expected = false},
        {
            script_str(string_hash_lit("A")),
            script_str(string_hash_lit("A")),
            .expected = true,
        },
        {
            script_str(string_hash_lit("A")),
            script_str(string_hash_lit("B")),
            .expected = false,
        },
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
        {script_null(), script_num(42), .expected = false},
        {script_num(42), script_null(), .expected = false},

        {script_num(1), script_num(2), .expected = true},
        {script_num(2), script_num(1), .expected = false},
        {script_num(1), script_num(1), .expected = false},

        {script_bool(true), script_bool(true), .expected = false},
        {script_bool(false), script_bool(false), .expected = false},
        {script_bool(true), script_bool(false), .expected = false},
        {script_bool(false), script_bool(true), .expected = true},

        {script_vec3_lit(1, 2, 0), script_vec3_lit(1, 2, 0), .expected = false},
        {script_vec3_lit(1, 3, 0), script_vec3_lit(1, 2, 0), .expected = false},
        {script_vec3_lit(1, 2, 0), script_vec3_lit(1, 3, 0), .expected = true},

        {script_quat(geo_quat_ident), script_quat(geo_quat_ident), .expected = false},

        {script_color(geo_color_clear), script_color(geo_color_clear), .expected = false},
        {script_color(geo_color_clear), script_color(geo_color_red), .expected = true},

        {script_time(time_seconds(1)), script_time(time_seconds(2)), .expected = true},
        {script_time(time_seconds(2)), script_time(time_seconds(1)), .expected = false},
        {script_time(time_seconds(1)), script_time(time_seconds(1)), .expected = false},

        {script_num(1), script_bool(true), .expected = false},

        {
            script_str(string_hash_lit("A")),
            script_str(string_hash_lit("B")),
            .expected = false,
        },
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      if (testData[i].expected) {
        check_less_val(testData[i].a, testData[i].b);
      } else {
        check_msg(
            !script_val_less(testData[i].a, testData[i].b),
            "{} >= {}",
            fmt_text(script_val_scratch(testData[i].a)),
            fmt_text(script_val_scratch(testData[i].b)));
      }
    }
  }

  it("can test if values are greater") {
    const struct {
      ScriptVal a, b;
      bool      expected;
    } testData[] = {
        {script_null(), script_null(), .expected = false},
        {script_null(), script_num(42), .expected = false},
        {script_num(42), script_null(), .expected = false},

        {script_num(2), script_num(1), .expected = true},
        {script_num(1), script_num(2), .expected = false},
        {script_num(1), script_num(1), .expected = false},

        {script_bool(true), script_bool(false), .expected = true},
        {script_bool(true), script_bool(true), .expected = false},
        {script_bool(false), script_bool(false), .expected = false},
        {script_bool(false), script_bool(true), .expected = false},

        {script_vec3_lit(1, 3, 0), script_vec3_lit(1, 2, 0), .expected = true},
        {script_vec3_lit(1, 2, 0), script_vec3_lit(1, 2, 0), .expected = false},
        {script_vec3_lit(1, 2, 0), script_vec3_lit(1, 3, 0), .expected = false},

        {script_quat(geo_quat_ident), script_quat(geo_quat_ident), .expected = false},

        {script_color(geo_color_clear), script_color(geo_color_clear), .expected = false},
        {script_color(geo_color_red), script_color(geo_color_clear), .expected = true},

        {script_time(time_seconds(2)), script_time(time_seconds(1)), .expected = true},
        {script_time(time_seconds(1)), script_time(time_seconds(2)), .expected = false},
        {script_time(time_seconds(1)), script_time(time_seconds(1)), .expected = false},

        {script_num(1), script_bool(true), .expected = false},

        {
            script_str(string_hash_lit("A")),
            script_str(string_hash_lit("B")),
            .expected = false,
        },
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      if (testData[i].expected) {
        check_greater_val(testData[i].a, testData[i].b);
      } else {
        check_msg(
            !script_val_greater(testData[i].a, testData[i].b),
            "{} <= {}",
            fmt_text(script_val_scratch(testData[i].a)),
            fmt_text(script_val_scratch(testData[i].b)));
      }
    }
  }

  it("can negate values") {
    const struct {
      ScriptVal val;
      ScriptVal expected;
    } testData[] = {
        {script_null(), .expected = script_null()},
        {script_num(42), .expected = script_num(-42)},
        {script_bool(true), .expected = script_null()},
        {script_vec3_lit(1, 2, 3), .expected = script_vec3_lit(-1, -2, -3)},
        {script_quat(geo_quat_forward_to_up), .expected = script_quat(geo_quat_forward_to_down)},
        {script_color(geo_color_red), .expected = script_color(geo_color(-1, 0, 0, -1))},
        {script_time(time_seconds(2)), .expected = script_time(time_seconds(-2))},
        {script_str(string_hash_lit("A")), .expected = script_null()},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      const ScriptVal actual = script_val_neg(testData[i].val);
      check_eq_val(actual, testData[i].expected);
    }
  }

  it("can invert values") {
    const struct {
      ScriptVal val;
      ScriptVal expected;
    } testData[] = {
        {script_null(), .expected = script_bool(true)},
        {script_num(42), .expected = script_bool(false)},
        {script_bool(true), .expected = script_bool(false)},
        {script_bool(false), .expected = script_bool(true)},
        {script_vec3_lit(1, 2, 3), .expected = script_bool(false)},
        {script_quat(geo_quat_ident), .expected = script_bool(false)},
        {script_color(geo_color_red), .expected = script_bool(false)},
        {script_time(time_seconds(2)), .expected = script_bool(false)},
        {script_str(string_hash_lit("A")), .expected = script_bool(false)},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      const ScriptVal actual = script_val_inv(testData[i].val);
      check_eq_val(actual, testData[i].expected);
    }
  }

  it("can add values") {
    const struct {
      ScriptVal a, b;
      ScriptVal expected;
    } testData[] = {
        {script_null(), script_null(), .expected = script_null()},
        {script_null(), script_num(42), .expected = script_null()},
        {script_num(42), script_null(), .expected = script_null()},
        {script_num(42), script_bool(false), .expected = script_null()},

        {script_num(42), script_num(1), .expected = script_num(43)},
        {script_num(42), script_num(1337), .expected = script_num(1379)},

        {script_bool(true), script_bool(false), .expected = script_null()},

        {.a        = script_vec3_lit(1, 2, 3),
         .b        = script_vec3_lit(4, 5, 6),
         .expected = script_vec3_lit(5, 7, 9)},

        {.a = script_vec3_lit(1, 2, 3), .b = script_num(42), .expected = script_null()},

        {
            script_quat(geo_quat_ident),
            script_quat(geo_quat_ident),
            .expected = script_null(),
        },

        {.a        = script_color(geo_color_red),
         .b        = script_color(geo_color_white),
         .expected = script_color(geo_color(2, 1, 1, 2))},

        {.a        = script_time(time_seconds(2)),
         .b        = script_time(time_seconds(3)),
         .expected = script_time(time_seconds(5))},

        {.a = script_time(time_seconds(1)), .b = script_null(), .expected = script_null()},

        {.a        = script_entity(dummyEntity1),
         .b        = script_entity(dummyEntity2),
         .expected = script_null()},

        {
            script_str(string_hash_lit("A")),
            script_str(string_hash_lit("B")),
            .expected = script_null(),
        },
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
        {script_null(), script_num(42), .expected = script_null()},
        {script_num(42), script_null(), .expected = script_null()},
        {script_num(42), script_bool(false), .expected = script_null()},

        {script_num(42), script_num(1), .expected = script_num(41)},
        {script_num(42), script_num(1337), .expected = script_num(-1295)},

        {script_bool(true), script_bool(false), .expected = script_null()},

        {.a        = script_vec3_lit(1, 2, 3),
         .b        = script_vec3_lit(4, 5, 6),
         .expected = script_vec3_lit(-3, -3, -3)},

        {.a = script_vec3_lit(1, 2, 3), .b = script_num(42), .expected = script_null()},

        {
            script_quat(geo_quat_ident),
            script_quat(geo_quat_ident),
            .expected = script_null(),
        },

        {.a        = script_color(geo_color_red),
         .b        = script_color(geo_color_white),
         .expected = script_color(geo_color(0, -1, -1, 0))},

        {.a        = script_time(time_seconds(1)),
         .b        = script_time(time_seconds(2)),
         .expected = script_time(time_seconds(-1))},

        {.a = script_time(time_seconds(1)), .b = script_null(), .expected = script_null()},

        {.a        = script_entity(dummyEntity1),
         .b        = script_entity(dummyEntity2),
         .expected = script_null()},

        {
            script_str(string_hash_lit("A")),
            script_str(string_hash_lit("B")),
            .expected = script_null(),
        },
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      const ScriptVal actual = script_val_sub(testData[i].a, testData[i].b);
      check_eq_val(actual, testData[i].expected);
    }
  }

  it("can multiply values") {
    const struct {
      ScriptVal a, b;
      ScriptVal expected;
    } testData[] = {
        {script_null(), script_null(), .expected = script_null()},
        {script_null(), script_num(42), .expected = script_null()},
        {script_num(42), script_null(), .expected = script_null()},
        {script_num(42), script_bool(false), .expected = script_null()},

        {script_num(42), script_num(2), .expected = script_num(84)},
        {script_num(42), script_num(1337), .expected = script_num(56154)},

        {script_bool(true), script_bool(false), .expected = script_null()},

        {.a        = script_vec3_lit(1, 2, 3),
         .b        = script_vec3_lit(4, 5, 6),
         .expected = script_vec3_lit(4, 10, 18)},

        {.a        = script_vec3_lit(1, 2, 3),
         .b        = script_num(42),
         .expected = script_vec3_lit(42, 84, 126)},

        {.a        = script_quat(geo_quat_ident),
         .b        = script_quat(geo_quat_ident),
         .expected = script_quat(geo_quat_ident)},
        {.a        = script_quat(geo_quat_forward_to_up),
         .b        = script_quat(geo_quat_ident),
         .expected = script_quat(geo_quat_forward_to_up)},
        {.a        = script_quat(geo_quat_ident),
         .b        = script_quat(geo_quat_forward_to_up),
         .expected = script_quat(geo_quat_forward_to_up)},

        {.a        = script_quat(geo_quat_ident),
         .b        = script_vec3_lit(1, 2, 3),
         .expected = script_vec3_lit(1, 2, 3)},

        {.a        = script_color(geo_color_red),
         .b        = script_num(2),
         .expected = script_color(geo_color(2, 0, 0, 2))},

        {.a        = script_time(time_seconds(2)),
         .b        = script_time(time_seconds(3)),
         .expected = script_time(time_seconds(6))},

        {.a = script_time(time_seconds(1)), .b = script_null(), .expected = script_null()},

        {.a        = script_entity(dummyEntity1),
         .b        = script_entity(dummyEntity2),
         .expected = script_null()},

        {
            script_str(string_hash_lit("A")),
            script_str(string_hash_lit("B")),
            .expected = script_null(),
        },
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      const ScriptVal actual = script_val_mul(testData[i].a, testData[i].b);
      check_eq_val(actual, testData[i].expected);
    }
  }

  it("can divide values") {
    const struct {
      ScriptVal a, b;
      ScriptVal expected;
    } testData[] = {
        {script_null(), script_null(), .expected = script_null()},
        {script_null(), script_num(42), .expected = script_null()},
        {script_num(42), script_null(), .expected = script_null()},
        {script_num(42), script_bool(false), .expected = script_null()},

        {script_num(42), script_num(2), .expected = script_num(21)},
        {script_num(1337), script_num(42), .expected = script_num(1337.0 / 42.0)},

        {script_bool(true), script_bool(false), .expected = script_null()},

        {.a        = script_vec3_lit(1, 2, 3),
         .b        = script_vec3_lit(4, 5, 6),
         .expected = script_vec3_lit(0.25f, 0.4f, 0.5f)},

        {.a = script_vec3_lit(2, 4, 8), .b = script_num(2), .expected = script_vec3_lit(1, 2, 4)},

        {script_quat(geo_quat_ident), script_quat(geo_quat_ident), .expected = script_null()},

        {.a        = script_color(geo_color_red),
         .b        = script_num(2),
         .expected = script_color(geo_color(0.5f, 0, 0, 0.5f))},

        {.a        = script_time(time_seconds(10)),
         .b        = script_time(time_seconds(2)),
         .expected = script_time(time_seconds(5))},

        {.a = script_time(time_seconds(1)), .b = script_null(), .expected = script_null()},

        {.a        = script_entity(dummyEntity1),
         .b        = script_entity(dummyEntity2),
         .expected = script_null()},

        {
            script_str(string_hash_lit("A")),
            script_str(string_hash_lit("B")),
            .expected = script_null(),
        },
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      const ScriptVal actual = script_val_div(testData[i].a, testData[i].b);
      check_eq_val(actual, testData[i].expected);
    }
  }

  it("can compute the modulo of values") {
    const struct {
      ScriptVal a, b;
      ScriptVal expected;
    } testData[] = {
        {script_null(), script_null(), .expected = script_null()},
        {script_null(), script_num(42), .expected = script_null()},
        {script_num(42), script_null(), .expected = script_null()},
        {script_num(42), script_bool(false), .expected = script_null()},

        {script_num(42), script_num(1), .expected = script_num(0)},
        {script_num(42), script_num(2), .expected = script_num(0)},
        {script_num(42), script_num(42), .expected = script_num(0)},
        {script_num(42), script_num(4), .expected = script_num(2)},
        {script_num(42), script_num(43), .expected = script_num(42)},
        {script_num(42), script_num(-1), .expected = script_num(0)},
        {script_num(42), script_num(-43), .expected = script_num(42)},

        {script_num(-42), script_num(1), .expected = script_num(0)},
        {script_num(-42), script_num(2), .expected = script_num(0)},
        {script_num(-42), script_num(42), .expected = script_num(0)},
        {script_num(-42), script_num(4), .expected = script_num(-2)},
        {script_num(-42), script_num(43), .expected = script_num(-42)},
        {script_num(-42), script_num(43), .expected = script_num(-42)},
        {script_num(-42), script_num(-1), .expected = script_num(0)},
        {script_num(-42), script_num(-43), .expected = script_num(-42)},

        {.a        = script_vec3_lit(4, 6, 6),
         .b        = script_vec3_lit(2, 3, 4),
         .expected = script_vec3_lit(0, 0, 2)},
        {.a = script_vec3_lit(4, 6, 6), .b = script_num(4), .expected = script_vec3_lit(0, 2, 2)},

        {script_quat(geo_quat_ident), script_quat(geo_quat_ident), .expected = script_null()},

        {script_color(geo_color_red), script_color(geo_color_red), .expected = script_null()},

        {
            script_str(string_hash_lit("A")),
            script_str(string_hash_lit("B")),
            .expected = script_null(),
        },
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      const ScriptVal actual = script_val_mod(testData[i].a, testData[i].b);
      check_eq_val(actual, testData[i].expected);
    }
  }

  it("can compute the distance between values") {
    const struct {
      ScriptVal a, b;
      ScriptVal expected;
    } testData[] = {
        {script_null(), script_null(), .expected = script_null()},
        {script_null(), script_num(42), .expected = script_null()},
        {script_num(42), script_null(), .expected = script_null()},
        {script_num(42), script_bool(false), .expected = script_null()},

        {script_num(0), script_num(0), .expected = script_num(0)},
        {script_num(-1), script_num(1), .expected = script_num(2)},
        {script_num(0), script_num(42), .expected = script_num(42)},
        {script_num(-42), script_num(0), .expected = script_num(42)},
        {script_num(42), script_num(2), .expected = script_num(40)},
        {script_num(-1337), script_num(42), .expected = script_num(1379)},

        {script_bool(true), script_bool(false), .expected = script_null()},

        {.a = script_vec3_lit(0, 0, 0), .b = script_vec3_lit(0, 42, 0), .expected = script_num(42)},

        {.a        = script_vec3_lit(0, -42, 0),
         .b        = script_vec3_lit(0, 42, 0),
         .expected = script_num(84)},

        {.a        = script_vec3_lit(1, 2, 3),
         .b        = script_vec3_lit(4, 5, 6),
         .expected = script_num(5.1961522)},

        {.a        = script_quat(geo_quat_ident),
         .b        = script_quat(geo_quat_ident),
         .expected = script_null()},

        {.a        = script_color(geo_color_white),
         .b        = script_color(geo_color_red),
         .expected = script_num(1.4142135)},

        {.a        = script_time(time_seconds(10)),
         .b        = script_time(time_seconds(2)),
         .expected = script_time(time_seconds(8))},

        {.a        = script_entity(dummyEntity1),
         .b        = script_entity(dummyEntity2),
         .expected = script_null()},

        {
            script_str(string_hash_lit("A")),
            script_str(string_hash_lit("B")),
            .expected = script_null(),
        },
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      const ScriptVal actual = script_val_dist(testData[i].a, testData[i].b);
      check_eq_val(actual, testData[i].expected);
    }
  }

  it("can clamp values") {
    const struct {
      ScriptVal v, min, max;
      ScriptVal expected;
    } testData[] = {
        {script_null(), script_null(), script_null(), .expected = script_null()},
        {script_bool(true), script_bool(false), script_bool(false), .expected = script_null()},
        {
            .v        = script_vec3_lit(0, 0, 3.0f),
            .min      = script_null(),
            .max      = script_num(1.25f),
            .expected = script_vec3_lit(0, 0, 1.25f),
        },
        {
            .v        = script_vec3_lit(-1, 0, 1),
            .min      = script_vec3_lit(2, -1, 3),
            .max      = script_vec3_lit(3, 1, 4),
            .expected = script_vec3_lit(2, 0, 3),
        },
        {
            .v        = script_color(geo_color(0, 0, 3.0f, 0)),
            .min      = script_null(),
            .max      = script_num(1.25f),
            .expected = script_color(geo_color(0, 0, 1.25f, 0)),
        },
        {
            .v        = script_color(geo_color(-1, 0, 1, 0)),
            .min      = script_color(geo_color(2, -1, 3, 0)),
            .max      = script_color(geo_color(3, 1, 4, 0)),
            .expected = script_color(geo_color(2, 0, 3, 0)),
        },
        {
            .v        = script_num(1.25f),
            .min      = script_num(1.5f),
            .max      = script_num(2.0f),
            .expected = script_num(1.5f),
        },
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      const ScriptVal actual = script_val_clamp(testData[i].v, testData[i].min, testData[i].max);
      check_eq_val(actual, testData[i].expected);
    }
  }

  it("can lerp values") {
    const struct {
      ScriptVal x, y, t;
      ScriptVal expected;
    } testData[] = {
        {script_null(), script_null(), script_null(), .expected = script_null()},
        {script_bool(true), script_bool(false), script_num(0.0f), .expected = script_null()},
        {
            .x        = script_num(0.1f),
            .y        = script_num(0.9f),
            .t        = script_num(0.5f),
            .expected = script_num(0.5f),
        },
        {
            .x        = script_vec3_lit(1.0f, 2.0f, 3.0f),
            .y        = script_vec3_lit(4.0f, 5.0f, 6.0f),
            .t        = script_num(0.5f),
            .expected = script_vec3_lit(2.5f, 3.5, 4.5f),
        },
        {
            .x        = script_color(geo_color(1, 0, 1, 1)),
            .y        = script_color(geo_color(2, 1, 3, 1)),
            .t        = script_num(0.25f),
            .expected = script_color(geo_color(1.25f, 0.25f, 1.5f, 1)),
        },
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      const ScriptVal actual = script_val_lerp(testData[i].x, testData[i].y, testData[i].t);
      check_eq_val(actual, testData[i].expected);
    }
  }

  it("can compose a vector3") {
    const struct {
      ScriptVal a, b, c;
      ScriptVal expected;
    } testData[] = {
        {
            script_num(1),
            script_num(2),
            script_num(3),
            .expected = script_vec3_lit(1, 2, 3),
        },
        {
            script_null(),
            script_num(2),
            script_num(3),
            .expected = script_null(),
        },
        {
            script_num(1),
            script_null(),
            script_num(3),
            .expected = script_null(),
        },
        {
            script_num(1),
            script_num(2),
            script_null(),
            .expected = script_null(),
        },
        {script_null(), script_null(), script_null(), .expected = script_null()},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      const ScriptVal actual = script_val_vec3_compose(testData[i].a, testData[i].b, testData[i].c);
      check_eq_val(actual, testData[i].expected);
    }
  }

  it("can compose a color") {
    const struct {
      ScriptVal a, b, c, d;
      ScriptVal expected;
    } testData[] = {
        {
            script_num(1),
            script_num(2),
            script_num(3),
            script_num(4),
            .expected = script_color(geo_color(1, 2, 3, 4)),
        },
        {
            script_null(),
            script_num(2),
            script_num(3),
            script_num(4),
            .expected = script_null(),
        },
        {
            script_num(1),
            script_null(),
            script_num(3),
            script_num(4),
            .expected = script_null(),
        },
        {
            script_num(1),
            script_num(2),
            script_null(),
            script_num(4),
            .expected = script_null(),
        },
        {
            script_num(1),
            script_num(2),
            script_num(3),
            script_null(),
            .expected = script_null(),
        },
        {script_null(), script_null(), script_null(), script_null(), .expected = script_null()},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      const ScriptVal actual =
          script_val_color_compose(testData[i].a, testData[i].b, testData[i].c, testData[i].d);
      check_eq_val(actual, testData[i].expected);
    }
  }

  it("can compose a color from hsv") {
    const struct {
      ScriptVal a, b, c, d;
      ScriptVal expected;
    } testData[] = {
        {
            script_num(0.25f),
            script_num(0.5f),
            script_num(1),
            script_num(1),
            .expected = script_color(geo_color(0.75f, 1.0f, 0.5f, 1.0f)),
        },
        {
            script_null(),
            script_num(1),
            script_num(1),
            script_num(1),
            .expected = script_null(),
        },
        {
            script_num(1),
            script_null(),
            script_num(1),
            script_num(1),
            .expected = script_null(),
        },
        {
            script_num(1),
            script_num(1),
            script_null(),
            script_num(1),
            .expected = script_null(),
        },
        {
            script_num(1),
            script_num(1),
            script_num(1),
            script_null(),
            .expected = script_null(),
        },
        {script_null(), script_null(), script_null(), script_null(), .expected = script_null()},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      const ScriptVal actual =
          script_val_color_compose_hsv(testData[i].a, testData[i].b, testData[i].c, testData[i].d);
      check_eq_val(actual, testData[i].expected);
    }
  }
}
