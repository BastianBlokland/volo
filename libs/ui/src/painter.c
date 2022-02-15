#include "ecs_world.h"
#include "ui_painter.h"

ecs_comp_define(UiPainterComp) { i32 dummy; };

ecs_module_init(ui_painter_module) { ecs_register_comp(UiPainterComp); }
