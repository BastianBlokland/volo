#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_dynstring.h"
#include "core_float.h"
#include "data_read.h"
#include "data_utils.h"
#include "data_write.h"

static void test_bin_roundtrip(
    CheckTestContext* _testCtx, const DataReg* reg, const DataMeta meta, const Mem data) {
  Mem       writeBuffer = mem_stack(usize_kibibyte * 16);
  DynString writeStr    = dynstring_create_over(writeBuffer);
  data_write_bin(reg, &writeStr, meta, data);

  const String writeResult = dynstring_view(&writeStr);

  DataReadResult readRes;

  DataBinHeader dataHeader;
  data_read_bin_header(writeResult, &dataHeader, &readRes);
  if (readRes.error != DataReadError_None) {
    check_error("Roundtrip read header failed: {}", fmt_text(readRes.errorMsg));
  }
  check_eq_int(dataHeader.checksum, data_read_bin_checksum(writeResult));

  Mem          readData = mem_stack(data.size);
  const String readRem  = data_read_bin(reg, writeResult, g_allocHeap, meta, readData, &readRes);
  if (readRes.error != DataReadError_None) {
    check_error("Roundtrip read failed: {}", fmt_text(readRes.errorMsg));
  }
  check_eq_string(readRem, string_empty);
  check(data_equal(reg, meta, data, readData));

  data_destroy(reg, g_allocHeap, meta, readData);
}

spec(bin) {

  DataReg* reg = null;

  setup() { reg = data_reg_create(g_allocHeap); }

  it("can serialize a boolean") {
    const DataMeta meta = data_meta_t(data_prim_t(bool));

    const bool val1 = true;
    test_bin_roundtrip(_testCtx, reg, meta, mem_var(val1));

    const bool val2 = false;
    test_bin_roundtrip(_testCtx, reg, meta, mem_var(val2));
  }

  it("can serialize a number") {
#define X(_T_)                                                                                     \
  const DataMeta meta_##_T_ = data_meta_t(data_prim_t(_T_));                                       \
  _T_            val_##_T_  = 42;                                                                  \
  test_bin_roundtrip(_testCtx, reg, meta_##_T_, mem_var(val_##_T_));

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
    f16            valF16  = float_f32_to_f16(42.0f);
    test_bin_roundtrip(_testCtx, reg, metaF16, mem_var(valF16));
  }

  it("can serialize a string") {
    const DataMeta meta = data_meta_t(data_prim_t(String));

    const String val1 = string_lit("Hello World");
    test_bin_roundtrip(_testCtx, reg, meta, mem_var(val1));

    const String val2 = string_empty;
    test_bin_roundtrip(_testCtx, reg, meta, mem_var(val2));
  }

  it("can serialize a string-hash") {
    const DataMeta meta = data_meta_t(data_prim_t(StringHash));

    const StringHash val1 = string_hash_lit("Hello World");
    test_bin_roundtrip(_testCtx, reg, meta, mem_var(val1));

    const StringHash val2 = 0;
    test_bin_roundtrip(_testCtx, reg, meta, mem_var(val2));
  }

  it("can serialize memory") {
    const DataMeta meta = data_meta_t(data_prim_t(DataMem));

    const DataMem val1 = data_mem_create(string_lit("Hello World"));
    test_bin_roundtrip(_testCtx, reg, meta, mem_var(val1));

    const DataMem val2 = data_mem_create(mem_empty);
    test_bin_roundtrip(_testCtx, reg, meta, mem_var(val2));
  }

  it("can serialize external memory") {
    const DataMeta meta = data_meta_t(data_prim_t(DataMem), .flags = DataFlags_ExternalMemory);

    const DataMem val1 = data_mem_create(string_lit("Hello World"));
    test_bin_roundtrip(_testCtx, reg, meta, mem_var(val1));

    const DataMem val2 = data_mem_create(mem_empty);
    test_bin_roundtrip(_testCtx, reg, meta, mem_var(val2));
  }

  it("can serialize a pointer") {
    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_Pointer);

    i32  target = 42;
    i32* val1   = &target;
    test_bin_roundtrip(_testCtx, reg, meta, mem_var(val1));

    i32* val2 = null;
    test_bin_roundtrip(_testCtx, reg, meta, mem_var(val2));
  }

  it("can serialize an inline-array") {
    const DataMeta meta =
        data_meta_t(data_prim_t(i32), .container = DataContainer_InlineArray, .fixedCount = 8);

    i32 values[] = {1, 2, 3, 4, 5, 6, 7, 8};
    test_bin_roundtrip(_testCtx, reg, meta, mem_var(values));
  }

  it("can serialize a heap-array") {
    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_HeapArray);

    i32 values[]            = {1, 2, 3, 4, 5, 6, 7};
    HeapArray_t(i32) array1 = {.values = values, .count = array_elems(values)};
    test_bin_roundtrip(_testCtx, reg, meta, mem_var(array1));

    HeapArray_t(i32) array2 = {0};
    test_bin_roundtrip(_testCtx, reg, meta, mem_var(array2));
  }

  it("can serialize a dynarray") {
    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_DynArray);

    DynArray arr                = dynarray_create_t(g_allocHeap, i32, 4);
    *dynarray_push_t(&arr, i32) = 1;
    *dynarray_push_t(&arr, i32) = 2;
    *dynarray_push_t(&arr, i32) = 3;
    *dynarray_push_t(&arr, i32) = 4;

    test_bin_roundtrip(_testCtx, reg, meta, mem_var(arr));

    dynarray_clear(&arr);

    test_bin_roundtrip(_testCtx, reg, meta, mem_var(arr));

    dynarray_destroy(&arr);
  }

  it("can serialize an enum") {
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
    test_bin_roundtrip(_testCtx, reg, meta, mem_var(val1));

    WriteJsonTestEnum val2 = WriteJsonTestEnum_B;
    test_bin_roundtrip(_testCtx, reg, meta, mem_var(val2));

    WriteJsonTestEnum val3 = WriteJsonTestEnum_C;
    test_bin_roundtrip(_testCtx, reg, meta, mem_var(val3));

    WriteJsonTestEnum val4 = 41;
    test_bin_roundtrip(_testCtx, reg, meta, mem_var(val4));
  }

  it("can serialize a structure") {
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
    test_bin_roundtrip(_testCtx, reg, data_meta_t(t_WriteJsonTestStruct), mem_var(val));
  }

  it("can serialize a union of primitive types") {
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
      test_bin_roundtrip(_testCtx, reg, data_meta_t(t_WriteJsonUnion), mem_var(val));
    }
    {
      const WriteJsonUnion val = {
          .tag         = WriteJsonUnionTag_String,
          .data_string = string_lit("Hello World"),
      };
      test_bin_roundtrip(_testCtx, reg, data_meta_t(t_WriteJsonUnion), mem_var(val));
    }
    {
      const WriteJsonUnion val = {
          .tag = WriteJsonUnionTag_Other,
      };
      test_bin_roundtrip(_testCtx, reg, data_meta_t(t_WriteJsonUnion), mem_var(val));
    }
  }

  it("can serialize a union of struct types") {
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
      test_bin_roundtrip(_testCtx, reg, data_meta_t(t_WriteJsonUnion), mem_var(val));
    }
    {
      const WriteJsonUnion val = {
          .tag = WriteJsonUnionTag_B,
      };
      test_bin_roundtrip(_testCtx, reg, data_meta_t(t_WriteJsonUnion), mem_var(val));
    }
  }

  it("can serialize a union with a name") {
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
    test_bin_roundtrip(_testCtx, reg, data_meta_t(t_WriteJsonUnion), mem_var(val));
  }

  it("can serialize opaque types") {
    typedef struct {
      ALIGNAS(16)
      u8 data[16];
    } OpaqueStruct;

    data_reg_opaque_t(reg, OpaqueStruct);

    const OpaqueStruct val = {.data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}};

    test_bin_roundtrip(_testCtx, reg, data_meta_t(t_OpaqueStruct), mem_var(val));
  }

  it("can read the binary header") {
    const DataMeta meta = data_meta_t(data_prim_t(bool), .flags = DataFlags_Opt);

    const bool val = true;

    Mem       writeBuffer = mem_stack(usize_kibibyte * 16);
    DynString writeStr    = dynstring_create_over(writeBuffer);
    data_write_bin(reg, &writeStr, meta, mem_var(val));

    DataBinHeader  header;
    DataReadResult headerRes;
    const String   data = data_read_bin_header(dynstring_view(&writeStr), &header, &headerRes);

    check_eq_int(data.size, sizeof(bool));

    check_require(!headerRes.error);
    check_eq_int(header.metaTypeNameHash, data_name_hash(reg, meta.type));
    check_eq_int(header.metaFormatHash, data_hash(reg, meta, DataHashFlags_ExcludeIds));
    check_eq_int(header.metaContainer, DataContainer_None);
    check_eq_int(header.metaFlags, DataFlags_Opt);
  }

  teardown() { data_reg_destroy(reg); }
}
