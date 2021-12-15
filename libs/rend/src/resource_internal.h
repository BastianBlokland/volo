#pragma once
#include "ecs_module.h"

#include "rvk/graphic_internal.h"
#include "rvk/shader_internal.h"

ecs_comp_extern_public(RendGraphicComp) { RvkGraphic* graphic; };
ecs_comp_extern_public(RendShaderComp) { RvkShader* shader; };
