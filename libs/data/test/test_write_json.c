#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "data.h"

static void test_write(
    CheckTestContext* _testCtx,
    const DataReg*    reg,
    const DataMeta    meta,
    Mem               data,
    const String      expected) {

  Mem       buffer    = mem_stack(1024);
  DynString dynString = dynstring_create_over(buffer);
  data_write_json(reg, &dynString, meta, data);

  check_eq_string(dynstring_view(&dynString), expected);
}

spec(write_json) {

  DataReg* reg = null;

  setup() { reg = data_reg_create(g_alloc_heap); }

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
  }

  it("can write a string") {
    const DataMeta meta = data_meta_t(data_prim_t(String));

    const String val1 = string_lit("Hello World");
    test_write(_testCtx, reg, meta, mem_var(val1), string_lit("\"Hello World\""));

    const String val2 = string_empty;
    test_write(_testCtx, reg, meta, mem_var(val2), string_lit("\"\""));
  }

  it("can write a pointer") {
    const DataMeta meta = data_meta_t(data_prim_t(u32), .container = DataContainer_Pointer);

    i32  target = 42;
    i32* val1   = &target;
    test_write(_testCtx, reg, meta, mem_var(val1), string_lit("42"));

    i32* val2 = null;
    test_write(_testCtx, reg, meta, mem_var(val2), string_lit("null"));
  }

  it("can write an array") {
    const DataMeta meta = data_meta_t(data_prim_t(u32), .container = DataContainer_Array);

    i32             values[] = {1, 2, 3, 4, 5, 6, 7};
    const DataArray array1   = {.values = values, .count = array_elems(values)};
    test_write(
        _testCtx,
        reg,
        meta,
        mem_var(array1),
        string_lit("[\n  1,\n  2,\n  3,\n  4,\n  5,\n  6,\n  7\n]"));

    const DataArray array2 = {0};
    test_write(_testCtx, reg, meta, mem_var(array2), string_lit("[]"));
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

  teardown() { data_reg_destroy(reg); }
}
