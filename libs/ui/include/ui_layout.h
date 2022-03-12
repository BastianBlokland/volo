#pragma once
#include "ui_canvas.h"

/**
 * Move the origin of the current rectangle..
 */
void ui_layout_move_to(UiCanvasComp*, UiBase, UiAlign, UiAxis);

/**
 * Update the rectangle so that the center is at the lower-left of the previous rectangle.
 */
void ui_layout_from_center(UiCanvasComp*, UiAxis);

/**
 * Set a specific rectangle in absolute pixels.
 */
void ui_layout_set_rect(UiCanvasComp*, UiRect);
