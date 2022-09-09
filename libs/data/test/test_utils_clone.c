#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "data.h"

spec(utils_clone) {

  DataReg* reg = null;

  setup() { reg = data_reg_create(g_alloc_heap); }

  it("can clone a string") {
    const String original = string_dup(g_alloc_heap, string_lit("Hello World"));
    const String clone    = {0};

    const DataMeta meta = data_meta_t(data_prim_t(String));
    data_clone(reg, g_alloc_heap, meta, mem_var(original), mem_var(clone));

    check_eq_string(clone, string_lit("Hello World"));

    data_destroy(reg, g_alloc_heap, meta, mem_var(original));
    data_destroy(reg, g_alloc_heap, meta, mem_var(clone));
  }

  it("can clone an empty string") {
    const String original = string_empty;
    const String clone    = {0};

    const DataMeta meta = data_meta_t(data_prim_t(String));
    data_clone(reg, g_alloc_heap, meta, mem_var(original), mem_var(clone));

    check_eq_string(clone, string_empty);
  }

  it("can clone a primitive pointer") {
    i32  original    = 42;
    i32* originalPtr = &original;

    i32* clone = null;

    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_Pointer);
    data_clone(reg, g_alloc_heap, meta, mem_var(originalPtr), mem_var(clone));

    check_eq_int(*clone, 42);

    data_destroy(reg, g_alloc_heap, meta, mem_var(clone));
  }

  it("can clone an array of primitives") {
    typedef struct {
      i32*  values;
      usize count;
    } PrimArray;

    i32 originalValues[] = {0, 1, 2, 3, 4, 5, 6, 7};

    const PrimArray original = {.values = originalValues, .count = array_elems(originalValues)};
    const PrimArray clone    = {0};

    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_Array);
    data_clone(reg, g_alloc_heap, meta, mem_var(original), mem_var(clone));

    for (usize i = 0; i != original.count; ++i) {
      check_eq_int(original.values[i], (i32)i);
    }

    data_destroy(reg, g_alloc_heap, meta, mem_var(clone));
  }

  it("can clone an empty array") {
    typedef struct {
      i32*  values;
      usize count;
    } PrimArray;

    const PrimArray original = {0};
    const PrimArray clone    = {0};

    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_Array);
    data_clone(reg, g_alloc_heap, meta, mem_var(original), mem_var(clone));
  }

  it("can clone a structure") {
    typedef struct {
      String a, b, c;
    } CloneStructA;

    data_reg_struct_t(reg, CloneStructA);
    data_reg_field_t(reg, CloneStructA, a, data_prim_t(String));
    data_reg_field_t(reg, CloneStructA, b, data_prim_t(String));
    data_reg_field_t(reg, CloneStructA, c, data_prim_t(String));

    const CloneStructA original = {
        .a = string_dup(g_alloc_heap, string_lit("Hello")),
        .c = string_dup(g_alloc_heap, string_lit("World")),
    };
    CloneStructA clone = {0};

    data_clone(reg, g_alloc_heap, data_meta_t(t_CloneStructA), mem_var(original), mem_var(clone));

    check_eq_string(clone.a, string_lit("Hello"));
    check_eq_string(clone.c, string_lit("World"));

    data_destroy(reg, g_alloc_heap, data_meta_t(t_CloneStructA), mem_var(original));
    data_destroy(reg, g_alloc_heap, data_meta_t(t_CloneStructA), mem_var(clone));
  }

  it("can clone nested structures") {
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

    CloneStructB originalPtrValue = {
        .a = string_lit("Some"),
        .b = string_lit("New"),
        .c = string_lit("Values"),
    };

    CloneStructB originalArrayValues[] = {
        {.a = string_lit("Hello")},
        {.a = string_lit("Beautiful")},
        {.a = string_lit("World")},
    };

    const CloneStructC original = {
        .value = {.a = string_lit("Hello"), .c = string_lit("World")},
        .ptr   = &originalPtrValue,
        .array = {.values = originalArrayValues, .count = array_elems(originalArrayValues)},
    };
    CloneStructC clone = {0};

    data_clone(reg, g_alloc_heap, data_meta_t(t_CloneStructC), mem_var(original), mem_var(clone));

    check_eq_string(clone.value.a, string_lit("Hello"));
    check_eq_string(clone.value.c, string_lit("World"));
    check_eq_string(clone.ptr->a, string_lit("Some"));
    check_eq_string(clone.ptr->b, string_lit("New"));
    check_eq_string(clone.ptr->c, string_lit("Values"));
    for (usize i = 0; i != clone.array.count; ++i) {
      check_eq_string(clone.array.values[i].a, originalArrayValues[i].a);
    }

    data_destroy(reg, g_alloc_heap, data_meta_t(t_CloneStructC), mem_var(clone));
  }

  it("can clone a union") {
    typedef enum {
      CloneUnionTag_Int,
      CloneUnionTag_Float,
      CloneUnionTag_String,
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

    {
      const CloneUnionA original = {
          .tag      = CloneUnionTag_Int,
          .data_int = 42,
      };
      CloneUnionA clone = {0};

      data_clone(reg, g_alloc_heap, data_meta_t(t_CloneUnionA), mem_var(original), mem_var(clone));

      check_eq_int(clone.tag, original.tag);
      check_eq_int(clone.data_int, 42);

      data_destroy(reg, g_alloc_heap, data_meta_t(t_CloneUnionA), mem_var(original));
      data_destroy(reg, g_alloc_heap, data_meta_t(t_CloneUnionA), mem_var(clone));
    }

    {
      const CloneUnionA original = {
          .tag         = CloneUnionTag_String,
          .data_string = string_dup(g_alloc_heap, string_lit("Hello World")),
      };
      CloneUnionA clone = {0};

      data_clone(reg, g_alloc_heap, data_meta_t(t_CloneUnionA), mem_var(original), mem_var(clone));

      check_eq_int(clone.tag, original.tag);
      check_eq_string(clone.data_string, string_lit("Hello World"));

      data_destroy(reg, g_alloc_heap, data_meta_t(t_CloneUnionA), mem_var(original));
      data_destroy(reg, g_alloc_heap, data_meta_t(t_CloneUnionA), mem_var(clone));
    }
  }

  teardown() { data_reg_destroy(reg); }
}
