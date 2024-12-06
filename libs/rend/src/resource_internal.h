#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "rend_resource.h"

#include "forward_internal.h"
#include "rvk/forward_internal.h"

ecs_comp_extern_public(RendResGraphicComp) {
  RvkDevice*        device;
  const RvkGraphic* graphic;
};

ecs_comp_extern_public(RendResShaderComp) {
  RvkDevice*       device;
  const RvkShader* shader;
};

ecs_comp_extern_public(RendResMeshComp) {
  RvkDevice*     device;
  const RvkMesh* mesh;
};

ecs_comp_extern_public(RendResTextureComp) {
  RvkDevice*        device;
  const RvkTexture* texture;
};

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
 * NOTE: Can fail if the resource is currently being unloaded.
 * Returns 'true' if the resource was successfully requested, otherwise false.
 */
bool rend_res_request(EcsWorld* world, EcsEntityId assetEntity);

/**
 * Mark this resource as in-use.
 * Resources that haven't been used for a while will be unloaded.
 */
void rend_res_mark_used(RendResComp*);

void rend_res_teardown(EcsWorld* world, const RendResComp*, EcsEntityId entity);
