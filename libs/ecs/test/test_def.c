#include "check/spec.h"
#include "core/alloc.h"
#include "core/diag.h"
#include "ecs/def.h"

/**
 * Private components can be shared between compilation units using 'ecs_comp_extern' but the other
 * compilation-units wont have the definition so they cannot access its fields.
 */
ecs_comp_extern(DefCompA);
ecs_comp_define(DefCompA) { u32 fieldA; };

/**
 * Public components can share their struct definition between compilation units using
 * 'ecs_comp_extern_public' this means other compilation-units will be able to acces the component
 * fields also.
 */
ecs_comp_extern_public(DefCompB) {
  u32  fieldA;
  bool fieldB;
};
ecs_comp_define(DefCompB);

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

typedef struct {
  u32 val;
} DefInitContext;

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

  const DefInitContext* ctx = ecs_init_ctx();
  if (ctx->val != 42) {
    diag_crash_msg("Invalid module init context");
  }

  ecs_order(CleanupSys, 1337);

  ecs_parallel(UpdateSys, 42);
}

spec(def) {

  EcsDef* def = null;

  setup() {
    def = ecs_def_create(g_allocHeap);

    const DefInitContext ctx = {
        .val = 42,
    };
    ecs_register_module_with_context(def, def_test_module, &ctx);
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

  it("can retrieve the default order of a system") {
    check_eq_int(ecs_def_system_order(def, ecs_system_id(EmptySys)), 0);
    check_eq_int(ecs_def_system_order(def, ecs_system_id(UpdateSys)), 0);
  }

  it("can retrieve the overriden order of a system") {
    check_eq_int(ecs_def_system_order(def, ecs_system_id(CleanupSys)), 1337);
  }

  it("can retrieve the default parallel count of a system") {
    check_eq_int(ecs_def_system_parallel(def, ecs_system_id(EmptySys)), 1);
    check_eq_int(ecs_def_system_parallel(def, ecs_system_id(CleanupSys)), 1);
  }

  it("can retrieve the overriden parallel count of a system") {
    check_eq_int(ecs_def_system_parallel(def, ecs_system_id(UpdateSys)), 42);
  }

  it("can retrieve the views of a registered system") {
    check_eq_int(ecs_def_system_views(def, ecs_system_id(EmptySys)).count, 0);

    check_eq_int(ecs_def_system_views(def, ecs_system_id(UpdateSys)).count, 2);
    check(
        ecs_def_system_views(def, ecs_system_id(UpdateSys)).values[0] == ecs_view_id(ReadAWriteB));
    check(ecs_def_system_views(def, ecs_system_id(UpdateSys)).values[1] == ecs_view_id(ReadAReadB));

    check_eq_int(ecs_def_system_views(def, ecs_system_id(CleanupSys)).count, 1);
    check(
        ecs_def_system_views(def, ecs_system_id(CleanupSys)).values[0] == ecs_view_id(ReadAReadB));
  }

  it("can check if a system has access to a view") {
    check(ecs_def_system_has_access(def, ecs_system_id(UpdateSys), ecs_view_id(ReadAWriteB)));
    check(ecs_def_system_has_access(def, ecs_system_id(UpdateSys), ecs_view_id(ReadAReadB)));

    check(!ecs_def_system_has_access(def, ecs_system_id(CleanupSys), ecs_view_id(ReadAWriteB)));
    check(ecs_def_system_has_access(def, ecs_system_id(CleanupSys), ecs_view_id(ReadAReadB)));
  }

  it("can retrieve module name of a component") {
    const EcsModuleId moduleId = ecs_def_comp_module(def, ecs_comp_id(DefCompA));
    check_eq_string(ecs_def_module_name(def, moduleId), string_lit("def_test_module"));
  }

  it("can retrieve module name of a view") {
    const EcsModuleId moduleId = ecs_def_view_module(def, ecs_view_id(ReadAWriteB));
    check_eq_string(ecs_def_module_name(def, moduleId), string_lit("def_test_module"));
  }

  it("can retrieve module name of a system") {
    const EcsModuleId moduleId = ecs_def_system_module(def, ecs_system_id(CleanupSys));
    check_eq_string(ecs_def_module_name(def, moduleId), string_lit("def_test_module"));
  }

  teardown() { ecs_def_destroy(def); }
}
