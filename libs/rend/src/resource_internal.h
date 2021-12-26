#pragma once
#include "ecs_module.h"

// Internal forward declarations:
typedef struct sRvkGraphic RvkGraphic;
typedef struct sRvkShader  RvkShader;
typedef struct sRvkMesh    RvkMesh;
typedef struct sRvkTexture RvkTexture;

ecs_comp_extern_public(RendResGraphicComp) { RvkGraphic* graphic; };
ecs_comp_extern_public(RendResShaderComp) { RvkShader* shader; };
ecs_comp_extern_public(RendResMeshComp) { RvkMesh* mesh; };
ecs_comp_extern_public(RendResTextureComp) { RvkTexture* texture; };

void rend_resource_teardown(EcsWorld*);
