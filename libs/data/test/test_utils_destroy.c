#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_dynarray.h"
#include "data_registry.h"
#include "data_utils.h"

spec(utils_destroy) {

  DataReg* reg = null;

  setup() { reg = data_reg_create(g_allocHeap); }

  it("can destroy a string") {
    const String val = string_dup(g_allocHeap, string_lit("Hello World"));

    const DataMeta meta = data_meta_t(data_prim_t(String));
    data_destroy(reg, g_allocHeap, meta, mem_var(val));
  }

  it("can destroy an interned string") {
    const String val = string_lit("Hello World");

    const DataMeta meta = data_meta_t(data_prim_t(String), .flags = DataFlags_Intern);
    data_destroy(reg, g_allocHeap, meta, mem_var(val));
  }

  it("can destroy an empty string") {
    const String val = string_empty;

    const DataMeta meta = data_meta_t(data_prim_t(String));
    data_destroy(reg, g_allocHeap, meta, mem_var(val));
  }

  it("can destroy memory") {
    const DataMem val = data_mem_create(string_dup(g_allocHeap, string_lit("Hello World")));

    const DataMeta meta = data_meta_t(data_prim_t(DataMem));
    data_destroy(reg, g_allocHeap, meta, mem_var(val));
  }

  it("can destroy external memory") {
    const DataMem val = data_mem_create_ext(string_lit("Hello World"));

    const DataMeta meta = data_meta_t(data_prim_t(DataMem));
    data_destroy(reg, g_allocHeap, meta, mem_var(val));
  }

  it("can destroy empty memory") {
    const DataMem val = data_mem_create(mem_empty);

    const DataMeta meta = data_meta_t(data_prim_t(DataMem));
    data_destroy(reg, g_allocHeap, meta, mem_var(val));
  }

  it("can destroy a primitive pointer") {
    i32* val = alloc_alloc_t(g_allocHeap, i32);
    *val     = 42;

    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_Pointer);
    data_destroy(reg, g_allocHeap, meta, mem_var(val));
  }

  it("can destroy an inline-array") {
    String val[2];
    val[0] = string_dup(g_allocHeap, string_lit("Hello"));
    val[1] = string_dup(g_allocHeap, string_lit("World"));

    const DataMeta meta =
        data_meta_t(data_prim_t(String), .container = DataContainer_InlineArray, .fixedCount = 2);
    data_destroy(reg, g_allocHeap, meta, mem_var(val));
  }

  it("can destroy a heap-array of primitives") {
    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_HeapArray);

    HeapArray_t(i32) array1 = {.values = alloc_array_t(g_allocHeap, i32, 8), .count = 8};
    data_destroy(reg, g_allocHeap, meta, mem_var(array1));

    HeapArray_t(i32) array2 = {0};
    data_destroy(reg, g_allocHeap, meta, mem_var(array2));
  }

  it("can destroy a dynarray") {
    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_DynArray);

    DynArray array1 = dynarray_create_t(g_allocHeap, i32, 0);
    data_destroy(reg, g_allocHeap, meta, mem_var(array1));

    DynArray array2                = dynarray_create_t(g_allocHeap, i32, 0);
    *dynarray_push_t(&array2, i32) = 42;
    data_destroy(reg, g_allocHeap, meta, mem_var(array2));
  }

  it("can destroy a structure") {
    typedef struct {
      String a, b, c;
    } DestroyStructA;

    data_reg_struct_t(reg, DestroyStructA);
    data_reg_field_t(reg, DestroyStructA, a, data_prim_t(String));
    data_reg_field_t(reg, DestroyStructA, b, data_prim_t(String));
    data_reg_field_t(reg, DestroyStructA, c, data_prim_t(String));

    const DestroyStructA val = {
        .a = string_dup(g_allocHeap, string_lit("Hello")),
        .c = string_dup(g_allocHeap, string_lit("World")),
    };

    data_destroy(reg, g_allocHeap, data_meta_t(t_DestroyStructA), mem_var(val));
  }

  it("can destroy nested structures") {
    typedef struct {
      String a, b, c;
    } DestroyStructB;

    typedef struct {
      DestroyStructB  value;
      DestroyStructB* ptr;
      struct {
        DestroyStructB* values;
        usize           count;
      } array;
    } DestroyStructC;

    data_reg_struct_t(reg, DestroyStructB);
    data_reg_field_t(reg, DestroyStructB, a, data_prim_t(String));
    data_reg_field_t(reg, DestroyStructB, b, data_prim_t(String));
    data_reg_field_t(reg, DestroyStructB, c, data_prim_t(String));

    data_reg_struct_t(reg, DestroyStructC);
    data_reg_field_t(reg, DestroyStructC, value, t_DestroyStructB);
    data_reg_field_t(
        reg, DestroyStructC, ptr, t_DestroyStructB, .container = DataContainer_Pointer);
    data_reg_field_t(
        reg, DestroyStructC, array, t_DestroyStructB, .container = DataContainer_HeapArray);

    DestroyStructB* ptr = alloc_alloc_t(g_allocHeap, DestroyStructB);
    *ptr                = (DestroyStructB){
                       .a = string_dup(g_allocHeap, string_lit("Some")),
                       .b = string_dup(g_allocHeap, string_lit("New")),
                       .c = string_dup(g_allocHeap, string_lit("Values")),
    };

    const usize     arrayCount  = 4;
    DestroyStructB* arrayValues = alloc_array_t(g_allocHeap, DestroyStructB, arrayCount);
    for (usize i = 0; i != arrayCount; ++i) {
      arrayValues[i] = (DestroyStructB){
          .a = string_dup(g_allocHeap, fmt_write_scratch("Array val {}", fmt_int(i))),
      };
    }

    const DestroyStructC val = {
        .value =
            {
                .a = string_dup(g_allocHeap, string_lit("Hello")),
                .c = string_dup(g_allocHeap, string_lit("World")),
            },
        .ptr   = ptr,
        .array = {.values = arrayValues, .count = arrayCount},
    };

    data_destroy(reg, g_allocHeap, data_meta_t(t_DestroyStructC), mem_var(val));
  }

  it("can destroy a union") {
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

    {
      const CloneUnionA val = {
          .tag      = CloneUnionTag_Int,
          .data_int = 42,
      };
      data_destroy(reg, g_allocHeap, data_meta_t(t_CloneUnionA), mem_var(val));
    }
    {
      const CloneUnionA val = {
          .tag         = CloneUnionTag_String,
          .data_string = string_dup(g_allocHeap, string_lit("Hello World")),
      };
      data_destroy(reg, g_allocHeap, data_meta_t(t_CloneUnionA), mem_var(val));
    }
    {
      const CloneUnionA val = {
          .tag = CloneUnionTag_Other,
      };
      data_destroy(reg, g_allocHeap, data_meta_t(t_CloneUnionA), mem_var(val));
    }
  }

  it("can destroy a union with a name") {
    typedef enum {
      CloneUnionTag_Int,
      CloneUnionTag_Float,
    } CloneUnionTag;

    typedef struct {
      CloneUnionTag tag;
      String        name;
      union {
        i32    data_int;
        f32    data_float;
        String data_string;
      };
    } CloneUnionA;

    data_reg_union_t(reg, CloneUnionA, tag);
    data_reg_union_name_t(reg, CloneUnionA, name, DataUnionNameType_String);
    data_reg_choice_t(reg, CloneUnionA, CloneUnionTag_Int, data_int, data_prim_t(i32));
    data_reg_choice_t(reg, CloneUnionA, CloneUnionTag_Float, data_float, data_prim_t(f32));

    const CloneUnionA val = {
        .tag      = CloneUnionTag_Int,
        .name     = string_dup(g_allocHeap, string_lit("Hello World")),
        .data_int = 42,
    };
    data_destroy(reg, g_allocHeap, data_meta_t(t_CloneUnionA), mem_var(val));
  }

  teardown() { data_reg_destroy(reg); }
}
