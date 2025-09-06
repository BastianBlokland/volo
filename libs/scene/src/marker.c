#include "core/array.h"
#include "scene/marker.h"

ecs_comp_define(SceneMarkerComp);

ecs_module_init(scene_marker_module) { ecs_register_comp(SceneMarkerComp); }

String scene_marker_name(const SceneMarkerType type) {
  static const String g_names[] = {
      string_static("Info"),
  };
  ASSERT(array_elems(g_names) == SceneMarkerType_Count, "Incorrect number of names");
  return g_names[type];
}
