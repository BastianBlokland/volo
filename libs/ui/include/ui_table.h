#pragma once
#include "ui.h"
#include "ui_units.h"
#include "ui_vector.h"

#define ui_table_max_columns 16

typedef enum {
  UiTableColumn_Fixed,
  UiTableColumn_Flexible, // NOTE: Can only be used on the last column.
} UiTableColumnType;

typedef struct {
  UiTableColumnType type;
  f32               width;
} UiTableColumn;

typedef struct {
  String label, tooltip;
} UiTableColumnName;

typedef struct sUiTable {
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
      .rowHeight = 22,                                                                             \
      .spacing   = ui_vector(8, 8),                                                                \
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
void ui_table_jump_row(UiCanvasComp*, UiTable*, u32 row);

/**
 * Sets the active rectangle to the next column in the current row.
 * Pre-condition: ui_table_active()
 */
void ui_table_next_column(UiCanvasComp*, UiTable*);

/**
 * Draw a table background.
 */
void ui_table_draw_bg(UiCanvasComp*, const UiTable*, u32 rows, UiColor color);

/**
 * Draw a table header.
 * NOTE: Set the current rectangle to remaining content area for the table.
 * Pre-condition: array_size(UiTableColumnName) == table->columnCount.
 */
void ui_table_draw_header(UiCanvasComp*, const UiTable*, const UiTableColumnName[]);

/**
 * Draw a background for the current row.
 */
void ui_table_draw_row_bg(UiCanvasComp*, const UiTable*, UiColor color);
