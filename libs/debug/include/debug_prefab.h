#pragma once
#include "debug_panel.h"
#include "ecs_module.h"

ecs_comp_extern(DebugPrefabPreviewComp);

EcsEntityId debug_prefab_panel_open(EcsWorld*, EcsEntityId window, DebugPanelType);
