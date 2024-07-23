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
    } CloneStructA;

    data_reg_struct_t(reg, CloneStructA);
    data_reg_field_t(reg, CloneStructA, a, data_prim_t(String));
    data_reg_field_t(reg, CloneStructA, b, data_prim_t(String));
    data_reg_field_t(reg, CloneStructA, c, data_prim_t(String));

    const u32 hash = data_hash(reg, data_meta_t(t_CloneStructA), DataHashFlags_None);
    check(hash != 0);
  }

  it("can compute the hash of nested structures") {
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

    const u32 hash = data_hash(reg, data_meta_t(t_CloneStructC), DataHashFlags_None);
    check(hash != 0);
  }

  it("can compute the hash of a union") {
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

    const u32 hash = data_hash(reg, data_meta_t(t_CloneUnionA), DataHashFlags_None);
    check(hash != 0);
  }

  teardown() { data_reg_destroy(reg); }
}
