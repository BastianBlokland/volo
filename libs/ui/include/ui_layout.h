#pragma once
#include "ecs_module.h"
#include "ui_rect.h"
#include "ui_units.h"

ecs_comp_extern(UiCanvasComp);

/**
 * Move the origin of the current rectangle.
 */
void ui_layout_move(UiCanvasComp*, UiVector, UiBase units, UiAxis);
void ui_layout_move_to(UiCanvasComp*, UiBase, UiAlign, UiAxis);

/**
 * Update the rectangle so that the center is at the lower-left of the previous rectangle.
 */
void ui_layout_from_center(UiCanvasComp*, UiAxis);

/**
 * Set a specific rectangle in absolute pixels.
 */
void ui_layout_set_rect(UiCanvasComp*, UiRect);
