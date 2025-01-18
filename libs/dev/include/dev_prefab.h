#pragma once
#include "dev.h"

ecs_comp_extern(DebugPrefabPreviewComp);

EcsEntityId debug_prefab_panel_open(EcsWorld*, EcsEntityId window, DebugPanelType);
