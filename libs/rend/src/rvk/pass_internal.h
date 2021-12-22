#pragma once
#include "core_types.h"
#include "rend_color.h"
#include "rend_size.h"

#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice  RvkDevice;
typedef struct sRvkGraphic RvkGraphic;
typedef struct sRvkImage   RvkImage;

typedef struct sRvkPass RvkPass;

typedef struct {
  RvkGraphic** values;
  usize        count;
} RvkPassDrawList;

RvkPass*     rvk_pass_create(RvkDevice*, VkCommandBuffer);
void         rvk_pass_destroy(RvkPass*);
bool         rvk_pass_active(const RvkPass*);
VkRenderPass rvk_pass_vkrenderpass(const RvkPass*);

RvkImage* rvk_pass_output(RvkPass*);
void      rvk_pass_output_barrier(RvkPass*);

void rvk_pass_setup(RvkPass*, RendSize size);
bool rvk_pass_prepare(RvkPass*, RvkGraphic*);

void rvk_pass_begin(RvkPass*, RendColor clearColor);
void rvk_pass_draw(RvkPass*, RvkPassDrawList);
void rvk_pass_end(RvkPass*);
