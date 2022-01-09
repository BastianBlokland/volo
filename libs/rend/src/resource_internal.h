#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

// Internal forward declarations:
typedef struct sRvkGraphic RvkGraphic;
typedef struct sRvkShader  RvkShader;
typedef struct sRvkMesh    RvkMesh;
typedef struct sRvkTexture RvkTexture;

ecs_comp_extern(RendResComp);
ecs_comp_extern_public(RendResGraphicComp) { RvkGraphic* graphic; };
ecs_comp_extern_public(RendResShaderComp) { RvkShader* shader; };
ecs_comp_extern_public(RendResMeshComp) { RvkMesh* mesh; };
ecs_comp_extern_public(RendResTextureComp) { RvkTexture* texture; };

/**
 * Component that indicates that this resource has finished loading and can be used.
 */
ecs_comp_extern(RendResFinishedComp);

/**
 * Component that indicates that this resource is currently being unloaded and should not be used.
 */
ecs_comp_extern(RendResUnloadComp);

/**
 * Request a render resource to be loaded for the given asset.
 */
void rend_resource_request(EcsWorld* world, EcsEntityId assetEntity);

/**
 * Mark this resource as in-use.
 * Resources that haven't been used for a while will be unloaded.
 */
void rend_resource_mark_used(RendResComp*);
