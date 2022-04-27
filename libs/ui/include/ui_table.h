#pragma once
#include "ecs_module.h"
#include "ui_color.h"
#include "ui_rect.h"
#include "ui_units.h"

ecs_comp_extern(UiCanvasComp);

#define ui_table_max_columns 8

typedef enum {
  UiTableColumn_Fixed,
  UiTableColumn_Flexible, // NOTE: Can only be used on the last column.
} UiTableColumnType;

typedef struct {
  UiTableColumnType type;
  f32               width;
} UiTableColumn;

typedef struct {
  UiBase        parent;
  UiAlign       align;
  f32           rowHeight;
  UiVector      spacing;
  UiTableColumn columns[ui_table_max_columns];
  u32           columnCount;
  u32           column;
  u32           row;
} UiTable;

/**
 * Create a layout table.
 * NOTE: Configure columns using 'ui_table_add_column()'
 */
#define ui_table(...)                                                                              \
  ((UiTable){                                                                                      \
      .parent    = UiBase_Container,                                                               \
      .align     = UiAlign_TopLeft,                                                                \
      .rowHeight = 25,                                                                             \
      .spacing   = ui_vector(10, 10),                                                              \
      .column    = sentinel_u32,                                                                   \
      .row       = sentinel_u32,                                                                   \
      __VA_ARGS__})

/**
 * Calculate the total height that the given number of rows will take.
 */
f32 ui_table_height(const UiTable*, u32 rows);

/**
 * Test if the table is currently active.
 * NOTE: Use 'ui_table_next_row()' to activate the first row.
 */
bool ui_table_active(const UiTable*);

/**
 * Add a new column to the table.
 * Pre-condition: !ui_table_active()
 */
void ui_table_add_column(UiTable*, UiTableColumnType, f32 width);

/**
 * Reset the table to the initial state.
 * NOTE: Use 'ui_table_next_row()' to activate the first row.
 * Pre-condition: ui_table_active()
 */
void ui_table_reset(UiTable*);

/**
 * Sets the active rectangle to the first column in the next row.
 * NOTE: If no row is currently active then the first row becomes the active row.
 */
void ui_table_next_row(UiCanvasComp*, UiTable*);

/**
 * Sets the active rectangle to the next column in the current row.
 * Pre-condition: ui_table_active()
 */
void ui_table_next_column(UiCanvasComp*, UiTable*);

/**
 * Draw a background for the current row.
 */
void ui_table_draw_row_bg(UiCanvasComp*, const UiTable*, UiColor color);
