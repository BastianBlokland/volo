#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_dynstring.h"
#include "core_float.h"
#include "core_math.h"
#include "core_stringtable.h"
#include "core_time.h"
#include "data_registry.h"
#include "data_write.h"

static void test_write(
    CheckTestContext* _testCtx,
    const DataReg*    reg,
    const DataMeta    meta,
    Mem               data,
    const String      expected) {

  Mem       buffer    = mem_stack(1024);
  DynString dynString = dynstring_create_over(buffer);
  data_write_json(reg, &dynString, meta, data, &data_write_json_opts());

  check_eq_string(dynstring_view(&dynString), expected);
}

spec(write_json) {

  DataReg* reg = null;

  setup() { reg = data_reg_create(g_allocHeap); }

  it("can write a boolean") {
    const DataMeta meta = data_meta_t(data_prim_t(bool));

    const bool val1 = true;
    test_write(_testCtx, reg, meta, mem_var(val1), string_lit("true"));

    const bool val2 = false;
    test_write(_testCtx, reg, meta, mem_var(val2), string_lit("false"));
  }

  it("can write a number") {
#define X(_T_)                                                                                     \
  const DataMeta meta_##_T_ = data_meta_t(data_prim_t(_T_));                                       \
  _T_            val_##_T_  = 42;                                                                  \
  test_write(_testCtx, reg, meta_##_T_, mem_var(val_##_T_), string_lit("42"));

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
    f16            valF16  = float_f32_to_f16(42);
    test_write(_testCtx, reg, metaF16, mem_var(valF16), string_lit("42"));
  }

  it("can write a duration") {
    const DataMeta     meta = data_meta_t(data_prim_t(TimeDuration));
    const TimeDuration val  = time_seconds(42);
    test_write(_testCtx, reg, meta, mem_var(val), string_lit("42"));
  }

  it("can write an angle") {
    const DataMeta meta = data_meta_t(data_prim_t(Angle));
    const Angle    val  = math_pi_f32;
    test_write(_testCtx, reg, meta, mem_var(val), string_lit("180"));
  }

  it("can write numbers with a configurable amount of digits after the decimal point") {
    const DataMeta meta = data_meta_t(data_prim_t(f64));
    const f64      val  = 42.12345678987654321;

    static const struct {
      u8     numberMaxDecDigits;
      String expectedOutput;
    } g_testData[] = {
        {.numberMaxDecDigits = 0, .expectedOutput = string_static("42")},
        {.numberMaxDecDigits = 1, .expectedOutput = string_static("42.1")},
        {.numberMaxDecDigits = 2, .expectedOutput = string_static("42.12")},
        {.numberMaxDecDigits = 3, .expectedOutput = string_static("42.123")},
        {.numberMaxDecDigits = 10, .expectedOutput = string_static("42.1234567899")},
        {.numberMaxDecDigits = 15, .expectedOutput = string_static("42.123456789876542")},
    };

    Mem       buffer    = mem_stack(1024);
    DynString dynString = dynstring_create_over(buffer);
    for (u32 i = 0; i != array_elems(g_testData); ++i) {
      dynstring_clear(&dynString);

      data_write_json(
          reg,
          &dynString,
          meta,
          mem_var(val),
          &data_write_json_opts(.numberMaxDecDigits = g_testData[i].numberMaxDecDigits));

      check_eq_string(dynstring_view(&dynString), g_testData[i].expectedOutput);
    }
  }

  it("can write a string") {
    const DataMeta meta = data_meta_t(data_prim_t(String));

    const String val1 = string_lit("Hello World");
    test_write(_testCtx, reg, meta, mem_var(val1), string_lit("\"Hello World\""));

    const String val2 = string_empty;
    test_write(_testCtx, reg, meta, mem_var(val2), string_lit("\"\""));
  }

  it("can write a string-hash") {
    const DataMeta meta = data_meta_t(data_prim_t(StringHash));

    const StringHash val1 = stringtable_add(g_stringtable, string_lit("Hello World"));
    test_write(_testCtx, reg, meta, mem_var(val1), string_lit("\"Hello World\""));

    const StringHash val2 = string_hash_lit("Unknown test string 42");
    test_write(_testCtx, reg, meta, mem_var(val2), fmt_write_scratch("{}", fmt_int(val2)));

    const StringHash val3 = 0;
    test_write(_testCtx, reg, meta, mem_var(val3), string_lit("\"\""));
  }

  it("can write memory as base64") {
    const DataMeta meta = data_meta_t(data_prim_t(DataMem));

    const DataMem val1 = data_mem_create(string_lit("Hello World"));
    test_write(_testCtx, reg, meta, mem_var(val1), string_lit("\"SGVsbG8gV29ybGQ=\""));

    const DataMem val2 = data_mem_create(mem_empty);
    test_write(_testCtx, reg, meta, mem_var(val2), string_lit("\"\""));
  }

  it("can write a pointer") {
    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_Pointer);

    i32  target = 42;
    i32* val1   = &target;
    test_write(_testCtx, reg, meta, mem_var(val1), string_lit("42"));

    i32* val2 = null;
    test_write(_testCtx, reg, meta, mem_var(val2), string_lit("null"));
  }

  it("can write an inline-array") {
    const DataMeta meta =
        data_meta_t(data_prim_t(i32), .container = DataContainer_InlineArray, .fixedCount = 8);

    i32 val[] = {1, 2, 3, 4, 5, 6, 7, 8};
    test_write(
        _testCtx,
        reg,
        meta,
        mem_var(val),
        string_lit("[\n  1,\n  2,\n  3,\n  4,\n  5,\n  6,\n  7,\n  8\n]"));
  }

  it("can write a heap-array") {
    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_HeapArray);

    i32 values[]                  = {1, 2, 3, 4, 5, 6, 7};
    const HeapArray_t(i32) array1 = {.values = values, .count = array_elems(values)};
    test_write(
        _testCtx,
        reg,
        meta,
        mem_var(array1),
        string_lit("[\n  1,\n  2,\n  3,\n  4,\n  5,\n  6,\n  7\n]"));

    const HeapArray_t(i32) array2 = {0};
    test_write(_testCtx, reg, meta, mem_var(array2), string_lit("[]"));
  }

  it("can write a dynarray") {
    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_DynArray);

    DynArray arr                = dynarray_create_t(g_allocHeap, i32, 4);
    *dynarray_push_t(&arr, i32) = 1;
    *dynarray_push_t(&arr, i32) = 2;
    *dynarray_push_t(&arr, i32) = 3;
    *dynarray_push_t(&arr, i32) = 4;

    test_write(_testCtx, reg, meta, mem_var(arr), string_lit("[\n  1,\n  2,\n  3,\n  4\n]"));

    dynarray_clear(&arr);

    test_write(_testCtx, reg, meta, mem_var(arr), string_lit("[]"));

    dynarray_destroy(&arr);
  }

  it("can write an enum") {
    typedef enum {
      WriteJsonTestEnum_A = -42,
      WriteJsonTestEnum_B = 42,
      WriteJsonTestEnum_C = 1337,
    } WriteJsonTestEnum;

    data_reg_enum_t(reg, WriteJsonTestEnum);
    data_reg_const_t(reg, WriteJsonTestEnum, A);
    data_reg_const_t(reg, WriteJsonTestEnum, B);
    data_reg_const_t(reg, WriteJsonTestEnum, C);

    const DataMeta meta = data_meta_t(t_WriteJsonTestEnum);

    WriteJsonTestEnum val1 = WriteJsonTestEnum_A;
    test_write(_testCtx, reg, meta, mem_var(val1), string_lit("\"A\""));

    WriteJsonTestEnum val2 = WriteJsonTestEnum_B;
    test_write(_testCtx, reg, meta, mem_var(val2), string_lit("\"B\""));

    WriteJsonTestEnum val3 = WriteJsonTestEnum_C;
    test_write(_testCtx, reg, meta, mem_var(val3), string_lit("\"C\""));

    WriteJsonTestEnum val4 = 41;
    test_write(_testCtx, reg, meta, mem_var(val4), string_lit("41"));
  }

  it("can write a multi enum") {
    typedef enum {
      WriteJsonTestFlags_None = 0,
      WriteJsonTestFlags_A    = 1 << 0,
      WriteJsonTestFlags_B    = 1 << 1,
      WriteJsonTestFlags_C    = 1 << 2,
    } WriteJsonTestFlags;

    data_reg_enum_multi_t(reg, WriteJsonTestFlags);
    data_reg_const_t(reg, WriteJsonTestFlags, A);
    data_reg_const_t(reg, WriteJsonTestFlags, B);
    data_reg_const_t(reg, WriteJsonTestFlags, C);

    const DataMeta meta = data_meta_t(t_WriteJsonTestFlags);

    WriteJsonTestFlags val1 = WriteJsonTestFlags_None;
    test_write(_testCtx, reg, meta, mem_var(val1), string_lit("[]"));

    WriteJsonTestFlags val2 = WriteJsonTestFlags_A;
    test_write(
        _testCtx,
        reg,
        meta,
        mem_var(val2),
        string_lit("[\n"
                   "  \"A\"\n"
                   "]"));

    WriteJsonTestFlags val3 = WriteJsonTestFlags_A | WriteJsonTestFlags_B;
    test_write(
        _testCtx,
        reg,
        meta,
        mem_var(val3),
        string_lit("[\n"
                   "  \"A\",\n"
                   "  \"B\"\n"
                   "]"));

    WriteJsonTestFlags val4 = WriteJsonTestFlags_A | WriteJsonTestFlags_B | WriteJsonTestFlags_C;
    test_write(
        _testCtx,
        reg,
        meta,
        mem_var(val4),
        string_lit("[\n"
                   "  \"A\",\n"
                   "  \"B\",\n"
                   "  \"C\"\n"
                   "]"));

    WriteJsonTestFlags val5 = 1 << 3;
    test_write(
        _testCtx,
        reg,
        meta,
        mem_var(val5),
        string_lit("[\n"
                   "  3\n"
                   "]"));
  }

  it("can write a structure") {
    typedef struct {
      i32    valA;
      String valB;
      f64    valC;
    } WriteJsonTestStruct;

    data_reg_struct_t(reg, WriteJsonTestStruct);
    data_reg_field_t(reg, WriteJsonTestStruct, valA, data_prim_t(i32));
    data_reg_field_t(reg, WriteJsonTestStruct, valB, data_prim_t(String));
    data_reg_field_t(reg, WriteJsonTestStruct, valC, data_prim_t(f64));

    const WriteJsonTestStruct val = {
        .valA = -42,
        .valB = string_lit("Hello World"),
        .valC = 42.42,
    };
    test_write(
        _testCtx,
        reg,
        data_meta_t(t_WriteJsonTestStruct),
        mem_var(val),
        string_lit("{\n"
                   "  \"valA\": -42,\n"
                   "  \"valB\": \"Hello World\",\n"
                   "  \"valC\": 42.42\n"
                   "}"));
  }

  it("skips default values in a structure") {
    typedef struct {
      i32    valA;
      String valB;
      bool   valC;
    } WriteJsonTestStruct;

    data_reg_struct_t(reg, WriteJsonTestStruct);
    data_reg_field_t(reg, WriteJsonTestStruct, valA, data_prim_t(i32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, WriteJsonTestStruct, valB, data_prim_t(String), .flags = DataFlags_Opt);
    data_reg_field_t(reg, WriteJsonTestStruct, valC, data_prim_t(bool), .flags = DataFlags_Opt);

    const WriteJsonTestStruct val = {
        .valA = 0,
        .valB = string_lit(""),
        .valC = false,
    };
    test_write(_testCtx, reg, data_meta_t(t_WriteJsonTestStruct), mem_var(val), string_lit("{}"));
  }

  it("can write a union of primitive types") {
    typedef enum {
      WriteJsonUnionTag_Int,
      WriteJsonUnionTag_Float,
      WriteJsonUnionTag_String,
      WriteJsonUnionTag_Other,
    } WriteJsonUnionTag;

    typedef struct {
      WriteJsonUnionTag tag;
      union {
        i32    data_int;
        f32    data_float;
        String data_string;
      };
    } WriteJsonUnion;

    data_reg_union_t(reg, WriteJsonUnion, tag);
    data_reg_choice_t(reg, WriteJsonUnion, WriteJsonUnionTag_Int, data_int, data_prim_t(i32));
    data_reg_choice_t(reg, WriteJsonUnion, WriteJsonUnionTag_Float, data_float, data_prim_t(f32));
    data_reg_choice_t(
        reg, WriteJsonUnion, WriteJsonUnionTag_String, data_string, data_prim_t(String));
    data_reg_choice_empty(reg, WriteJsonUnion, WriteJsonUnionTag_Other);

    {
      const WriteJsonUnion val = {
          .tag      = WriteJsonUnionTag_Int,
          .data_int = 42,
      };
      test_write(
          _testCtx,
          reg,
          data_meta_t(t_WriteJsonUnion),
          mem_var(val),
          string_lit("{\n"
                     "  \"$type\": \"WriteJsonUnionTag_Int\",\n"
                     "  \"$data\": 42\n"
                     "}"));
    }
    {
      const WriteJsonUnion val = {
          .tag         = WriteJsonUnionTag_String,
          .data_string = string_lit("Hello World"),
      };
      test_write(
          _testCtx,
          reg,
          data_meta_t(t_WriteJsonUnion),
          mem_var(val),
          string_lit("{\n"
                     "  \"$type\": \"WriteJsonUnionTag_String\",\n"
                     "  \"$data\": \"Hello World\"\n"
                     "}"));
    }
    {
      const WriteJsonUnion val = {
          .tag = WriteJsonUnionTag_Other,
      };
      test_write(
          _testCtx,
          reg,
          data_meta_t(t_WriteJsonUnion),
          mem_var(val),
          string_lit("{\n"
                     "  \"$type\": \"WriteJsonUnionTag_Other\"\n"
                     "}"));
    }
  }

  it("can write a union of struct types") {
    typedef struct {
      i32    valA;
      String valB;
      f64    valC;
    } WriteJsonStruct;

    data_reg_struct_t(reg, WriteJsonStruct);
    data_reg_field_t(reg, WriteJsonStruct, valA, data_prim_t(i32));
    data_reg_field_t(reg, WriteJsonStruct, valB, data_prim_t(String));
    data_reg_field_t(reg, WriteJsonStruct, valC, data_prim_t(f64));

    typedef enum {
      WriteJsonUnionTag_A,
      WriteJsonUnionTag_B,
    } WriteJsonUnionTag;

    typedef struct {
      WriteJsonUnionTag tag;
      union {
        WriteJsonStruct data_a;
      };
    } WriteJsonUnion;

    data_reg_union_t(reg, WriteJsonUnion, tag);
    data_reg_choice_t(reg, WriteJsonUnion, WriteJsonUnionTag_A, data_a, t_WriteJsonStruct);
    data_reg_choice_empty(reg, WriteJsonUnion, WriteJsonUnionTag_B);

    {
      const WriteJsonUnion val = {
          .tag = WriteJsonUnionTag_A,
          .data_a =
              {
                  .valA = -42,
                  .valB = string_lit("Hello World"),
                  .valC = 42.42,
              },
      };
      test_write(
          _testCtx,
          reg,
          data_meta_t(t_WriteJsonUnion),
          mem_var(val),
          string_lit("{\n"
                     "  \"$type\": \"WriteJsonUnionTag_A\",\n"
                     "  \"valA\": -42,\n"
                     "  \"valB\": \"Hello World\",\n"
                     "  \"valC\": 42.42\n"
                     "}"));
    }
    {
      const WriteJsonUnion val = {
          .tag = WriteJsonUnionTag_B,
      };
      test_write(
          _testCtx,
          reg,
          data_meta_t(t_WriteJsonUnion),
          mem_var(val),
          string_lit("{\n"
                     "  \"$type\": \"WriteJsonUnionTag_B\"\n"
                     "}"));
    }
  }

  it("can write a union with a name") {
    typedef enum {
      WriteJsonUnionTag_Int,
      WriteJsonUnionTag_Float,
    } WriteJsonUnionTag;

    typedef struct {
      WriteJsonUnionTag tag;
      String            name;
      union {
        i32 data_int;
        f32 data_float;
      };
    } WriteJsonUnion;

    data_reg_union_t(reg, WriteJsonUnion, tag);
    data_reg_union_name_t(reg, WriteJsonUnion, name);
    data_reg_choice_t(reg, WriteJsonUnion, WriteJsonUnionTag_Int, data_int, data_prim_t(i32));
    data_reg_choice_t(reg, WriteJsonUnion, WriteJsonUnionTag_Float, data_float, data_prim_t(f32));

    const WriteJsonUnion val = {
        .tag      = WriteJsonUnionTag_Int,
        .name     = string_lit("Hello World"),
        .data_int = 42,
    };
    test_write(
        _testCtx,
        reg,
        data_meta_t(t_WriteJsonUnion),
        mem_var(val),
        string_lit("{\n"
                   "  \"$type\": \"WriteJsonUnionTag_Int\",\n"
                   "  \"$name\": \"Hello World\",\n"
                   "  \"$data\": 42\n"
                   "}"));
  }

  it("can write opaque types") {
    typedef struct {
      ALIGNAS(16)
      u8 data[16];
    } OpaqueStruct;

    data_reg_opaque_t(reg, OpaqueStruct);

    const OpaqueStruct val1 = {.data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}};
    const OpaqueStruct val2 = {0};

    test_write(
        _testCtx,
        reg,
        data_meta_t(t_OpaqueStruct),
        mem_var(val1),
        string_lit("\"AQIDBAUGBwgJCgsMDQ4PEA==\""));

    test_write(
        _testCtx,
        reg,
        data_meta_t(t_OpaqueStruct),
        mem_var(val2),
        string_lit("\"AAAAAAAAAAAAAAAAAAAAAA==\""));
  }

  teardown() { data_reg_destroy(reg); }
}
