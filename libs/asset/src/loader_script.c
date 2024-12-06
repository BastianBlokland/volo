#include "asset_script.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_stringtable.h"
#include "data_read.h"
#include "data_utils.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "script_binder.h"
#include "script_compile.h"
#include "script_diag.h"
#include "script_doc.h"
#include "script_optimize.h"
#include "script_read.h"

#include "manager_internal.h"
#include "repo_internal.h"

DataMeta g_assetScriptMeta;

static const ScriptBinder* asset_script_domain_binder(const AssetScriptDomain domain) {
  switch (domain) {
  case AssetScriptDomain_ImportMesh:
    return g_assetScriptImportMeshBinder;
  case AssetScriptDomain_ImportTexture:
    return g_assetScriptImportTextureBinder;
  case AssetScriptDomain_Scene:
    return g_assetScriptSceneBinder;
  }
  diag_crash();
}

static bool asset_script_domain_match(const String fileIdentifier, AssetScriptDomain* out) {
  if (script_binder_match(g_assetScriptImportMeshBinder, fileIdentifier)) {
    *out = AssetScriptDomain_ImportMesh;
    return true;
  }
  if (script_binder_match(g_assetScriptImportTextureBinder, fileIdentifier)) {
    *out = AssetScriptDomain_ImportTexture;
    return true;
  }
  if (script_binder_match(g_assetScriptSceneBinder, fileIdentifier)) {
    *out = AssetScriptDomain_Scene;
    return true;
  }
  return false;
}

static u32 asset_script_prog_hash(const ScriptProgram* prog) {
  u32 hash = bits_hash_32(mem_create(prog->code.ptr, prog->code.size));
  for (u32 i = 0; i != prog->literals.count; ++i) {
    hash = bits_hash_32_combine(hash, script_hash(prog->literals.values[i]));
  }
  return hash;
}

ecs_comp_define_public(AssetScriptComp);
ecs_comp_define(AssetScriptSourceComp) { AssetSource* src; };

static void ecs_destruct_script_comp(void* data) {
  AssetScriptComp* comp = data;
  data_destroy(
      g_dataReg, g_allocHeap, g_assetScriptMeta, mem_create(comp, sizeof(AssetScriptComp)));
}

static void ecs_destruct_script_source_comp(void* data) {
  AssetScriptSourceComp* comp = data;
  asset_repo_source_close(comp->src);
}

ecs_view_define(ScriptUnloadView) {
  ecs_access_with(AssetScriptComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any script-asset component for unloaded assets.
 */
ecs_system_define(ScriptUnloadAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, ScriptUnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetScriptComp);
    ecs_utils_maybe_remove_t(world, entity, AssetScriptSourceComp);
  }
}

ecs_module_init(asset_script_module) {
  ecs_register_comp(AssetScriptComp, .destructor = ecs_destruct_script_comp);
  ecs_register_comp(AssetScriptSourceComp, .destructor = ecs_destruct_script_source_comp);

  ecs_register_view(ScriptUnloadView);

  ecs_register_system(ScriptUnloadAssetSys, ecs_view_id(ScriptUnloadView));
}

void asset_data_init_script(void) {
  // clang-format off
  data_reg_opaque_t(g_dataReg, ScriptVal);

  data_reg_struct_t(g_dataReg, ScriptPosLineCol);
  data_reg_field_t(g_dataReg, ScriptPosLineCol, line, data_prim_t(u16));
  data_reg_field_t(g_dataReg, ScriptPosLineCol, column, data_prim_t(u16));

  data_reg_struct_t(g_dataReg, ScriptRangeLineCol);
  data_reg_field_t(g_dataReg, ScriptRangeLineCol, start, t_ScriptPosLineCol);
  data_reg_field_t(g_dataReg, ScriptRangeLineCol, end, t_ScriptPosLineCol);

  data_reg_struct_t(g_dataReg, ScriptProgramLoc);
  data_reg_field_t(g_dataReg, ScriptProgramLoc, instruction, data_prim_t(u16));
  data_reg_field_t(g_dataReg, ScriptProgramLoc, range, t_ScriptRangeLineCol);

  data_reg_struct_t(g_dataReg, ScriptProgram);
  data_reg_field_t(g_dataReg, ScriptProgram, code, data_prim_t(DataMem), .flags = DataFlags_ExternalMemory);
  data_reg_field_t(g_dataReg, ScriptProgram, binderHash, data_prim_t(u64));
  data_reg_field_t(g_dataReg, ScriptProgram, literals, t_ScriptVal, .container = DataContainer_HeapArray);
  data_reg_field_t(g_dataReg, ScriptProgram, locations, t_ScriptProgramLoc, .container = DataContainer_HeapArray);

  data_reg_enum_t(g_dataReg, AssetScriptDomain);
  data_reg_const_t(g_dataReg, AssetScriptDomain, ImportMesh);
  data_reg_const_t(g_dataReg, AssetScriptDomain, ImportTexture);
  data_reg_const_t(g_dataReg, AssetScriptDomain, Scene);

  data_reg_struct_t(g_dataReg, AssetScriptComp);
  data_reg_field_t(g_dataReg, AssetScriptComp, domain, t_AssetScriptDomain);
  data_reg_field_t(g_dataReg, AssetScriptComp, hash, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetScriptComp, prog, t_ScriptProgram);
  data_reg_field_t(g_dataReg, AssetScriptComp, stringLiterals, data_prim_t(String), .container = DataContainer_HeapArray, .flags = DataFlags_Intern);
  // clang-format on

  g_assetScriptMeta = data_meta_t(t_AssetScriptComp);
}

void asset_load_script(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {
  (void)importEnv;

  Allocator* tempAlloc = alloc_bump_create_stack(2 * usize_kibibyte);

  ScriptDoc*     doc         = script_create(g_allocHeap);
  StringTable*   stringtable = stringtable_create(g_allocHeap);
  ScriptDiagBag* diags       = script_diag_bag_create(tempAlloc, ScriptDiagFilter_Error);
  ScriptSymBag*  symsNull    = null;

  ScriptLookup* lookup = script_lookup_create(g_allocHeap);
  script_lookup_update(lookup, src->data);

  AssetScriptDomain domain;
  if (UNLIKELY(!asset_script_domain_match(id, &domain))) {
    log_e(
        "Failed to match script domain",
        log_param("id", fmt_text(id)),
        log_param("entity", ecs_entity_fmt(entity)));
    goto Error;
  }
  const ScriptBinder* domainBinder = asset_script_domain_binder(domain);

  // Parse the script.
  ScriptExpr expr = script_read(doc, domainBinder, src->data, stringtable, diags, symsNull);

  const u32 diagCount = script_diag_count(diags, ScriptDiagFilter_All);
  for (u32 i = 0; i != diagCount; ++i) {
    const ScriptDiag* diag = script_diag_data(diags) + i;
    const String      msg  = script_diag_pretty_scratch(lookup, diag);
    log_e(
        "Script read error",
        log_param("id", fmt_text(id)),
        log_param("entity", ecs_entity_fmt(entity)),
        log_param("error", fmt_text(msg)));
  }

  script_diag_bag_destroy(diags);

  if (UNLIKELY(sentinel_check(expr) || diagCount > 0)) {
    goto Error;
  }

  // Perform optimization passes.
  expr = script_optimize(doc, expr);

  // Compile the program.
  ScriptProgram            prog;
  const ScriptCompileError compileErr = script_compile(doc, lookup, expr, g_allocHeap, &prog);
  if (UNLIKELY(compileErr)) {
    log_e(
        "Script compile error",
        log_param("id", fmt_text(id)),
        log_param("entity", ecs_entity_fmt(entity)),
        log_param("error", fmt_text(script_compile_error_str(compileErr))));
    goto Error;
  }

  diag_assert(script_prog_validate(&prog, domainBinder));

  const StringTableArray strings = stringtable_clone_strings(stringtable, g_allocHeap);

  // Register string-literals to the global string-table.
  heap_array_for_t(strings, String, str) { stringtable_add(g_stringtable, *str); }

  AssetScriptComp* scriptAsset = ecs_world_add_t(
      world,
      entity,
      AssetScriptComp,
      .domain                = domain,
      .hash                  = asset_script_prog_hash(&prog),
      .prog                  = prog,
      .stringLiterals.values = strings.values,
      .stringLiterals.count  = strings.count);

  ecs_world_add_empty_t(world, entity, AssetLoadedComp);

  if (scriptAsset) {
    asset_cache(world, entity, g_assetScriptMeta, mem_create(scriptAsset, sizeof(AssetScriptComp)));
  }

  goto Cleanup;

Error:
  ecs_world_add_empty_t(world, entity, AssetFailedComp);

Cleanup:
  script_destroy(doc);
  stringtable_destroy(stringtable);
  script_lookup_destroy(lookup);
  asset_repo_source_close(src);
}

void asset_load_script_bin(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {
  (void)importEnv;

  AssetScriptComp script;
  DataReadResult  result;
  data_read_bin(g_dataReg, src->data, g_allocHeap, g_assetScriptMeta, mem_var(script), &result);

  if (UNLIKELY(result.error)) {
    log_e(
        "Failed to load binary script",
        log_param("id", fmt_text(id)),
        log_param("entity", ecs_entity_fmt(entity)),
        log_param("error-code", fmt_int(result.error)),
        log_param("error", fmt_text(result.errorMsg)));
    ecs_world_add_empty_t(world, entity, AssetFailedComp);
    asset_repo_source_close(src);
    return;
  }

  const ScriptBinder* binder = asset_script_domain_binder(script.domain);
  if (UNLIKELY(!script_prog_validate(&script.prog, binder))) {
    log_e(
        "Malformed binary script",
        log_param("id", fmt_text(id)),
        log_param("entity", ecs_entity_fmt(entity)));

    data_destroy(g_dataReg, g_allocHeap, g_assetScriptMeta, mem_var(script));
    ecs_world_add_empty_t(world, entity, AssetFailedComp);
    asset_repo_source_close(src);
    return;
  }

  *ecs_world_add_t(world, entity, AssetScriptComp) = script;
  ecs_world_add_t(world, entity, AssetScriptSourceComp, .src = src);

  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
}
