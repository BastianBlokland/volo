#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "data.h"

spec(utils_clone) {

  DataReg* reg = null;

  setup() { reg = data_reg_create(g_allocHeap); }

  it("can clone a string") {
    const String original = string_dup(g_allocHeap, string_lit("Hello World"));
    const String clone    = {0};

    const DataMeta meta = data_meta_t(data_prim_t(String));
    data_clone(reg, g_allocHeap, meta, mem_var(original), mem_var(clone));

    check_eq_string(clone, string_lit("Hello World"));

    data_destroy(reg, g_allocHeap, meta, mem_var(original));
    data_destroy(reg, g_allocHeap, meta, mem_var(clone));
  }

  it("can clone an interned string") {
    const String original = string_lit("Hello World");
    const String clone    = {0};

    const DataMeta meta = data_meta_t(data_prim_t(String), .flags = DataFlags_Intern);
    data_clone(reg, g_allocHeap, meta, mem_var(original), mem_var(clone));

    check_eq_string(clone, string_lit("Hello World"));

    data_destroy(reg, g_allocHeap, meta, mem_var(original));
    data_destroy(reg, g_allocHeap, meta, mem_var(clone));
  }

  it("can clone an empty string") {
    const String original = string_empty;
    const String clone    = {0};

    const DataMeta meta = data_meta_t(data_prim_t(String));
    data_clone(reg, g_allocHeap, meta, mem_var(original), mem_var(clone));

    check_eq_string(clone, string_empty);
  }

  it("can clone memory") {
    const DataMem original = data_mem_create(string_dup(g_allocHeap, string_lit("Hello World")));
    const DataMem clone    = {0};

    const DataMeta meta = data_meta_t(data_prim_t(DataMem));
    data_clone(reg, g_allocHeap, meta, mem_var(original), mem_var(clone));

    check_eq_string(data_mem(clone), string_lit("Hello World"));

    data_destroy(reg, g_allocHeap, meta, mem_var(original));
    data_destroy(reg, g_allocHeap, meta, mem_var(clone));
  }

  it("can clone external memory") {
    const DataMem original = data_mem_create_ext(string_lit("Hello World"));
    const DataMem clone    = {0};

    const DataMeta meta = data_meta_t(data_prim_t(DataMem));
    data_clone(reg, g_allocHeap, meta, mem_var(original), mem_var(clone));

    check_eq_string(data_mem(clone), string_lit("Hello World"));
  }

  it("can clone empty memory") {
    const DataMem original = data_mem_create(mem_empty);
    const DataMem clone    = {0};

    const DataMeta meta = data_meta_t(data_prim_t(DataMem));
    data_clone(reg, g_allocHeap, meta, mem_var(original), mem_var(clone));

    check_eq_string(data_mem(clone), string_empty);
  }

  it("can clone a primitive pointer") {
    i32  original    = 42;
    i32* originalPtr = &original;

    i32* clone = null;

    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_Pointer);
    data_clone(reg, g_allocHeap, meta, mem_var(originalPtr), mem_var(clone));

    check_eq_int(*clone, 42);

    data_destroy(reg, g_allocHeap, meta, mem_var(clone));
  }

  it("can clone an inline-array of primitives") {
    i32 original[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    i32 clone[8];

    const DataMeta meta =
        data_meta_t(data_prim_t(i32), .container = DataContainer_InlineArray, .fixedCount = 8);
    data_clone(reg, g_allocHeap, meta, mem_var(original), mem_var(clone));

    for (usize i = 0; i != 8; ++i) {
      check_eq_int(clone[i], original[i]);
    }
  }

  it("can clone a heap-array of primitives") {

    i32 orgValues[] = {0, 1, 2, 3, 4, 5, 6, 7};

    HeapArray_t(i32) original = {.values = orgValues, .count = array_elems(orgValues)};
    HeapArray_t(i32) clone    = {0};

    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_HeapArray);
    data_clone(reg, g_allocHeap, meta, mem_var(original), mem_var(clone));

    for (usize i = 0; i != original.count; ++i) {
      check_eq_int(clone.values[i], original.values[i]);
    }

    data_destroy(reg, g_allocHeap, meta, mem_var(clone));
  }

  it("can clone an empty heap-array") {
    HeapArray_t(i32) original = {0};
    HeapArray_t(i32) clone    = {0};

    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_HeapArray);
    data_clone(reg, g_allocHeap, meta, mem_var(original), mem_var(clone));
  }

  it("can clone dynamic-arrays") {
    DynArray original                = dynarray_create_t(g_allocHeap, i32, 4);
    *dynarray_push_t(&original, i32) = 0;
    *dynarray_push_t(&original, i32) = 1;
    *dynarray_push_t(&original, i32) = 2;
    *dynarray_push_t(&original, i32) = 3;

    DynArray clone;

    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_DynArray);
    data_clone(reg, g_allocHeap, meta, mem_var(original), mem_var(clone));

    check_eq_int(original.size, clone.size);
    for (usize i = 0; i != original.size; ++i) {
      check_eq_int(*dynarray_at_t(&original, i, i32), *dynarray_at_t(&clone, i, i32));
    }

    dynarray_destroy(&original);
    dynarray_destroy(&clone);
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
        .a = string_dup(g_allocHeap, string_lit("Hello")),
        .c = string_dup(g_allocHeap, string_lit("World")),
    };
    CloneStructA clone = {0};

    data_clone(reg, g_allocHeap, data_meta_t(t_CloneStructA), mem_var(original), mem_var(clone));

    check_eq_string(clone.a, string_lit("Hello"));
    check_eq_string(clone.c, string_lit("World"));

    data_destroy(reg, g_allocHeap, data_meta_t(t_CloneStructA), mem_var(original));
    data_destroy(reg, g_allocHeap, data_meta_t(t_CloneStructA), mem_var(clone));
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
    data_reg_field_t(
        reg, CloneStructC, array, t_CloneStructB, .container = DataContainer_HeapArray);

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

    data_clone(reg, g_allocHeap, data_meta_t(t_CloneStructC), mem_var(original), mem_var(clone));

    check_eq_string(clone.value.a, string_lit("Hello"));
    check_eq_string(clone.value.c, string_lit("World"));
    check_eq_string(clone.ptr->a, string_lit("Some"));
    check_eq_string(clone.ptr->b, string_lit("New"));
    check_eq_string(clone.ptr->c, string_lit("Values"));
    for (usize i = 0; i != clone.array.count; ++i) {
      check_eq_string(clone.array.values[i].a, originalArrayValues[i].a);
    }

    data_destroy(reg, g_allocHeap, data_meta_t(t_CloneStructC), mem_var(clone));
  }

  it("can clone a union") {
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
      const CloneUnionA original = {
          .tag      = CloneUnionTag_Int,
          .data_int = 42,
      };
      CloneUnionA clone = {0};

      data_clone(reg, g_allocHeap, data_meta_t(t_CloneUnionA), mem_var(original), mem_var(clone));

      check_eq_int(clone.tag, original.tag);
      check_eq_int(clone.data_int, 42);

      data_destroy(reg, g_allocHeap, data_meta_t(t_CloneUnionA), mem_var(original));
      data_destroy(reg, g_allocHeap, data_meta_t(t_CloneUnionA), mem_var(clone));
    }
    {
      const CloneUnionA original = {
          .tag         = CloneUnionTag_String,
          .data_string = string_dup(g_allocHeap, string_lit("Hello World")),
      };
      CloneUnionA clone = {0};

      data_clone(reg, g_allocHeap, data_meta_t(t_CloneUnionA), mem_var(original), mem_var(clone));

      check_eq_int(clone.tag, original.tag);
      check_eq_string(clone.data_string, string_lit("Hello World"));

      data_destroy(reg, g_allocHeap, data_meta_t(t_CloneUnionA), mem_var(original));
      data_destroy(reg, g_allocHeap, data_meta_t(t_CloneUnionA), mem_var(clone));
    }
    {
      const CloneUnionA original = {.tag = CloneUnionTag_Other};
      CloneUnionA       clone    = {0};

      data_clone(reg, g_allocHeap, data_meta_t(t_CloneUnionA), mem_var(original), mem_var(clone));

      check_eq_int(clone.tag, original.tag);

      data_destroy(reg, g_allocHeap, data_meta_t(t_CloneUnionA), mem_var(original));
      data_destroy(reg, g_allocHeap, data_meta_t(t_CloneUnionA), mem_var(clone));
    }
  }

  it("can clone a union with a name") {
    typedef enum {
      CloneUnionTag_Int,
      CloneUnionTag_Float,
    } CloneUnionTag;

    typedef struct {
      CloneUnionTag tag;
      String        name;
      union {
        i32 data_int;
        f32 data_float;
      };
    } CloneUnionA;

    data_reg_union_t(reg, CloneUnionA, tag);
    data_reg_union_name_t(reg, CloneUnionA, name);
    data_reg_choice_t(reg, CloneUnionA, CloneUnionTag_Int, data_int, data_prim_t(i32));
    data_reg_choice_t(reg, CloneUnionA, CloneUnionTag_Float, data_float, data_prim_t(f32));

    const CloneUnionA original = {
        .tag      = CloneUnionTag_Int,
        .name     = string_dup(g_allocHeap, string_lit("Hello")),
        .data_int = 42,
    };
    CloneUnionA clone = {0};

    data_clone(reg, g_allocHeap, data_meta_t(t_CloneUnionA), mem_var(original), mem_var(clone));

    check_eq_string(clone.name, original.name);
    check_eq_int(clone.tag, original.tag);
    check_eq_int(clone.data_int, 42);

    data_destroy(reg, g_allocHeap, data_meta_t(t_CloneUnionA), mem_var(original));
    data_destroy(reg, g_allocHeap, data_meta_t(t_CloneUnionA), mem_var(clone));
  }

  teardown() { data_reg_destroy(reg); }
}
