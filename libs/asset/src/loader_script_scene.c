#include "asset/script.h"
#include "core/alloc.h"
#include "core/format.h"
#include "script/binder.h"
#include "script/sig.h"

ScriptBinder* g_assetScriptSceneBinder;

static void bind(
    ScriptBinder*      binder,
    const String       name,
    const String       doc,
    const ScriptMask   retMask,
    const ScriptSigArg args[],
    const u8           argCount) {
  const ScriptSig* sig = script_sig_create(g_allocScratch, retMask, args, argCount);
  script_binder_declare(binder, name, doc, sig, null);
}

void asset_data_init_script_scene(void) {
  const ScriptBinderFlags flags  = ScriptBinderFlags_None;
  ScriptBinder*           binder = script_binder_create(g_allocPersist, string_lit("scene"), flags);
  script_binder_filter_set(binder, string_lit("scene/*.script"));

  // clang-format off
  static const String g_combinatorDoc      = string_static("Supported combinators:\n\n-`Add`\n\n-`Sub`\n\n-`Mul`\n\n-`Div`\n\n-`Min`\n\n-`Max`\n\n-`And`\n\n-`Or`");
  static const String g_layerDoc           = string_static("Supported layers:\n\n-`Environment`\n\n-`Destructible`\n\n-`Infantry`\n\n-`Vehicle`\n\n-`Structure`\n\n-`Unit`\n\n-`Debug`\n\n-`AllIncludingDebug`\n\n-`AllNonDebug` (default)");
  static const String g_factionDoc         = string_static("Supported factions:\n\n-`FactionA`\n\n-`FactionB`\n\n-`FactionC`\n\n-`FactionD`\n\n-`FactionNone`");
  static const String g_queryOptionDoc     = string_static("Supported options:\n\n-`FactionSelf`\n\n-`FactionOther`");
  static const String g_capabilityDoc      = string_static("Supported capabilities:\n\n-`NavTravel`\n\n-`Animation`\n\n-`Attack`\n\n-`Status`\n\n-`Teleport`\n\n-`Bark`\n\n-`Renderable`\n\n-`Vfx`\n\n-`Light`\n\n-`Sound`");
  static const String g_activityDoc        = string_static("Supported activities:\n\n-`Dead`\n\n-`Moving`\n\n-`Traveling`\n\n-`Attacking`\n\n-`Firing`\n\n-`AttackReadying`\n\n-`AttackAiming`");
  static const String g_statusDoc          = string_static("Supported status:\n\n-`Burning`\n\n-`Bleeding`\n\n-`Healing`\n\n-`Veteran`");
  static const String g_barkDoc            = string_static("Supported types:\n\n-`Death`\n\n-`Confirm`");
  static const String g_healthStatsDoc     = string_static("Supported stats:\n\n-`DealtDamage`\n\n-`DealtHealing`\n\n-`Kills`");
  static const String g_targetExcludeDoc   = string_static("Supported options:\n\n-`Unreachable`\n\n-`Obscured`");
  static const String g_clockDoc           = string_static("Supported clocks:\n\n-`LevelTime` (default)\n\n-`Time`\n\n-`RealTime`\n\n-`Delta`\n\n-`RealDelta`\n\n-`Ticks`");
  static const String g_navLayerDoc        = string_static("Supported layers:\n\n-`Normal` (default)\n\n-`Large`");
  static const String g_navFindTypeDoc     = string_static("Supported types:\n\n-`ClosestCell` (default)\n\n-`UnblockedCell`\n\n-`FreeCell`");
  static const String g_markerTypeDoc      = string_static("Supported types:\n\n-`Info`\n\n-`Danger`\n\n-`Goal`");
  static const String g_vfxParamDoc        = string_static("Supported parameters:\n\n-`Alpha`\n\n-`EmitMultiplier`");
  static const String g_renderableParamDoc = string_static("Supported parameters:\n\n-`Color`\n\n-`Alpha`\n\n-`Emissive`");
  static const String g_lightParamDoc      = string_static("Supported parameters:\n\n-`Ambient`\n\n-`Radiance`\n\n-`Length`\n\n-`Angle`");
  static const String g_soundParamDoc      = string_static("Supported parameters:\n\n-`Gain`\n\n-`Pitch`");
  static const String g_animParamDoc       = string_static("Supported parameters:\n\n-`Time`\n\n-`TimeNorm`\n\n-`Speed`\n\n-`Weight`\n\n-`Active`\n\n-`Loop`\n\n-`FadeIn`\n\n-`FadeOut`\n\n-`Duration`");
  {
    const String     name = string_lit("self");
    const String     doc  = string_lit("Return the entity that is executing the current script.");
    const ScriptMask ret  = script_mask_entity;
    bind(binder, name, doc, ret, null, 0);
  }
  {
    const String       name   = string_lit("exists");
    const String       doc    = string_lit("Test if the given entity still exists.\n\n*Note*: Returns false if input value is null.");
    const ScriptMask   ret    = script_mask_bool;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("position");
    const String       doc    = string_lit("Lookup the position of the given entity.");
    const ScriptMask   ret    = script_mask_vec3 | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("velocity");
    const String       doc    = string_lit("Lookup the velocity of the given entity.");
    const ScriptMask   ret    = script_mask_vec3 | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("rotation");
    const String       doc    = string_lit("Lookup the rotation of the given entity.");
    const ScriptMask   ret    = script_mask_quat | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("scale");
    const String       doc    = string_lit("Lookup the scale of the given entity.");
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("name");
    const String       doc    = string_lit("Lookup the name of the given entity.");
    const ScriptMask   ret    = script_mask_str | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("faction");
    const String       doc    = fmt_write_scratch("Lookup or change the faction of the given entity.\n\n{}", fmt_text(g_factionDoc));
    const ScriptMask   ret    = script_mask_str | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
        {string_lit("faction"), script_mask_str | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("health");
    const String       doc    = string_lit("Lookup the health points of the given entity.");
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
        {string_lit("normalized"), script_mask_bool | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("health_stat");
    const String       doc    = fmt_write_scratch("Lookup a health stat of the given entity.\n\n{}", fmt_text(g_healthStatsDoc));
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
        {string_lit("stat"), script_mask_str},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("vision");
    const String       doc    = string_lit("Lookup the vision radius of the given entity.");
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("visible");
    const String       doc    = string_lit("Check if the given position is visible for this faction.");
    const ScriptMask   ret    = script_mask_bool;
    const ScriptSigArg args[] = {
        {string_lit("pos"), script_mask_vec3},
        {string_lit("faction"), script_mask_str | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("time");
    const String       doc    = fmt_write_scratch("Lookup the current time.\n\n{}", fmt_text(g_clockDoc));
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("clock"), script_mask_str | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("set");
    const String       doc    = string_lit("Change or query if the target entity is contained in the given set.");
    const ScriptMask   ret    = script_mask_bool | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
        {string_lit("set"), script_mask_str},
        {string_lit("add"), script_mask_bool | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("query_set");
    const String       doc    = fmt_write_scratch("Find all entities in the given set.\n\n*Note*: Returns a query handle.\n\n{}", fmt_text(g_factionDoc));
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("set"), script_mask_str},
        {string_lit("faction"), script_mask_str | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("query_sphere");
    const String       doc    = fmt_write_scratch("Find all the entities that are touching the given sphere.\n\n*Note*: Returns a query handle.\n\n{}\n\n{}", fmt_text(g_queryOptionDoc), fmt_text(g_layerDoc));
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("pos"), script_mask_vec3},
        {string_lit("radius"), script_mask_num},
        {string_lit("option"), script_mask_str | script_mask_null},
        {string_lit("layers"), script_mask_str | script_mask_null, ScriptSigArgFlags_Multi},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("query_box");
    const String       doc    = fmt_write_scratch("Find all the entities that are touching the given box.\n\n*Note*: Returns a query handle.\n\n{}\n\n{}", fmt_text(g_queryOptionDoc), fmt_text(g_layerDoc));
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("pos"), script_mask_vec3},
        {string_lit("size"), script_mask_vec3},
        {string_lit("rot"), script_mask_quat | script_mask_null},
        {string_lit("option"), script_mask_str | script_mask_null},
        {string_lit("layers"), script_mask_str | script_mask_null, ScriptSigArgFlags_Multi},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("query_remaining");
    const String       doc    = string_lit("Returns the amount of entries remaining in the query.");
    const ScriptMask   ret    = script_mask_num;
    const ScriptSigArg args[] = {
        {string_lit("query"), script_mask_num},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("query_pop");
    const String       doc    = string_lit("Pops the first query value, returns null when reaching the end of the query.");
    const ScriptMask   ret    = script_mask_entity | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("query"), script_mask_num},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("query_random");
    const String       doc    = string_lit("Return a random remaining value in the given query, returns null when the current query is empty.");
    const ScriptMask   ret    = script_mask_entity | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("query"), script_mask_num},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("nav_find");
    const String       doc    = fmt_write_scratch("Find a navigation position.\n\n{}\n\n{}", fmt_text(g_navLayerDoc), fmt_text(g_navFindTypeDoc));
    const ScriptMask   ret    = script_mask_vec3 | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("pos"), script_mask_vec3},
        {string_lit("layer"), script_mask_str | script_mask_null},
        {string_lit("type"), script_mask_str | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("nav_target");
    const String       doc    = string_lit("Lookup the current navigation target of the given entity. Either a position or an entity.");
    const ScriptMask   ret    = script_mask_vec3 | script_mask_entity | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("line_of_sight");
    const String       doc    = string_lit("Test if there is a clear line of sight between the given entities.\nNote: Returns the distance to the target.");
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("src"), script_mask_entity},
        {string_lit("dst"), script_mask_entity},
        {string_lit("radius"), script_mask_num | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("capable");
    const String       doc    = fmt_write_scratch("Test if the given entity has a specific capability.\n\n{}", fmt_text(g_capabilityDoc));
    const ScriptMask   ret    = script_mask_bool | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
        {string_lit("capability"), script_mask_str},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("active");
    const String       doc    = fmt_write_scratch("Test if the given entity is performing an activity.\n\n{}", fmt_text(g_activityDoc));
    const ScriptMask   ret    = script_mask_bool | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
        {string_lit("activity"), script_mask_str},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("target_primary");
    const String       doc    = string_lit("Lookup the primary target of the given entity.");
    const ScriptMask   ret    = script_mask_entity | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("target_range_min");
    const String       doc    = string_lit("Lookup the minimum targeting range of the given entity.");
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("target_range_max");
    const String       doc    = string_lit("Lookup the maximum targeting range of the given entity.");
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("target_exclude");
    const String       doc    = fmt_write_scratch("Test if the given target exclude option is set.\n\n{}", fmt_text(g_targetExcludeDoc));
    const ScriptMask   ret    = script_mask_bool | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
        {string_lit("option"), script_mask_str},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("tell");
    const String       doc    = fmt_write_scratch("Set a property value for the given entity.\n\n*Note*: The updated property is visible to scripts in the next frame.\n\n{}", fmt_text(g_combinatorDoc));
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("target"), script_mask_entity},
        {string_lit("key"), script_mask_str},
        {string_lit("value"), script_mask_any},
        {string_lit("combinator"), script_mask_str | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("tell_global");
    const String       doc    = fmt_write_scratch("Set a global property value.\n\n*Note*: The updated property is visible to scripts in the next frame.\n\n{}", fmt_text(g_combinatorDoc));
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("key"), script_mask_str},
        {string_lit("value"), script_mask_any},
        {string_lit("combinator"), script_mask_str | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("ask");
    const String       doc    = fmt_write_scratch("Ask a source entity for a property value.\n\n*Note*: The result value is visible to the destination entity under the same key in the next frame.\n\n{}", fmt_text(g_combinatorDoc));
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("src"), script_mask_entity},
        {string_lit("key"), script_mask_str},
        {string_lit("dst"), script_mask_entity},
        {string_lit("combinator"), script_mask_str | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("ask_as");
    const String       doc    = fmt_write_scratch("Ask a source entity for a property value.\n\n*Note*: The result value is visible to the destination entity under the specified key in the next frame.\n\n{}", fmt_text(g_combinatorDoc));
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("src"), script_mask_entity},
        {string_lit("srcKey"), script_mask_str},
        {string_lit("dst"), script_mask_entity},
        {string_lit("dstKey"), script_mask_str},
        {string_lit("combinator"), script_mask_str | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("ask_global");
    const String       doc    = fmt_write_scratch("Ask a global property value.\n\n*Note*: The result value is visible to the destination entity under the same key in the next frame.\n\n{}", fmt_text(g_combinatorDoc));
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("key"), script_mask_str},
        {string_lit("dst"), script_mask_entity},
        {string_lit("combinator"), script_mask_str | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("get_global");
    const String       doc    = string_lit("Get a global property value.");
    const ScriptMask   ret    = script_mask_any;
    const ScriptSigArg args[] = {
        {string_lit("key"), script_mask_str},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("prefab_spawn");
    const String       doc    = string_lit("Spawn a prefab.\n\n*Note*: Resulting entity is not automatically destroyed.");
    const ScriptMask   ret    = script_mask_entity | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("prefabId"), script_mask_str},
        {string_lit("pos"), script_mask_vec3 | script_mask_null},
        {string_lit("rot"), script_mask_quat | script_mask_null},
        {string_lit("scale"), script_mask_num | script_mask_null},
        {string_lit("faction"), script_mask_str | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("prefab_id");
    const String       doc    = string_lit("Lookup the prefab identifier of the given instance, returns null if the entity is not a prefab instance.");
    const ScriptMask   ret    = script_mask_str | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("destroy");
    const String       doc    = string_lit("Destroy the given entity.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("destroy_after");
    const String       doc    = string_lit("Destroy the given entity after a delay.\n\n*Note*: When providing an entity it will wait until the entity no longer exists.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
        {string_lit("delay"), script_mask_entity | script_mask_time},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("teleport");
    const String       doc    = string_lit("Teleport the given entity.\n\nRequired capability: 'Teleport'");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
        {string_lit("pos"), script_mask_vec3 | script_mask_null},
        {string_lit("rot"), script_mask_quat | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("nav_travel");
    const String       doc    = string_lit("Instruct the given entity to travel to a target location or entity.\n\nRequired capability: 'NavTravel'");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
        {string_lit("target"), script_mask_entity | script_mask_vec3},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("nav_stop");
    const String       doc    = string_lit("Instruct the given entity to stop traveling.\n\nRequired capability: 'NavTravel'");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("attach");
    const String       doc    = string_lit("Attach the given entity to another entity (optionally at a specific joint).");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
        {string_lit("target"), script_mask_entity},
        {string_lit("jointName"), script_mask_str | script_mask_null},
        {string_lit("offset"), script_mask_vec3 | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("detach");
    const String       doc    = string_lit("Detach the given entity from all other entities.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("damage");
    const String       doc    = string_lit("Deal damage to the given entity.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
        {string_lit("amount"), script_mask_num},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("heal");
    const String       doc    = string_lit("Heal the given entity.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
        {string_lit("amount"), script_mask_num},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("status");
    const String       doc    = fmt_write_scratch("Change or query if an entity is affected by the specified status.\n\n{}", fmt_text(g_statusDoc));
    const ScriptMask   ret    = script_mask_bool | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
        {string_lit("type"), script_mask_str},
        {string_lit("enable"), script_mask_bool | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("attack");
    const String       doc    = string_lit("Instruct the given entity to attack another entity.\nNote: Changing targets can take some time if the entity is currently mid-attack.\n\nRequired capability: 'Attack'");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
        {string_lit("target"), script_mask_entity | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("attack_target");
    const String       doc    = string_lit("Query the current attack target of the given entity.");
    const ScriptMask   ret    = script_mask_entity | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("attack_weapon");
    const String       doc    = string_lit("Query the current weapon of the given entity.");
    const ScriptMask   ret    = script_mask_str | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("bark");
    const String       doc    = fmt_write_scratch("Request a bark to be played.\n\nRequired capability: 'Bark'\n\n{}", fmt_text(g_barkDoc));
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
        {string_lit("type"), script_mask_str},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("renderable_spawn");
    const String       doc    = string_lit("Spawn a renderable entity.\n\n*Note*: Resulting entity is not automatically destroyed.\n\n*Note*: It takes one frame before it can be used with the 'renderable_param()' api.");
    const ScriptMask   ret    = script_mask_bool | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("asset"), script_mask_entity},
        {string_lit("pos"), script_mask_vec3},
        {string_lit("rot"), script_mask_quat | script_mask_null},
        {string_lit("scale"), script_mask_num | script_mask_null},
        {string_lit("color"), script_mask_color | script_mask_null},
        {string_lit("emissive"), script_mask_color | script_mask_null},
        {string_lit("requireVisibility"), script_mask_bool | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("renderable_param");
    const String       doc    = fmt_write_scratch("Change or query a renderable parameter on the given entity.\n\nRequired capability: 'Renderable'\n\n{}", fmt_text(g_renderableParamDoc));
    const ScriptMask   ret    = script_mask_bool | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
        {string_lit("param"), script_mask_str},
        {string_lit("value"), script_mask_num | script_mask_color | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("vfx_system_spawn");
    const String       doc    = string_lit("Spawn a vfx system.\n\n*Note*: Resulting entity is not automatically destroyed.\n\n*Note*: It takes one frame before it can be used with the 'vfx_param()' api.");
    const ScriptMask   ret    = script_mask_entity;
    const ScriptSigArg args[] = {
        {string_lit("asset"), script_mask_entity},
        {string_lit("pos"), script_mask_vec3},
        {string_lit("rot"), script_mask_quat},
        {string_lit("alpha"), script_mask_num | script_mask_null},
        {string_lit("emitMultiplier"), script_mask_num | script_mask_null},
        {string_lit("requireVisibility"), script_mask_bool | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("vfx_decal_spawn");
    const String       doc    = string_lit("Spawn a decal.\n\n*Note*: Resulting entity is not automatically destroyed.\n\n*Note*: It takes one frame before it can be used with the 'vfx_param()' api.");
    const ScriptMask   ret    = script_mask_entity;
    const ScriptSigArg args[] = {
        {string_lit("asset"), script_mask_entity},
        {string_lit("pos"), script_mask_vec3},
        {string_lit("rot"), script_mask_quat},
        {string_lit("alpha"), script_mask_num | script_mask_null},
        {string_lit("requireVisibility"), script_mask_bool | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("vfx_param");
    const String       doc    = fmt_write_scratch("Change or query a vfx parameter on the given entity.\n\nRequired capability: 'Vfx'\n\n{}", fmt_text(g_vfxParamDoc));
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
        {string_lit("param"), script_mask_str},
        {string_lit("value"), script_mask_num | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("collision_box_spawn");
    const String       doc    = string_lit("Spawn a collision box.\n\n*Note*: Resulting entity is not automatically destroyed.");
    const ScriptMask   ret    = script_mask_entity;
    const ScriptSigArg args[] = {
        {string_lit("pos"), script_mask_vec3},
        {string_lit("size"), script_mask_vec3},
        {string_lit("rot"), script_mask_quat | script_mask_null},
        {string_lit("layer"), script_mask_str | script_mask_null},
        {string_lit("navBlocker"), script_mask_bool  | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("collision_sphere_spawn");
    const String       doc    = string_lit("Spawn a collision sphere.\n\n*Note*: Resulting entity is not automatically destroyed.");
    const ScriptMask   ret    = script_mask_entity;
    const ScriptSigArg args[] = {
        {string_lit("pos"), script_mask_vec3},
        {string_lit("radius"), script_mask_num},
        {string_lit("layer"), script_mask_str | script_mask_null},
        {string_lit("navBlocker"), script_mask_bool  | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("light_point_spawn");
    const String       doc    = string_lit("Spawn a point light.\n\n*Note*: Resulting entity is not automatically destroyed.\n\n*Note*: It takes one frame before it can be used with the 'light_param()' api.");
    const ScriptMask   ret    = script_mask_entity;
    const ScriptSigArg args[] = {
        {string_lit("pos"), script_mask_vec3},
        {string_lit("radiance"), script_mask_color},
        {string_lit("radius"), script_mask_num},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("light_spot_spawn");
    const String       doc    = string_lit("Spawn a spot light.\n\n*Note*: Resulting entity is not automatically destroyed.\n\n*Note*: It takes one frame before it can be used with the 'light_param()' api.");
    const ScriptMask   ret    = script_mask_entity;
    const ScriptSigArg args[] = {
        {string_lit("pos"), script_mask_vec3},
        {string_lit("rot"), script_mask_quat},
        {string_lit("radiance"), script_mask_color},
        {string_lit("angle"), script_mask_num},
        {string_lit("length"), script_mask_num},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("light_line_spawn");
    const String       doc    = string_lit("Spawn a line light.\n\n*Note*: Resulting entity is not automatically destroyed.\n\n*Note*: It takes one frame before it can be used with the 'light_param()' api.");
    const ScriptMask   ret    = script_mask_entity;
    const ScriptSigArg args[] = {
        {string_lit("pos"), script_mask_vec3},
        {string_lit("rot"), script_mask_quat},
        {string_lit("radiance"), script_mask_color},
        {string_lit("radius"), script_mask_num},
        {string_lit("length"), script_mask_num},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("light_param");
    const String       doc    = fmt_write_scratch("Change or query a light parameter on the given entity.\n\nRequired capability: 'Light'\n\n{}", fmt_text(g_lightParamDoc));
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
        {string_lit("param"), script_mask_str},
        {string_lit("value"), script_mask_color | script_mask_num | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("sound_spawn");
    const String       doc    = string_lit("Spawn a sound instance.\n\n*Note*: Resulting entity is not automatically destroyed.\n\n*Note*: It takes one frame before it can be used with the 'sound_param()' api.");
    const ScriptMask   ret    = script_mask_entity;
    const ScriptSigArg args[] = {
        {string_lit("asset"), script_mask_entity},
        {string_lit("pos"), script_mask_vec3 | script_mask_null},
        {string_lit("gain"), script_mask_num | script_mask_null},
        {string_lit("pitch"), script_mask_num | script_mask_null},
        {string_lit("looping"), script_mask_bool | script_mask_null},
        {string_lit("requireVisibility"), script_mask_bool | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("sound_param");
    const String       doc    = fmt_write_scratch("Change or query a sound parameter on the given entity.\n\nRequired capability: 'Sound'\n\n{}", fmt_text(g_soundParamDoc));
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
        {string_lit("param"), script_mask_str},
        {string_lit("value"), script_mask_num | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("anim_param");
    const String       doc    = fmt_write_scratch("Change or query an animation parameter on the given entity.\n\nRequired capability: 'Animation'\n\n{}", fmt_text(g_animParamDoc));
    const ScriptMask   ret    = script_mask_any;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
        {string_lit("layer"), script_mask_str},
        {string_lit("param"), script_mask_str},
        {string_lit("value"), script_mask_any},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("marker_spawn");
    const String       doc    = fmt_write_scratch("Spawn a new marker.\n\n*Note*: Resulting entity is not automatically destroyed.\n\n{}", fmt_text(g_markerTypeDoc));
    const ScriptMask   ret    = script_mask_entity;
    const ScriptSigArg args[] = {
        {string_lit("pos"), script_mask_vec3},
        {string_lit("type"), script_mask_str},
        {string_lit("radius"), script_mask_num | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("mission_state");
    const String       doc    = string_lit("Query the current mission state.\n\nStates:\n\n-`Idle`\n\n-`Active`\n\n-`Success`\n\n-`Fail`.");
    const ScriptMask   ret    = script_mask_str;
    bind(binder, name, doc, ret, null, 0);
  }
  {
    const String       name   = string_lit("mission_begin");
    const String       doc    = string_lit("Begin a new mission.\n\n*Note*: Only valid if there's no mission active.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("name"), script_mask_str},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("mission_end");
    const String       doc    = string_lit("End the current mission.\n\n*Note*: Only valid if there's a mission active.\n\nValid results:\n\n-`Success`\n\n-`Fail`.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("result"), script_mask_str},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("objective_begin");
    const String       doc    = string_lit("Begin a new objective.\n\n*Note*: Returns an objective id (only valid starting from the next frame).\n\n*Note*: Only valid if there's a mission active.");
    const ScriptMask   ret    = script_mask_num;
    const ScriptSigArg args[] = {
        {string_lit("name"), script_mask_str},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("objective_state");
    const String       doc    = string_lit("Query the state of the given objective.\n\n*Note*: Returns null if the objective is not valid (yet).\n\nStates:\n\n-`Active`\n\n-`Success`\n\n-`Fail`.");
    const ScriptMask   ret    = script_mask_str | script_mask_null;
    const ScriptSigArg args[] = {
      {string_lit("id"), script_mask_num},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("objective_time");
    const String       doc    = string_lit("Query how long the objective has been active.\n\n*Note*: Returns null if the objective is not valid (yet).");
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
      {string_lit("id"), script_mask_num},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("objective_goal");
    const String       doc    = string_lit("Update the objective goal.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
      {string_lit("id"), script_mask_num},
      {string_lit("progress"), script_mask_num},
      {string_lit("goal"), script_mask_num},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("objective_timeout");
    const String       doc    = string_lit("Update the objective timeout.\n\nValid results:\n\n-`Success`\n\n-`Fail`.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
      {string_lit("id"), script_mask_num},
      {string_lit("duration"), script_mask_time},
      {string_lit("result"), script_mask_str},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("objective_end");
    const String       doc    = string_lit("End the given objective.\n\nValid results:\n\n-`Success`\n\n-`Fail`.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
      {string_lit("id"), script_mask_num},
      {string_lit("result"), script_mask_str},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("joint_position");
    const String       doc    = string_lit("Lookup the world position of a joint on the given entity.\n\n*Note*: Animation update from this frame is not taken into account.");
    const ScriptMask   ret    = script_mask_vec3 | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_entity},
        {string_lit("joint"), script_mask_str},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("random_of");
    const String       doc    = string_lit("Return a random (non-null) value from the given arguments.");
    const ScriptMask   ret    = script_mask_any;
    const ScriptSigArg args[] = {
        {string_lit("values"), script_mask_any, ScriptSigArgFlags_Multi},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("debug_log");
    const String       doc    = string_lit("Log the given values.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("values"), script_mask_any, ScriptSigArgFlags_Multi},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("debug_line");
    const String       doc    = string_lit("Draw a 3D debug line between the two given points.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("start"), script_mask_vec3},
        {string_lit("end"), script_mask_vec3},
        {string_lit("color"), script_mask_color | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("debug_sphere");
    const String       doc    = string_lit("Draw a 3D debug sphere.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("pos"), script_mask_vec3},
        {string_lit("radius"), script_mask_num | script_mask_null},
        {string_lit("color"), script_mask_color | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("debug_box");
    const String       doc    = string_lit("Draw a 3D debug box.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("pos"), script_mask_vec3},
        {string_lit("size"), script_mask_vec3},
        {string_lit("rot"), script_mask_quat | script_mask_null},
        {string_lit("color"), script_mask_color | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("debug_arrow");
    const String       doc    = string_lit("Draw a 3D debug arrow.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("start"), script_mask_vec3},
        {string_lit("end"), script_mask_vec3},
        {string_lit("radius"), script_mask_num | script_mask_null},
        {string_lit("color"), script_mask_color | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("debug_orientation");
    const String       doc    = string_lit("Draw a 3D orientation gizmos.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("pos"), script_mask_vec3},
        {string_lit("rot"), script_mask_quat},
        {string_lit("size"), script_mask_num | script_mask_null},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("debug_text");
    const String       doc    = string_lit("Draw debug text at a position in 3D space.\n\n*Note*: Size is in UI canvas pixels.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("pos"), script_mask_vec3},
        {string_lit("color"), script_mask_color},
        {string_lit("size"), script_mask_num},
        {string_lit("values"), script_mask_any, ScriptSigArgFlags_Multi},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("debug_trace");
    const String       doc    = string_lit("Emit a debug-trace for this entity with the given values.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("values"), script_mask_any, ScriptSigArgFlags_Multi},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String     name = string_lit("debug_break");
    const String     doc  = string_lit("Break into the debugger if there is one attached.");
    const ScriptMask ret  = script_mask_null;
    bind(binder, name, doc, ret, null, 0);
  }
  {
    const String       name   = string_lit("debug_input_position");
    const String       doc    = fmt_write_scratch("Lookup the position at the debug input ray.\n\n{}\n\n{}", fmt_text(g_queryOptionDoc), fmt_text(g_layerDoc));
    const ScriptMask   ret    = script_mask_vec3 | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("option"), script_mask_str | script_mask_null},
        {string_lit("layers"), script_mask_str | script_mask_null, ScriptSigArgFlags_Multi},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("debug_input_rotation");
    const String       doc    = fmt_write_scratch("Lookup the rotation at the debug input ray.\n\n{}\n\n{}", fmt_text(g_queryOptionDoc), fmt_text(g_layerDoc));
    const ScriptMask   ret    = script_mask_quat | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("option"), script_mask_str | script_mask_null},
        {string_lit("layers"), script_mask_str | script_mask_null, ScriptSigArgFlags_Multi},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("debug_input_entity");
    const String       doc    = fmt_write_scratch("Lookup the entity at the debug input ray.\n\n{}\n\n{}", fmt_text(g_queryOptionDoc), fmt_text(g_layerDoc));
    const ScriptMask   ret    = script_mask_entity | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("option"), script_mask_str | script_mask_null},
        {string_lit("layers"), script_mask_str | script_mask_null, ScriptSigArgFlags_Multi},
    };
    bind(binder, name, doc, ret, args, array_elems(args));
  }
  // clang-format on

  script_binder_finalize(binder);
  g_assetScriptSceneBinder = binder;
}
