#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "data.h"

static void
test_read_success(CheckTestContext* _testCtx, const String input, const DataMeta meta, Mem data) {

  DataReadResult res;
  const String   remaining = data_read_json(input, g_alloc_heap, meta, data, &res);

  check_eq_string(remaining, string_empty);
  check_eq_int(res.error, DataReadError_None);
}

static void test_read_fail(
    CheckTestContext* _testCtx, const String input, const DataMeta meta, const DataReadError err) {

  Mem            data = mem_stack(data_meta_size(meta));
  DataReadResult res;
  const String   remaining = data_read_json(input, g_alloc_heap, meta, data, &res);

  check_eq_string(remaining, string_empty);
  check_eq_int(res.error, err);
}

spec(read_json) {

  it("can read a boolean") {
    const DataMeta meta = data_meta_t(data_prim_t(bool));

    bool val;
    test_read_success(_testCtx, string_lit("true"), meta, mem_var(val));
    check(val);

    test_read_fail(_testCtx, string_lit("null"), meta, DataReadError_MismatchedType);
  }

  it("can read a number") {
#define X(_T_)                                                                                     \
  const DataMeta meta_##_T_ = data_meta_t(data_prim_t(_T_));                                       \
  _T_            val_##_T_;                                                                        \
  test_read_success(_testCtx, string_lit("42"), meta_##_T_, mem_var(val_##_T_));                   \
  check_eq_int(val_##_T_, 42);                                                                     \
  test_read_fail(_testCtx, string_lit("null"), meta_##_T_, DataReadError_MismatchedType);

    X(i8)
    X(i16)
    X(i32)
    X(i64)
    X(u8)
    X(u16)
    X(u32)
    X(u64)
    X(f32)
    X(f64)
#undef X
  }

  it("fails when a number is out of bounds") {
    static const struct {
      String   input;
      DataPrim prim;
    } data[] = {
        {.input = string_static("129"), DataPrim_i8},
        {.input = string_static("-129"), DataPrim_i8},
        {.input = string_static("32768"), DataPrim_i16},
        {.input = string_static("-32769"), DataPrim_i16},
        {.input = string_static("2147483648"), DataPrim_i32},
        {.input = string_static("-2147483649"), DataPrim_i32},
        {.input = string_static("-1"), DataPrim_u8},
        {.input = string_static("256"), DataPrim_u8},
        {.input = string_static("-1"), DataPrim_u16},
        {.input = string_static("65536"), DataPrim_u16},
        {.input = string_static("-1"), DataPrim_u32},
        {.input = string_static("4294967296"), DataPrim_u32},
        {.input = string_static("-1"), DataPrim_u64},
    };
    for (usize i = 0; i != array_elems(data); ++i) {
      const DataMeta meta = data_meta_t(data_prim(data[i].prim));
      test_read_fail(_testCtx, data[i].input, meta, DataReadError_NumberOutOfBounds);
    }
  }

  it("can read a string") {
    const DataMeta meta = data_meta_t(data_prim_t(String));

    String val;
    test_read_success(_testCtx, string_lit("\"Hello World\""), meta, mem_var(val));
    check_eq_string(val, string_lit("Hello World"));
    string_free(g_alloc_heap, val);

    test_read_success(_testCtx, string_lit("\"\""), meta, mem_var(val));
    check_eq_string(val, string_empty);

    test_read_fail(_testCtx, string_lit("null"), meta, DataReadError_MismatchedType);
  }

  it("can read a pointer") {
    const DataMeta meta = data_meta_t(data_prim_t(u32), .container = DataContainer_Pointer);

    u32* val;
    test_read_success(_testCtx, string_lit("42"), meta, mem_var(val));
    check_eq_int(*val, 42);
    alloc_free_t(g_alloc_heap, val);

    test_read_success(_testCtx, string_lit("null"), meta, mem_var(val));
    check_eq_int(val, null);

    test_read_fail(_testCtx, string_lit("true"), meta, DataReadError_MismatchedType);
  }

  it("can read an array") {
    const DataMeta meta = data_meta_t(data_prim_t(u32), .container = DataContainer_Array);

    struct {
      u32*  ptr;
      usize count;
    } val;
    test_read_success(_testCtx, string_lit("[]"), meta, mem_var(val));
    check_eq_int(val.count, 0);

    test_read_success(_testCtx, string_lit("[42]"), meta, mem_var(val));
    check_eq_int(val.count, 1);
    check_eq_int(val.ptr[0], 42);
    alloc_free_array_t(g_alloc_heap, val.ptr, val.count);

    test_read_success(_testCtx, string_lit("[1, 2, 3]"), meta, mem_var(val));
    check_eq_int(val.count, 3);
    check_eq_int(val.ptr[0], 1);
    check_eq_int(val.ptr[1], 2);
    check_eq_int(val.ptr[2], 3);
    alloc_free_array_t(g_alloc_heap, val.ptr, val.count);

    test_read_fail(_testCtx, string_lit("42"), meta, DataReadError_MismatchedType);
    test_read_fail(_testCtx, string_lit("null"), meta, DataReadError_MismatchedType);
  }

  it("can read an enum") {
    typedef enum {
      ReadJsonTestEnum_A = -42,
      ReadJsonTestEnum_B = 42,
      ReadJsonTestEnum_C = 1337,
    } ReadJsonTestEnum;

    static DataMeta meta; // Registrations persist over the entire application lifetime.
    if (!meta.type) {

      data_register_enum_t(ReadJsonTestEnum);
      data_register_const_t(ReadJsonTestEnum, A);
      data_register_const_t(ReadJsonTestEnum, B);
      data_register_const_t(ReadJsonTestEnum, C);

      meta = data_meta_t(t_ReadJsonTestEnum);
    }

    ReadJsonTestEnum val;
    test_read_success(_testCtx, string_lit("\"A\""), meta, mem_var(val));
    check_eq_int(val, ReadJsonTestEnum_A);

    test_read_success(_testCtx, string_lit("\"B\""), meta, mem_var(val));
    check_eq_int(val, ReadJsonTestEnum_B);

    test_read_success(_testCtx, string_lit("\"C\""), meta, mem_var(val));
    check_eq_int(val, ReadJsonTestEnum_C);

    test_read_fail(_testCtx, string_lit("\"D\""), meta, DataReadError_InvalidEnumEntry);
    test_read_fail(_testCtx, string_lit("\"\""), meta, DataReadError_InvalidEnumEntry);
    test_read_fail(_testCtx, string_lit("null"), meta, DataReadError_MismatchedType);
  }

  it("can read a structure") {
    typedef struct {
      i32    valA;
      String valB;
      f32    valC;
    } ReadJsonTestStruct;

    static DataMeta meta; // Registrations persist over the entire application lifetime.
    if (!meta.type) {

      data_register_struct_t(ReadJsonTestStruct);
      data_register_field_t(ReadJsonTestStruct, valA, data_prim_t(i32));
      data_register_field_t(ReadJsonTestStruct, valB, data_prim_t(String));
      data_register_field_t(ReadJsonTestStruct, valC, data_prim_t(f32));

      meta = data_meta_t(t_ReadJsonTestStruct);
    }

    ReadJsonTestStruct val;
    test_read_success(
        _testCtx,
        string_lit("{"
                   "\"valA\": -42,"
                   "\"valB\": \"Hello World\","
                   "\"valC\": 42.42"
                   "}"),
        meta,
        mem_var(val));

    check_eq_int(val.valA, -42);
    check_eq_string(val.valB, string_lit("Hello World"));
    check_eq_float(val.valC, 42.42f, 1e-6);
    string_free(g_alloc_heap, val.valB);

    test_read_fail(_testCtx, string_lit("{}"), meta, DataReadError_FieldNotFound);
    test_read_fail(
        _testCtx,
        string_lit("{"
                   "\"valA\": -42,"
                   "\"valB\": \"Hello World\","
                   "\"valD\": 42.42"
                   "}"),
        meta,
        DataReadError_FieldNotFound);
    test_read_fail(_testCtx, string_lit("null"), meta, DataReadError_MismatchedType);
  }
}
