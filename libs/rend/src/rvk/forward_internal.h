#pragma once
#include "core.h"

/**
 * Internal forward header for the rvk library.
 */

typedef enum eRvkDescKind          RvkDescKind;
typedef enum eRvkImageCapability   RvkImageCapability;
typedef enum eRvkImageFlags        RvkImageFlags;
typedef enum eRvkImagePhase        RvkImagePhase;
typedef enum eRvkJobPhase          RvkJobPhase;
typedef enum eRvkStat              RvkStat;
typedef struct sRvkAttachPool      RvkAttachPool;
typedef struct sRvkAttachSpec      RvkAttachSpec;
typedef struct sRvkBuffer          RvkBuffer;
typedef struct sRvkCanvas          RvkCanvas;
typedef struct sRvkDebug           RvkDebug;
typedef struct sRvkDescGroup       RvkDescGroup;
typedef struct sRvkDescMeta        RvkDescMeta;
typedef struct sRvkDescPool        RvkDescPool;
typedef struct sRvkDescSet         RvkDescSet;
typedef struct sRvkDescUpdateBatch RvkDescUpdateBatch;
typedef struct sRvkDevice          RvkDevice;
typedef struct sRvkGraphic         RvkGraphic;
typedef struct sRvkImage           RvkImage;
typedef struct sRvkJob             RvkJob;
typedef struct sRvkLib             RvkLib;
typedef struct sRvkMemPool         RvkMemPool;
typedef struct sRvkMesh            RvkMesh;
typedef struct sRvkPass            RvkPass;
typedef struct sRvkRepository      RvkRepository;
typedef struct sRvkSamplerPool     RvkSamplerPool;
typedef struct sRvkSamplerSpec     RvkSamplerSpec;
typedef struct sRvkShader          RvkShader;
typedef struct sRvkShaderOverride  RvkShaderOverride;
typedef struct sRvkStatRecorder    RvkStatRecorder;
typedef struct sRvkStopwatch       RvkStopwatch;
typedef struct sRvkSwapchainStats  RvkSwapchainStats;
typedef struct sRvkTexture         RvkTexture;
typedef struct sRvkTransferer      RvkTransferer;
typedef struct sRvkUniformPool     RvkUniformPool;
typedef union uRvkSize             RvkSize;
