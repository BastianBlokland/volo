#include "core_diag.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_shape.h"
#include "ui_style.h"
#include "ui_table.h"

static UiDir ui_table_column_dir(const UiAlign align) {
  switch (align) {
  case UiAlign_TopLeft:
  case UiAlign_MiddleLeft:
  case UiAlign_BottomLeft:
    return Ui_Right;
  case UiAlign_TopCenter:
  case UiAlign_MiddleCenter:
  case UiAlign_BottomCenter:
  case UiAlign_TopRight:
  case UiAlign_MiddleRight:
  case UiAlign_BottomRight:
    return Ui_Left;
  }
  diag_crash();
}

static UiDir ui_table_row_dir(const UiAlign align) {
  switch (align) {
  case UiAlign_TopLeft:
  case UiAlign_TopCenter:
  case UiAlign_TopRight:
  case UiAlign_MiddleLeft:
  case UiAlign_MiddleCenter:
  case UiAlign_MiddleRight:
    return Ui_Down;
  case UiAlign_BottomLeft:
  case UiAlign_BottomCenter:
  case UiAlign_BottomRight:
    return Ui_Up;
  }
  diag_crash();
}

static UiAlign ui_table_align_opposite(const UiAlign align) {
  switch (align) {
  case UiAlign_TopLeft:
    return UiAlign_BottomRight;
  case UiAlign_MiddleLeft:
    return UiAlign_BottomRight;
  case UiAlign_BottomLeft:
    return UiAlign_TopRight;
  case UiAlign_TopCenter:
  case UiAlign_TopRight:
    return UiAlign_BottomLeft;
  case UiAlign_MiddleCenter:
  case UiAlign_MiddleRight:
    return UiAlign_BottomLeft;
  case UiAlign_BottomCenter:
  case UiAlign_BottomRight:
    return UiAlign_TopLeft;
  }
  diag_crash();
}

f32 ui_table_height(const UiTable* table, const u32 rows) {
  return rows * table->rowHeight + (rows + 1) * table->spacing.y;
}

bool ui_table_active(const UiTable* table) { return !sentinel_check(table->row); }

void ui_table_add_column(UiTable* table, const UiTableColumnType type, const f32 width) {
  diag_assert_msg(!ui_table_active(table), "Column cannot be added: Table is already active");
  diag_assert_msg(table->columnCount < ui_table_max_columns, "Max column count exceeded");
  table->columns[table->columnCount++] = (UiTableColumn){
      .type  = type,
      .width = width,
  };
}

void ui_table_next_row(UiCanvasComp* canvas, UiTable* table) {
  const UiDir rowDir = ui_table_row_dir(table->align);

  if (!ui_table_active(table)) {
    /**
     * First row: Initialize the position and cell height.
     */
    ui_layout_move_to(canvas, table->parent, table->align, Ui_Y);
    ui_layout_resize(canvas, table->align, ui_vector(0, table->rowHeight), UiBase_Absolute, Ui_Y);
    ui_layout_move_dir(canvas, rowDir, table->spacing.y, UiBase_Absolute);
    table->row = 0;
  } else {
    /**
     * Continuation row: Advance the y position.
     */
    const f32 offset = table->rowHeight + table->spacing.y;
    ui_layout_move_dir(canvas, rowDir, offset, UiBase_Absolute);
    ++table->row;
  }

  /**
   * Initialize the first column.
   */
  table->column = sentinel_u32;
  ui_table_next_column(canvas, table);
}

void ui_table_next_column(UiCanvasComp* canvas, UiTable* table) {
  diag_assert_msg(ui_table_active(table), "Column cannot be advanced: No row is active");
  const UiDir columnDir = ui_table_column_dir(table->align);

  if (sentinel_check(table->column)) {
    /**
     * First column: Initialize the x position.
     */
    ui_layout_move_to(canvas, table->parent, table->align, Ui_X);
    ui_layout_move_dir(canvas, columnDir, table->spacing.x, UiBase_Absolute);
    table->column = 0;
  } else {
    /**
     * Continuation column: Advance the x position.
     */
    diag_assert_msg(table->column < table->columnCount, "No more columns in the table");
    const f32 offset = table->columns[table->column].width + table->spacing.x;
    ui_layout_move_dir(canvas, columnDir, offset, UiBase_Absolute);
    ++table->column;
  }

  /**
   * Set the cell width.
   */
  switch (table->columns[table->column].type) {
  case UiTableColumn_Fixed: {
    const UiVector cellSize = ui_vector(table->columns[table->column].width, 0);
    ui_layout_resize(canvas, table->align, cellSize, UiBase_Absolute, Ui_X);
  } break;
  case UiTableColumn_Flexible: {
    // Grow the cell to the end of the container.
    const UiAlign endAlign = ui_table_align_opposite(table->align);
    ui_layout_resize_to(canvas, table->parent, endAlign, Ui_X);
    // Shrink the cell by the spacing (to avoid ending at the very edge of the container).
    ui_layout_grow(canvas, table->align, ui_vector(-table->spacing.x, 0), UiBase_Absolute, Ui_X);
  } break;
  }
}

void ui_table_draw_row_bg(UiCanvasComp* canvas, const UiTable* table) {
  ui_layout_push(canvas);

  const UiAlign endAlign = ui_table_align_opposite(table->align);
  ui_layout_move_to(canvas, table->parent, table->align, Ui_X);
  ui_layout_resize_to(canvas, table->parent, endAlign, Ui_X);
  ui_layout_grow(
      canvas, UiAlign_MiddleCenter, ui_vector(0, table->spacing.y), UiBase_Absolute, Ui_Y);

  ui_style_push(canvas);
  ui_style_color(canvas, ui_color(48, 48, 48, 192));
  ui_style_outline(canvas, 1);
  ui_canvas_draw_glyph(canvas, UiShape_Square, 0, UiFlags_None);
  ui_style_pop(canvas);

  ui_layout_pop(canvas);
}
