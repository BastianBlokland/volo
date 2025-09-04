#pragma once
#include "ecs/module.h"

ecs_comp_extern_public(SceneNameComp) {
  StringHash nameLoc;   // Localization key, 0 if entity has no user facing name.
  StringHash nameDebug; // Debug name, stored in the global string-table.
};
