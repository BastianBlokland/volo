#include "check_spec.h"
#include "data.h"

spec(registry) {

  it("can lookup a primitive type's name") {
#define X(_T_) check_eq_string(data_name(data_prim_t(_T_)), string_lit(#_T_));
    DATA_PRIMS
#undef X
  }

  it("can lookup a primitive type's size") {
#define X(_T_) check_eq_int(data_size(data_prim_t(_T_)), sizeof(_T_));
    DATA_PRIMS
#undef X
  }

  it("can lookup a primitive type's alignment requirement") {
#define X(_T_) check_eq_int(data_align(data_prim_t(_T_)), alignof(_T_));
    DATA_PRIMS
#undef X
  }

  it("can lookup the size of a plain value") {
    const DataMeta meta = data_meta_t(data_prim_t(i32));
    check_eq_int(data_meta_size(meta), sizeof(i32));
  }

  it("can lookup the size of a pointer value") {
    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_Pointer);
    check_eq_int(data_meta_size(meta), sizeof(i32*));
  }

  it("can lookup the size of a array value") {
    const DataMeta meta = data_meta_t(data_prim_t(i32), .container = DataContainer_Array);
    check_eq_int(data_meta_size(meta), sizeof(i32*) + sizeof(usize));
  }

  it("can register custom structs") {
    typedef struct sRegStructA {
      i32                 valA;
      String              valB;
      f32                 valC;
      DataArray           values;
      struct sRegStructA* next;
    } RegStructA;

    static DataType type; // Registrations persist over the entire application lifetime.
    if (!type) {

      data_register_struct_t(RegStructA);
      data_register_field_t(RegStructA, valA, data_prim_t(i32));
      data_register_field_t(RegStructA, valB, data_prim_t(String));
      data_register_field_t(RegStructA, valC, data_prim_t(f32));
      data_register_field_t(RegStructA, values, t_RegStructA, .container = DataContainer_Array);
      data_register_field_t(RegStructA, next, t_RegStructA, .container = DataContainer_Pointer);

      type = t_RegStructA;
    }

    check_eq_string(data_name(type), string_lit("RegStructA"));
    check_eq_int(data_size(type), sizeof(RegStructA));
    check_eq_int(data_align(type), alignof(RegStructA));
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

    static DataType type; // Registrations persist over the entire application lifetime.
    if (!type) {

      data_register_struct_t(NestedStruct);
      data_register_field_t(NestedStruct, valA, data_prim_t(i32));
      data_register_field_t(NestedStruct, valB, data_prim_t(String));
      data_register_field_t(NestedStruct, valC, data_prim_t(i32));

      data_register_struct_t(RegStructB);
      data_register_field_t(RegStructB, valA, t_NestedStruct);
      data_register_field_t(RegStructB, valB, t_NestedStruct, .container = DataContainer_Array);
      data_register_field_t(RegStructB, valC, t_NestedStruct, .container = DataContainer_Pointer);

      type = t_RegStructB;
    }

    check_eq_string(data_name(type), string_lit("RegStructB"));
    check_eq_int(data_size(type), sizeof(RegStructB));
    check_eq_int(data_align(type), alignof(RegStructB));
  }

  it("can register custom enums") {
    typedef enum {
      MyCustomEnum_A = -42,
      MyCustomEnum_B = 42,
      MyCustomEnum_C = 1337,
    } MyCustomEnum;

    MAYBE_UNUSED MyCustomEnum a; // Fix for compiler thinking the typedef is unused.

    static DataType type; // Registrations persist over the entire application lifetime.
    if (!type) {

      data_register_enum_t(MyCustomEnum);
      data_register_const_t(MyCustomEnum, A);
      data_register_const_t(MyCustomEnum, B);
      data_register_const_t(MyCustomEnum, C);

      type = t_MyCustomEnum;
    }

    check_eq_string(data_name(type), string_lit("MyCustomEnum"));
    check_eq_int(data_size(type), sizeof(MyCustomEnum));
    check_eq_int(data_align(type), alignof(MyCustomEnum));
  }
}
