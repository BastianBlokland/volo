#pragma once
#include "dev.h"

ecs_comp_extern(DevPrefabPreviewComp);

EcsEntityId dev_prefab_panel_open(EcsWorld*, EcsEntityId window, DevPanelType);
