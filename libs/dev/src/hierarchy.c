#include "core_alloc.h"
#include "core_array.h"
#include "core_dynarray.h"
#include "core_dynbitset.h"
#include "core_format.h"
#include "core_stringtable.h"
#include "dev_hierarchy.h"
#include "dev_panel.h"
#include "ecs_entity.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "input_manager.h"
#include "scene.h"
#include "scene_attachment.h"
#include "scene_lifetime.h"
#include "scene_name.h"
#include "scene_projectile.h"
#include "scene_set.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_panel.h"
#include "ui_scrollview.h"
#include "ui_shape.h"
#include "ui_style.h"
#include "ui_table.h"
#include "ui_widget.h"

typedef u32 HierarchyLinkId;

typedef enum {
  HierarchyLinkMask_None       = 0,
  HierarchyLinkMask_Lifetime   = 1 << 0,
  HierarchyLinkMask_Attachment = 1 << 1,
  HierarchyLinkMask_Instigator = 1 << 2,
} HierarchyLinkMask;

typedef struct {
  HierarchyLinkMask mask;
  HierarchyLinkId   next;
  EcsEntityId       entity;
} HierarchyLink;

typedef struct {
  HierarchyLinkMask parentMask, childMask;
  EcsEntityId       entity;
  StringHash        nameHash;
  HierarchyLinkId   linkHead;
} HierarchyEntry;

ecs_comp_define(DevHierarchyPanelComp) {
  UiPanel      panel;
  u32          panelRowCount;
  UiScrollview scrollview;
  DynArray     entries; // HierarchyEntry[]
  DynArray     links;   // HierarchyLink[]
  DynBitSet    openEntities;

  EcsEntityId lastMainSelection;
};

static void ecs_destruct_hierarchy_panel(void* data) {
  DevHierarchyPanelComp* comp = data;
  dynarray_destroy(&comp->entries);
  dynarray_destroy(&comp->links);
  dynbitset_destroy(&comp->openEntities);
}

ecs_view_define(HierarchyEntryView) {
  ecs_access_with(SceneLevelInstanceComp);
  ecs_access_read(SceneNameComp);
  ecs_access_maybe_read(SceneLifetimeOwnerComp);
  ecs_access_maybe_read(SceneAttachmentComp);
  ecs_access_maybe_read(SceneProjectileComp);
}

ecs_view_define(PanelUpdateGlobalView) {
  ecs_access_write(SceneSetEnvComp);
  ecs_access_read(InputManagerComp);
}

ecs_view_define(PanelUpdateView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DevHierarchyPanelComp's are exclusively managed here.

  ecs_access_read(DevPanelComp);
  ecs_access_write(DevHierarchyPanelComp);
  ecs_access_write(UiCanvasComp);
}

typedef struct {
  EcsWorld*               world;
  SceneSetEnvComp*        setEnv;
  const InputManagerComp* input;
  DevHierarchyPanelComp*  panel;
  EcsEntityId             focusEntity;
} HierarchyContext;

static bool hierarchy_open(HierarchyContext* ctx, const EcsEntityId e) {
  return dynbitset_test(&ctx->panel->openEntities, ecs_entity_id_index(e));
}

static void hierarchy_open_update(HierarchyContext* ctx, const EcsEntityId e, const bool open) {
  if (open) {
    dynbitset_set(&ctx->panel->openEntities, ecs_entity_id_index(e));
  } else {
    dynbitset_clear(&ctx->panel->openEntities, ecs_entity_id_index(e));
  }
}

static i8 hierarchy_compare_entry(const void* a, const void* b) {
  const EcsEntityId entityA = ((const HierarchyEntry*)a)->entity;
  const EcsEntityId entityB = ((const HierarchyEntry*)b)->entity;
  return entityA < entityB ? -1 : entityA > entityB ? 1 : 0;
}

static HierarchyEntry* hierarchy_find(HierarchyContext* ctx, const EcsEntityId entity) {
  const HierarchyEntry tgt = {.entity = entity};
  return dynarray_search_binary(&ctx->panel->entries, hierarchy_compare_entry, &tgt);
}

static bool hierarchy_link_add(
    HierarchyContext*       ctx,
    const EcsEntityId       parent,
    const EcsEntityId       child,
    const HierarchyLinkMask type) {
  HierarchyEntry* parentEntry = hierarchy_find(ctx, parent);
  if (!parentEntry) {
    return false;
  }
  HierarchyEntry* childEntry = hierarchy_find(ctx, child);
  if (!childEntry) {
    return false;
  }
  parentEntry->parentMask |= type;
  childEntry->childMask |= type;

  if (ctx->focusEntity == child) {
    hierarchy_open_update(ctx, parent, true);
  }

  // Walk the existing links.
  HierarchyLinkId* linkTail = &parentEntry->linkHead;
  while (!sentinel_check(*linkTail)) {
    HierarchyLink* link = dynarray_at_t(&ctx->panel->links, *linkTail, HierarchyLink);
    if (link->entity == child) {
      link->mask |= type;
      return true;
    }
    linkTail = &link->next;
  }

  // Add a new link.
  *linkTail                                           = (HierarchyLinkId)ctx->panel->links.size;
  *dynarray_push_t(&ctx->panel->links, HierarchyLink) = (HierarchyLink){
      .mask   = type,
      .entity = child,
      .next   = sentinel_u32,
  };

  return true;
}

static EcsEntityId hierarchy_link_entity(HierarchyContext* ctx, const HierarchyLinkId id) {
  return dynarray_at_t(&ctx->panel->links, id, HierarchyLink)->entity;
}

static HierarchyLinkId hierarchy_link_next(HierarchyContext* ctx, const HierarchyLinkId id) {
  return dynarray_at_t(&ctx->panel->links, id, HierarchyLink)->next;
}

static u32 hierarchy_next_root(HierarchyContext* ctx, u32 entryIdx) {
  for (; entryIdx != ctx->panel->entries.size; ++entryIdx) {
    HierarchyEntry* entry = dynarray_at_t(&ctx->panel->entries, entryIdx, HierarchyEntry);
    if (!entry->childMask) {
      break;
    }
  }
  return entryIdx;
}

static void hierarchy_query(HierarchyContext* ctx) {
  dynarray_clear(&ctx->panel->entries);

  EcsView*     entryView = ecs_world_view_t(ctx->world, HierarchyEntryView);
  EcsIterator* entryItr  = ecs_view_itr(entryView);

  // Add entries.
  for (EcsIterator* itr = ecs_view_itr_reset(entryItr); ecs_view_walk(itr);) {
    HierarchyEntry* entry = dynarray_push_t(&ctx->panel->entries, HierarchyEntry);
    entry->parentMask     = 0;
    entry->childMask      = 0;
    entry->entity         = ecs_view_entity(itr);
    entry->nameHash       = ecs_view_read_t(itr, SceneNameComp)->name;
    entry->linkHead       = sentinel_u32; // No links.
  }
  dynarray_sort(&ctx->panel->entries, hierarchy_compare_entry);

  // Add links.
  for (u32 entryIdx = 0; entryIdx != ctx->panel->entries.size; ++entryIdx) {
    HierarchyEntry* entry = dynarray_at_t(&ctx->panel->entries, entryIdx, HierarchyEntry);
    EcsIterator*    itr   = ecs_view_jump(entryItr, entry->entity);

    const SceneLifetimeOwnerComp* ownerComp = ecs_view_read_t(itr, SceneLifetimeOwnerComp);
    if (ownerComp) {
      for (u32 ownerIdx = 0; ownerIdx != scene_lifetime_owners_max; ++ownerIdx) {
        const EcsEntityId owner = ownerComp->owners[ownerIdx];
        if (owner) {
          hierarchy_link_add(ctx, owner, entry->entity, HierarchyLinkMask_Lifetime);
        }
      }
    }
    const SceneAttachmentComp* attachComp = ecs_view_read_t(itr, SceneAttachmentComp);
    if (attachComp && attachComp->target) {
      hierarchy_link_add(ctx, attachComp->target, entry->entity, HierarchyLinkMask_Attachment);
    }
    const SceneProjectileComp* projComp = ecs_view_read_t(itr, SceneProjectileComp);
    if (projComp && projComp->instigator) {
      hierarchy_link_add(ctx, projComp->instigator, entry->entity, HierarchyLinkMask_Instigator);
    }
  }
}

static String hierarchy_name(const StringHash nameHash) {
  const String name = stringtable_lookup(g_stringtable, nameHash);
  return string_is_empty(name) ? string_lit("<unnamed>") : name;
}

static Unicode hierarchy_icon(HierarchyContext* ctx, const EcsEntityId e) {
  if (ecs_world_has_t(ctx->world, e, SceneScriptComp)) {
    return UiShape_Description;
  }
  if (ecs_world_has_t(ctx->world, e, ScenePropertyComp)) {
    return UiShape_Description;
  }
  if (ecs_world_has_t(ctx->world, e, SceneVfxDecalComp)) {
    return UiShape_Image;
  }
  if (ecs_world_has_t(ctx->world, e, SceneVfxSystemComp)) {
    return UiShape_Grain;
  }
  if (ecs_world_has_t(ctx->world, e, SceneLightPointComp)) {
    return UiShape_Light;
  }
  if (ecs_world_has_t(ctx->world, e, SceneLightSpotComp)) {
    return UiShape_Light;
  }
  if (ecs_world_has_t(ctx->world, e, SceneLightLineComp)) {
    return UiShape_Light;
  }
  if (ecs_world_has_t(ctx->world, e, SceneLightDirComp)) {
    return UiShape_Light;
  }
  if (ecs_world_has_t(ctx->world, e, SceneLightAmbientComp)) {
    return UiShape_Light;
  }
  if (ecs_world_has_t(ctx->world, e, SceneSoundComp)) {
    return UiShape_MusicNote;
  }
  if (ecs_world_has_t(ctx->world, e, SceneRenderableComp)) {
    return UiShape_WebAsset;
  }
  if (ecs_world_has_t(ctx->world, e, SceneCollisionComp)) {
    return UiShape_Dashboard;
  }
  return '?';
}

static void hierarchy_entry_draw(
    HierarchyContext*     ctx,
    UiCanvasComp*         canvas,
    UiTable*              table,
    const HierarchyEntry* entry,
    const u32             depth) {
  const bool    selected = scene_set_contains(ctx->setEnv, g_sceneSetSelected, entry->entity);
  const UiColor color    = selected ? ui_color(48, 48, 178, 192) : ui_color(48, 48, 48, 192);
  ui_table_draw_row_bg(canvas, table, color);

  if (depth) {
    const f32 inset = -25.0f * depth;
    ui_layout_grow(canvas, UiAlign_MiddleRight, ui_vector(inset, 0), UiBase_Absolute, Ui_X);
  }
  if (entry->parentMask) {
    bool isOpen = hierarchy_open(ctx, entry->entity);
    if (ui_fold(canvas, &isOpen)) {
      hierarchy_open_update(ctx, entry->entity, isOpen);
    }
  }

  ui_layout_grow(canvas, UiAlign_MiddleRight, ui_vector(-17.0f, 0), UiBase_Absolute, Ui_X);
  ui_layout_push(canvas);
  ui_layout_inner(canvas, UiBase_Current, UiAlign_MiddleLeft, ui_vector(15, 15), UiBase_Absolute);
  ui_canvas_draw_glyph(canvas, hierarchy_icon(ctx, entry->entity), 0, UiFlags_None);
  ui_layout_pop(canvas);

  ui_layout_grow(canvas, UiAlign_MiddleRight, ui_vector(-20.0f, 0), UiBase_Absolute, Ui_X);
  ui_style_push(canvas);
  if (selected) {
    ui_style_outline(canvas, 2);
  }
  ui_label(canvas, hierarchy_name(entry->nameHash), .selectable = true);
  ui_style_pop(canvas);

  ui_layout_push(canvas);
  ui_layout_inner(canvas, UiBase_Current, UiAlign_MiddleRight, ui_vector(25, 25), UiBase_Absolute);
  if (ui_button(
          canvas,
          .label      = ui_shape_scratch(UiShape_SelectAll),
          .fontSize   = 18,
          .frameColor = ui_color(0, 16, 255, 192),
          .tooltip    = string_static("Select the entity."))) {
    const InputModifier modifiers = input_modifiers(ctx->input);
    if (!(modifiers & (InputModifier_Control | InputModifier_Shift))) {
      scene_set_clear(ctx->setEnv, g_sceneSetSelected);
    }
    if (modifiers & InputModifier_Shift) {
      scene_set_remove(ctx->setEnv, g_sceneSetSelected, entry->entity);
    } else {
      scene_set_add(ctx->setEnv, g_sceneSetSelected, entry->entity, SceneSetFlags_None);
    }
  }
  ui_layout_next(canvas, Ui_Left, 10);
  if (ui_button(
          canvas,
          .label      = ui_shape_scratch(UiShape_Delete),
          .fontSize   = 18,
          .frameColor = ui_color(255, 16, 0, 192),
          .tooltip    = string_lit("Destroy the entity."))) {
    ecs_world_entity_destroy(ctx->world, entry->entity);
  }
  ui_layout_pop(canvas);
}

static void hierarchy_panel_draw(HierarchyContext* ctx, UiCanvasComp* canvas) {
  const String title = fmt_write_scratch(
      "{} Hierarchy Panel ({})", fmt_ui_shape(Tree), fmt_int(ctx->panel->entries.size));
  ui_panel_begin(
      canvas, &ctx->panel->panel, .title = title, .topBarColor = ui_color(100, 0, 0, 192));

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  const f32 height = ui_table_height(&table, ctx->panel->panelRowCount);
  ui_scrollview_begin(canvas, &ctx->panel->scrollview, UiLayer_Normal, height);
  ui_canvas_id_block_next(canvas); // Start the list of entities on its own id block.

  u32             rootIdx = hierarchy_next_root(ctx, 0);
  HierarchyLinkId childItrQueue[64];
  u32             childItrQueueSize = 0;

  ctx->panel->panelRowCount = 0;
  while (childItrQueueSize || rootIdx != ctx->panel->entries.size) {
    // Pop next entry from the queue.
    const HierarchyEntry* entry;
    u32                   entryDepth;
    if (childItrQueueSize) {
      HierarchyLinkId* linkItr = &childItrQueue[childItrQueueSize - 1];
      entry                    = hierarchy_find(ctx, hierarchy_link_entity(ctx, *linkItr));
      entryDepth               = childItrQueueSize;
      *linkItr                 = hierarchy_link_next(ctx, *linkItr);
      if (sentinel_check(*linkItr)) {
        --childItrQueueSize;
      }
    } else {
      entry      = dynarray_at_t(&ctx->panel->entries, rootIdx, HierarchyEntry);
      entryDepth = 0;
      rootIdx    = hierarchy_next_root(ctx, rootIdx + 1);
    }

    // Draw entry.
    ui_table_next_row(canvas, &table);
    const f32 y = ui_table_height(&table, ctx->panel->panelRowCount++);
    if (ui_scrollview_cull(&ctx->panel->scrollview, y, table.rowHeight)) {
      if (ctx->focusEntity == entry->entity) {
        ctx->panel->scrollview.offset = y;
        ctx->focusEntity              = 0;
      }
    } else {
      hierarchy_entry_draw(ctx, canvas, &table, entry, entryDepth);
    }

    // Push children.
    if (hierarchy_open(ctx, entry->entity)) {
      if (!sentinel_check(entry->linkHead) && childItrQueueSize != array_elems(childItrQueue)) {
        childItrQueue[childItrQueueSize++] = entry->linkHead;
      }
    }
  }
  ui_canvas_id_block_next(canvas);

  ui_scrollview_end(canvas, &ctx->panel->scrollview);
  ui_panel_end(canvas, &ctx->panel->panel);
}

ecs_system_define(DevHierarchyUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, PanelUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  HierarchyContext ctx = {
      .world  = world,
      .setEnv = ecs_view_write_t(globalItr, SceneSetEnvComp),
      .input  = ecs_view_read_t(globalItr, InputManagerComp),
  };

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    UiCanvasComp*     canvas = ecs_view_write_t(itr, UiCanvasComp);

    ctx.panel = ecs_view_write_t(itr, DevHierarchyPanelComp);

    ui_canvas_reset(canvas);
    const bool pinned = ui_panel_pinned(&ctx.panel->panel);
    if (dev_panel_hidden(ecs_view_read_t(itr, DevPanelComp)) && !pinned) {
      continue;
    }
    if (ctx.panel->lastMainSelection != scene_set_main(ctx.setEnv, g_sceneSetSelected)) {
      ctx.panel->lastMainSelection = scene_set_main(ctx.setEnv, g_sceneSetSelected);
      ctx.focusEntity              = ctx.panel->lastMainSelection;
    }
    hierarchy_query(&ctx);
    hierarchy_panel_draw(&ctx, canvas);

    if (ui_panel_closed(&ctx.panel->panel)) {
      ecs_world_entity_destroy(world, entity);
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(dev_hierarchy_module) {
  ecs_register_comp(DevHierarchyPanelComp, .destructor = ecs_destruct_hierarchy_panel);

  ecs_register_system(
      DevHierarchyUpdatePanelSys,
      ecs_register_view(PanelUpdateGlobalView),
      ecs_register_view(PanelUpdateView),
      ecs_register_view(HierarchyEntryView));
}

EcsEntityId
dev_hierarchy_panel_open(EcsWorld* world, const EcsEntityId window, const DevPanelType type) {
  const EcsEntityId      panelEntity    = dev_panel_create(world, window, type);
  DevHierarchyPanelComp* hierarchyPanel = ecs_world_add_t(
      world,
      panelEntity,
      DevHierarchyPanelComp,
      .panel        = ui_panel(.position = ui_vector(1.0f, 0.0f), .size = ui_vector(500, 350)),
      .scrollview   = ui_scrollview(),
      .entries      = dynarray_create_t(g_allocHeap, HierarchyEntry, 1024),
      .links        = dynarray_create_t(g_allocHeap, HierarchyLink, 1024),
      .openEntities = dynbitset_create(g_allocHeap, 0));

  if (type == DevPanelType_Detached) {
    ui_panel_maximize(&hierarchyPanel->panel);
  }

  return panelEntity;
}
