#include "check_spec.h"
#include "core_alloc.h"
#include "data.h"

spec(utils_destroy) {

  it("can destroy a string") {
    const String val = string_dup(g_alloc_heap, string_lit("Hello World"));

    const DataMeta meta = data_meta_t(data_prim_t(String));
    data_destroy(g_alloc_heap, meta, mem_var(val));
  }

  it("can destroy an empty string") {
    const String val = string_empty;

    const DataMeta meta = data_meta_t(data_prim_t(String));
    data_destroy(g_alloc_heap, meta, mem_var(val));
  }

  it("can destroy a primitive pointer") {
    i32* val = alloc_alloc_t(g_alloc_heap, i32);
    *val     = 42;

    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_Pointer);
    data_destroy(g_alloc_heap, meta, mem_var(val));
  }

  it("can destroy an array of primitives") {
    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_Array);

    const struct {
      i32*  values;
      usize count;
    } array1 = {.values = alloc_array_t(g_alloc_heap, i32, 8), .count = 8};
    data_destroy(g_alloc_heap, meta, mem_var(array1));

    const struct {
      i32*  values;
      usize count;
    } array2 = {0};
    data_destroy(g_alloc_heap, meta, mem_var(array2));
  }

  it("can destroy a structure") {
    typedef struct {
      String a, b, c;
    } DestroyStructA;

    static DataType g_typeA;
    if (!g_typeA) {
      data_register_struct_t(DestroyStructA);
      data_register_field_t(DestroyStructA, a, data_prim_t(String));
      data_register_field_t(DestroyStructA, b, data_prim_t(String));
      data_register_field_t(DestroyStructA, c, data_prim_t(String));
      g_typeA = t_DestroyStructA;
    }

    const DestroyStructA val = {
        .a = string_dup(g_alloc_heap, string_lit("Hello")),
        .c = string_dup(g_alloc_heap, string_lit("World")),
    };

    data_destroy(g_alloc_heap, data_meta_t(g_typeA), mem_var(val));
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

    static DataType g_typeB, g_typeC;
    if (!g_typeB) {
      data_register_struct_t(DestroyStructB);
      data_register_field_t(DestroyStructB, a, data_prim_t(String));
      data_register_field_t(DestroyStructB, b, data_prim_t(String));
      data_register_field_t(DestroyStructB, c, data_prim_t(String));
      g_typeB = t_DestroyStructB;

      data_register_struct_t(DestroyStructC);
      data_register_field_t(DestroyStructC, value, g_typeB);
      data_register_field_t(DestroyStructC, ptr, g_typeB, .container = DataContainer_Pointer);
      data_register_field_t(DestroyStructC, array, g_typeB, .container = DataContainer_Array);
      g_typeC = t_DestroyStructC;
    }

    DestroyStructB* ptr = alloc_alloc_t(g_alloc_heap, DestroyStructB);
    *ptr                = (DestroyStructB){
        .a = string_dup(g_alloc_heap, string_lit("Some")),
        .b = string_dup(g_alloc_heap, string_lit("New")),
        .c = string_dup(g_alloc_heap, string_lit("Values")),
    };

    const usize     arrayCount  = 4;
    DestroyStructB* arrayValues = alloc_array_t(g_alloc_heap, DestroyStructB, arrayCount);
    for (usize i = 0; i != arrayCount; ++i) {
      arrayValues[i] = (DestroyStructB){
          .a = string_dup(g_alloc_heap, fmt_write_scratch("Array val {}", fmt_int(i))),
      };
    }

    const DestroyStructC val = {
        .value =
            {
                .a = string_dup(g_alloc_heap, string_lit("Hello")),
                .c = string_dup(g_alloc_heap, string_lit("World")),
            },
        .ptr   = ptr,
        .array = {.values = arrayValues, .count = arrayCount},
    };

    data_destroy(g_alloc_heap, data_meta_t(g_typeC), mem_var(val));
  }
}
