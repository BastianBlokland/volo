#pragma once
#include "dev/forward.h"

ecs_comp_extern(DevInspectorSettingsComp);

EcsEntityId dev_inspector_panel_open(EcsWorld*, EcsEntityId window, DevPanelType);

bool dev_inspector_picker_active(const DevInspectorSettingsComp*);
void dev_inspector_picker_update(DevInspectorSettingsComp*, EcsEntityId result);
void dev_inspector_picker_close(DevInspectorSettingsComp*);
