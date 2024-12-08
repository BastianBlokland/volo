#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_float.h"
#include "core_stringtable.h"
#include "data_read.h"
#include "data_utils.h"

static void test_read_success(
    CheckTestContext* _testCtx,
    const DataReg*    reg,
    const String      input,
    const DataMeta    meta,
    Mem               data) {

  DataReadResult res;
  const String   remaining = data_read_json(reg, input, g_allocHeap, meta, data, &res);

  check_eq_string(remaining, string_empty);
  check_require_msg(
      res.error == DataReadError_None,
      "{} == {} ({})",
      fmt_int(res.error),
      fmt_int(DataReadError_None),
      fmt_text(res.errorMsg));
}

static void test_read_fail(
    CheckTestContext*   _testCtx,
    const DataReg*      reg,
    const String        input,
    const DataMeta      meta,
    const DataReadError err) {

  Mem            data = mem_stack(data_meta_size(reg, meta));
  DataReadResult res;
  const String   remaining = data_read_json(reg, input, g_allocHeap, meta, data, &res);

  check_eq_string(remaining, string_empty);
  check_eq_int(res.error, err);
}

static void test_normalizer_enum(const DataMeta meta, const Mem data) {
  (void)meta;
  diag_assert(data.size == sizeof(i32));

  i32* val = data.ptr;
  if (*val < 0) {
    *val = 42;
  }
}

spec(read_json) {

  DataReg* reg = null;

  setup() { reg = data_reg_create(g_allocHeap); }

  it("can read a boolean") {
    const DataMeta meta = data_meta_t(data_prim_t(bool));

    bool val;
    test_read_success(_testCtx, reg, string_lit("true"), meta, mem_var(val));
    check(val);

    test_read_fail(_testCtx, reg, string_lit("null"), meta, DataReadError_MismatchedType);
  }

  it("can read a number") {
#define X(_T_)                                                                                     \
  const DataMeta meta_##_T_ = data_meta_t(data_prim_t(_T_));                                       \
  _T_            val_##_T_;                                                                        \
  test_read_success(_testCtx, reg, string_lit("0"), meta_##_T_, mem_var(val_##_T_));               \
  check_eq_int(val_##_T_, 0);                                                                      \
  test_read_success(_testCtx, reg, string_lit("42"), meta_##_T_, mem_var(val_##_T_));              \
  check_eq_int(val_##_T_, 42);                                                                     \
  test_read_fail(_testCtx, reg, string_lit("null"), meta_##_T_, DataReadError_MismatchedType);

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

    const DataMeta metaF16 = data_meta_t(data_prim_t(f16));
    f16            valF16;
    test_read_success(_testCtx, reg, string_lit("0"), metaF16, mem_var(valF16));
    check_eq_int(float_f16_to_f32(valF16), 0);
    test_read_success(_testCtx, reg, string_lit("42"), metaF16, mem_var(valF16));
    check_eq_int(float_f16_to_f32(valF16), 42);
    test_read_fail(_testCtx, reg, string_lit("null"), metaF16, DataReadError_MismatchedType);
  }

  it("fails when a number is out of bounds") {
    static const struct {
      String   input;
      DataKind prim;
    } g_data[] = {
        {.input = string_static("129"), DataKind_i8},
        {.input = string_static("-129"), DataKind_i8},
        {.input = string_static("32768"), DataKind_i16},
        {.input = string_static("-32769"), DataKind_i16},
        {.input = string_static("2147483648"), DataKind_i32},
        {.input = string_static("-2147483649"), DataKind_i32},
        {.input = string_static("-1"), DataKind_u8},
        {.input = string_static("256"), DataKind_u8},
        {.input = string_static("-1"), DataKind_u16},
        {.input = string_static("65536"), DataKind_u16},
        {.input = string_static("-1"), DataKind_u32},
        {.input = string_static("4294967296"), DataKind_u32},
        {.input = string_static("-1"), DataKind_u64},
    };
    for (usize i = 0; i != array_elems(g_data); ++i) {
      const DataMeta meta = data_meta_t((DataType)g_data[i].prim);
      test_read_fail(_testCtx, reg, g_data[i].input, meta, DataReadError_NumberOutOfBounds);
    }
  }

  it("fails when a number value cannot be empty") {
    static const struct {
      String   input;
      DataKind prim;
    } g_data[] = {
        {.input = string_static("0"), DataKind_i8},
        {.input = string_static("0.1"), DataKind_i8},
        {.input = string_static("0"), DataKind_i16},
        {.input = string_static("-0.1"), DataKind_i16},
        {.input = string_static("-0.9"), DataKind_i16},
        {.input = string_static("0.9"), DataKind_i16},
        {.input = string_static("0"), DataKind_f32},
        {.input = string_static("0"), DataKind_f64},
    };
    for (usize i = 0; i != array_elems(g_data); ++i) {
      const DataMeta meta = data_meta_t((DataType)g_data[i].prim, .flags = DataFlags_NotEmpty);
      test_read_fail(_testCtx, reg, g_data[i].input, meta, DataReadError_ZeroIsInvalid);
    }
  }

  it("can read a string") {
    const DataMeta meta = data_meta_t(data_prim_t(String));

    String val;
    test_read_success(_testCtx, reg, string_lit("\"Hello World\""), meta, mem_var(val));
    check_eq_string(val, string_lit("Hello World"));
    string_free(g_allocHeap, val);

    test_read_success(_testCtx, reg, string_lit("\"\""), meta, mem_var(val));
    check_eq_string(val, string_empty);

    test_read_fail(_testCtx, reg, string_lit("null"), meta, DataReadError_MismatchedType);
  }

  it("can read an interned string") {
    const DataMeta meta = data_meta_t(data_prim_t(String), .flags = DataFlags_Intern);

    String val;
    test_read_success(_testCtx, reg, string_lit("\"Hello World\""), meta, mem_var(val));
    check_eq_string(val, string_lit("Hello World"));
    check(val.ptr == stringtable_lookup(g_stringtable, string_hash_lit("Hello World")).ptr);
  }

  it("fails when a string value cannot be empty") {
    const DataMeta meta = data_meta_t(data_prim_t(String), .flags = DataFlags_NotEmpty);

    test_read_fail(_testCtx, reg, string_lit("\"\""), meta, DataReadError_EmptyStringIsInvalid);
  }

  it("can read a string-hash") {
    const DataMeta meta = data_meta_t(data_prim_t(StringHash));

    StringHash val;
    test_read_success(_testCtx, reg, string_lit("\"Hello World\""), meta, mem_var(val));
    check_eq_int(val, string_hash_lit("Hello World"));

    test_read_success(_testCtx, reg, string_lit("\"\""), meta, mem_var(val));
    check_eq_int(val, 0);

    test_read_success(_testCtx, reg, string_lit("1337"), meta, mem_var(val));
    check_eq_int(val, 1337);

    test_read_success(_testCtx, reg, string_lit("0"), meta, mem_var(val));
    check_eq_int(val, 0);

    test_read_fail(_testCtx, reg, string_lit("null"), meta, DataReadError_MismatchedType);
  }

  it("fails when a string-hash value cannot be zero") {
    const DataMeta meta = data_meta_t(data_prim_t(StringHash), .flags = DataFlags_NotEmpty);

    test_read_fail(_testCtx, reg, string_lit("\"\""), meta, DataReadError_EmptyStringIsInvalid);
  }

  it("can read raw memory as base64") {
    const DataMeta meta = data_meta_t(data_prim_t(DataMem));

    DataMem val;
    test_read_success(_testCtx, reg, string_lit("\"SGVsbG8gV29ybGQ=\""), meta, mem_var(val));
    check_eq_string(data_mem(val), string_lit("Hello World"));
    data_destroy(reg, g_allocHeap, meta, mem_var(val));

    test_read_success(_testCtx, reg, string_lit("\"\""), meta, mem_var(val));
    check_eq_string(data_mem(val), string_empty);

    test_read_fail(
        _testCtx, reg, string_lit("\"SGVsbG8-gV29ybGQ\""), meta, DataReadError_Base64DataInvalid);

    test_read_fail(_testCtx, reg, string_lit("null"), meta, DataReadError_MismatchedType);
  }

  it("can read a pointer") {
    const DataMeta meta = data_meta_t(data_prim_t(u32), .container = DataContainer_Pointer);

    u32* val;
    test_read_success(_testCtx, reg, string_lit("42"), meta, mem_var(val));
    check_eq_int(*val, 42);
    alloc_free_t(g_allocHeap, val);

    test_read_success(_testCtx, reg, string_lit("null"), meta, mem_var(val));
    check_eq_int(val, null);

    test_read_fail(_testCtx, reg, string_lit("true"), meta, DataReadError_MismatchedType);
  }

  it("fails when a pointer value cannot be empty") {
    const DataMeta meta = data_meta_t(
        data_prim_t(u32), .container = DataContainer_Pointer, .flags = DataFlags_NotEmpty);

    test_read_fail(_testCtx, reg, string_lit("null"), meta, DataReadError_NullIsInvalid);
  }

  it("can read an inline-array") {
    const DataMeta meta =
        data_meta_t(data_prim_t(u32), .container = DataContainer_InlineArray, .fixedCount = 4);

    u32 val[4];

    test_read_success(_testCtx, reg, string_lit("[1, 2, 3, 4]"), meta, mem_var(val));
    check_eq_int(val[0], 1);
    check_eq_int(val[1], 2);
    check_eq_int(val[2], 3);
    check_eq_int(val[3], 4);

    test_read_fail(_testCtx, reg, string_lit("[]"), meta, DataReadError_MismatchedInlineArrayCount);
    test_read_fail(_testCtx, reg, string_lit("42"), meta, DataReadError_MismatchedType);
    test_read_fail(_testCtx, reg, string_lit("null"), meta, DataReadError_MismatchedType);
  }

  it("can read a heap-array") {
    const DataMeta meta = data_meta_t(data_prim_t(u32), .container = DataContainer_HeapArray);

    HeapArray_t(u32) val;
    test_read_success(_testCtx, reg, string_lit("[]"), meta, mem_var(val));
    check_eq_int(val.count, 0);

    test_read_success(_testCtx, reg, string_lit("[42]"), meta, mem_var(val));
    check_eq_int(val.count, 1);
    check_eq_int(val.values[0], 42);
    alloc_free_array_t(g_allocHeap, val.values, val.count);

    test_read_success(_testCtx, reg, string_lit("[1, 2, 3]"), meta, mem_var(val));
    check_eq_int(val.count, 3);
    check_eq_int(val.values[0], 1);
    check_eq_int(val.values[1], 2);
    check_eq_int(val.values[2], 3);
    alloc_free_array_t(g_allocHeap, val.values, val.count);

    test_read_fail(_testCtx, reg, string_lit("42"), meta, DataReadError_MismatchedType);
    test_read_fail(_testCtx, reg, string_lit("null"), meta, DataReadError_MismatchedType);
  }

  it("fails when an heap-array value cannot be empty") {
    const DataMeta meta = data_meta_t(
        data_prim_t(u32), .container = DataContainer_HeapArray, .flags = DataFlags_NotEmpty);

    test_read_fail(_testCtx, reg, string_lit("[]"), meta, DataReadError_EmptyArrayIsInvalid);
  }

  it("can read an dyn-array") {
    const DataMeta meta = data_meta_t(data_prim_t(u32), .container = DataContainer_DynArray);

    DynArray val;

    test_read_success(_testCtx, reg, string_lit("[]"), meta, mem_var(val));
    check_eq_int(val.size, 0);

    test_read_success(_testCtx, reg, string_lit("[42]"), meta, mem_var(val));
    check_eq_int(val.size, 1);
    check_eq_int(*dynarray_at_t(&val, 0, i32), 42);
    dynarray_destroy(&val);

    test_read_success(_testCtx, reg, string_lit("[1, 2, 3]"), meta, mem_var(val));
    check_eq_int(val.size, 3);
    check_eq_int(*dynarray_at_t(&val, 0, i32), 1);
    check_eq_int(*dynarray_at_t(&val, 1, i32), 2);
    check_eq_int(*dynarray_at_t(&val, 2, i32), 3);
    dynarray_destroy(&val);

    test_read_fail(_testCtx, reg, string_lit("42"), meta, DataReadError_MismatchedType);
    test_read_fail(_testCtx, reg, string_lit("null"), meta, DataReadError_MismatchedType);
  }

  it("can read an enum") {
    typedef enum {
      ReadJsonTestEnum_A = -42,
      ReadJsonTestEnum_B = 42,
      ReadJsonTestEnum_C = 1337,
    } ReadJsonTestEnum;

    data_reg_enum_t(reg, ReadJsonTestEnum);
    data_reg_const_t(reg, ReadJsonTestEnum, A);
    data_reg_const_t(reg, ReadJsonTestEnum, B);
    data_reg_const_t(reg, ReadJsonTestEnum, C);

    const DataMeta meta = data_meta_t(t_ReadJsonTestEnum);

    ReadJsonTestEnum val;
    test_read_success(_testCtx, reg, string_lit("\"A\""), meta, mem_var(val));
    check_eq_int(val, ReadJsonTestEnum_A);

    test_read_success(_testCtx, reg, string_lit("-42"), meta, mem_var(val));
    check_eq_int(val, ReadJsonTestEnum_A);

    test_read_success(_testCtx, reg, string_lit("\"B\""), meta, mem_var(val));
    check_eq_int(val, ReadJsonTestEnum_B);

    test_read_success(_testCtx, reg, string_lit("42"), meta, mem_var(val));
    check_eq_int(val, ReadJsonTestEnum_B);

    test_read_success(_testCtx, reg, string_lit("\"C\""), meta, mem_var(val));
    check_eq_int(val, ReadJsonTestEnum_C);

    test_read_success(_testCtx, reg, string_lit("1337"), meta, mem_var(val));
    check_eq_int(val, ReadJsonTestEnum_C);

    test_read_fail(_testCtx, reg, string_lit("\"D\""), meta, DataReadError_InvalidEnumEntry);
    test_read_fail(_testCtx, reg, string_lit("\"\""), meta, DataReadError_InvalidEnumEntry);
    test_read_fail(_testCtx, reg, string_lit("0"), meta, DataReadError_InvalidEnumEntry);
    test_read_fail(_testCtx, reg, string_lit("41"), meta, DataReadError_InvalidEnumEntry);
    test_read_fail(_testCtx, reg, string_lit("null"), meta, DataReadError_MismatchedType);
  }

  it("can read a multi enum") {
    typedef enum {
      ReadJsonTestFlags_A = 1 << 0,
      ReadJsonTestFlags_B = 1 << 1,
      ReadJsonTestFlags_C = 1 << 2,
    } ReadJsonTestFlags;

    data_reg_enum_multi_t(reg, ReadJsonTestFlags);
    data_reg_const_t(reg, ReadJsonTestFlags, A);
    data_reg_const_t(reg, ReadJsonTestFlags, B);
    data_reg_const_t(reg, ReadJsonTestFlags, C);

    const DataMeta meta = data_meta_t(t_ReadJsonTestFlags);

    ReadJsonTestFlags val;

    test_read_success(_testCtx, reg, string_lit("[]"), meta, mem_var(val));
    check_eq_int(val, 0);

    test_read_success(_testCtx, reg, string_lit("[\"A\"]"), meta, mem_var(val));
    check_eq_int(val, ReadJsonTestFlags_A);

    test_read_success(_testCtx, reg, string_lit("[\"A\", \"B\"]"), meta, mem_var(val));
    check_eq_int(val, ReadJsonTestFlags_A | ReadJsonTestFlags_B);

    test_read_success(_testCtx, reg, string_lit("[\"A\", \"B\", \"C\"]"), meta, mem_var(val));
    check_eq_int(val, ReadJsonTestFlags_A | ReadJsonTestFlags_B | ReadJsonTestFlags_C);

    test_read_success(_testCtx, reg, string_lit("[0]"), meta, mem_var(val));
    check_eq_int(val, ReadJsonTestFlags_A);

    test_read_success(_testCtx, reg, string_lit("[0, 2]"), meta, mem_var(val));
    check_eq_int(val, ReadJsonTestFlags_A | ReadJsonTestFlags_C);

    test_read_fail(_testCtx, reg, string_lit("null"), meta, DataReadError_MismatchedType);
    test_read_fail(_testCtx, reg, string_lit("\"A\""), meta, DataReadError_MismatchedType);
    test_read_fail(_testCtx, reg, string_lit("[\"D\"]"), meta, DataReadError_InvalidEnumEntry);
    test_read_fail(_testCtx, reg, string_lit("[-1]"), meta, DataReadError_InvalidEnumEntry);
    test_read_fail(_testCtx, reg, string_lit("[3]"), meta, DataReadError_InvalidEnumEntry);
    test_read_fail(
        _testCtx, reg, string_lit("[\"A\", \"A\"]"), meta, DataReadError_DuplicateEnumEntry);
    test_read_fail(_testCtx, reg, string_lit("[2, 2]"), meta, DataReadError_DuplicateEnumEntry);
  }

  it("can read a structure") {
    typedef struct {
      i32    valA;
      String valB;
      f32    valC;
      bool   valD;
    } ReadJsonTestStruct;

    data_reg_struct_t(reg, ReadJsonTestStruct);
    data_reg_field_t(reg, ReadJsonTestStruct, valA, data_prim_t(i32));
    data_reg_field_t(reg, ReadJsonTestStruct, valB, data_prim_t(String));
    data_reg_field_t(reg, ReadJsonTestStruct, valC, data_prim_t(f32));
    data_reg_field_t(reg, ReadJsonTestStruct, valD, data_prim_t(bool), .flags = DataFlags_Opt);

    const DataMeta meta = data_meta_t(t_ReadJsonTestStruct);

    ReadJsonTestStruct val;
    test_read_success(
        _testCtx,
        reg,
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
    string_free(g_allocHeap, val.valB);

    test_read_fail(_testCtx, reg, string_lit("{}"), meta, DataReadError_FieldNotFound);
    test_read_fail(
        _testCtx,
        reg,
        string_lit("{"
                   "\"valA\": -42,"
                   "\"valB\": \"Hello World\","
                   "\"valE\": 42.42"
                   "}"),
        meta,
        DataReadError_FieldNotFound);
    test_read_fail(
        _testCtx,
        reg,
        string_lit("{"
                   "\"valA\": -42,"
                   "\"valB\": \"Hello World\","
                   "\"Hello\": \"World\","
                   "\"valC\": 42.42"
                   "}"),
        meta,
        DataReadError_UnknownField);
    test_read_fail(_testCtx, reg, string_lit("null"), meta, DataReadError_MismatchedType);
  }

  it("can read a union of primitive types") {
    typedef enum {
      ReadJsonUnionTag_Int,
      ReadJsonUnionTag_Float,
      ReadJsonUnionTag_String,
      ReadJsonUnionTag_Other,
    } ReadJsonUnionTag;

    typedef struct {
      ReadJsonUnionTag tag;
      union {
        i32    data_int;
        f32    data_float;
        String data_string;
      };
    } ReadJsonUnion;

    data_reg_union_t(reg, ReadJsonUnion, tag);
    data_reg_choice_t(reg, ReadJsonUnion, ReadJsonUnionTag_Int, data_int, data_prim_t(i32));
    data_reg_choice_t(reg, ReadJsonUnion, ReadJsonUnionTag_Float, data_float, data_prim_t(f32));
    data_reg_choice_t(
        reg, ReadJsonUnion, ReadJsonUnionTag_String, data_string, data_prim_t(String));
    data_reg_choice_empty(reg, ReadJsonUnion, ReadJsonUnionTag_Other);

    const DataMeta meta = data_meta_t(t_ReadJsonUnion);

    {
      ReadJsonUnion val;
      test_read_success(
          _testCtx,
          reg,
          string_lit("{\n"
                     "  \"$type\": \"ReadJsonUnionTag_Int\",\n"
                     "  \"$data\": 42\n"
                     "}"),
          meta,
          mem_var(val));

      check_eq_int(val.tag, ReadJsonUnionTag_Int);
      check_eq_int(val.data_int, 42);
    }
    {
      ReadJsonUnion val;
      test_read_success(
          _testCtx,
          reg,
          string_lit("{\n"
                     "  \"$type\": \"ReadJsonUnionTag_String\",\n"
                     "  \"$data\": \"Hello World\"\n"
                     "}"),
          meta,
          mem_var(val));

      check_eq_int(val.tag, ReadJsonUnionTag_String);
      check_eq_string(val.data_string, string_lit("Hello World"));
      string_free(g_allocHeap, val.data_string);
    }
    {
      ReadJsonUnion val;
      test_read_success(
          _testCtx,
          reg,
          string_lit("{\n"
                     "  \"$type\": \"ReadJsonUnionTag_Other\"\n"
                     "}"),
          meta,
          mem_var(val));

      check_eq_int(val.tag, ReadJsonUnionTag_Other);
    }

    test_read_fail(_testCtx, reg, string_lit("{}"), meta, DataReadError_UnionTypeMissing);
    test_read_fail(
        _testCtx,
        reg,
        string_lit("{\n"
                   "  \"$type\": 42\n"
                   "}"),
        meta,
        DataReadError_UnionTypeInvalid);
    test_read_fail(
        _testCtx,
        reg,
        string_lit("{\n"
                   "  \"$type\": \"Hello\"\n"
                   "}"),
        meta,
        DataReadError_UnionTypeUnsupported);
    test_read_fail(
        _testCtx,
        reg,
        string_lit("{\n"
                   "  \"$type\": \"ReadJsonUnionTag_String\"\n"
                   "}"),
        meta,
        DataReadError_UnionDataMissing);
    test_read_fail(
        _testCtx,
        reg,
        string_lit("{\n"
                   "  \"$type\": \"ReadJsonUnionTag_String\",\n"
                   "  \"$data\": 42\n"
                   "}"),
        meta,
        DataReadError_UnionDataInvalid);
    test_read_fail(
        _testCtx,
        reg,
        string_lit("{\n"
                   "  \"$type\": \"ReadJsonUnionTag_Int\",\n"
                   "  \"$data\": 42,\n"
                   "  \"more\": 1337\n"
                   "}"),
        meta,
        DataReadError_UnionUnknownField);
    test_read_fail(_testCtx, reg, string_lit("null"), meta, DataReadError_MismatchedType);
  }

  it("can read a union of struct types") {
    typedef struct {
      i32    valA;
      String valB;
      f64    valC;
    } ReadJsonStruct;

    data_reg_struct_t(reg, ReadJsonStruct);
    data_reg_field_t(reg, ReadJsonStruct, valA, data_prim_t(i32));
    data_reg_field_t(reg, ReadJsonStruct, valB, data_prim_t(String));
    data_reg_field_t(reg, ReadJsonStruct, valC, data_prim_t(f64));

    typedef enum {
      ReadJsonUnionTag_A,
      ReadJsonUnionTag_B,
    } ReadJsonUnionTag;

    typedef struct {
      ReadJsonUnionTag tag;
      union {
        ReadJsonStruct data_a;
      };
    } ReadJsonUnion;

    data_reg_union_t(reg, ReadJsonUnion, tag);
    data_reg_choice_t(reg, ReadJsonUnion, ReadJsonUnionTag_A, data_a, t_ReadJsonStruct);
    data_reg_choice_empty(reg, ReadJsonUnion, ReadJsonUnionTag_B);

    const DataMeta meta = data_meta_t(t_ReadJsonUnion);

    {
      ReadJsonUnion val;
      test_read_success(
          _testCtx,
          reg,
          string_lit("{\n"
                     "  \"$type\": \"ReadJsonUnionTag_A\",\n"
                     "  \"valA\": -42,\n"
                     "  \"valB\": \"Hello World\",\n"
                     "  \"valC\": 42.42\n"
                     "}"),
          meta,
          mem_var(val));

      check_eq_int(val.tag, ReadJsonUnionTag_A);
      check_eq_int(val.data_a.valA, -42);
      check_eq_string(val.data_a.valB, string_lit("Hello World"));
      check_eq_float(val.data_a.valC, 42.42, 1e-6f);
      string_free(g_allocHeap, val.data_a.valB);
    }
    {
      ReadJsonUnion val;
      test_read_success(
          _testCtx,
          reg,
          string_lit("{\n"
                     "  \"$type\": \"ReadJsonUnionTag_B\"\n"
                     "}"),
          meta,
          mem_var(val));

      check_eq_int(val.tag, ReadJsonUnionTag_B);
    }

    test_read_fail(_testCtx, reg, string_lit("{}"), meta, DataReadError_UnionTypeMissing);
    test_read_fail(
        _testCtx,
        reg,
        string_lit("{\n"
                   "  \"$type\": \"ReadJsonUnionTag_A\",\n"
                   "  \"valA\": -42,\n"
                   "  \"valC\": 42.42\n"
                   "}"),
        meta,
        DataReadError_FieldNotFound);
    test_read_fail(
        _testCtx,
        reg,
        string_lit("{\n"
                   "  \"$type\": \"ReadJsonUnionTag_A\",\n"
                   "  \"valA\": -42,\n"
                   "  \"valB\": \"Hello World\",\n"
                   "  \"valC\": 42.42,\n"
                   "  \"valD\": 1337,\n"
                   "}"),
        meta,
        DataReadError_UnknownField);
    test_read_fail(
        _testCtx,
        reg,
        string_lit("{\n"
                   "  \"$type\": \"ReadJsonUnionTag_A\",\n"
                   "  \"$name\": \"Hello World\",\n"
                   "  \"valA\": -42,\n"
                   "  \"valB\": \"Hello World\",\n"
                   "  \"valC\": 42.42\n"
                   "}"),
        meta,
        DataReadError_UnionNameNotSupported);
  }

  it("can read a union with a name") {
    typedef enum {
      ReadJsonUnionTag_Int,
      ReadJsonUnionTag_Float,
    } ReadJsonUnionTag;

    typedef struct {
      ReadJsonUnionTag tag;
      String           name;
      union {
        i32 data_int;
        f32 data_float;
      };
    } ReadJsonUnion;

    data_reg_union_t(reg, ReadJsonUnion, tag);
    data_reg_union_name_t(reg, ReadJsonUnion, name);
    data_reg_choice_t(reg, ReadJsonUnion, ReadJsonUnionTag_Int, data_int, data_prim_t(i32));
    data_reg_choice_t(reg, ReadJsonUnion, ReadJsonUnionTag_Float, data_float, data_prim_t(f32));

    const DataMeta meta = data_meta_t(t_ReadJsonUnion);

    {
      ReadJsonUnion val;
      test_read_success(
          _testCtx,
          reg,
          string_lit("{\n"
                     "  \"$type\": \"ReadJsonUnionTag_Int\",\n"
                     "  \"$data\": 42\n"
                     "}"),
          meta,
          mem_var(val));

      check_eq_int(val.tag, ReadJsonUnionTag_Int);
      check_eq_string(val.name, string_empty);
      check_eq_int(val.data_int, 42);
    }
    {
      ReadJsonUnion val;
      test_read_success(
          _testCtx,
          reg,
          string_lit("{\n"
                     "  \"$type\": \"ReadJsonUnionTag_Int\",\n"
                     "  \"$name\": \"\",\n"
                     "  \"$data\": 42\n"
                     "}"),
          meta,
          mem_var(val));

      check_eq_int(val.tag, ReadJsonUnionTag_Int);
      check_eq_string(val.name, string_empty);
      check_eq_int(val.data_int, 42);
    }
    {
      ReadJsonUnion val;
      test_read_success(
          _testCtx,
          reg,
          string_lit("{\n"
                     "  \"$type\": \"ReadJsonUnionTag_Int\",\n"
                     "  \"$name\": \"Hello World\",\n"
                     "  \"$data\": 42\n"
                     "}"),
          meta,
          mem_var(val));

      check_eq_int(val.tag, ReadJsonUnionTag_Int);
      check_eq_string(val.name, string_lit("Hello World"));
      check_eq_int(val.data_int, 42);
      string_free(g_allocHeap, val.name);
    }

    test_read_fail(
        _testCtx,
        reg,
        string_lit("{\n"
                   "  \"$type\": \"ReadJsonUnionTag_Int\",\n"
                   "  \"$name\": 42,\n"
                   "  \"$data\": 42\n"
                   "}"),
        meta,
        DataReadError_UnionInvalidName);
  }

  it("will invoke a normalizer if registered") {
    typedef enum {
      ReadJsonTestEnum_A = -42,
      ReadJsonTestEnum_B = 42,
      ReadJsonTestEnum_C = 1337,
    } ReadJsonTestEnum;

    data_reg_enum_t(reg, ReadJsonTestEnum);
    data_reg_const_t(reg, ReadJsonTestEnum, A);
    data_reg_const_t(reg, ReadJsonTestEnum, B);
    data_reg_const_t(reg, ReadJsonTestEnum, C);
    data_reg_normalizer_t(reg, ReadJsonTestEnum, test_normalizer_enum);

    const DataMeta meta = data_meta_t(t_ReadJsonTestEnum);

    ReadJsonTestEnum val;
    test_read_success(_testCtx, reg, string_lit("\"A\""), meta, mem_var(val));
    check_eq_int(val, ReadJsonTestEnum_B);

    test_read_success(_testCtx, reg, string_lit("\"B\""), meta, mem_var(val));
    check_eq_int(val, ReadJsonTestEnum_B);

    test_read_success(_testCtx, reg, string_lit("\"C\""), meta, mem_var(val));
    check_eq_int(val, ReadJsonTestEnum_C);
  }

  it("can read opaque types") {
    typedef struct {
      ALIGNAS(16)
      u8 data[16];
    } OpaqueStruct;

    data_reg_opaque_t(reg, OpaqueStruct);

    const DataMeta meta = data_meta_t(t_OpaqueStruct);

    {
      const OpaqueStruct ref = {0};
      OpaqueStruct       val;
      test_read_success(
          _testCtx, reg, string_lit("\"AAAAAAAAAAAAAAAAAAAAAA==\""), meta, mem_var(val));

      check(mem_eq(mem_var(val), mem_var(ref)));
    }
    {
      const OpaqueStruct ref = {.data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}};
      OpaqueStruct       val;
      test_read_success(
          _testCtx, reg, string_lit("\"AQIDBAUGBwgJCgsMDQ4PEA==\""), meta, mem_var(val));

      check(mem_eq(mem_var(val), mem_var(ref)));
    }

    test_read_fail(_testCtx, reg, string_empty, meta, DataReadError_Malformed);
    test_read_fail(_testCtx, reg, string_lit("\"\""), meta, DataReadError_Base64DataInvalid);
    test_read_fail(
        _testCtx,
        reg,
        string_lit("\"AAAAAAAAAAAAAAAAAAAAAA=\""),
        meta,
        DataReadError_Base64DataInvalid);
  }

  teardown() { data_reg_destroy(reg); }
}
