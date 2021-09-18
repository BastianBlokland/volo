#include "check_spec.h"
#include "ecs_meta.h"

typedef struct {
  i32 health;
} TestCompA;

typedef struct {
  i32 health;
} TestCompB;

ecs_comp_define(TestCompA);
ecs_comp_define(TestCompB);

spec(meta) {

  EcsMeta* meta = null;

  setup() {
    meta = ecs_meta_create(g_alloc_heap);
    ecs_comp_register_t(meta, TestCompA);
    ecs_comp_register_t(meta, TestCompB);
  }

  it("can lookup component names") {
    check_eq_string(ecs_comp_name(meta, ecs_comp_id(TestCompA)), string_lit("TestCompA"));
    check_eq_string(ecs_comp_name(meta, ecs_comp_id(TestCompB)), string_lit("TestCompB"));
  }

  teardown() { ecs_meta_destroy(meta); }
}
