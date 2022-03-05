#include "ui_layout.h"

void ui_layout_to_center(UiCanvasComp* canvas, const UiAxis axis) {
  ui_canvas_rect_move(canvas, ui_vector(0.5f, 0.5f), UiOrigin_Current, UiUnits_Current, axis);
}

void ui_layout_from_center(UiCanvasComp* canvas, const UiAxis axis) {
  ui_canvas_rect_move(canvas, ui_vector(-0.5f, -0.5f), UiOrigin_Current, UiUnits_Current, axis);
}
