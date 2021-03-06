#include "check_spec.h"
#include "core_alloc.h"
#include "data.h"

spec(registry) {

  DataReg* reg = null;

  setup() { reg = data_reg_create(g_alloc_heap); }

  it("can lookup a primitive type's name") {
#define X(_T_) check_eq_string(data_name(reg, data_prim_t(_T_)), string_lit(#_T_));
    DATA_PRIMS
#undef X
  }

  it("can lookup a primitive type's size") {
#define X(_T_) check_eq_int(data_size(reg, data_prim_t(_T_)), sizeof(_T_));
    DATA_PRIMS
#undef X
  }

  it("can lookup a primitive type's alignment requirement") {
#define X(_T_) check_eq_int(data_align(reg, data_prim_t(_T_)), alignof(_T_));
    DATA_PRIMS
#undef X
  }

  it("can lookup the size of a plain value") {
    const DataMeta meta = data_meta_t(data_prim_t(i32));
    check_eq_int(data_meta_size(reg, meta), sizeof(i32));
  }

  it("can lookup the size of a pointer value") {
    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_Pointer);
    check_eq_int(data_meta_size(reg, meta), sizeof(i32*));
  }

  it("can lookup the size of a array value") {
    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_Array);
    check_eq_int(data_meta_size(reg, meta), sizeof(i32*) + sizeof(usize));
  }

  it("can register custom structs") {
    typedef struct sRegStructA {
      i32                 valA;
      String              valB;
      f32                 valC;
      DataArray           values;
      struct sRegStructA* next;
    } RegStructA;

    data_reg_struct_t(reg, RegStructA);
    data_reg_field_t(reg, RegStructA, valA, data_prim_t(i32));
    data_reg_field_t(reg, RegStructA, valB, data_prim_t(String));
    data_reg_field_t(reg, RegStructA, valC, data_prim_t(f32));
    data_reg_field_t(reg, RegStructA, values, t_RegStructA, .container = DataContainer_Array);
    data_reg_field_t(reg, RegStructA, next, t_RegStructA, .container = DataContainer_Pointer);

    check_eq_string(data_name(reg, t_RegStructA), string_lit("RegStructA"));
    check_eq_int(data_size(reg, t_RegStructA), sizeof(RegStructA));
    check_eq_int(data_align(reg, t_RegStructA), alignof(RegStructA));
  }

  it("can register structs with nested types") {
    typedef struct {
      i32    valA;
      String valB;
      f32    valC;
    } NestedStruct;

    typedef struct {
      NestedStruct* values;
      usize         count;
    } NestedStructArray;

    typedef struct {
      NestedStruct      valA;
      NestedStructArray valB;
      NestedStruct*     valC;
    } RegStructB;

    data_reg_struct_t(reg, NestedStruct);
    data_reg_field_t(reg, NestedStruct, valA, data_prim_t(i32));
    data_reg_field_t(reg, NestedStruct, valB, data_prim_t(String));
    data_reg_field_t(reg, NestedStruct, valC, data_prim_t(i32));

    data_reg_struct_t(reg, RegStructB);
    data_reg_field_t(reg, RegStructB, valA, t_NestedStruct);
    data_reg_field_t(reg, RegStructB, valB, t_NestedStruct, .container = DataContainer_Array);
    data_reg_field_t(reg, RegStructB, valC, t_NestedStruct, .container = DataContainer_Pointer);

    check_eq_string(data_name(reg, t_RegStructB), string_lit("RegStructB"));
    check_eq_int(data_size(reg, t_RegStructB), sizeof(RegStructB));
    check_eq_int(data_align(reg, t_RegStructB), alignof(RegStructB));
  }

  it("can register custom enums") {
    typedef enum {
      MyCustomEnum_A = -42,
      MyCustomEnum_B = 42,
      MyCustomEnum_C = 1337,
    } MyCustomEnum;

    data_reg_enum_t(reg, MyCustomEnum);
    data_reg_const_t(reg, MyCustomEnum, A);
    data_reg_const_t(reg, MyCustomEnum, B);
    data_reg_const_t(reg, MyCustomEnum, C);

    check_eq_string(data_name(reg, t_MyCustomEnum), string_lit("MyCustomEnum"));
    check_eq_int(data_size(reg, t_MyCustomEnum), sizeof(MyCustomEnum));
    check_eq_int(data_align(reg, t_MyCustomEnum), alignof(MyCustomEnum));
  }

  teardown() { data_reg_destroy(reg); }
}
