#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "data.h"

spec(utils_equal) {

  DataReg* reg = null;

  setup() { reg = data_reg_create(g_allocHeap); }

  it("can compare strings") {
    const String a = string_lit("Hello World");
    const String b = string_lit("Hello World2");
    const String c = string_empty;

    const DataMeta meta = data_meta_t(data_prim_t(String));
    check(data_equal(reg, meta, mem_var(a), mem_var(a)));
    check(data_equal(reg, meta, mem_var(c), mem_var(c)));
    check(!data_equal(reg, meta, mem_var(a), mem_var(b)));
  }

  it("can compare raw memory") {
    const Mem a = string_lit("Hello World");
    const Mem b = string_lit("Hello World2");
    const Mem c = string_empty;

    const DataMeta meta = data_meta_t(data_prim_t(Mem));
    check(data_equal(reg, meta, mem_var(a), mem_var(a)));
    check(data_equal(reg, meta, mem_var(c), mem_var(c)));
    check(!data_equal(reg, meta, mem_var(a), mem_var(b)));
  }

  it("can compare primitive pointers") {
    i32  a    = 42;
    i32* aPtr = &a;

    i32  b    = 1337;
    i32* bPtr = &b;

    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_Pointer);
    check(data_equal(reg, meta, mem_var(aPtr), mem_var(aPtr)));
    check(!data_equal(reg, meta, mem_var(aPtr), mem_var(bPtr)));
  }

  it("can compare arrays of primitives") {
    typedef struct {
      i32*  values;
      usize count;
    } PrimArray;

    i32 valuesA[] = {0, 1, 2, 3, 4, 5, 6, 7};
    i32 valuesB[] = {0, 1, 3, 2, 4, 5, 6, 7};

    const PrimArray arrayA = {.values = valuesA, .count = array_elems(valuesA)};
    const PrimArray arrayB = {.values = valuesB, .count = array_elems(valuesB)};

    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_Array);
    check(data_equal(reg, meta, mem_var(arrayA), mem_var(arrayA)));
    check(!data_equal(reg, meta, mem_var(arrayA), mem_var(arrayB)));
  }

  it("can compare empty arrays") {
    typedef struct {
      i32*  values;
      usize count;
    } PrimArray;

    const PrimArray arrayA = {0};
    const PrimArray arrayB = {0};

    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_Array);
    check(data_equal(reg, meta, mem_var(arrayA), mem_var(arrayB)));
  }

  it("can compare structures") {
    typedef struct {
      String a, b, c;
    } CloneStructA;

    data_reg_struct_t(reg, CloneStructA);
    data_reg_field_t(reg, CloneStructA, a, data_prim_t(String));
    data_reg_field_t(reg, CloneStructA, b, data_prim_t(String));
    data_reg_field_t(reg, CloneStructA, c, data_prim_t(String));

    const CloneStructA structA = {
        .a = string_lit("Hello"),
        .c = string_lit("World"),
    };
    const CloneStructA structB = {
        .a = string_lit("Hello"),
        .b = string_lit("World"),
    };

    check(data_equal(reg, data_meta_t(t_CloneStructA), mem_var(structA), mem_var(structA)));
    check(!data_equal(reg, data_meta_t(t_CloneStructA), mem_var(structA), mem_var(structB)));
  }

  it("can compare nested structures") {
    typedef struct {
      String a, b, c;
    } CloneStructB;

    typedef struct {
      CloneStructB  value;
      CloneStructB* ptr;
      struct {
        CloneStructB* values;
        usize         count;
      } array;
    } CloneStructC;

    data_reg_struct_t(reg, CloneStructB);
    data_reg_field_t(reg, CloneStructB, a, data_prim_t(String));
    data_reg_field_t(reg, CloneStructB, b, data_prim_t(String));
    data_reg_field_t(reg, CloneStructB, c, data_prim_t(String));

    data_reg_struct_t(reg, CloneStructC);
    data_reg_field_t(reg, CloneStructC, value, t_CloneStructB);
    data_reg_field_t(reg, CloneStructC, ptr, t_CloneStructB, .container = DataContainer_Pointer);
    data_reg_field_t(reg, CloneStructC, array, t_CloneStructB, .container = DataContainer_Array);

    CloneStructB ptrValueA = {
        .a = string_lit("Some"),
        .b = string_lit("New"),
        .c = string_lit("Values"),
    };

    CloneStructB ptrValueB = {
        .a = string_lit("Some"),
        .b = string_lit("Different"),
        .c = string_lit("Values"),
    };

    CloneStructB arrayValuesA[] = {
        {.a = string_lit("Hello")},
        {.a = string_lit("Beautiful")},
        {.a = string_lit("World")},
    };

    const CloneStructC structA = {
        .value = {.a = string_lit("Hello"), .c = string_lit("World")},
        .ptr   = &ptrValueA,
        .array = {.values = arrayValuesA, .count = array_elems(arrayValuesA)},
    };

    const CloneStructC structB = {
        .value = {.a = string_lit("Hello"), .c = string_lit("World")},
        .ptr   = &ptrValueB,
        .array = {.values = arrayValuesA, .count = array_elems(arrayValuesA)},
    };

    check(data_equal(reg, data_meta_t(t_CloneStructC), mem_var(structA), mem_var(structA)));
    check(!data_equal(reg, data_meta_t(t_CloneStructC), mem_var(structA), mem_var(structB)));
  }

  it("can compare unions") {
    typedef enum {
      CloneUnionTag_Int,
      CloneUnionTag_Float,
      CloneUnionTag_String,
      CloneUnionTag_Other,
    } CloneUnionTag;

    typedef struct {
      CloneUnionTag tag;
      union {
        i32    data_int;
        f32    data_float;
        String data_string;
      };
    } CloneUnionA;

    data_reg_union_t(reg, CloneUnionA, tag);
    data_reg_choice_t(reg, CloneUnionA, CloneUnionTag_Int, data_int, data_prim_t(i32));
    data_reg_choice_t(reg, CloneUnionA, CloneUnionTag_Float, data_float, data_prim_t(f32));
    data_reg_choice_t(reg, CloneUnionA, CloneUnionTag_String, data_string, data_prim_t(String));
    data_reg_choice_empty(reg, CloneUnionA, CloneUnionTag_Other);

    const CloneUnionA unionA = {
        .tag         = CloneUnionTag_String,
        .data_string = string_lit("Hello World"),
    };

    const CloneUnionA unionB = {
        .tag         = CloneUnionTag_String,
        .data_string = string_lit("Hello World2"),
    };

    check(data_equal(reg, data_meta_t(t_CloneUnionA), mem_var(unionA), mem_var(unionA)));
    check(!data_equal(reg, data_meta_t(t_CloneUnionA), mem_var(unionA), mem_var(unionB)));
  }

  teardown() { data_reg_destroy(reg); }
}
