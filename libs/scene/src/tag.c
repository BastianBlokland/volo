#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "scene_tag.h"

ecs_comp_define_public(SceneTagComp);

static void ecs_combine_tags(void* dataA, void* dataB) {
  SceneTagComp* compA = dataA;
  SceneTagComp* compB = dataB;
  compA->tags |= compB->tags;
}

ecs_module_init(scene_tag_module) {
  ecs_register_comp(SceneTagComp, .combinator = ecs_combine_tags);
}

String scene_tag_name(const SceneTags tags) {
  diag_assert_msg(bits_popcnt(tags) == 1, "Exactly one tag should be set");
  const u32           index     = bits_ctz_32(tags);
  static const String g_names[] = {
      string_static("Background"),
      string_static("Geometry"),
      string_static("Debug"),
      string_static("Selected"),
  };
  ASSERT(array_elems(g_names) == SceneTags_Count, "Incorrect number of tag names");
  return g_names[index];
}

void scene_tag_add(EcsWorld* world, const EcsEntityId entity, const SceneTags tags) {
  ecs_world_add_t(world, entity, SceneTagComp, .tags = tags);
}

bool scene_tag_filter(const SceneTagFilter filter, const SceneTags tags) {
  return ((tags & filter.required) == filter.required) && ((tags & filter.illegal) == 0);
}
