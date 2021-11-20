#include "check_spec.h"
#include "core_alloc.h"
#include "data.h"

typedef struct {
  String a, b, c;
} CloneStructA;

typedef struct {
  CloneStructA  value;
  CloneStructA* ptr;
  struct {
    CloneStructA* values;
    usize         count;
  } array;
} CloneStructB;

static DataType struct_a_type() {
  static DataType type;
  if (!type) {
    data_register_struct_t(CloneStructA);
    data_register_field_t(CloneStructA, a, data_prim_t(String));
    data_register_field_t(CloneStructA, b, data_prim_t(String));
    data_register_field_t(CloneStructA, c, data_prim_t(String));

    type = t_CloneStructA;
  }
  return type;
}

static DataType struct_b_type() {
  static DataType type;
  if (!type) {
    data_register_struct_t(CloneStructB);
    data_register_field_t(CloneStructB, value, struct_a_type());
    data_register_field_t(CloneStructB, ptr, struct_a_type(), .container = DataContainer_Pointer);
    data_register_field_t(CloneStructB, array, struct_a_type(), .container = DataContainer_Array);

    type = t_CloneStructB;
  }
  return type;
}

spec(utils_clone) {

  it("can clone a string") {
    const String original = string_dup(g_alloc_heap, string_lit("Hello World"));
    const String clone    = {0};

    const DataMeta meta = data_meta_t(data_prim_t(String));
    data_clone(g_alloc_heap, meta, mem_var(original), mem_var(clone));

    check_eq_string(clone, string_lit("Hello World"));

    data_destroy(g_alloc_heap, meta, mem_var(original));
    data_destroy(g_alloc_heap, meta, mem_var(clone));
  }

  it("can clone an empty string") {
    const String original = string_empty;
    const String clone    = {0};

    const DataMeta meta = data_meta_t(data_prim_t(String));
    data_clone(g_alloc_heap, meta, mem_var(original), mem_var(clone));

    check_eq_string(clone, string_empty);
  }

  it("can clone a primitive pointer") {
    i32* original = alloc_alloc_t(g_alloc_heap, i32);
    *original     = 42;

    i32* clone = null;

    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_Pointer);
    data_clone(g_alloc_heap, meta, mem_var(original), mem_var(clone));

    check_eq_int(*clone, 42);

    data_destroy(g_alloc_heap, meta, mem_var(original));
    data_destroy(g_alloc_heap, meta, mem_var(clone));
  }

  it("can clone an array of primitives") {
    typedef struct {
      i32*  values;
      usize count;
    } PrimArray;

    const PrimArray original = {.values = alloc_array_t(g_alloc_heap, i32, 8), .count = 8};
    const PrimArray clone    = {0};

    for (usize i = 0; i != original.count; ++i) {
      original.values[i] = (i32)i;
    }

    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_Array);
    data_clone(g_alloc_heap, meta, mem_var(original), mem_var(clone));

    for (usize i = 0; i != original.count; ++i) {
      check_eq_int(original.values[i], (i32)i);
    }

    data_destroy(g_alloc_heap, meta, mem_var(original));
    data_destroy(g_alloc_heap, meta, mem_var(clone));
  }

  it("can clone an empty array") {
    typedef struct {
      i32*  values;
      usize count;
    } PrimArray;

    const PrimArray original = {0};
    const PrimArray clone    = {0};

    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_Array);
    data_clone(g_alloc_heap, meta, mem_var(original), mem_var(clone));
  }

  it("can clone a structure") {
    const CloneStructA original = {
        .a = string_dup(g_alloc_heap, string_lit("Hello")),
        .c = string_dup(g_alloc_heap, string_lit("World")),
    };
    CloneStructA clone = {0};

    const DataMeta meta = data_meta_t(struct_a_type());
    data_clone(g_alloc_heap, meta, mem_var(original), mem_var(clone));

    check_eq_string(clone.a, string_lit("Hello"));
    check_eq_string(clone.c, string_lit("World"));

    data_destroy(g_alloc_heap, meta, mem_var(original));
    data_destroy(g_alloc_heap, meta, mem_var(clone));
  }

  it("can clone nested structures") {
    CloneStructA* originalPtr = alloc_alloc_t(g_alloc_heap, CloneStructA);
    *originalPtr              = (CloneStructA){
        .a = string_dup(g_alloc_heap, string_lit("Some")),
        .b = string_dup(g_alloc_heap, string_lit("New")),
        .c = string_dup(g_alloc_heap, string_lit("Values")),
    };

    const usize   arrayCount          = 4;
    CloneStructA* originalArrayValues = alloc_array_t(g_alloc_heap, CloneStructA, arrayCount);
    for (usize i = 0; i != arrayCount; ++i) {
      originalArrayValues[i] = (CloneStructA){
          .a = string_dup(g_alloc_heap, fmt_write_scratch("Array val {}", fmt_int(i))),
      };
    }

    const CloneStructB original = {
        .value =
            {.a = string_dup(g_alloc_heap, string_lit("Hello")),
             .c = string_dup(g_alloc_heap, string_lit("World"))},
        .ptr   = originalPtr,
        .array = {.values = originalArrayValues, .count = arrayCount},
    };
    CloneStructB clone = {0};

    const DataMeta meta = data_meta_t(struct_b_type());
    data_clone(g_alloc_heap, meta, mem_var(original), mem_var(clone));

    check_eq_string(clone.value.a, string_lit("Hello"));
    check_eq_string(clone.value.c, string_lit("World"));
    check_eq_string(clone.ptr->a, string_lit("Some"));
    check_eq_string(clone.ptr->b, string_lit("New"));
    check_eq_string(clone.ptr->c, string_lit("Values"));
    for (usize i = 0; i != clone.array.count; ++i) {
      check_eq_string(clone.array.values[i].a, fmt_write_scratch("Array val {}", fmt_int(i)));
    }

    data_destroy(g_alloc_heap, meta, mem_var(original));
    data_destroy(g_alloc_heap, meta, mem_var(clone));
  }
}
