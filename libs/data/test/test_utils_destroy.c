#include "check_spec.h"
#include "core_alloc.h"
#include "data.h"

typedef struct {
  String a, b, c;
} DestroyStructA;

typedef struct {
  DestroyStructA  value;
  DestroyStructA* ptr;
  struct {
    DestroyStructA* values;
    usize           count;
  } array;
} DestroyStructB;

static DataType struct_a_type() {
  static DataType type;
  if (!type) {
    data_register_struct_t(DestroyStructA);
    data_register_field_t(DestroyStructA, a, data_prim_t(String));
    data_register_field_t(DestroyStructA, b, data_prim_t(String));
    data_register_field_t(DestroyStructA, c, data_prim_t(String));

    type = t_DestroyStructA;
  }
  return type;
}

static DataType struct_b_type() {
  static DataType type;
  if (!type) {
    data_register_struct_t(DestroyStructB);
    data_register_field_t(DestroyStructB, value, struct_a_type());
    data_register_field_t(DestroyStructB, ptr, struct_a_type(), .container = DataContainer_Pointer);
    data_register_field_t(DestroyStructB, array, struct_a_type(), .container = DataContainer_Array);

    type = t_DestroyStructB;
  }
  return type;
}

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
    const DestroyStructA val = {
        .a = string_dup(g_alloc_heap, string_lit("Hello")),
        .c = string_dup(g_alloc_heap, string_lit("World")),
    };

    const DataMeta meta = data_meta_t(struct_a_type());
    data_destroy(g_alloc_heap, meta, mem_var(val));
  }

  it("can destroy nested structures") {
    DestroyStructA* ptr = alloc_alloc_t(g_alloc_heap, DestroyStructA);
    *ptr                = (DestroyStructA){
        .a = string_dup(g_alloc_heap, string_lit("Some")),
        .b = string_dup(g_alloc_heap, string_lit("New")),
        .c = string_dup(g_alloc_heap, string_lit("Values")),
    };

    const usize     arrayCount  = 4;
    DestroyStructA* arrayValues = alloc_array_t(g_alloc_heap, DestroyStructA, arrayCount);
    for (usize i = 0; i != arrayCount; ++i) {
      arrayValues[i] = (DestroyStructA){
          .a = string_dup(g_alloc_heap, fmt_write_scratch("Array val {}", fmt_int(i))),
      };
    }

    const DestroyStructB val = {
        .value =
            {.a = string_dup(g_alloc_heap, string_lit("Hello")),
             .c = string_dup(g_alloc_heap, string_lit("World"))},
        .ptr   = ptr,
        .array = {.values = arrayValues, .count = arrayCount},
    };

    const DataMeta meta = data_meta_t(struct_b_type());
    data_destroy(g_alloc_heap, meta, mem_var(val));
  }
}
