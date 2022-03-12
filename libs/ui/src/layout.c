#include "ui_canvas.h"
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

void ui_layout_next(UiCanvasComp* canvas, const UiDir dir, const f32 spacing) {
  switch (dir) {
  case Ui_Right:
    ui_layout_move(canvas, ui_vector(1, 0), UiBase_Current, Ui_X);
    ui_layout_move(canvas, ui_vector(spacing, 0), UiBase_Absolute, Ui_X);
    break;
  case Ui_Left:
    ui_layout_move(canvas, ui_vector(-1, 0), UiBase_Current, Ui_X);
    ui_layout_move(canvas, ui_vector(-spacing, 0), UiBase_Absolute, Ui_X);
    break;
  case Ui_Up:
    ui_layout_move(canvas, ui_vector(0, 1), UiBase_Current, Ui_Y);
    ui_layout_move(canvas, ui_vector(0, spacing), UiBase_Absolute, Ui_Y);
    break;
  case Ui_Down:
    ui_layout_move(canvas, ui_vector(0, -1), UiBase_Current, Ui_Y);
    ui_layout_move(canvas, ui_vector(0, -spacing), UiBase_Absolute, Ui_Y);
    break;
  }
}

void ui_layout_resize(
    UiCanvasComp*  canvas,
    const UiAlign  origin,
    const UiVector size,
    const UiBase   units,
    const UiAxis   axis) {
  ui_canvas_rect_size(canvas, size, units, Ui_XY);
  switch (origin) {
  case UiAlign_TopLeft:
    ui_canvas_rect_pos(canvas, UiBase_Current, ui_vector(0.0, -1.0), UiBase_Current, axis);
    break;
  case UiAlign_TopCenter:
    ui_canvas_rect_pos(canvas, UiBase_Current, ui_vector(-0.5, -1.0), UiBase_Current, axis);
    break;
  case UiAlign_TopRight:
    ui_canvas_rect_pos(canvas, UiBase_Current, ui_vector(-1.0, -1.0), UiBase_Current, axis);
    break;
  case UiAlign_MiddleLeft:
    ui_canvas_rect_pos(canvas, UiBase_Current, ui_vector(0.0, -0.5), UiBase_Current, axis);
    break;
  case UiAlign_MiddleCenter:
    ui_canvas_rect_pos(canvas, UiBase_Current, ui_vector(-0.5, -0.5), UiBase_Current, axis);
    break;
  case UiAlign_MiddleRight:
    ui_canvas_rect_pos(canvas, UiBase_Current, ui_vector(-1.0, -0.5), UiBase_Current, axis);
    break;
  case UiAlign_BottomLeft:
    break;
  case UiAlign_BottomCenter:
    ui_canvas_rect_pos(canvas, UiBase_Current, ui_vector(-0.5, 0.0), UiBase_Current, axis);
    break;
  case UiAlign_BottomRight:
    ui_canvas_rect_pos(canvas, UiBase_Current, ui_vector(-1.0, 0.0), UiBase_Current, axis);
    break;
  }
}

void ui_layout_set_rect(UiCanvasComp* canvas, const UiRect rect) {
  ui_canvas_rect_pos(canvas, UiBase_Absolute, rect.pos, UiBase_Absolute, Ui_XY);
  ui_canvas_rect_size(canvas, rect.size, UiBase_Absolute, Ui_XY);
}
