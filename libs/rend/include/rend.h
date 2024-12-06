#pragma once
#include "core.h"
#include "ecs_module.h"

/**
 * Forward header for the rend library.
 */

ecs_comp_extern(RendLightComp);
ecs_comp_extern(RendObjectComp);
ecs_comp_extern(RendResComp);
ecs_comp_extern(RendResGraphicComp);
ecs_comp_extern(RendResMeshComp);
ecs_comp_extern(RendResShaderComp);
ecs_comp_extern(RendResTextureComp);
ecs_comp_extern(RendSettingsComp);
ecs_comp_extern(RendSettingsGlobalComp);
ecs_comp_extern(RendStatsComp);

typedef enum eRendAmbientMode RendAmbientMode;
typedef enum eRendFlags       RendFlags;
typedef enum eRendGlobalFlags RendGlobalFlags;
typedef enum eRendObjectFlags RendObjectFlags;
typedef enum eRendObjectRes   RendObjectRes;
typedef enum eRendPresentMode RendPresentMode;
typedef enum eRendSkyMode     RendSkyMode;
typedef enum eRendTonemapper  RendTonemapper;
