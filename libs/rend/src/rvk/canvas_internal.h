#pragma once
#include "gap_window.h"
#include "rend_size.h"

// Internal forward declarations:
typedef struct sRvkDevice   RvkDevice;
typedef struct sRvkPass     RvkPass;
typedef struct sRvkRenderer RvkRenderer;

typedef struct sRvkCanvas RvkCanvas;

RvkCanvas* rvk_canvas_create(RvkDevice*, const GapWindowComp*);
void       rvk_canvas_destroy(RvkCanvas*);
bool       rvk_canvas_begin(RvkCanvas*, RendSize);
RvkPass*   rvk_canvas_pass_forward(RvkCanvas*);
void       rvk_canvas_end(RvkCanvas*);
