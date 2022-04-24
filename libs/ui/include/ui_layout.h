#pragma once
#include "ecs_module.h"
#include "ui_rect.h"
#include "ui_units.h"

ecs_comp_extern(UiCanvasComp);

/**
 * Push / Pop an element to / from the rectangle stack.
 * Usefull for local changes to the current rectangle with an easy way to restore the previous.
 */
void ui_layout_push(UiCanvasComp*);
void ui_layout_pop(UiCanvasComp*);

/**
 * Push / Pop an element to / from the container stack.
 * When pushing a new container the current rectangle value will be used.
 */
void ui_layout_container_push(UiCanvasComp*);
void ui_layout_container_pop(UiCanvasComp*);

/**
 * Move the origin of the current rectangle.
 */
void ui_layout_move(UiCanvasComp*, UiVector, UiBase units, UiAxis);
void ui_layout_move_dir(UiCanvasComp*, UiDir, f32 value, UiBase units);
void ui_layout_move_to(UiCanvasComp*, UiBase, UiAlign, UiAxis);
void ui_layout_next(UiCanvasComp*, UiDir, f32 spacing);

/**
 * Update the current rectangle size, from a specific origin in the new size.
 */
void ui_layout_grow(UiCanvasComp*, UiAlign origin, UiVector delta, UiBase units, UiAxis);
void ui_layout_resize(UiCanvasComp*, UiAlign origin, UiVector size, UiBase units, UiAxis);
void ui_layout_resize_to(UiCanvasComp*, UiBase, UiAlign, UiAxis);

/**
 * Set a specific rectangle.
 */
void ui_layout_set(UiCanvasComp*, UiRect);
void ui_layout_inner(UiCanvasComp*, UiBase parent, UiAlign, UiVector size, UiBase units);

typedef struct {
  UiBase   parent;
  UiAlign  align;
  UiVector size;
  f32      spacing;
  UiBase   units;
} UiGridOpts;

typedef struct {
  UiDir    colDir, rowDir;
  UiVector size;
  f32      spacing;
  UiBase   units;
  u16      col, row;
} UiGridState;

// clang-format off

/**
 * Initialize a layout grid.
 */
#define ui_grid_init(_CANVAS_, ...) ui_grid_init_with_opts((_CANVAS_),                             \
  &((UiGridOpts){                                                                                  \
    .parent      = UiBase_Container,                                                               \
    .align       = UiAlign_TopLeft,                                                                \
    .size        = {100, 100},                                                                     \
    .spacing     = 10,                                                                             \
    .units       = UiBase_Absolute,                                                                \
    __VA_ARGS__}))

// clang-format on

UiGridState ui_grid_init_with_opts(UiCanvasComp*, const UiGridOpts*);
void        ui_grid_next_col(UiCanvasComp*, UiGridState*);
void        ui_grid_next_row(UiCanvasComp*, UiGridState*);
