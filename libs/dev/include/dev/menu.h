#pragma once
#include "dev/forward.h"

ecs_comp_extern(DevMenuComp);

EcsEntityId dev_menu_create(EcsWorld*, EcsEntityId window, bool hidden);

void dev_menu_edit_panels_open(EcsWorld*, DevMenuComp*);
void dev_menu_edit_panels_close(EcsWorld*, DevMenuComp*);
