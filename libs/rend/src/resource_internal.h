#pragma once
#include "ecs_module.h"

// Internal forward declarations:
typedef struct sRvkGraphic RvkGraphic;
typedef struct sRvkShader  RvkShader;
typedef struct sRvkMesh    RvkMesh;
typedef struct sRvkTexture RvkTexture;

ecs_comp_extern_public(RendGraphicComp) { RvkGraphic* graphic; };
ecs_comp_extern_public(RendShaderComp) { RvkShader* shader; };
ecs_comp_extern_public(RendMeshComp) { RvkMesh* mesh; };
ecs_comp_extern_public(RendTextureComp) { RvkTexture* texture; };

void rend_resource_teardown(EcsWorld*);
