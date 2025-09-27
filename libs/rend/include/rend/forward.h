#pragma once
#include "core/forward.h"
#include "ecs/module.h"

/**
 * Forward header for the rend library.
 */

ecs_comp_extern(RendErrorComp);
ecs_comp_extern(RendLightComp);
ecs_comp_extern(RendObjectComp);
ecs_comp_extern(RendResComp);
ecs_comp_extern(RendResFinishedComp);
ecs_comp_extern(RendResGraphicComp);
ecs_comp_extern(RendResMeshComp);
ecs_comp_extern(RendResShaderComp);
ecs_comp_extern(RendResTextureComp);
ecs_comp_extern(RendSettingsComp);
ecs_comp_extern(RendSettingsGlobalComp);
ecs_comp_extern(RendStatsComp);

typedef enum eRendAmbientMode   RendAmbientMode;
typedef enum eRendFlags         RendFlags;
typedef enum eRendGlobalFlags   RendGlobalFlags;
typedef enum eRendObjectFlags   RendObjectFlags;
typedef enum eRendObjectRes     RendObjectRes;
typedef enum eRendSkyMode       RendSkyMode;
typedef enum eRendSyncMode      RendSyncMode;
typedef enum eRendTonemapper    RendTonemapper;
typedef struct sRendReport      RendReport;
typedef struct sRendReportEntry RendReportEntry;
