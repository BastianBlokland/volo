#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_dynbitset.h"
#include "core_dynstring.h"
#include "core_format.h"
#include "core_stringtable.h"
#include "dev_hierarchy.h"
#include "dev_inspector.h"
#include "dev_panel.h"
#include "dev_stats.h"
#include "ecs_def.h"
#include "ecs_entity.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "input_manager.h"
#include "scene.h"
#include "scene_attachment.h"
#include "scene_creator.h"
#include "scene_lifetime.h"
#include "scene_name.h"
#include "scene_property.h"
#include "scene_set.h"
#include "scene_time.h"
#include "script_mem.h"
#include "script_val.h"
#include "trace_tracer.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_panel.h"
#include "ui_scrollview.h"
#include "ui_shape.h"
#include "ui_style.h"
#include "ui_table.h"
#include "ui_widget.h"

// clang-format off

static const String g_tooltipFilter = string_static("Filter entries by name.\nSupports glob characters \a.b*\ar and \a.b?\ar (\a.b!\ar prefix to invert).");
static const String g_tooltipFreeze = string_static("Freeze the data set (halts data collection).");
static const String g_tooltipSets   = string_static("Include sets in the hierarchy.");

// clang-format on

typedef enum {
  HierarchyKind_Entity,
  HierarchyKind_Set,
} HierarchyKind;

#define hierarchy_kind_bits 1
#define hierarchy_kind_mask bit_range_32(0, hierarchy_kind_bits)

typedef u32 HierarchyId;
typedef u32 HierarchyLinkId;
typedef u32 HierarchyStableId;

typedef enum {
  HierarchyLinkMask_None       = 0,
  HierarchyLinkMask_SetMember  = 1 << 0,
  HierarchyLinkMask_Creator    = 1 << 1,
  HierarchyLinkMask_Lifetime   = 1 << 2,
  HierarchyLinkMask_Attachment = 1 << 3,
  HierarchyLinkMask_Reference  = 1 << 4,
} HierarchyLinkMask;

typedef struct {
  HierarchyLinkMask mask;
  HierarchyLinkId   next;
  HierarchyId       target;
} HierarchyLink;

typedef struct {
  HierarchyLinkMask parentMask : 16;
  HierarchyLinkMask childMask : 16;
  StringHash        nameHash;
  EcsEntityId       entity; // Optional reference to an entity.
  HierarchyLinkId   linkHead, linkTail;
  HierarchyId       firstParent;
  HierarchyStableId stableId;
} HierarchyEntry;

typedef struct {
  HierarchyLinkMask type;
  HierarchyKind     parentKind;
  union {
    EcsEntityId entity;
    StringHash  set;
  } parent;
  EcsEntityId child;
} HierarchyLinkEntityRequest;

ecs_comp_define(DevHierarchyPanelComp) {
  UiPanel      panel;
  u32          panelRowCount;
  UiScrollview scrollview;
  bool         freeze;
  bool         includeSets;
  bool         focusOnSelection;

  bool      filterActive;
  DynString filterName;
  DynBitSet filterResult;
  u32       filterMatches;

  DynArray  entries;            // HierarchyEntry[]
  DynArray  links;              // HierarchyLink[]
  DynArray  linkEntityRequests; // HierarchyLinkEntityRequest[]
  DynBitSet openEntries;
  DynBitSet visibleEntries;

  EcsEntityId lastMainSelection;

  HierarchyStableId lastClickEntry;
  TimeDuration      lastClickTime;
};

static void ecs_destruct_hierarchy_panel(void* data) {
  DevHierarchyPanelComp* comp = data;
  dynstring_destroy(&comp->filterName);
  dynbitset_destroy(&comp->filterResult);
  dynarray_destroy(&comp->entries);
  dynarray_destroy(&comp->links);
  dynarray_destroy(&comp->linkEntityRequests);
  dynbitset_destroy(&comp->openEntries);
  dynbitset_destroy(&comp->visibleEntries);
}

ecs_view_define(HierarchyEntryView) {
  ecs_access_with(SceneLevelInstanceComp);
  ecs_access_read(SceneNameComp);
  ecs_access_maybe_read(SceneAttachmentComp);
  ecs_access_maybe_read(SceneCreatorComp);
  ecs_access_maybe_read(SceneLifetimeOwnerComp);
  ecs_access_maybe_read(SceneSetMemberComp);
  ecs_access_maybe_read(ScenePropertyComp);
}

ecs_view_define(PanelUpdateGlobalView) {
  ecs_access_write(SceneSetEnvComp);
  ecs_access_read(InputManagerComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_maybe_write(DevInspectorSettingsComp);
  ecs_access_maybe_write(DevStatsGlobalComp);
}

ecs_view_define(PanelUpdateView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DevHierarchyPanelComp's are exclusively managed here.

  ecs_access_read(DevPanelComp);
  ecs_access_write(DevHierarchyPanelComp);
  ecs_access_write(UiCanvasComp);
}

typedef struct {
  EcsWorld*                 world;
  SceneSetEnvComp*          setEnv;
  const InputManagerComp*   input;
  const SceneTimeComp*      time;
  DevHierarchyPanelComp*    panel;
  DevInspectorSettingsComp* inspector;
  DevStatsGlobalComp*       stats;
  HierarchyId               focusEntry;
} HierarchyContext;

static HierarchyKind hierarchy_stable_id_kind(const HierarchyStableId id) {
  return (HierarchyKind)(id & hierarchy_kind_mask);
}

static HierarchyStableId hierarchy_stable_id_entity(const EcsEntityId e) {
  return (ecs_entity_id_index(e) << hierarchy_kind_bits) | HierarchyKind_Entity;
}

static HierarchyStableId hierarchy_stable_id_set(const u32 setSlotIndex) {
  return (setSlotIndex << hierarchy_kind_bits) | HierarchyKind_Set;
}

static i8 hierarchy_compare_entry(const void* a, const void* b) {
  const HierarchyStableId stableIdA = ((const HierarchyEntry*)a)->stableId;
  const HierarchyStableId stableIdB = ((const HierarchyEntry*)b)->stableId;
  return stableIdA < stableIdB ? -1 : stableIdA > stableIdB ? 1 : 0;
}

static i8 hierarchy_compare_link_entity_request(const void* a, const void* b) {
  const HierarchyLinkEntityRequest* reqA = a;
  const HierarchyLinkEntityRequest* reqB = b;
  if (LIKELY(reqA->child != reqB->child)) {
    return reqA->child < reqB->child ? -1 : 1;
  }
  if (reqA->parentKind != reqB->parentKind) {
    return reqA->parentKind < reqB->parentKind ? -1 : 1;
  }
  switch (reqA->parentKind) {
  case HierarchyKind_Entity:
    return reqA->parent.entity < reqB->parent.entity   ? -1
           : reqA->parent.entity > reqB->parent.entity ? 1
                                                       : 0;
  case HierarchyKind_Set:
    return reqA->parent.set < reqB->parent.set ? -1 : reqA->parent.set > reqB->parent.set ? 1 : 0;
    break;
  }
  UNREACHABLE
}

static HierarchyEntry* hierarchy_entry(HierarchyContext* ctx, const HierarchyId id) {
  return &dynarray_begin_t(&ctx->panel->entries, HierarchyEntry)[id];
}

static HierarchyLink* hierarchy_link(HierarchyContext* ctx, const HierarchyLinkId id) {
  return &dynarray_begin_t(&ctx->panel->links, HierarchyLink)[id];
}

static HierarchyId hierarchy_entry_id(HierarchyContext* ctx, const HierarchyEntry* entry) {
  return (HierarchyId)(entry - dynarray_begin_t(&ctx->panel->entries, HierarchyEntry));
}

static HierarchyId hierarchy_find(HierarchyContext* ctx, const HierarchyStableId stableId) {
  const HierarchyEntry tgt = {.stableId = stableId};
  const void* res = dynarray_search_binary(&ctx->panel->entries, hierarchy_compare_entry, &tgt);
  return LIKELY(res) ? hierarchy_entry_id(ctx, res) : sentinel_u32;
}

static HierarchyId hierarchy_find_entity(HierarchyContext* ctx, const EcsEntityId e) {
  const HierarchyId id = hierarchy_find(ctx, hierarchy_stable_id_entity(e));
  if (UNLIKELY(sentinel_check(id))) {
    return sentinel_u32;
  }
  const HierarchyEntry* entry = hierarchy_entry(ctx, id);
  if (UNLIKELY(entry->entity != e)) {
    return sentinel_u32; // Entity index has been re-used; not the same entity.
  }
  return id;
}

/**
 * Register a link between the parent and child entries.
 * NOTE: Does not handle duplicates (not even of different link types).
 */
static void hierarchy_link_add(
    HierarchyContext*       ctx,
    const HierarchyId       parent,
    const HierarchyId       child,
    const HierarchyLinkMask type) {
  HierarchyEntry* parentEntry = hierarchy_entry(ctx, parent);
  HierarchyEntry* childEntry  = hierarchy_entry(ctx, child);

  parentEntry->parentMask |= type;
  childEntry->childMask |= type;

  if (sentinel_check(childEntry->firstParent)) {
    childEntry->firstParent = parent;
  }

  // Add a new link.
  const HierarchyLinkId linkId                        = (HierarchyLinkId)ctx->panel->links.size;
  *dynarray_push_t(&ctx->panel->links, HierarchyLink) = (HierarchyLink){
      .mask   = type,
      .target = child,
      .next   = sentinel_u32,
  };

  if (!sentinel_check(parentEntry->linkTail)) {
    hierarchy_link(ctx, parentEntry->linkTail)->next = linkId;
  } else {
    parentEntry->linkHead = linkId;
  }
  parentEntry->linkTail = linkId;
}

/**
 * Register a new link between the parent and child entries.
 * NOTE: This automatically deduplicates links between the same parent <-> child.
 */
static void hierarchy_link_add_unique(
    HierarchyContext*       ctx,
    const HierarchyId       parent,
    const HierarchyId       child,
    const HierarchyLinkMask type) {
  HierarchyEntry* parentEntry = hierarchy_entry(ctx, parent);
  HierarchyEntry* childEntry  = hierarchy_entry(ctx, child);

  parentEntry->parentMask |= type;
  childEntry->childMask |= type;

  if (sentinel_check(childEntry->firstParent)) {
    childEntry->firstParent = parent;
  }

  // Walk the existing links to check for duplicates.
  HierarchyLinkId* linkItr = &parentEntry->linkHead;
  while (!sentinel_check(*linkItr)) {
    HierarchyLink* link = hierarchy_link(ctx, *linkItr);
    if (link->target == child) {
      link->mask |= type; // Merge links.
      return;
    }
    linkItr = &link->next;
  }

  // Add a new link.
  *linkItr              = (HierarchyLinkId)ctx->panel->links.size;
  parentEntry->linkTail = *linkItr;

  *dynarray_push_t(&ctx->panel->links, HierarchyLink) = (HierarchyLink){
      .mask   = type,
      .target = child,
      .next   = sentinel_u32,
  };
}

/**
 * Request the given entity to be linked to a parent entity.
 */
static void hierarchy_link_entity_request(
    HierarchyContext*       ctx,
    const EcsEntityId       parent,
    const EcsEntityId       child,
    const HierarchyLinkMask type) {
  *dynarray_push_t(&ctx->panel->linkEntityRequests, HierarchyLinkEntityRequest) =
      (HierarchyLinkEntityRequest){
          .type          = type,
          .parentKind    = HierarchyKind_Entity,
          .parent.entity = parent,
          .child         = child,
      };
}

/**
 * Request the given entity to be linked to a set.
 * NOTE: No duplicate requests are allowed between the same entity <-> set.
 */
static void hierarchy_link_entity_to_set_request(
    HierarchyContext*       ctx,
    const StringHash        set,
    const EcsEntityId       child,
    const HierarchyLinkMask type) {
  *dynarray_push_t(&ctx->panel->linkEntityRequests, HierarchyLinkEntityRequest) =
      (HierarchyLinkEntityRequest){
          .type       = type,
          .parentKind = HierarchyKind_Set,
          .parent.set = set,
          .child      = child,
      };
}

static void hierarchy_link_entity_apply_requests(HierarchyContext* ctx) {
  trace_begin("requests_sort", TraceColor_Blue);
  dynarray_sort(&ctx->panel->linkEntityRequests, hierarchy_compare_link_entity_request);
  trace_end();

  trace_begin("requests_apply", TraceColor_Blue);

  HierarchyId setEntries[256];
  const u32   setSlotCount = scene_set_slot_count(ctx->setEnv);
  if (UNLIKELY(setSlotCount > array_elems(setEntries))) {
    diag_crash_msg("Global set count exceeds maximum");
  }
  for (u32 setIdx = 0; setIdx != setSlotCount; ++setIdx) {
    setEntries[setIdx] = hierarchy_find(ctx, hierarchy_stable_id_set(setIdx));
  }

  // Cache of the previously looked up childId, usefull as often childs have multiple links.
  EcsEntityId lastChild   = ecs_entity_invalid;
  HierarchyId lastChildId = sentinel_u32;

  dynarray_for_t(&ctx->panel->linkEntityRequests, HierarchyLinkEntityRequest, req) {
    HierarchyId childId;
    if (req->child == lastChild) {
      childId = lastChildId;
    } else {
      childId = hierarchy_find_entity(ctx, req->child);

      // Cache the result in case this child has another link request right after.
      lastChild   = req->child;
      lastChildId = childId;
    }
    if (sentinel_check(childId)) {
      continue; // Child does not exist anymore.
    }

    switch (req->parentKind) {
    case HierarchyKind_Entity: {
      const HierarchyId parentId = hierarchy_find_entity(ctx, req->parent.entity);
      if (sentinel_check(parentId)) {
        continue; // Parent does not exist anymore.
      }
      hierarchy_link_add_unique(ctx, parentId, childId, req->type);
      break;
    }
    case HierarchyKind_Set: {
      const u32 slotIndex = scene_set_slot_find(ctx->setEnv, req->parent.set);
      diag_assert(!sentinel_check(slotIndex));

      if (sentinel_check(setEntries[slotIndex])) {
        continue; // Set does not have a hierarchy entry.
      }
      // NOTE: No duplicates are allowed in set requests.
      hierarchy_link_add(ctx, setEntries[slotIndex], childId, req->type);
      break;
    }
    }
  }
  trace_end();

  dynarray_clear(&ctx->panel->linkEntityRequests);
}

static bool hierarchy_is_root(const HierarchyEntry* entry) {
  return (entry->childMask & ~HierarchyLinkMask_Reference) == 0;
}

static u32 hierarchy_next_root(HierarchyContext* ctx, u32 entryIdx) {
  for (; entryIdx != ctx->panel->entries.size; ++entryIdx) {
    const HierarchyEntry* entry = hierarchy_entry(ctx, entryIdx);
    if (hierarchy_is_root(entry)) {
      break;
    }
  }
  return entryIdx;
}

static void hierarchy_query(HierarchyContext* ctx) {
  dynarray_clear(&ctx->panel->entries);
  dynarray_clear(&ctx->panel->links);

  trace_begin("find_entities", TraceColor_Red);
  EcsView* entryView = ecs_world_view_t(ctx->world, HierarchyEntryView);
  for (EcsIterator* itr = ecs_view_itr(entryView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);

    *dynarray_push_t(&ctx->panel->entries, HierarchyEntry) = (HierarchyEntry){
        .entity      = entity,
        .nameHash    = ecs_view_read_t(itr, SceneNameComp)->name,
        .linkHead    = sentinel_u32,
        .linkTail    = sentinel_u32,
        .firstParent = sentinel_u32,
        .stableId    = hierarchy_stable_id_entity(entity),
    };

    const SceneCreatorComp* creatorComp = ecs_view_read_t(itr, SceneCreatorComp);
    if (creatorComp && creatorComp->creator) {
      hierarchy_link_entity_request(ctx, creatorComp->creator, entity, HierarchyLinkMask_Creator);
    }
    const SceneLifetimeOwnerComp* ownerComp = ecs_view_read_t(itr, SceneLifetimeOwnerComp);
    if (ownerComp) {
      for (u32 ownerIdx = 0; ownerIdx != scene_lifetime_owners_max; ++ownerIdx) {
        const EcsEntityId owner = ownerComp->owners[ownerIdx];
        if (owner) {
          hierarchy_link_entity_request(ctx, owner, entity, HierarchyLinkMask_Lifetime);
        }
      }
    }
    const SceneAttachmentComp* attachComp = ecs_view_read_t(itr, SceneAttachmentComp);
    if (attachComp && attachComp->target) {
      hierarchy_link_entity_request(ctx, attachComp->target, entity, HierarchyLinkMask_Attachment);
    }
    const SceneSetMemberComp* setMember = ecs_view_read_t(itr, SceneSetMemberComp);
    if (setMember && ctx->panel->includeSets) {
      StringHash sets[scene_set_member_max_sets];
      const u32  setCount = scene_set_member_all(setMember, sets);
      for (u32 setIdx = 0; setIdx != setCount; ++setIdx) {
        const StringHash set = sets[setIdx];
        hierarchy_link_entity_to_set_request(ctx, set, entity, HierarchyLinkMask_SetMember);
      }
    }
    const ScenePropertyComp* propComp = ecs_view_read_t(itr, ScenePropertyComp);
    if (propComp) {
      const ScriptMem* memory = scene_prop_memory(propComp);
      for (ScriptMemItr i = script_mem_begin(memory); i.key; i = script_mem_next(memory, i)) {
        const EcsEntityId ref = script_get_entity(script_mem_load(memory, i.key), 0);
        if (ref) {
          hierarchy_link_entity_request(ctx, entity, ref, HierarchyLinkMask_Reference);
        }
      }
    }
  }
  trace_end();

  if (ctx->panel->includeSets) {
    trace_begin("find_sets", TraceColor_Red);
    const u32 slotSetCount = scene_set_slot_count(ctx->setEnv);
    for (u32 setSlotIdx = 0; setSlotIdx != slotSetCount; ++setSlotIdx) {
      const StringHash set = scene_set_slot_get(ctx->setEnv, setSlotIdx);
      if (!set) {
        continue; // Empty slot.
      }
      if (!set || set == g_sceneSetSelected) {
        continue; // Filter out selected set as it doesn't add much value
      }
      *dynarray_push_t(&ctx->panel->entries, HierarchyEntry) = (HierarchyEntry){
          .nameHash    = set,
          .linkHead    = sentinel_u32,
          .linkTail    = sentinel_u32,
          .firstParent = sentinel_u32,
          .stableId    = hierarchy_stable_id_set(setSlotIdx),
      };
    }
    trace_end();
  }

  trace_begin("sort", TraceColor_Red);
  dynarray_sort(&ctx->panel->entries, hierarchy_compare_entry);
  trace_end();

  trace_begin("link", TraceColor_Red);
  hierarchy_link_entity_apply_requests(ctx);
  trace_end();
}

static bool hierarchy_is_open(HierarchyContext* ctx, const HierarchyEntry* e) {
  return dynbitset_test(&ctx->panel->openEntries, e->stableId);
}

static void hierarchy_open(HierarchyContext* ctx, const HierarchyEntry* e, const bool v) {
  if (v) {
    dynbitset_set(&ctx->panel->openEntries, e->stableId);
  } else {
    dynbitset_clear(&ctx->panel->openEntries, e->stableId);
  }
}

static void hierarchy_open_rec(HierarchyContext* ctx, const HierarchyEntry* e, const bool v) {
  hierarchy_open(ctx, e, v);

  HierarchyLinkId childQueue[16];
  u32             childQueueSize = 0;

  if (e->parentMask) {
    childQueue[childQueueSize++] = e->linkHead;
  }

  while (childQueueSize) {
    HierarchyLink*        link  = hierarchy_link(ctx, childQueue[childQueueSize - 1]);
    const HierarchyEntry* child = hierarchy_entry(ctx, link->target);

    hierarchy_open(ctx, child, v);

    if (sentinel_check(link->next)) {
      --childQueueSize;
    } else {
      childQueue[childQueueSize - 1] = link->next;
    }

    if (child->parentMask && childQueueSize != array_elems(childQueue)) {
      childQueue[childQueueSize++] = child->linkHead;
    }
  }
}

static void hierarchy_open_to_root(HierarchyContext* ctx, const HierarchyEntry* e, const bool v) {
  for (HierarchyId p = e->firstParent; !sentinel_check(p);) {
    HierarchyEntry* entry = hierarchy_entry(ctx, p);
    hierarchy_open(ctx, entry, v);
    p = entry->firstParent;
  }
}

static void hierarchy_filter(HierarchyContext* ctx) {
  ctx->panel->filterActive = false;
  dynbitset_clear_all(&ctx->panel->filterResult);

  // Apply name filter.
  if (!string_is_empty(ctx->panel->filterName)) {
    const String rawFilter = dynstring_view(&ctx->panel->filterName);
    const String filter    = fmt_write_scratch("*{}*", fmt_text(rawFilter));

    for (HierarchyId id = 0; id != ctx->panel->entries.size; ++id) {
      const HierarchyEntry* entry = hierarchy_entry(ctx, id);
      const String          name  = stringtable_lookup(g_stringtable, entry->nameHash);
      if (!string_match_glob(name, filter, StringMatchFlags_IgnoreCase)) {
        dynbitset_set(&ctx->panel->filterResult, id);
        ctx->panel->filterActive = true;
      }
    }
  }

  // Count the results.
  ctx->panel->filterMatches = (u32)ctx->panel->entries.size;
  if (ctx->panel->filterActive) {
    ctx->panel->filterMatches -= (u32)dynbitset_count(&ctx->panel->filterResult);
  }

  // Make all results visible by including their parents.
  if (ctx->panel->filterActive) {
    for (HierarchyId id = 0; id != ctx->panel->entries.size; ++id) {
      if (dynbitset_test(&ctx->panel->filterResult, id)) {
        continue; // Filtered out.
      }
      for (HierarchyId p = hierarchy_entry(ctx, id)->firstParent; !sentinel_check(p);) {
        HierarchyEntry* entry = hierarchy_entry(ctx, p);
        dynbitset_clear(&ctx->panel->filterResult, p);
        p = entry->firstParent;
      }
    }
  }
}

static String hierarchy_name(const StringHash nameHash) {
  const String name = stringtable_lookup(g_stringtable, nameHash);
  return string_is_empty(name) ? string_lit("<unnamed>") : name;
}

static Unicode hierarchy_icon_entity(HierarchyContext* ctx, const EcsEntityId e) {
  if (!ecs_world_exists(ctx->world, e)) {
    return UiShape_Delete;
  }
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

static Unicode hierarchy_icon(HierarchyContext* ctx, const HierarchyEntry* entry) {
  switch (hierarchy_stable_id_kind(entry->stableId)) {
  case HierarchyKind_Entity:
    return hierarchy_icon_entity(ctx, entry->entity);
  case HierarchyKind_Set:
    return UiShape_Category;
  }
  diag_crash();
}

static void hierarchy_entry_select_add(HierarchyContext* ctx, const HierarchyEntry* entry) {
  if (!ecs_entity_valid(entry->entity)) {
    return; // Only entities can be selected.
  }
  if (input_modifiers(ctx->input) & InputModifier_Shift) {
    scene_set_remove(ctx->setEnv, g_sceneSetSelected, entry->entity);
  } else {
    scene_set_add(ctx->setEnv, g_sceneSetSelected, entry->entity, SceneSetFlags_None);
  }
}

static void hierarchy_entry_select(HierarchyContext* ctx, const HierarchyEntry* entry) {
  if (!(input_modifiers(ctx->input) & (InputModifier_Control | InputModifier_Shift))) {
    scene_set_clear(ctx->setEnv, g_sceneSetSelected);
  }
  hierarchy_entry_select_add(ctx, entry);
}

static void hierarchy_entry_select_rec(HierarchyContext* ctx, const HierarchyEntry* entry) {
  const InputModifier modifiers = input_modifiers(ctx->input);
  if (!(modifiers & (InputModifier_Control | InputModifier_Shift))) {
    scene_set_clear(ctx->setEnv, g_sceneSetSelected);
  }

  hierarchy_entry_select_add(ctx, entry);
  hierarchy_open(ctx, entry, true);

  HierarchyLinkId childQueue[16];
  u32             childQueueSize = 0;

  if (entry->parentMask) {
    childQueue[childQueueSize++] = entry->linkHead;
  }

  while (childQueueSize) {
    HierarchyLink*        link  = hierarchy_link(ctx, childQueue[childQueueSize - 1]);
    const HierarchyEntry* child = hierarchy_entry(ctx, link->target);

    hierarchy_entry_select_add(ctx, child);
    hierarchy_open(ctx, child, true);

    if (sentinel_check(link->next)) {
      --childQueueSize;
    } else {
      childQueue[childQueueSize - 1] = link->next;
    }

    if (child->parentMask && childQueueSize != array_elems(childQueue)) {
      childQueue[childQueueSize++] = child->linkHead;
    }
  }
}

static bool hierarchy_doubleclick_update(HierarchyContext* ctx, const HierarchyEntry* entry) {
  const TimeDuration timeElapsed = ctx->time->realTime - ctx->panel->lastClickTime;

  bool result = true;
  result &= ctx->panel->lastClickEntry == entry->stableId;
  result &= timeElapsed < input_doubleclick_interval(ctx->input);

  ctx->panel->lastClickEntry = entry->stableId;
  ctx->panel->lastClickTime  = ctx->time->realTime;

  return result;
}

static String hierarchy_entry_tooltip_scratch(HierarchyContext* ctx, const HierarchyEntry* entry) {
  DynString str = dynstring_create_over(alloc_alloc(g_allocScratch, 8 * usize_kibibyte, 1));
  if (hierarchy_stable_id_kind(entry->stableId) == HierarchyKind_Set) {
    fmt_write(&str, "Set: {}\n", fmt_int(entry->nameHash));
  }
  if (ecs_entity_valid(entry->entity)) {
    fmt_write(&str, "Entity: {}\n", ecs_entity_fmt(entry->entity));

    const EcsArchetypeId archetype = ecs_world_entity_archetype(ctx->world, entry->entity);
    if (!sentinel_check(archetype)) {
      const BitSet  compMask = ecs_world_component_mask(ctx->world, archetype);
      const EcsDef* ecsDef   = ecs_world_def(ctx->world);
      bitset_for(compMask, compId) {
        const String compName = ecs_def_comp_name(ecsDef, (EcsCompId)compId);
        fmt_write(&str, "- {}\n", fmt_text(compName));
      }
    }
  }
  return dynstring_view(&str);
}

static bool hierarchy_is_selected(HierarchyContext* ctx, const HierarchyEntry* entry) {
  if (!ecs_entity_valid(entry->entity)) {
    return false; // Only entities can be selected.
  }
  return scene_set_contains(ctx->setEnv, g_sceneSetSelected, entry->entity);
}

static void hierarchy_entry_draw(
    HierarchyContext*     ctx,
    UiCanvasComp*         canvas,
    UiTable*              table,
    const HierarchyEntry* entry,
    const u32             depth) {
  const bool   selected  = hierarchy_is_selected(ctx, entry);
  const bool   isPicking = ctx->inspector && dev_inspector_picker_active(ctx->inspector);
  const String name      = hierarchy_name(entry->nameHash);
  UiColor      bgColor   = selected ? ui_color(32, 32, 255, 192) : ui_color(48, 48, 48, 192);

  ui_style_push(canvas);
  ui_style_mode(canvas, UiMode_Invisible);
  const UiId     bgId     = ui_canvas_draw_glyph(canvas, UiShape_Square, 0, UiFlags_Interactable);
  const UiStatus bgStatus = ui_canvas_elem_status(canvas, bgId);
  ui_style_pop(canvas);

  if (bgStatus == UiStatus_Hovered) {
    if (isPicking && ecs_entity_valid(entry->entity)) {
      bgColor = ui_color(16, 128, 16, 192);
      dev_inspector_picker_update(ctx->inspector, entry->entity);
      if (ctx->stats) {
        dev_stats_notify(ctx->stats, string_lit("Picker entity"), name);
      }
      ui_tooltip(canvas, bgId, string_lit("Pick this entity."));
    } else {
      ui_tooltip(canvas, bgId, hierarchy_entry_tooltip_scratch(ctx, entry));
    }
  } else {
    ui_canvas_id_skip(canvas, 2);
  }

  switch (bgStatus) {
  case UiStatus_Hovered:
    bgColor = ui_color_mul(bgColor, 1.25f);
    break;
  case UiStatus_Pressed:
    bgColor = ui_color_mul(bgColor, 1.5f);
    break;
  case UiStatus_Activated:
    if (isPicking) {
      dev_inspector_picker_close(ctx->inspector);
    } else if (hierarchy_doubleclick_update(ctx, entry) || !entry->entity) {
      hierarchy_entry_select_rec(ctx, entry);
    } else {
      hierarchy_entry_select(ctx, entry);
    }
    ui_canvas_sound(canvas, UiSoundType_Click);
    break;
  default:
    break;
  }
  ui_table_draw_row_bg(canvas, table, bgColor);

  if (depth) {
    const f32 inset = -25.0f * depth;
    ui_layout_grow(canvas, UiAlign_MiddleRight, ui_vector(inset, 0), UiBase_Absolute, Ui_X);
  }
  if (entry->parentMask) {
    const UiWidgetFlags foldFlags = ctx->panel->filterActive ? UiWidget_Disabled : UiWidget_Default;
    bool                isOpen    = hierarchy_is_open(ctx, entry) || ctx->panel->filterActive;
    if (ui_fold(canvas, &isOpen, .flags = foldFlags)) {
      if (input_modifiers(ctx->input) & InputModifier_Control) {
        hierarchy_open_rec(ctx, entry, isOpen);
      } else {
        hierarchy_open(ctx, entry, isOpen);
      }
    }
  }

  ui_style_push(canvas);
  if (selected) {
    ui_style_outline(canvas, 2);
  }
  ui_layout_grow(canvas, UiAlign_MiddleRight, ui_vector(-17.0f, 0), UiBase_Absolute, Ui_X);
  ui_layout_push(canvas);
  ui_layout_inner(canvas, UiBase_Current, UiAlign_MiddleLeft, ui_vector(15, 15), UiBase_Absolute);
  ui_canvas_draw_glyph(canvas, hierarchy_icon(ctx, entry), 0, UiFlags_None);
  ui_layout_pop(canvas);

  ui_layout_grow(canvas, UiAlign_MiddleRight, ui_vector(-20.0f, 0), UiBase_Absolute, Ui_X);
  ui_label(canvas, name);
  ui_style_pop(canvas);

  ui_layout_push(canvas);
  ui_layout_inner(canvas, UiBase_Current, UiAlign_MiddleRight, ui_vector(25, 22), UiBase_Absolute);
  if (ui_button(
          canvas,
          .flags      = isPicking ? UiWidget_Disabled : UiWidget_Default,
          .label      = ui_shape_scratch(UiShape_SelectAll),
          .fontSize   = 18,
          .frameColor = ui_color(0, 16, 255, 192),
          .tooltip    = string_static("Select the entity."))) {
    if (!entry->entity || (input_modifiers(ctx->input) & InputModifier_Control)) {
      hierarchy_entry_select_rec(ctx, entry);
    } else {
      hierarchy_entry_select(ctx, entry);
    }
  }
  if (ecs_entity_valid(entry->entity)) {
    ui_layout_next(canvas, Ui_Left, 10);
    if (ui_button(
            canvas,
            .flags      = isPicking ? UiWidget_Disabled : UiWidget_Default,
            .label      = ui_shape_scratch(UiShape_Delete),
            .fontSize   = 18,
            .frameColor = ui_color(255, 16, 0, 192),
            .tooltip    = string_lit("Destroy the entity."))) {
      ecs_world_entity_destroy(ctx->world, entry->entity);
    }
  }
  ui_layout_pop(canvas);
}

static void hierarchy_options_draw(HierarchyContext* ctx, UiCanvasComp* canvas) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 55);
  ui_table_add_column(&table, UiTableColumn_Fixed, 150);
  ui_table_add_column(&table, UiTableColumn_Fixed, 70);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 55);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Filter:"));
  ui_table_next_column(canvas, &table);
  if (ui_textbox(
          canvas,
          &ctx->panel->filterName,
          .placeholder = string_lit("*"),
          .tooltip     = g_tooltipFilter)) {
    ctx->panel->focusOnSelection = true;
  }

  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("Freeze:"));
  ui_table_next_column(canvas, &table);
  ui_toggle(canvas, &ctx->panel->freeze, .tooltip = g_tooltipFreeze);

  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("Sets:"));
  ui_table_next_column(canvas, &table);
  if (ui_toggle(canvas, &ctx->panel->includeSets, .tooltip = g_tooltipSets)) {
    ctx->panel->focusOnSelection = true;
  }

  ui_layout_pop(canvas);
}

static void hierarchy_bg_draw(UiCanvasComp* canvas) {
  ui_style_push(canvas);
  ui_style_color(canvas, ui_color_clear);
  ui_style_outline(canvas, 4);
  ui_canvas_draw_glyph(canvas, UiShape_Square, 10, UiFlags_None);
  ui_style_pop(canvas);
}

static void hierarchy_panel_draw(HierarchyContext* ctx, UiCanvasComp* canvas) {
  const String title = fmt_write_scratch(
      "{} Hierarchy Panel ({})", fmt_ui_shape(Tree), fmt_int(ctx->panel->filterMatches));
  ui_panel_begin(
      canvas, &ctx->panel->panel, .title = title, .topBarColor = ui_color(100, 0, 0, 192));

  hierarchy_options_draw(ctx, canvas);
  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -32), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None, UiLayer_Normal);
  hierarchy_bg_draw(canvas);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  const f32 height = ui_table_height(&table, ctx->panel->panelRowCount);
  ui_scrollview_begin(canvas, &ctx->panel->scrollview, UiLayer_Normal, height);
  ui_canvas_id_block_next(canvas); // Start the list of entities on its own id block.

  u32             rootIdx = hierarchy_next_root(ctx, 0);
  HierarchyLinkId childQueue[16];
  u32             childDepth[16];
  u32             childQueueSize = 0;

  dynbitset_clear_all(&ctx->panel->visibleEntries);

  ctx->panel->panelRowCount = 0;
  while (rootIdx != ctx->panel->entries.size || childQueueSize) {
    // Pick entry.
    const HierarchyEntry* entry;
    u32                   entryDepth;
    if (childQueueSize) {
      HierarchyLink* link = hierarchy_link(ctx, childQueue[childQueueSize - 1]);
      entry               = hierarchy_entry(ctx, link->target);
      entryDepth          = childDepth[childQueueSize - 1];

      if (sentinel_check(link->next)) {
        --childQueueSize;
      } else {
        childQueue[childQueueSize - 1] = link->next;
      }
    } else {
      entry      = hierarchy_entry(ctx, rootIdx);
      entryDepth = 0;
      rootIdx    = hierarchy_next_root(ctx, rootIdx + 1);
    }

    // Apply filter.
    if (ctx->panel->filterActive) {
      const HierarchyId id = hierarchy_entry_id(ctx, entry);
      if (dynbitset_test(&ctx->panel->filterResult, id)) {
        continue;
      }
    }

    // Draw entry.
    const f32 y = ui_table_height(&table, ctx->panel->panelRowCount++);
    if (ui_scrollview_cull(&ctx->panel->scrollview, y, table.rowHeight)) {
      if (ctx->focusEntry == hierarchy_entry_id(ctx, entry)) {
        const f32 viewportHalfHeight  = ctx->panel->scrollview.lastViewportHeight * 0.5f;
        ctx->panel->scrollview.offset = y - viewportHalfHeight + table.rowHeight * 0.5f;
        ctx->focusEntry               = sentinel_u32;
      }
    } else {
      ui_table_jump_row(canvas, &table, ctx->panel->panelRowCount - 1);
      hierarchy_entry_draw(ctx, canvas, &table, entry, entryDepth);
      dynbitset_set(&ctx->panel->visibleEntries, entry->stableId);
    }

    // Push children.
    if (entry->parentMask && childQueueSize != array_elems(childQueue)) {
      if (ctx->panel->filterActive || hierarchy_is_open(ctx, entry)) {
        childQueue[childQueueSize] = entry->linkHead;
        childDepth[childQueueSize] = entryDepth + 1;
        ++childQueueSize;
      }
    }
  }
  ui_canvas_id_block_next(canvas);

  ui_scrollview_end(canvas, &ctx->panel->scrollview);
  ui_layout_container_pop(canvas);
  ui_panel_end(canvas, &ctx->panel->panel);
}

static void hierarchy_focus_entity(HierarchyContext* ctx, const EcsEntityId entity) {
  if (dynbitset_test(&ctx->panel->visibleEntries, hierarchy_stable_id_entity(entity))) {
    return; // Already visible.
  }
  ctx->focusEntry = hierarchy_find_entity(ctx, entity);
  if (!sentinel_check(ctx->focusEntry)) {
    hierarchy_open_to_root(ctx, hierarchy_entry(ctx, ctx->focusEntry), true);
  }
}

ecs_system_define(DevHierarchyUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, PanelUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  HierarchyContext ctx = {
      .world      = world,
      .setEnv     = ecs_view_write_t(globalItr, SceneSetEnvComp),
      .input      = ecs_view_read_t(globalItr, InputManagerComp),
      .time       = ecs_view_read_t(globalItr, SceneTimeComp),
      .stats      = ecs_view_write_t(globalItr, DevStatsGlobalComp),
      .inspector  = ecs_view_write_t(globalItr, DevInspectorSettingsComp),
      .focusEntry = sentinel_u32,
  };
  const EcsEntityId mainSelection = scene_set_main(ctx.setEnv, g_sceneSetSelected);

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
    if (!ctx.panel->freeze) {
      trace_begin("query", TraceColor_Blue);
      hierarchy_query(&ctx);
      trace_end();
    }

    trace_begin("filter", TraceColor_Blue);
    hierarchy_filter(&ctx);
    trace_end();

    if (ctx.panel->lastMainSelection != mainSelection) {
      ctx.panel->lastMainSelection = mainSelection;
      hierarchy_focus_entity(&ctx, mainSelection);
    }
    if (ctx.panel->focusOnSelection) {
      // HACK: Intentially delayed a frame so the visiblity bits has been updated before focussing.
      ctx.panel->lastMainSelection = 0;
      ctx.panel->focusOnSelection  = false;
    }

    trace_begin("draw", TraceColor_Blue);
    hierarchy_panel_draw(&ctx, canvas);
    trace_end();

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
      .includeSets  = true,
      .filterName   = dynstring_create(g_allocHeap, 32),
      .filterResult = dynbitset_create(g_allocHeap, 0),
      .entries      = dynarray_create_t(g_allocHeap, HierarchyEntry, 1024),
      .links        = dynarray_create_t(g_allocHeap, HierarchyLink, 1024),
      .linkEntityRequests = dynarray_create_t(g_allocHeap, HierarchyLinkEntityRequest, 512),
      .openEntries        = dynbitset_create(g_allocHeap, 0),
      .visibleEntries     = dynbitset_create(g_allocHeap, 512));

  if (type == DevPanelType_Detached) {
    ui_panel_maximize(&hierarchyPanel->panel);
  }

  return panelEntity;
}
