#include "check_spec.h"
#include "core_alloc.h"
#include "data.h"

typedef struct {
  String a, b, c;
} FreeStructA;

typedef struct {
  FreeStructA  value;
  FreeStructA* ptr;
  struct {
    FreeStructA* values;
    usize        count;
  } array;
} FreeStructB;

static DataType struct_a_type() {
  static DataType type;
  if (!type) {
    data_register_struct_t(FreeStructA);
    data_register_field_t(FreeStructA, a, data_prim_t(String));
    data_register_field_t(FreeStructA, b, data_prim_t(String));
    data_register_field_t(FreeStructA, c, data_prim_t(String));

    type = t_FreeStructA;
  }
  return type;
}

static DataType struct_b_type() {
  static DataType type;
  if (!type) {
    data_register_struct_t(FreeStructB);
    data_register_field_t(FreeStructB, value, struct_a_type());
    data_register_field_t(FreeStructB, ptr, struct_a_type(), .container = DataContainer_Pointer);
    data_register_field_t(FreeStructB, array, struct_a_type(), .container = DataContainer_Array);

    type = t_FreeStructB;
  }
  return type;
}

spec(free) {

  it("can free a string") {
    const String val = string_dup(g_alloc_heap, string_lit("Hello World"));

    const DataMeta meta = data_meta_t(data_prim_t(String));
    data_free(g_alloc_heap, meta, mem_var(val));
  }

  it("can free an empty string") {
    const String val = string_empty;

    const DataMeta meta = data_meta_t(data_prim_t(String));
    data_free(g_alloc_heap, meta, mem_var(val));
  }

  it("can free a primitive pointer") {
    i32* val = alloc_alloc_t(g_alloc_heap, i32);
    *val     = 42;

    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_Pointer);
    data_free(g_alloc_heap, meta, mem_var(val));
  }

  it("can free an array of primitives") {
    const struct {
      i32*  values;
      usize count;
    } array = {.values = alloc_array_t(g_alloc_heap, i32, 8), .count = 8};

    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_Array);
    data_free(g_alloc_heap, meta, mem_var(array));
  }

  it("can free a struct") {
    const FreeStructA val = {
        .a = string_dup(g_alloc_heap, string_lit("Hello")),
        .c = string_dup(g_alloc_heap, string_lit("World")),
    };

    const DataMeta meta = data_meta_t(struct_a_type());
    data_free(g_alloc_heap, meta, mem_var(val));
  }

  it("can free nested structs") {
    FreeStructA* ptr = alloc_alloc_t(g_alloc_heap, FreeStructA);
    *ptr             = (FreeStructA){
        .a = string_dup(g_alloc_heap, string_lit("Some")),
        .b = string_dup(g_alloc_heap, string_lit("New")),
        .c = string_dup(g_alloc_heap, string_lit("Values")),
    };

    const usize  arrayCount  = 4;
    FreeStructA* arrayValues = alloc_array_t(g_alloc_heap, FreeStructA, arrayCount);
    for (usize i = 0; i != arrayCount; ++i) {
      arrayValues[i] = (FreeStructA){
          .a = string_dup(g_alloc_heap, fmt_write_scratch("Array val {}", fmt_int(i))),
      };
    }

    const FreeStructB val = {
        .value =
            {.a = string_dup(g_alloc_heap, string_lit("Hello")),
             .c = string_dup(g_alloc_heap, string_lit("World"))},
        .ptr   = ptr,
        .array = {.values = arrayValues, .count = arrayCount},
    };

    const DataMeta meta = data_meta_t(struct_b_type());
    data_free(g_alloc_heap, meta, mem_var(val));
  }
}
