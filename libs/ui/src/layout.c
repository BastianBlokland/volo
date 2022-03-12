#include "ui_layout.h"

void ui_layout_move(
    UiCanvasComp* canvas, const UiVector offset, const UiBase units, const UiAxis axis) {
  ui_canvas_rect_pos(canvas, UiBase_Current, offset, units, axis);
}

void ui_layout_move_to(
    UiCanvasComp* canvas, const UiBase base, const UiAlign align, const UiAxis axis) {
  switch (align) {
  case UiAlign_TopLeft:
    ui_canvas_rect_pos(canvas, base, ui_vector(0.0, 1.0), base, axis);
    break;
  case UiAlign_TopCenter:
    ui_canvas_rect_pos(canvas, base, ui_vector(0.5, 1.0), base, axis);
    break;
  case UiAlign_TopRight:
    ui_canvas_rect_pos(canvas, base, ui_vector(1.0, 1.0), base, axis);
    break;
  case UiAlign_MiddleLeft:
    ui_canvas_rect_pos(canvas, base, ui_vector(0.0, 0.5), base, axis);
    break;
  case UiAlign_MiddleCenter:
    ui_canvas_rect_pos(canvas, base, ui_vector(0.5, 0.5), base, axis);
    break;
  case UiAlign_MiddleRight:
    ui_canvas_rect_pos(canvas, base, ui_vector(1.0, 0.5), base, axis);
    break;
  case UiAlign_BottomLeft:
    ui_canvas_rect_pos(canvas, base, ui_vector(0.0, 0.0), base, axis);
    break;
  case UiAlign_BottomCenter:
    ui_canvas_rect_pos(canvas, base, ui_vector(0.5, 0.0), base, axis);
    break;
  case UiAlign_BottomRight:
    ui_canvas_rect_pos(canvas, base, ui_vector(1.0, 0.0), base, axis);
    break;
  }
}

void ui_layout_from_center(UiCanvasComp* canvas, const UiAxis axis) {
  ui_canvas_rect_pos(canvas, UiBase_Current, ui_vector(-0.5f, -0.5f), UiBase_Current, axis);
}

void ui_layout_set_rect(UiCanvasComp* canvas, const UiRect rect) {
  ui_canvas_rect_pos(canvas, UiBase_Absolute, rect.pos, UiBase_Absolute, Ui_XY);
  ui_canvas_rect_size(canvas, rect.size, UiBase_Absolute, Ui_XY);
}
