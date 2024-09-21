#include "asset_graphic.h"
#include "asset_manager.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_format.h"
#include "core_math.h"
#include "debug_register.h"
#include "debug_rend.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "rend_object.h"
#include "rend_register.h"
#include "rend_reset.h"
#include "rend_resource.h"
#include "rend_settings.h"
#include "scene_camera.h"
#include "ui.h"

#include "widget_internal.h"

// clang-format off

static const String g_tooltipPresent          = string_static("Presentation mode.\n\n"
                                                            "Options:\n"
                                                            "- \a.bImmediate\ar: Don't wait for a vblank but immediately output the new image.\n"
                                                            "- \a.bSync\ar: Wait for the next vblank to output the new image.\n"
                                                            "- \a.bVSyncRelaxed\ar: Wait for the next vblank if the application is early, if the application is late then immediately output the new image.\n"
                                                            "- \a.bMailbox\ar: Wait for the next vblank to output a new image, but does not block acquiring a next image. If the application finishes another image before the vblank then it will replace the currently waiting image.");
static const String g_tooltipScale            = string_static("Render resolution scale.");
static const String g_tooltipLimiter          = string_static("Frame frequency limiter (in hz).\n\a.bNote\ar: 0 disables the limiter.");
static const String g_tooltipFrustumCulling   = string_static("Should objects be culled if their bounds are outside of the view frustum?");
static const String g_tooltipAmbientMode      = string_static("Controls the ambient draw in the forward pass.\n\n"
                                                            "Options:\n"
                                                            "- \a.bSolid\ar: Ambient radiance is white multiplied by the brightness.\n"
                                                            "- \a.bDiffuseIrradiance\ar: Diffuse ambient radiance is sampled from an diffuse-irradiance map.\n"
                                                            "- \a.bSpecularIrradiance\ar: Both diffuse and specular ambient radiance are sampled from irradiance maps and an BRDF integration lookup.\n\n"
                                                            "Debug options:\n"
                                                            "- \a.bDebugColor\ar: Geometry color output.\n"
                                                            "- \a.bDebugRoughness\ar: Geometry roughness output.\n"
                                                            "- \a.bDebugEmissive\ar: Geometry emissive output.\n"
                                                            "- \a.bDebugNormal\ar: Geometry world-space normals output.\n"
                                                            "- \a.bDebugDepth\ar: Geometry depth buffer.\n"
                                                            "- \a.bDebugTags\ar: Geometry tags output.\n"
                                                            "- \a.bDebugAmbientOcclusion\ar: AmbientOcclusion pass output.\n"
                                                            "- \a.bDebugFresnel\ar: Fresnel contribution.\n"
                                                            "- \a.bDebugDiffuseIrradiance\ar: Ambient diffuse irradiance.\n"
                                                            "- \a.bDebugSpecularIrradiance\ar: Ambient brdf specular irradiance.\n");
static const String g_tooltipDebugCamera      = string_static("Enable a top-down orthographic debug camera projection.\n\n\a.bNote\ar: The view properties of the 'real' camera will be used, this is useful for debugging the frustum culling.");
static const String g_tooltipDebugWireframe   = string_static("Enable a geometry wireframe debug overlay.");
static const String g_tooltipDebugSkinning    = string_static("Enable a skinning-weight debug overlay.");
static const String g_tooltipDebugShadow      = string_static("Draw the shadow-map as a fullscreen overlay.\n\a.bNote\ar: Click anywhere on the screen to disable.");
static const String g_tooltipDebugLight       = string_static("Visualize the (point) light draws.\n\a.bNote\ar: The brightness represents the light attenuation.");
static const String g_tooltipValidation       = string_static("Should gpu api validation be enabled?\n\a.bNote\ar: Requires a reset to take effect.");
static const String g_tooltipDebugGpu         = string_static("Should additional gpu debug info be emitted?\n\a.bNote\ar: Requires a reset to take effect.");
static const String g_tooltipVerbose          = string_static("Should verbose logging be enabled?");
static const String g_tooltipDefaults         = string_static("Reset all settings to their defaults.");
static const String g_tooltipReset            = string_static("Re-initialize the renderer.");
static const String g_tooltipFreeze           = string_static("Freeze the data set (halts data collection).");
static const String g_tooltipResourceFilter   = string_static("Filter resources by name.\nSupports glob characters \a.b*\ar and \a.b?\ar (\a.b!\ar prefix to invert).");
static const String g_tooltipShadows          = string_static("Enable shadow-map rendering to allow geometry to occlude the light radiance.");
static const String g_tooltipShadowFilterSize = string_static("Shadow filter size (in meters).\nControls the size of the soft shadow edge.");
static const String g_tooltipAmbientOcclusion = string_static("\a.b[SSAO]\ar Sample the geometry depth-buffer to compute a occlusion factor (how exposed it is to ambient lighting) for each fragment.");
static const String g_tooltipAoBlur           = string_static("\a.b[SSAO]\ar Take multiple samples from the occlusion buffer and average the results, reduces the noise that is present in the raw occlusion buffer.");
static const String g_tooltipAoAngle          = string_static("\a.b[SSAO]\ar Angle (in degrees) of the sample kernel cone.\nA wider angle will include more of the surrounding geometry.");
static const String g_tooltipAoRadius         = string_static("\a.b[SSAO]\ar Radius (in meters) of the sample kernel cone.\nA higher radius will include more of the surrounding geometry.");
static const String g_tooltipAoRadiusPow      = string_static("\a.b[SSAO]\ar Controls the distribution of the samples in the kernel cone.\n\n"
                                                              "Values:\n"
                                                              " < 1: Samples are distributed away from the origin.\n"
                                                              " == 1: Samples are distributed uniformly.\n"
                                                              " > 1: Samples are distributed closer to the origin.\n");
static const String g_tooltipAoPow            = string_static("\a.b[SSAO]\ar Power of the resulting occlusion factor, the higher the value the more occluded.");
static const String g_tooltipAoResScale       = string_static("Fraction of the geometry render resolution to use for the occlusion buffer.");
static const String g_tooltipExposure         = string_static("Multiplier over the hdr output before tone-mapping.");
static const String g_tooltipTonemapper       = string_static("Tone-mapper to map the hdr output to sdr.");
static const String g_tooltipBloom            = string_static("\a.b[Bloom]\ar Enable the bloom effect.\nCauses bright pixels to 'bleed' into the surrounding pixels.");
static const String g_tooltipBloomIntensity   = string_static("\a.b[Bloom]\ar Fraction of bloom to mix into the hdr output before tone-mapping.");
static const String g_tooltipBloomSteps       = string_static("\a.b[Bloom]\ar Number of blur steps.\nHigher gives a larger bloom area at the expense of additional gpu time and memory.");
static const String g_tooltipBloomRadius      = string_static("\a.b[Bloom]\ar Filter radius to use during the up-sample phase of the bloom blurring.\nToo high can result in ghosting or discontinuities in the bloom and too low requires many blur steps.");
static const String g_tooltipResourcePreview  = string_static("Preview this resource.\n\a.bNote\ar: Click anywhere on the screen to disable.");

// clang-format on

typedef enum {
  DebugRendTab_Settings,
  DebugRendTab_Objects,
  DebugRendTab_Resources,
  DebugRendTab_Light,
  DebugRendTab_Post,

  DebugRendTab_Count,
} DebugRendTab;

static const String g_rendTabNames[] = {
    string_static("\uE8B8 Settings"),
    string_static("Objects"),
    string_static("Resources"),
    string_static("\uE518 Light"),
    string_static("\uE429 Post"),
};
ASSERT(array_elems(g_rendTabNames) == DebugRendTab_Count, "Incorrect number of names");

typedef enum {
  DebugRendObjectSort_Graphic,
  DebugRendObjectSort_Order,
  DebugRendObjectSort_Instances,
  DebugRendObjectSort_Size,

  DebugRendObjectSort_Count,
} DebugRendObjectSort;

static const String g_objectSortNames[] = {
    string_static("Graphic"),
    string_static("Order"),
    string_static("Instances"),
    string_static("Size"),
};
ASSERT(array_elems(g_objectSortNames) == DebugRendObjectSort_Count, "Incorrect number of names");

typedef enum {
  DebugRendResSort_Name,
  DebugRendResSort_Type,
  DebugRendResSort_Size,

  DebugRendResSort_Count,
} DebugRendResSort;

static const String g_resSortNames[] = {
    string_static("Name"),
    string_static("Type"),
    string_static("Size"),
};
ASSERT(array_elems(g_resSortNames) == DebugRendResSort_Count, "Incorrect number of names");

typedef enum {
  DebugRendResType_Unknown,
  DebugRendResType_Graphic,
  DebugRendResType_Shader,
  DebugRendResType_Mesh,
  DebugRendResType_Texture,
  DebugRendResType_TextureCube,

  DebugRendResType_Count,
} DebugRendResType;

static const String g_resTypeNames[] = {
    string_static("Unknown"),
    string_static("Graphic"),
    string_static("Shader"),
    string_static("Mesh"),
    string_static("Texture"),
    string_static("TextureCube"),
};
ASSERT(array_elems(g_resTypeNames) == DebugRendResType_Count, "Incorrect number of names");

static const String g_presentOptions[] = {
    string_static("Immediate"),
    string_static("VSync"),
    string_static("VSyncRelaxed"),
    string_static("Mailbox"),
};

static const String g_ambientModeNames[] = {
    string_static("Solid"),
    string_static("DiffuseIrradiance"),
    string_static("SpecularIrradiance"),
    string_static("DebugColor"),
    string_static("DebugRoughness"),
    string_static("DebugEmissive"),
    string_static("DebugNormal"),
    string_static("DebugDepth"),
    string_static("DebugTags"),
    string_static("DebugAmbientOcclusion"),
    string_static("DebugFresnel"),
    string_static("DebugDiffuseIrradiance"),
    string_static("DebugSpecularIrradiance"),
};

static const String g_skyModeNames[] = {
    string_static("None"),
    string_static("Gradient"),
    string_static("CubeMap"),
};

static const String g_tonemapperNames[] = {
    string_static("Linear"),
    string_static("LinearSmooth"),
    string_static("Reinhard"),
    string_static("ReinhardJodie"),
    string_static("Aces"),
};

typedef struct {
  String           graphicName;
  AssetGraphicPass pass;
  i32              passOrder;
  u32              instanceCount;
  u32              dataSize, dataInstSize;
} DebugObjInfo;

typedef enum {
  DebugRendResFlags_IsLoading    = 1 << 0,
  DebugRendResFlags_IsFailed     = 1 << 1,
  DebugRendResFlags_IsUnused     = 1 << 2,
  DebugRendResFlags_IsPersistent = 1 << 3,
} DebugRendResFlags;

typedef struct {
  EcsEntityId       entity;
  String            name;
  DebugRendResType  type : 8;
  DebugRendResFlags flags : 8;
  u32               ticksTillUnload;
  usize             memory;
} DebugResourceInfo;

ecs_comp_define(DebugRendPanelComp) {
  UiPanel             panel;
  EcsEntityId         window;
  UiScrollview        scrollview;
  DynString           nameFilter;
  DebugRendObjectSort objSortMode;
  DebugRendResSort    resSortMode;
  DynArray            objects;   // DebugObjInfo[]
  DynArray            resources; // DebugResourceInfo[]
  bool                freeze;
  bool                hideEmptyObjects;
};

ecs_view_define(RendObjView) { ecs_access_read(RendObjectComp); }

ecs_view_define(GraphicView) {
  ecs_access_read(AssetComp);
  ecs_access_maybe_read(RendResGraphicComp);
}

ecs_view_define(ResourceView) {
  ecs_access_read(RendResComp);
  ecs_access_read(AssetComp);
  ecs_access_maybe_read(RendResGraphicComp);
  ecs_access_maybe_read(RendResShaderComp);
  ecs_access_maybe_read(RendResMeshComp);
  ecs_access_maybe_read(RendResTextureComp);
}

static void ecs_destruct_rend_panel(void* data) {
  DebugRendPanelComp* comp = data;
  dynstring_destroy(&comp->nameFilter);
  dynarray_destroy(&comp->objects);
  dynarray_destroy(&comp->resources);
}

static i8 rend_obj_compare_name(const void* a, const void* b) {
  return compare_string(
      field_ptr(a, DebugObjInfo, graphicName), field_ptr(b, DebugObjInfo, graphicName));
}

static i8 rend_obj_compare_order(const void* a, const void* b) {
  const DebugObjInfo* objA  = a;
  const DebugObjInfo* objB  = b;
  i8                  order = compare_u32(&objA->pass, &objB->pass);
  if (!order) {
    order = compare_i32(&objA->passOrder, &objB->passOrder);
  }
  if (!order) {
    order = compare_string(&objA->graphicName, &objB->graphicName);
  }
  return order;
}

static i8 rend_obj_compare_instances(const void* a, const void* b) {
  const DebugObjInfo* objA  = a;
  const DebugObjInfo* objB  = b;
  i8                  order = compare_i32_reverse(&objA->instanceCount, &objB->instanceCount);
  if (!order) {
    order = compare_string(&objA->graphicName, &objB->graphicName);
  }
  return order;
}

static i8 rend_obj_compare_size(const void* a, const void* b) {
  const DebugObjInfo* objA     = a;
  const DebugObjInfo* objB     = b;
  const usize         objASize = objA->dataSize + objA->dataInstSize * objA->instanceCount;
  const usize         objBSize = objB->dataSize + objB->dataInstSize * objB->instanceCount;

  i8 order = compare_usize_reverse(&objASize, &objBSize);
  if (!order) {
    order = compare_string(&objA->graphicName, &objB->graphicName);
  }
  return order;
}

static i8 rend_resource_compare_name(const void* a, const void* b) {
  return compare_string(
      field_ptr(a, DebugResourceInfo, name), field_ptr(b, DebugResourceInfo, name));
}

static i8 rend_resource_compare_type(const void* a, const void* b) {
  const DebugResourceInfo* resA  = a;
  const DebugResourceInfo* resB  = b;
  i8                       order = resA->type < resB->type ? -1 : resA->type > resB->type ? 1 : 0;
  if (!order) {
    order = compare_string(&resA->name, &resB->name);
  }
  return order;
}

static i8 rend_resource_compare_size(const void* a, const void* b) {
  const DebugResourceInfo* resA  = a;
  const DebugResourceInfo* resB  = b;
  i8                       order = compare_usize_reverse(&resA->memory, &resB->memory);
  if (!order) {
    order = compare_string(&resA->name, &resB->name);
  }
  return order;
}

static bool rend_panel_filter(DebugRendPanelComp* panelComp, const String name) {
  if (string_is_empty(panelComp->nameFilter)) {
    return true;
  }
  const String rawFilter = dynstring_view(&panelComp->nameFilter);
  const String filter    = fmt_write_scratch("*{}*", fmt_text(rawFilter));
  return string_match_glob(name, filter, StringMatchFlags_IgnoreCase);
}

static bool debug_overlay_blocker(UiCanvasComp* canvas) {
  const UiId id = ui_canvas_id_peek(canvas);
  ui_layout_push(canvas);
  ui_style_push(canvas);
  {
    ui_layout_set(canvas, ui_rect(ui_vector(0, 0), ui_vector(1, 1)), UiBase_Canvas); // Fullscreen.
    ui_style_color(canvas, ui_color(0, 0, 0, 225));
    ui_style_layer(canvas, UiLayer_Overlay);
    ui_canvas_draw_glyph(canvas, UiShape_Square, 10, UiFlags_Interactable);
  }
  ui_style_pop(canvas);
  ui_layout_pop(canvas);
  const UiStatus status = ui_canvas_elem_status(canvas, id);
  if (status >= UiStatus_Hovered) {
    ui_canvas_interact_type(canvas, UiInteractType_Action);
  }
  if (status == UiStatus_Activated) {
    ui_canvas_sound(canvas, UiSoundType_Click);
  }
  return status == UiStatus_Activated;
}

static void debug_overlay_bg(UiCanvasComp* c) {
  ui_style_push(c);
  ui_style_color(c, ui_color(0, 0, 0, 175));
  ui_style_outline(c, 3);
  ui_canvas_draw_glyph(c, UiShape_Square, 10, UiFlags_Interactable);
  ui_style_pop(c);
}

static void debug_overlay_str(UiCanvasComp* c, UiTable* t, const String label, const String v) {
  ui_table_next_row(c, t);
  ui_label(c, label, .fontSize = 14);
  ui_table_next_column(c, t);
  ui_label(c, v, .fontSize = 14, .selectable = true);
}

static void debug_overlay_int(UiCanvasComp* c, UiTable* t, const String label, const i64 v) {
  debug_overlay_str(c, t, label, fmt_write_scratch("{}", fmt_int(v)));
}

static void debug_overlay_size(UiCanvasComp* c, UiTable* t, const String label, const usize v) {
  debug_overlay_str(c, t, label, fmt_write_scratch("{}", fmt_size(v)));
}

static void
debug_overlay_entity(UiCanvasComp* c, UiTable* t, const String label, const EcsEntityId v) {
  debug_overlay_str(c, t, label, fmt_write_scratch("{}", ecs_entity_fmt(v)));
}

static void debug_overlay_resource(UiCanvasComp* c, RendSettingsComp* set, EcsView* resView) {
  EcsIterator* resourceItr = ecs_view_maybe_at(resView, set->debugViewerResource);
  if (!resourceItr) {
    return;
  }

  const EcsEntityId  entity    = ecs_view_entity(resourceItr);
  const AssetComp*   assetComp = ecs_view_read_t(resourceItr, AssetComp);
  const RendResComp* resComp   = ecs_view_read_t(resourceItr, RendResComp);

  static const UiVector g_panelSize = {900, 180};
  static const UiVector g_inset     = {-5, -5};

  ui_style_push(c);
  ui_style_layer(c, UiLayer_Overlay);

  ui_layout_push(c);
  ui_layout_move_to(c, UiBase_Canvas, UiAlign_BottomCenter, Ui_XY);
  ui_layout_move_dir(c, Ui_Up, 0.125f, UiBase_Canvas); // Center of the bottom 25% of screen.
  ui_layout_resize(c, UiAlign_MiddleCenter, g_panelSize, UiBase_Absolute, Ui_XY);

  f32 lodMax = 0.0f;

  debug_overlay_bg(c);
  ui_layout_grow(c, UiAlign_MiddleCenter, g_inset, UiBase_Absolute, Ui_XY);
  ui_layout_resize(c, UiAlign_BottomLeft, ui_vector(0.5f, 0), UiBase_Current, Ui_X);
  ui_layout_container_push(c, UiClip_None);

  UiTable table = ui_table(.spacing = {4, 4}, .rowHeight = 17);
  ui_table_add_column(&table, UiTableColumn_Fixed, 125);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  // Info section (left side of panel).
  debug_overlay_str(c, &table, string_lit("Name"), asset_id(assetComp));
  debug_overlay_entity(c, &table, string_lit("Entity"), entity);
  debug_overlay_int(c, &table, string_lit("Dependents"), rend_res_dependents(resComp));
  const RendResTextureComp* texture = ecs_view_read_t(resourceItr, RendResTextureComp);
  if (texture) {
    lodMax = (f32)(rend_res_texture_mip_levels(texture) - 1);
    debug_overlay_size(c, &table, string_lit("Memory"), rend_res_texture_memory(texture));
    const u16    width   = rend_res_texture_width(texture);
    const u16    height  = rend_res_texture_height(texture);
    const String sizeStr = fmt_write_scratch("{} x {}", fmt_int(width), fmt_int(height));
    debug_overlay_str(c, &table, string_lit("Size"), sizeStr);
    debug_overlay_str(c, &table, string_lit("Format"), rend_res_texture_format_str(texture));
    debug_overlay_int(c, &table, string_lit("Mips"), rend_res_texture_mip_levels(texture));
    debug_overlay_int(c, &table, string_lit("Layers"), rend_res_texture_layers(texture));
  }
  const RendResMeshComp* mesh = ecs_view_read_t(resourceItr, RendResMeshComp);
  if (mesh) {
    debug_overlay_size(c, &table, string_lit("Memory"), rend_res_mesh_memory(mesh));
    debug_overlay_int(c, &table, string_lit("Vertices"), rend_res_mesh_vertices(mesh));
    debug_overlay_int(c, &table, string_lit("Indices"), rend_res_mesh_indices(mesh));
    debug_overlay_int(c, &table, string_lit("Triangles"), rend_res_mesh_indices(mesh) / 3);
  }
  ui_layout_set(c, ui_rect(ui_vector(0, 0), ui_vector(1, 1)), UiBase_Container);
  ui_layout_container_pop(c);

  // Settings section (right side of panel).
  ui_layout_move_dir(c, Ui_Right, 1.0f, UiBase_Current);
  ui_layout_container_push(c, UiClip_None);
  ui_table_reset(&table);

  if (lodMax > 0.0f) {
    ui_table_next_row(c, &table);
    ui_label(c, string_lit("Lod"), .fontSize = 14);
    ui_table_next_column(c, &table);
    ui_slider(c, &set->debugViewerLod, .max = lodMax, .step = 1.0f);
  }
  if (texture) {
    ui_table_next_row(c, &table);
    ui_label(c, string_lit("Interpolate"), .fontSize = 14);
    ui_table_next_column(c, &table);
    ui_toggle_flag(c, (u32*)&set->debugViewerFlags, RendDebugViewer_Interpolate);

    ui_table_next_row(c, &table);
    ui_label(c, string_lit("Alpha Ignore"), .fontSize = 14);
    ui_table_next_column(c, &table);
    ui_toggle_flag(c, (u32*)&set->debugViewerFlags, RendDebugViewer_AlphaIgnore);

    ui_table_next_row(c, &table);
    ui_label(c, string_lit("Alpha Only"), .fontSize = 14);
    ui_table_next_column(c, &table);
    ui_toggle_flag(c, (u32*)&set->debugViewerFlags, RendDebugViewer_AlphaOnly);
  }

  ui_layout_container_pop(c);
  ui_layout_pop(c);
  ui_style_pop(c);
}

static void rend_settings_tab_draw(
    EcsWorld*               world,
    UiCanvasComp*           canvas,
    RendSettingsComp*       settings,
    RendSettingsGlobalComp* settingsGlobal) {
  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 250);
  ui_table_add_column(&table, UiTableColumn_Fixed, 350);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Present mode"));
  ui_table_next_column(canvas, &table);
  ui_select(
      canvas,
      (i32*)&settings->presentMode,
      g_presentOptions,
      array_elems(g_presentOptions),
      .tooltip = g_tooltipPresent);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Limiter"));
  ui_table_next_column(canvas, &table);
  f32 limiterFreq = settingsGlobal->limiterFreq;
  if (ui_slider(
          canvas, &limiterFreq, .min = 0, .max = 240, .step = 30, .tooltip = g_tooltipLimiter)) {
    settingsGlobal->limiterFreq = (u16)limiterFreq;
  }

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Scale"));
  ui_table_next_column(canvas, &table);
  ui_slider(
      canvas,
      &settings->resolutionScale,
      .min     = 0.2f,
      .max     = 2.0f,
      .step    = 0.05f,
      .tooltip = g_tooltipScale);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Frustum culling"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(
      canvas, (u32*)&settings->flags, RendFlags_FrustumCulling, .tooltip = g_tooltipFrustumCulling);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Ambient mode"));
  ui_table_next_column(canvas, &table);
  ui_select(
      canvas,
      (i32*)&settings->ambientMode,
      g_ambientModeNames,
      array_elems(g_ambientModeNames),
      .tooltip = g_tooltipAmbientMode);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Sky mode"));
  ui_table_next_column(canvas, &table);
  ui_select(canvas, (i32*)&settings->skyMode, g_skyModeNames, array_elems(g_skyModeNames));

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Debug Camera"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(
      canvas, (u32*)&settings->flags, RendFlags_DebugCamera, .tooltip = g_tooltipDebugCamera);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Debug Wireframe"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(
      canvas, (u32*)&settings->flags, RendFlags_DebugWireframe, .tooltip = g_tooltipDebugWireframe);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Debug Skinning"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(
      canvas, (u32*)&settings->flags, RendFlags_DebugSkinning, .tooltip = g_tooltipDebugSkinning);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Debug Gpu"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(
      canvas, (u32*)&settingsGlobal->flags, RendGlobalFlags_DebugGpu, .tooltip = g_tooltipDebugGpu);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Validation"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(
      canvas,
      (u32*)&settingsGlobal->flags,
      RendGlobalFlags_Validation,
      .tooltip = g_tooltipValidation);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Verbose"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(
      canvas, (u32*)&settingsGlobal->flags, RendGlobalFlags_Verbose, .tooltip = g_tooltipVerbose);

  ui_table_next_row(canvas, &table);
  if (ui_button(canvas, .label = string_lit("Defaults"), .tooltip = g_tooltipDefaults)) {
    rend_settings_to_default(settings);
    rend_settings_global_to_default(settingsGlobal);
  }
  ui_table_next_row(canvas, &table);
  if (ui_button(
          canvas,
          .label      = string_lit("Reset"),
          .frameColor = ui_color(255, 16, 0, 192),
          .tooltip    = g_tooltipReset)) {
    rend_reset(world);
  }
}

static UiColor rend_obj_bg_color(const DebugObjInfo* objInfo) {
  return objInfo->instanceCount ? ui_color(16, 64, 16, 192) : ui_color(48, 48, 48, 192);
}

static void rend_obj_options_draw(UiCanvasComp* canvas, DebugRendPanelComp* panelComp) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 150);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 25);
  ui_table_add_column(&table, UiTableColumn_Fixed, 110);
  ui_table_add_column(&table, UiTableColumn_Fixed, 25);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Sort:"));
  ui_table_next_column(canvas, &table);
  ui_select(canvas, (i32*)&panelComp->objSortMode, g_objectSortNames, DebugRendObjectSort_Count);
  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("Freeze:"));
  ui_table_next_column(canvas, &table);
  ui_toggle(canvas, &panelComp->freeze, .tooltip = g_tooltipFreeze);
  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("Hide empty:"));
  ui_table_next_column(canvas, &table);
  ui_toggle(canvas, &panelComp->hideEmptyObjects);

  ui_layout_pop(canvas);
}

static void rend_obj_info_query(DebugRendPanelComp* panelComp, EcsWorld* world) {
  if (!panelComp->freeze) {
    dynarray_clear(&panelComp->objects);
    EcsView*     graphicView = ecs_world_view_t(world, GraphicView);
    EcsIterator* graphicItr  = ecs_view_itr(graphicView);
    EcsView*     objView     = ecs_world_view_t(world, RendObjView);
    for (EcsIterator* itr = ecs_view_itr(objView); ecs_view_walk(itr);) {
      const RendObjectComp* obj = ecs_view_read_t(itr, RendObjectComp);
      if (panelComp->hideEmptyObjects && !rend_object_instance_count(obj)) {
        continue;
      }

      String           graphicName = string_lit("< unknown >");
      AssetGraphicPass pass        = AssetGraphicPass_None;
      i32              passOrder   = 0;
      if (ecs_view_maybe_jump(graphicItr, rend_object_resource(obj, RendObjectRes_Graphic))) {
        const AssetComp*          graphicAssetComp = ecs_view_read_t(graphicItr, AssetComp);
        const RendResGraphicComp* graphicComp = ecs_view_read_t(graphicItr, RendResGraphicComp);
        graphicName                           = asset_id(graphicAssetComp);
        if (graphicComp) {
          pass      = rend_res_pass(graphicComp);
          passOrder = rend_res_pass_order(graphicComp);
        }
      }
      *dynarray_push_t(&panelComp->objects, DebugObjInfo) = (DebugObjInfo){
          .graphicName   = graphicName,
          .instanceCount = rend_object_instance_count(obj),
          .dataSize      = rend_object_data_size(obj),
          .dataInstSize  = rend_object_data_inst_size(obj),
          .pass          = pass,
          .passOrder     = passOrder,
      };
    }
  }

  switch (panelComp->objSortMode) {
  case DebugRendObjectSort_Graphic:
    dynarray_sort(&panelComp->objects, rend_obj_compare_name);
    break;
  case DebugRendObjectSort_Order:
    dynarray_sort(&panelComp->objects, rend_obj_compare_order);
    break;
  case DebugRendObjectSort_Instances:
    dynarray_sort(&panelComp->objects, rend_obj_compare_instances);
    break;
  case DebugRendObjectSort_Size:
    dynarray_sort(&panelComp->objects, rend_obj_compare_size);
    break;
  case DebugRendObjectSort_Count:
    break;
  }
}

static void rend_obj_tab_draw(UiCanvasComp* canvas, DebugRendPanelComp* panelComp) {
  rend_obj_options_draw(canvas, panelComp);
  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 300);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Fixed, 70);
  ui_table_add_column(&table, UiTableColumn_Fixed, 85);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      canvas,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Graphic"), string_lit("Name of this objects's graphic asset.")},
          {string_lit("Pass"), string_lit("Pass that this object's graphic will be drawn in.")},
          {string_lit("Order"), string_lit("Order in the pass.")},
          {string_lit("Instances"), string_lit("Number of instances of this object.")},
          {string_lit("Inst Size"), string_lit("Per instance data-size.")},
          {string_lit("Total Size"), string_lit("Total data-size.")},
      });

  const u32 numObjects = (u32)panelComp->objects.size;
  ui_scrollview_begin(canvas, &panelComp->scrollview, ui_table_height(&table, numObjects));

  ui_canvas_id_block_next(canvas); // Start the list of objects on its own id block.
  dynarray_for_t(&panelComp->objects, DebugObjInfo, objInfo) {
    ui_table_next_row(canvas, &table);
    ui_table_draw_row_bg(canvas, &table, rend_obj_bg_color(objInfo));

    ui_canvas_id_block_string(canvas, objInfo->graphicName); // Set a stable canvas id.

    ui_label(canvas, objInfo->graphicName, .selectable = true);
    ui_table_next_column(canvas, &table);
    if (objInfo->pass != AssetGraphicPass_None) {
      ui_label(canvas, fmt_write_scratch("{}", fmt_text(asset_graphic_pass_name(objInfo->pass))));
      ui_table_next_column(canvas, &table);
      ui_label(canvas, fmt_write_scratch("{}", fmt_int(objInfo->passOrder)));
    } else {
      ui_table_next_column(canvas, &table);
      ui_canvas_id_skip(canvas, 2);
    }
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_int(objInfo->instanceCount)));
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_size(objInfo->dataInstSize)));
    ui_table_next_column(canvas, &table);
    const u32 totalDataSize = objInfo->dataSize + objInfo->dataInstSize * objInfo->instanceCount;
    ui_label(canvas, fmt_write_scratch("{}", fmt_size(totalDataSize)));
  }
  ui_canvas_id_block_next(canvas);

  ui_scrollview_end(canvas, &panelComp->scrollview);
  ui_layout_container_pop(canvas);
}

static void rend_resource_options_draw(UiCanvasComp* canvas, DebugRendPanelComp* panelComp) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 60);
  ui_table_add_column(&table, UiTableColumn_Fixed, 250);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 150);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 25);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Filter:"));
  ui_table_next_column(canvas, &table);
  ui_textbox(
      canvas,
      &panelComp->nameFilter,
      .placeholder = string_lit("*"),
      .tooltip     = g_tooltipResourceFilter);
  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("Sort:"));
  ui_table_next_column(canvas, &table);
  ui_select(canvas, (i32*)&panelComp->resSortMode, g_resSortNames, DebugRendResSort_Count);
  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("Freeze:"));
  ui_table_next_column(canvas, &table);
  ui_toggle(canvas, &panelComp->freeze, .tooltip = g_tooltipFreeze);

  ui_layout_pop(canvas);
}

static void rend_resource_info_query(DebugRendPanelComp* panelComp, EcsWorld* world) {
  if (!panelComp->freeze) {
    dynarray_clear(&panelComp->resources);
    EcsView* resourceView = ecs_world_view_t(world, ResourceView);
    for (EcsIterator* itr = ecs_view_itr(resourceView); ecs_view_walk(itr);) {
      const RendResComp* resComp   = ecs_view_read_t(itr, RendResComp);
      const AssetComp*   assetComp = ecs_view_read_t(itr, AssetComp);
      const String       name      = asset_id(assetComp);
      if (!rend_panel_filter(panelComp, name)) {
        continue;
      }
      const RendResGraphicComp* graphic = ecs_view_read_t(itr, RendResGraphicComp);
      const RendResShaderComp*  shader  = ecs_view_read_t(itr, RendResShaderComp);
      const RendResMeshComp*    mesh    = ecs_view_read_t(itr, RendResMeshComp);
      const RendResTextureComp* texture = ecs_view_read_t(itr, RendResTextureComp);

      DebugRendResType type   = DebugRendResType_Unknown;
      usize            memory = 0;
      if (graphic) {
        type = DebugRendResType_Graphic;
      } else if (shader) {
        type = DebugRendResType_Shader;
      } else if (mesh) {
        type   = DebugRendResType_Mesh;
        memory = rend_res_mesh_memory(mesh);
      } else if (texture) {
        type   = rend_res_texture_is_cube(texture) ? DebugRendResType_TextureCube
                                                   : DebugRendResType_Texture;
        memory = rend_res_texture_memory(texture);
      }
      DebugRendResFlags flags = 0;
      flags |= rend_res_is_loading(resComp) ? DebugRendResFlags_IsLoading : 0;
      flags |= rend_res_is_failed(resComp) ? DebugRendResFlags_IsFailed : 0;
      flags |= rend_res_is_unused(resComp) ? DebugRendResFlags_IsUnused : 0;
      flags |= rend_res_is_persistent(resComp) ? DebugRendResFlags_IsPersistent : 0;

      *dynarray_push_t(&panelComp->resources, DebugResourceInfo) = (DebugResourceInfo){
          .entity          = ecs_view_entity(itr),
          .name            = name,
          .type            = type,
          .flags           = flags,
          .ticksTillUnload = rend_res_ticks_until_unload(resComp),
          .memory          = memory,
      };
    }
  }

  switch (panelComp->resSortMode) {
  case DebugRendResSort_Name:
    dynarray_sort(&panelComp->resources, rend_resource_compare_name);
    break;
  case DebugRendResSort_Type:
    dynarray_sort(&panelComp->resources, rend_resource_compare_type);
    break;
  case DebugRendResSort_Size:
    dynarray_sort(&panelComp->resources, rend_resource_compare_size);
    break;
  case DebugRendResSort_Count:
    break;
  }
}

static UiColor rend_resource_bg_color(const DebugResourceInfo* resInfo) {
  if (resInfo->flags & DebugRendResFlags_IsLoading) {
    return ui_color(16, 64, 64, 192);
  }
  if (resInfo->flags & DebugRendResFlags_IsFailed) {
    return ui_color(64, 16, 16, 192);
  }
  if (resInfo->flags & DebugRendResFlags_IsUnused) {
    return ui_color(16, 16, 64, 192);
  }
  return ui_color(48, 48, 48, 192);
}

static void rend_resource_actions_draw(
    UiCanvasComp* canvas, RendSettingsComp* settings, const DebugResourceInfo* resInfo) {
  ui_layout_resize(canvas, UiAlign_MiddleLeft, ui_vector(25, 0), UiBase_Absolute, Ui_X);

  const bool previewActive   = ecs_entity_valid(settings->debugViewerResource);
  const bool supportsPreview = resInfo->type == DebugRendResType_Texture ||
                               resInfo->type == DebugRendResType_TextureCube ||
                               resInfo->type == DebugRendResType_Mesh;

  if (supportsPreview &&
      ui_button(
          canvas,
          .flags      = previewActive ? UiWidget_Disabled : 0,
          .label      = ui_shape_scratch(UiShape_Visibility),
          .fontSize   = 18,
          .frameColor = previewActive ? ui_color(64, 64, 64, 192) : ui_color(0, 16, 255, 192),
          .tooltip    = g_tooltipResourcePreview)) {
    settings->debugViewerResource = resInfo->entity;
    settings->debugViewerLod      = 0.0f;
  }
}

static void rend_resource_tab_draw(
    UiCanvasComp* canvas, DebugRendPanelComp* panelComp, RendSettingsComp* settings) {
  rend_resource_options_draw(canvas, panelComp);
  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 270);
  ui_table_add_column(&table, UiTableColumn_Fixed, 115);
  ui_table_add_column(&table, UiTableColumn_Fixed, 90);
  ui_table_add_column(&table, UiTableColumn_Fixed, 90);
  ui_table_add_column(&table, UiTableColumn_Fixed, 90);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      canvas,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Name"), string_lit("Name of the resource.")},
          {string_lit("Type"), string_lit("Type of the resource.")},
          {string_lit("Unload"), string_lit("Tick count until this resource will be unloaded.")},
          {string_lit("Size"), string_lit("Data size of the resource.")},
          {string_lit("Persistent"), string_lit("Is the resource persistent.")},
          {string_lit("Actions"), string_empty},
      });

  const u32 numResources = (u32)panelComp->resources.size;
  ui_scrollview_begin(canvas, &panelComp->scrollview, ui_table_height(&table, numResources));

  ui_canvas_id_block_next(canvas); // Start the list of resources on its own id block.
  dynarray_for_t(&panelComp->resources, DebugResourceInfo, resInfo) {
    ui_table_next_row(canvas, &table);
    ui_table_draw_row_bg(canvas, &table, rend_resource_bg_color(resInfo));

    ui_canvas_id_block_string(canvas, resInfo->name); // Set a stable canvas id.

    ui_label(canvas, resInfo->name, .selectable = true);
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_text(g_resTypeNames[resInfo->type])));
    ui_table_next_column(canvas, &table);
    if (resInfo->flags & DebugRendResFlags_IsUnused) {
      ui_label(canvas, fmt_write_scratch("{}", fmt_int(resInfo->ticksTillUnload)));
    }
    ui_table_next_column(canvas, &table);
    if (resInfo->memory) {
      ui_label(canvas, fmt_write_scratch("{}", fmt_size(resInfo->memory)));
    }
    ui_table_next_column(canvas, &table);
    const bool isPersistent = (resInfo->flags & DebugRendResFlags_IsPersistent) != 0;
    ui_label(canvas, fmt_write_scratch("{}", fmt_bool(isPersistent)));

    ui_table_next_column(canvas, &table);
    rend_resource_actions_draw(canvas, settings, resInfo);
  }
  ui_canvas_id_block_next(canvas);

  ui_scrollview_end(canvas, &panelComp->scrollview);
  ui_layout_container_pop(canvas);
}

static void rend_light_tab_draw(
    UiCanvasComp* canvas, RendSettingsComp* settings, RendSettingsGlobalComp* settingsGlobal) {
  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 250);
  ui_table_add_column(&table, UiTableColumn_Fixed, 350);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Debug light"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(
      canvas,
      (u32*)&settingsGlobal->flags,
      RendGlobalFlags_DebugLight,
      .tooltip = g_tooltipDebugLight);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Debug shadow"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(
      canvas, (u32*)&settings->flags, RendFlags_DebugShadow, .tooltip = g_tooltipDebugShadow);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Shadows"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(canvas, (u32*)&settings->flags, RendFlags_Shadows, .tooltip = g_tooltipShadows);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Shadow resolution"));
  ui_table_next_column(canvas, &table);
  if (debug_widget_editor_u16(canvas, &settings->shadowResolution, UiWidget_Default)) {
    if (settings->shadowResolution == 0) {
      settings->shadowResolution = 512;
    } else if (settings->shadowResolution > 16384) {
      settings->shadowResolution = 16384;
    }
  }

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Shadow Filter Size"));
  ui_table_next_column(canvas, &table);
  ui_slider(
      canvas, &settingsGlobal->shadowFilterSize, .max = 0.5f, .tooltip = g_tooltipShadowFilterSize);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Vfx shadows"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(canvas, (u32*)&settings->flags, RendFlags_VfxShadows);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Ambient occlusion"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(
      canvas,
      (u32*)&settings->flags,
      RendFlags_AmbientOcclusion,
      .tooltip = g_tooltipAmbientOcclusion);

  if (settings->flags & RendFlags_AmbientOcclusion) {
    ui_table_next_row(canvas, &table);
    ui_label(canvas, string_lit("AO blur"));
    ui_table_next_column(canvas, &table);
    ui_toggle_flag(
        canvas, (u32*)&settings->flags, RendFlags_AmbientOcclusionBlur, .tooltip = g_tooltipAoBlur);

    ui_table_next_row(canvas, &table);
    ui_label(canvas, string_lit("AO angle"));
    ui_table_next_column(canvas, &table);
    f32 aoAngleDeg = settings->aoAngle * math_rad_to_deg;
    if (ui_slider(canvas, &aoAngleDeg, .max = 180, .tooltip = g_tooltipAoAngle)) {
      settings->aoAngle = aoAngleDeg * math_deg_to_rad;
      rend_settings_generate_ao_kernel(settings);
    }

    ui_table_next_row(canvas, &table);
    ui_label(canvas, string_lit("AO radius"));
    ui_table_next_column(canvas, &table);
    if (ui_slider(canvas, &settings->aoRadius, .tooltip = g_tooltipAoRadius)) {
      rend_settings_generate_ao_kernel(settings);
    }

    ui_table_next_row(canvas, &table);
    ui_label(canvas, string_lit("AO radius power"));
    ui_table_next_column(canvas, &table);
    if (ui_slider(canvas, &settings->aoRadiusPower, .max = 5.0f, .tooltip = g_tooltipAoRadiusPow)) {
      rend_settings_generate_ao_kernel(settings);
    }

    ui_table_next_row(canvas, &table);
    ui_label(canvas, string_lit("AO power"));
    ui_table_next_column(canvas, &table);
    ui_slider(canvas, &settings->aoPower, .max = 7.5f, .tooltip = g_tooltipAoPow);

    ui_table_next_row(canvas, &table);
    ui_label(canvas, string_lit("AO resolution scale"));
    ui_table_next_column(canvas, &table);
    ui_slider(
        canvas,
        &settings->aoResolutionScale,
        .min     = 0.1f,
        .max     = 1.0f,
        .step    = 0.05f,
        .tooltip = g_tooltipAoResScale);
  }
  ui_canvas_id_block_next(canvas); // Resume on a stable canvas id.
}

static void rend_post_tab_draw(
    UiCanvasComp* canvas, RendSettingsComp* settings, RendSettingsGlobalComp* settingsGlobal) {
  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 250);
  ui_table_add_column(&table, UiTableColumn_Fixed, 350);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Exposure"));
  ui_table_next_column(canvas, &table);
  ui_slider(canvas, &settings->exposure, .min = 0.01f, .max = 5.0f, .tooltip = g_tooltipExposure);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Tonemapper"));
  ui_table_next_column(canvas, &table);
  ui_select(
      canvas,
      (i32*)&settings->tonemapper,
      g_tonemapperNames,
      array_elems(g_tonemapperNames),
      .tooltip = g_tooltipTonemapper);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Bloom"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(canvas, (u32*)&settings->flags, RendFlags_Bloom, .tooltip = g_tooltipBloom);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Bloom intensity"));
  ui_table_next_column(canvas, &table);
  ui_slider(canvas, &settings->bloomIntensity, .tooltip = g_tooltipBloomIntensity);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Bloom steps"));
  ui_table_next_column(canvas, &table);
  f32 blSteps = (f32)settings->bloomSteps;
  if (ui_slider(canvas, &blSteps, .min = 1, .max = 6, .step = 1, .tooltip = g_tooltipBloomSteps)) {
    settings->bloomSteps = (u32)blSteps;
  }

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Bloom radius"));
  ui_table_next_column(canvas, &table);
  f32 blRadius = settings->bloomRadius * 1e3f;
  if (ui_slider(canvas, &blRadius, .min = 0.01f, .max = 5.0f, .tooltip = g_tooltipBloomRadius)) {
    settings->bloomRadius = blRadius * 1e-3f;
  }

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Distortion"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(canvas, (u32*)&settings->flags, RendFlags_Distortion);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Distortion resolution scale"));
  ui_table_next_column(canvas, &table);
  ui_slider(canvas, &settings->distortionResolutionScale, .min = 0.1f, .max = 1.0f, .step = 0.05f);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Distortion debug"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(canvas, (u32*)&settings->flags, RendFlags_DebugDistortion);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Decals"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(canvas, (u32*)&settings->flags, RendFlags_Decals);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Fog"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(canvas, (u32*)&settingsGlobal->flags, RendGlobalFlags_Fog);

  const bool    fogActive      = (settingsGlobal->flags & RendGlobalFlags_Fog) != 0;
  UiWidgetFlags fogWidgetFlags = fogActive ? UiWidget_Default : UiWidget_Disabled;

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Fog blur steps"));
  ui_table_next_column(canvas, &table);
  f32 fogBlurSteps = (f32)settings->fogBlurSteps;
  if (ui_slider(canvas, &fogBlurSteps, .min = 0, .max = 4, .step = 1)) {
    settings->fogBlurSteps = (u32)fogBlurSteps;
  }

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Fog blur scale"));
  ui_table_next_column(canvas, &table);
  ui_slider(canvas, &settings->fogBlurScale, .min = 0.1f, .max = 2.0f);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Fog dilation"));
  ui_table_next_column(canvas, &table);
  ui_slider(canvas, &settingsGlobal->fogDilation, .min = -10.0f, .max = 10.0f);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Fog debug"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(canvas, (u32*)&settings->flags, RendFlags_DebugFog, .flags = fogWidgetFlags);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Fog resolution"));
  ui_table_next_column(canvas, &table);
  if (debug_widget_editor_u16(canvas, &settings->fogResolution, UiWidget_Default)) {
    if (settings->fogResolution == 0) {
      settings->fogResolution = 128;
    } else if (settings->fogResolution > 16384) {
      settings->fogResolution = 16384;
    }
  }
}

static void rend_panel_draw(
    EcsWorld*               world,
    UiCanvasComp*           canvas,
    DebugRendPanelComp*     panelComp,
    RendSettingsComp*       settings,
    RendSettingsGlobalComp* settingsGlobal) {

  const String title = fmt_write_scratch("{} Renderer Panel", fmt_ui_shape(Brush));
  ui_panel_begin(
      canvas,
      &panelComp->panel,
      .title       = title,
      .tabNames    = g_rendTabNames,
      .tabCount    = DebugRendTab_Count,
      .topBarColor = ui_color(100, 0, 0, 192));

  switch (panelComp->panel.activeTab) {
  case DebugRendTab_Settings:
    rend_settings_tab_draw(world, canvas, settings, settingsGlobal);
    break;
  case DebugRendTab_Objects:
    rend_obj_info_query(panelComp, world);
    rend_obj_tab_draw(canvas, panelComp);
    break;
  case DebugRendTab_Resources:
    rend_resource_info_query(panelComp, world);
    rend_resource_tab_draw(canvas, panelComp, settings);
    break;
  case DebugRendTab_Light:
    rend_light_tab_draw(canvas, settings, settingsGlobal);
    break;
  case DebugRendTab_Post:
    rend_post_tab_draw(canvas, settings, settingsGlobal);
    break;
  }

  ui_panel_end(canvas, &panelComp->panel);
}

ecs_view_define(GlobalView) { ecs_access_write(RendSettingsGlobalComp); }

ecs_view_define(PainterView) {
  ecs_access_with(SceneCameraComp);
  ecs_access_write(RendSettingsComp);
}

ecs_view_define(PanelUpdateView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DebugRendPanelComp's are exclusively managed here.

  ecs_access_read(DebugPanelComp);
  ecs_access_write(DebugRendPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_system_define(DebugRendUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  RendSettingsGlobalComp* settingsGlobal = ecs_view_write_t(globalItr, RendSettingsGlobalComp);

  EcsIterator* painterItr = ecs_view_itr(ecs_world_view_t(world, PainterView));

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugRendPanelComp* panelComp = ecs_view_write_t(itr, DebugRendPanelComp);
    UiCanvasComp*       canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ecs_view_itr_reset(painterItr);

    // NOTE: Target a 3d painter (with camera), for detached panels we use the first camera we find.
    if (!ecs_view_maybe_jump(painterItr, panelComp->window) && !ecs_view_walk(painterItr)) {
      continue; // No painter found.
    }
    RendSettingsComp* settings = ecs_view_write_t(painterItr, RendSettingsComp);

    ui_canvas_reset(canvas);
    const bool pinned = ui_panel_pinned(&panelComp->panel);
    if (debug_panel_hidden(ecs_view_read_t(itr, DebugPanelComp)) && !pinned) {
      settings->debugViewerResource = 0;
      settings->flags &= ~RendFlags_DebugOverlay;
      continue;
    }
    rend_panel_draw(world, canvas, panelComp, settings, settingsGlobal);

    // Check if any renderer debug overlay is active.
    const bool overlayActive = ecs_entity_valid(settings->debugViewerResource) ||
                               (settings->flags & RendFlags_DebugOverlay) != 0;
    if (overlayActive) {
      if (debug_overlay_blocker(canvas)) {
        settings->debugViewerResource = 0;
        settings->flags &= ~RendFlags_DebugOverlay;
      } else {
        debug_overlay_resource(canvas, settings, ecs_world_view_t(world, ResourceView));
      }
    }

    if (ui_panel_closed(&panelComp->panel)) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }

  /**
   * Disable the debug overlay if no render panel is open.
   * Can happen when a panel is closed external to this module while having an overlay active.
   */
  if (!ecs_utils_any(world, PanelUpdateView)) {
    for (ecs_view_itr_reset(painterItr); ecs_view_walk(painterItr);) {
      RendSettingsComp* settings    = ecs_view_write_t(painterItr, RendSettingsComp);
      settings->debugViewerResource = 0;
      settings->flags &= ~RendFlags_DebugOverlay;
    }
  }
}

ecs_module_init(debug_rend_module) {
  ecs_register_comp(DebugRendPanelComp, .destructor = ecs_destruct_rend_panel);

  ecs_register_view(RendObjView);
  ecs_register_view(GraphicView);
  ecs_register_view(ResourceView);
  ecs_register_view(GlobalView);
  ecs_register_view(PainterView);
  ecs_register_view(PanelUpdateView);

  ecs_register_system(
      DebugRendUpdatePanelSys,
      ecs_view_id(RendObjView),
      ecs_view_id(GraphicView),
      ecs_view_id(ResourceView),
      ecs_view_id(PanelUpdateView),
      ecs_view_id(PainterView),
      ecs_view_id(GlobalView));

  // NOTE: Update the panel before clearing the objects so we can inspect the last frame's objects.
  ASSERT((u32)DebugOrder_RendUpdate < (u32)RendOrder_ObjectClear, "Invalid update order");
  ecs_order(DebugRendUpdatePanelSys, DebugOrder_RendUpdate);
}

EcsEntityId
debug_rend_panel_open(EcsWorld* world, const EcsEntityId window, const DebugPanelType type) {
  const EcsEntityId   panelEntity = debug_panel_create(world, window, type);
  DebugRendPanelComp* rendPanel   = ecs_world_add_t(
      world,
      panelEntity,
      DebugRendPanelComp,
      .panel            = ui_panel(.size = ui_vector(800, 520)),
      .window           = window,
      .scrollview       = ui_scrollview(),
      .nameFilter       = dynstring_create(g_allocHeap, 32),
      .objSortMode      = DebugRendObjectSort_Order,
      .resSortMode      = DebugRendResSort_Size,
      .objects          = dynarray_create_t(g_allocHeap, DebugObjInfo, 256),
      .resources        = dynarray_create_t(g_allocHeap, DebugResourceInfo, 256),
      .hideEmptyObjects = true);

  if (type == DebugPanelType_Detached) {
    ui_panel_maximize(&rendPanel->panel);
  }

  return panelEntity;
}
