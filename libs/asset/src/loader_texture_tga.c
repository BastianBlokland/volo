#include "asset_texture.h"
#include "core_alloc.h"
#include "ecs_world.h"

#include "repo_internal.h"

void asset_load_tga(EcsWorld* world, EcsEntityId assetEntity, AssetSource* src) {
  (void)src;

  const u32                width  = 42;
  const u32                height = 42;
  const AssetTexturePixel* pixels = null;

  ecs_world_add_t(
      world, assetEntity, AssetTextureComp, .width = width, .height = height, .pixels = pixels);
  ecs_world_add_empty_t(world, assetEntity, AssetLoadedComp);
}
