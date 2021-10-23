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

ecs_view_define(EmptyView) {}

ecs_system_define(EmptySys) {}
ecs_system_define(UpdateSys) {}
ecs_system_define(CleanupSys) {}

ecs_module_init(def_test_module) {
  ecs_register_comp(DefCompA);
  ecs_register_comp(DefCompB);
  ecs_register_comp_empty(DefCompEmpty);

  ecs_register_view(ReadAWriteB);
  ecs_register_view(ReadAReadB);
  ecs_register_view(EmptyView);

  ecs_register_system(EmptySys);
  ecs_register_system(UpdateSys, ecs_view_id(ReadAWriteB), ecs_view_id(ReadAReadB));
  ecs_register_system(CleanupSys, ecs_view_id(ReadAReadB));
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
    check_eq_string(ecs_def_system_name(def, ecs_system_id(UpdateSys)), string_lit("UpdateSys"));
    check_eq_string(ecs_def_system_name(def, ecs_system_id(CleanupSys)), string_lit("CleanupSys"));
  }

  it("can retrieve the views of a registered system") {
    check_eq_int(ecs_def_system_views(def, ecs_system_id(EmptySys)).count, 0);

    check_eq_int(ecs_def_system_views(def, ecs_system_id(UpdateSys)).count, 2);
    check(ecs_def_system_views(def, ecs_system_id(UpdateSys)).head[0] == ecs_view_id(ReadAWriteB));
    check(ecs_def_system_views(def, ecs_system_id(UpdateSys)).head[1] == ecs_view_id(ReadAReadB));

    check_eq_int(ecs_def_system_views(def, ecs_system_id(CleanupSys)).count, 1);
    check(ecs_def_system_views(def, ecs_system_id(CleanupSys)).head[0] == ecs_view_id(ReadAReadB));
  }

  it("can check if a system has access to a view") {
    check(ecs_def_system_has_access(def, ecs_system_id(UpdateSys), ecs_view_id(ReadAWriteB)));
    check(ecs_def_system_has_access(def, ecs_system_id(UpdateSys), ecs_view_id(ReadAReadB)));

    check(!ecs_def_system_has_access(def, ecs_system_id(CleanupSys), ecs_view_id(ReadAWriteB)));
    check(ecs_def_system_has_access(def, ecs_system_id(CleanupSys), ecs_view_id(ReadAReadB)));
  }

  teardown() { ecs_def_destroy(def); }
}
