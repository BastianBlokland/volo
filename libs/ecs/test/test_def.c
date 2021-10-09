#include "check_spec.h"
#include "core_alloc.h"
#include "ecs_def.h"

ecs_comp_define(DefCompA) { u32 fieldA; };

ecs_comp_define(DefCompB) {
  u32  fieldA;
  bool fieldB;
};

ecs_comp_define(DefCompEmpty);

ecs_view_define(ReadAWriteB) {
  ecs_access_read(DefCompA);
  ecs_access_write(DefCompB);
}

ecs_view_define(ReadAReadB) {
  ecs_access_read(DefCompA);
  ecs_access_read(DefCompB);
}

ecs_view_define(Empty) {}

ecs_system_define(Update) {}
ecs_system_define(Cleanup) {}

ecs_module_init(def_test_module) {
  ecs_register_comp(DefCompA);
  ecs_register_comp(DefCompB);
  ecs_register_comp_empty(DefCompEmpty);

  ecs_register_view(ReadAWriteB);
  ecs_register_view(ReadAReadB);
  ecs_register_view(Empty);

  ecs_register_system(Update, ecs_view_id(ReadAWriteB), ecs_view_id(ReadAReadB));
  ecs_register_system(Cleanup, ecs_view_id(ReadAReadB));
}

spec(def) {

  EcsDef* def = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    ecs_register_module(def, def_test_module);
  }

  it("can retrieve the amount of registered components") {
    check_eq_int(ecs_def_comp_count(def), 3);
  }

  it("can retrieve the amount of registered views") { check_eq_int(ecs_def_view_count(def), 3); }

  it("can retrieve the name of registered components") {
    check_eq_string(ecs_def_comp_name(def, ecs_comp_id(DefCompA)), string_lit("DefCompA"));
    check_eq_string(ecs_def_comp_name(def, ecs_comp_id(DefCompB)), string_lit("DefCompB"));
    check_eq_string(ecs_def_comp_name(def, ecs_comp_id(DefCompEmpty)), string_lit("DefCompEmpty"));
  }

  it("can retrieve the size of registered components") {
    check_eq_int(ecs_def_comp_size(def, ecs_comp_id(DefCompA)), sizeof(DefCompA));
    check_eq_int(ecs_def_comp_size(def, ecs_comp_id(DefCompB)), sizeof(DefCompB));
    check_eq_int(ecs_def_comp_size(def, ecs_comp_id(DefCompEmpty)), 0);
  }

  it("can retrieve the alignment requirement of registered components") {
    check_eq_int(ecs_def_comp_align(def, ecs_comp_id(DefCompA)), alignof(DefCompA));
    check_eq_int(ecs_def_comp_align(def, ecs_comp_id(DefCompB)), alignof(DefCompB));
    check_eq_int(ecs_def_comp_align(def, ecs_comp_id(DefCompEmpty)), 1);
  }

  it("can retrieve the name of registered views") {
    check_eq_string(ecs_def_view_name(def, ecs_view_id(ReadAWriteB)), string_lit("ReadAWriteB"));
  }

  it("can retrieve the name of registered systems") {
    check_eq_string(ecs_def_system_name(def, ecs_system_id(Update)), string_lit("Update"));
    check_eq_string(ecs_def_system_name(def, ecs_system_id(Cleanup)), string_lit("Cleanup"));
  }

  it("can retrieve the views of a registered system") {
    check_eq_int(ecs_def_system_views(def, ecs_system_id(Update)).count, 2);
    check(ecs_def_system_views(def, ecs_system_id(Update)).head[0] == ecs_view_id(ReadAWriteB));
    check(ecs_def_system_views(def, ecs_system_id(Update)).head[1] == ecs_view_id(ReadAReadB));

    check_eq_int(ecs_def_system_views(def, ecs_system_id(Cleanup)).count, 1);
    check(ecs_def_system_views(def, ecs_system_id(Cleanup)).head[0] == ecs_view_id(ReadAReadB));
  }

  it("can check if a system has access to a view") {
    check(ecs_def_system_has_access(def, ecs_system_id(Update), ecs_view_id(ReadAWriteB)));
    check(ecs_def_system_has_access(def, ecs_system_id(Update), ecs_view_id(ReadAReadB)));

    check(!ecs_def_system_has_access(def, ecs_system_id(Cleanup), ecs_view_id(ReadAWriteB)));
    check(ecs_def_system_has_access(def, ecs_system_id(Cleanup), ecs_view_id(ReadAReadB)));
  }

  teardown() { ecs_def_destroy(def); }
}
