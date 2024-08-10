#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "data.h"
#include "data_utils.h"

spec(utils_hash) {

  DataReg* reg = null;

  setup() { reg = data_reg_create(g_allocHeap); }

  it("can compute the hash of a structure") {
    typedef struct {
      String a, b, c;
    } HashStructA;

    data_reg_struct_t(reg, HashStructA);
    data_reg_field_t(reg, HashStructA, a, data_prim_t(String));
    data_reg_field_t(reg, HashStructA, b, data_prim_t(String));
    data_reg_field_t(reg, HashStructA, c, data_prim_t(String));

    const u32 hash = data_hash(reg, data_meta_t(t_HashStructA), DataHashFlags_None);
    check(hash != 0);
  }

  it("can compute the hash of nested structures") {
    typedef struct {
      String a, b, c;
    } HashStructB;

    typedef struct {
      HashStructB  value;
      HashStructB* ptr;
      struct {
        HashStructB* values;
        usize        count;
      } array;
    } HashStructC;

    data_reg_struct_t(reg, HashStructB);
    data_reg_field_t(reg, HashStructB, a, data_prim_t(String));
    data_reg_field_t(reg, HashStructB, b, data_prim_t(String));
    data_reg_field_t(reg, HashStructB, c, data_prim_t(String));

    data_reg_struct_t(reg, HashStructC);
    data_reg_field_t(reg, HashStructC, value, t_HashStructB);
    data_reg_field_t(reg, HashStructC, ptr, t_HashStructB, .container = DataContainer_Pointer);
    data_reg_field_t(reg, HashStructC, array, t_HashStructB, .container = DataContainer_HeapArray);

    const u32 hash = data_hash(reg, data_meta_t(t_HashStructC), DataHashFlags_None);
    check(hash != 0);
  }

  it("can compute the hash of a union") {
    typedef enum {
      HashUnionTag_Int,
      HashUnionTag_Float,
      HashUnionTag_String,
      HashUnionTag_Other,
    } HashUnionTag;

    typedef struct {
      HashUnionTag tag;
      union {
        i32    data_int;
        f32    data_float;
        String data_string;
      };
    } HashUnionA;

    data_reg_union_t(reg, HashUnionA, tag);
    data_reg_choice_t(reg, HashUnionA, HashUnionTag_Int, data_int, data_prim_t(i32));
    data_reg_choice_t(reg, HashUnionA, HashUnionTag_Float, data_float, data_prim_t(f32));
    data_reg_choice_t(reg, HashUnionA, HashUnionTag_String, data_string, data_prim_t(String));
    data_reg_choice_empty(reg, HashUnionA, HashUnionTag_Other);

    const u32 hash = data_hash(reg, data_meta_t(t_HashUnionA), DataHashFlags_None);
    check(hash != 0);
  }

  it("includes wether a union has a name in the hash") {
    typedef enum {
      HashUnionTag_One,
    } HashUnionTag;

    typedef struct {
      HashUnionTag tag;
      String       name;
      union {
        u32 data_one;
      };
    } HashUnionA;

    data_reg_union_t(reg, HashUnionA, tag);
    data_reg_union_name_t(reg, HashUnionA, name);
    data_reg_choice_t(reg, HashUnionA, HashUnionTag_One, data_one, data_prim_t(u32));

    typedef struct {
      HashUnionTag tag;
      union {
        u32 data_one;
      };
    } HashUnionB;

    data_reg_union_t(reg, HashUnionB, tag);
    data_reg_choice_t(reg, HashUnionB, HashUnionTag_One, data_one, data_prim_t(u32));

    const u32 hashA = data_hash(reg, data_meta_t(t_HashUnionA), DataHashFlags_None);
    const u32 hashB = data_hash(reg, data_meta_t(t_HashUnionB), DataHashFlags_None);
    check(hashA != hashB);
  }

  it("can compute the hash excluding ids") {
    typedef struct {
      String a, b;
    } HashStructA;

    data_reg_struct_t(reg, HashStructA);
    data_reg_field_t(reg, HashStructA, a, data_prim_t(String));
    data_reg_field_t(reg, HashStructA, b, data_prim_t(String));

    typedef struct {
      String c, d;
    } HashStructB;

    data_reg_struct_t(reg, HashStructB);
    data_reg_field_t(reg, HashStructB, c, data_prim_t(String));
    data_reg_field_t(reg, HashStructB, d, data_prim_t(String));

    const u32 hashA = data_hash(reg, data_meta_t(t_HashStructA), DataHashFlags_ExcludeIds);
    const u32 hashB = data_hash(reg, data_meta_t(t_HashStructB), DataHashFlags_ExcludeIds);
    check_eq_int(hashA, hashB);
  }

  it("includes the not-empty flag in the hash") {
    typedef struct {
      u32 val;
    } HashStructA;

    data_reg_struct_t(reg, HashStructA);
    data_reg_field_t(reg, HashStructA, val, data_prim_t(u32), .flags = DataFlags_NotEmpty);

    typedef struct {
      u32 val;
    } HashStructB;

    data_reg_struct_t(reg, HashStructB);
    data_reg_field_t(reg, HashStructB, val, data_prim_t(u32));

    const u32 hashA = data_hash(reg, data_meta_t(t_HashStructA), DataHashFlags_None);
    const u32 hashB = data_hash(reg, data_meta_t(t_HashStructB), DataHashFlags_None);
    check(hashA != hashB);
  }

  it("includes the external-memory flag in the hash") {
    typedef struct {
      DataMem v;
    } HashStructA;

    data_reg_struct_t(reg, HashStructA);
    data_reg_field_t(reg, HashStructA, v, data_prim_t(DataMem), .flags = DataFlags_ExternalMemory);

    typedef struct {
      DataMem v;
    } HashStructB;

    data_reg_struct_t(reg, HashStructB);
    data_reg_field_t(reg, HashStructB, v, data_prim_t(DataMem));

    const u32 hashA = data_hash(reg, data_meta_t(t_HashStructA), DataHashFlags_None);
    const u32 hashB = data_hash(reg, data_meta_t(t_HashStructB), DataHashFlags_None);
    check(hashA != hashB);
  }

  it("includes the fixedCount in the hash") {
    typedef struct {
      u32 val[2];
    } HashStructA;

    data_reg_struct_t(reg, HashStructA);
    data_reg_field_t(
        reg,
        HashStructA,
        val,
        data_prim_t(u32),
        .container  = DataContainer_InlineArray,
        .fixedCount = 2);

    typedef struct {
      u32 val[3];
    } HashStructB;

    data_reg_struct_t(reg, HashStructB);
    data_reg_field_t(
        reg,
        HashStructB,
        val,
        data_prim_t(u32),
        .container  = DataContainer_InlineArray,
        .fixedCount = 3);

    const u32 hashA = data_hash(reg, data_meta_t(t_HashStructA), DataHashFlags_None);
    const u32 hashB = data_hash(reg, data_meta_t(t_HashStructB), DataHashFlags_None);
    check(hashA != hashB);
  }

  teardown() { data_reg_destroy(reg); }
}
