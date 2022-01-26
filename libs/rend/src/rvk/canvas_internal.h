#pragma once
#include "gap_window.h"

#include "renderer_internal.h"
#include "types_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;
typedef struct sRvkPass   RvkPass;

typedef struct sRvkCanvas RvkCanvas;

RvkCanvas* rvk_canvas_create(RvkDevice*, const GapWindowComp*);
void       rvk_canvas_destroy(RvkCanvas*);
RvkSize    rvk_canvas_size(const RvkCanvas*);

/**
 * Query for statistics about the previous submitted draw.
 * NOTE: Will block until the previous draw has finished.
 */
RvkRenderStats rvk_canvas_stats(const RvkCanvas*);

bool     rvk_canvas_begin(RvkCanvas*, RvkSize);
RvkPass* rvk_canvas_pass_forward(RvkCanvas*);
void     rvk_canvas_end(RvkCanvas*);
