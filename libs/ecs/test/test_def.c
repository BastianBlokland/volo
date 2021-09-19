#include "check_spec.h"
#include "ecs_def.h"

ecs_comp_define(DefTestCompA, {});

ecs_comp_define(DefTestCompB, {
  u32  fieldA;
  bool fieldB;
});

ecs_view_define(ReadAWriteB, {
  ecs_view_read(DefTestCompA);
  ecs_view_write(DefTestCompB);
});

ecs_system_define(Update, {});

ecs_module_init(def_test_module) {
  ecs_register_comp(DefTestCompA);
  ecs_register_comp(DefTestCompB);

  ecs_register_view(ReadAWriteB);

  ecs_register_system(Update);
}

spec(def) {

  EcsDef* def = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    ecs_register_module(def, def_test_module);
  }

  it("can retrieve the amount of registered components") {
    check_eq_int(ecs_def_comp_count(def), 2);
  }

  it("can retrieve the amount of registered views") { check_eq_int(ecs_def_view_count(def), 1); }

  it("can retrieve the name of registered components") {
    check_eq_string(ecs_def_comp_name(def, ecs_comp_id(DefTestCompA)), string_lit("DefTestCompA"));
    check_eq_string(ecs_def_comp_name(def, ecs_comp_id(DefTestCompB)), string_lit("DefTestCompB"));
  }

  it("can retrieve the size of registered components") {
    check_eq_int(ecs_def_comp_size(def, ecs_comp_id(DefTestCompA)), sizeof(DefTestCompA));
    check_eq_int(ecs_def_comp_size(def, ecs_comp_id(DefTestCompB)), sizeof(DefTestCompB));
  }

  it("can retrieve the alignment requirement of registered components") {
    check_eq_int(ecs_def_comp_align(def, ecs_comp_id(DefTestCompA)), alignof(DefTestCompA));
    check_eq_int(ecs_def_comp_align(def, ecs_comp_id(DefTestCompB)), alignof(DefTestCompB));
  }

  it("can retrieve the name of registered views") {
    check_eq_string(ecs_def_view_name(def, ecs_view_id(ReadAWriteB)), string_lit("ReadAWriteB"));
  }

  it("can retrieve the name of registered systems") {
    check_eq_string(ecs_def_system_name(def, ecs_system_id(Update)), string_lit("Update"));
  }

  teardown() { ecs_def_destroy(def); }
}
