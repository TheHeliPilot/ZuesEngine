// Zues — Lync compiler plugin.
//
// Wires the `[attribute]` annotations a `mygame.lync` user writes to the
// boilerplate the Zues editor expects from a project DLL:
//
//   [component] Position: struct { x: float, y: float }
//     -> ZuesFieldInfo[] reflection table emitted at file scope
//     -> register_component(...) call emitted into auto-generated on_load
//
//   [on_load] / [on_update] / [on_unload]
//     -> wired into the ZuesProjectApi struct
//     -> user functions called from the plugin's wrapper functions
//
//   [system(phase, domain)] def my_system(eng, dt, user)
//     -> add_system_with_domain(...) call emitted into the on_load wrapper
//     -> a thunk function adapts Lync's mangled C name to ZuesSystemFn
//
// Build:
//   clang -shared zues_lync_plugin.c -I path/to/LyncLang/src \
//     -o zues_lync_plugin.dll
//
// Use:
//   lync mygame.lync --target=dll --plugin=zues_lync_plugin.dll \
//     --include=path/to/ZuesEngine/ProjectAPI/include -o mygame.dll

#include "../../LyncLang/src/lync_plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- Per-compilation accumulators -----------------------------------------
// All mutable state is process-local; each `lync` invocation runs the
// plugin in its own process so we don't share between compiles.

#define MAX_COMPONENTS 64
#define MAX_HOOKS      32
#define MAX_SYSTEMS    64

typedef struct {
    char  type_name[64];   // struct's name in Lync = its C typedef name
    char  category [128];  // optional [Category("path")] value; "" = default
    int   is_singleton;    // 1 if [Singleton] attribute was present
} ComponentEntry;

typedef struct {
    char  mangled_name[256];
} HookEntry;

typedef struct {
    char  mangled_name[256];
    char  user_name[64];     // for error messages + thunk symbol uniqueness
    char  phase[32];         // string from [system("Phase", ...)]; mapped at emit
    char  domain[16];        // "Editor" | "Game" | "Both"
} SystemEntry;

static ComponentEntry g_components[MAX_COMPONENTS];
static int            g_component_count;

static HookEntry g_on_load[MAX_HOOKS];
static int       g_on_load_count;
static HookEntry g_on_update[MAX_HOOKS];
static int       g_on_update_count;
static HookEntry g_on_unload[MAX_HOOKS];
static int       g_on_unload_count;

// Physics-event hooks. Each user-declared function tagged with one of the
// physics attributes lands in the matching table; the on_finalize pass
// emits a dispatch stub that calls every entry in source order, and the
// stub is wired into the ZuesProjectApi for the host's physics module to
// invoke from its post-step drain.
static HookEntry g_on_collision[MAX_HOOKS];
static int       g_on_collision_count;
static HookEntry g_on_trigger_enter[MAX_HOOKS];
static int       g_on_trigger_enter_count;
static HookEntry g_on_trigger_exit[MAX_HOOKS];
static int       g_on_trigger_exit_count;

static SystemEntry g_systems[MAX_SYSTEMS];
static int         g_system_count;

static int g_emitted_header = 0;     // emit project_api.h #include exactly once

// ---- Helpers --------------------------------------------------------------

// Map a C type name (as the Lync codegen emits it) to a ZuesFieldKind enum
// constant. Unknown types fall back to ZUES_FIELD_UNKNOWN — the inspector
// shows a "this needs a kind" message at runtime, which is a clearer
// failure than silently mapping to garbage.
static const char* zues_kind_for(const char* type_name) {
    if (!type_name) return "ZUES_FIELD_UNKNOWN";
    // Primitives.
    if (strcmp(type_name, "float")  == 0) return "ZUES_FIELD_F32";
    if (strcmp(type_name, "double") == 0) return "ZUES_FIELD_F64";
    if (strcmp(type_name, "int")    == 0) return "ZUES_FIELD_I32";
    if (strcmp(type_name, "bool")   == 0) return "ZUES_FIELD_BOOL";
    if (strcmp(type_name, "char")   == 0) return "ZUES_FIELD_I8";
    if (strcmp(type_name, "void*")  == 0) return "ZUES_FIELD_HANDLE";
    // Engine math types. Lync emits these as their typedef names because
    // the prelude declares them as plain structs (Vec2 / Vec3 / Color).
    // The typedef names round-trip through the C codegen unchanged, so
    // we match them here directly.
    if (strcmp(type_name, "Vec2")   == 0) return "ZUES_FIELD_VEC2";
    if (strcmp(type_name, "Vec3")   == 0) return "ZUES_FIELD_VEC3";
    if (strcmp(type_name, "Vec4")   == 0) return "ZUES_FIELD_VEC4";
    if (strcmp(type_name, "Color")  == 0) return "ZUES_FIELD_COLOR";
    // Reference types (entity / prefab / sprite / texture / audio / font).
    // Layout-equivalent on the wire: EntityRef = 8 bytes (idx+gen),
    // every asset *Ref = 16 bytes of guid. The Lync-side structs match
    // these byte counts exactly (see zues_api.lync) so offsetof/sizeof
    // come out identical across the project DLL and the engine.
    if (strcmp(type_name, "EntityRef")  == 0) return "ZUES_FIELD_ENTITY_REF";
    if (strcmp(type_name, "PrefabRef")  == 0) return "ZUES_FIELD_PREFAB_REF";
    if (strcmp(type_name, "SpriteRef")  == 0) return "ZUES_FIELD_SPRITE_REF";
    if (strcmp(type_name, "TextureRef") == 0) return "ZUES_FIELD_TEXTURE_REF";
    if (strcmp(type_name, "AudioRef")   == 0) return "ZUES_FIELD_AUDIO_REF";
    if (strcmp(type_name, "FontRef")    == 0) return "ZUES_FIELD_FONT_REF";
    // String fields (lync `string` -> C `char*`). Render as a text
    // input via FieldKind::CharBuffer once we sort the storage layout
    // (lync strings are heap-owned char*, not inline buffers - the
    // inspector would need an indirect path). For now leave unknown.
    return "ZUES_FIELD_UNKNOWN";
}

// "PreUpdate" -> "ZUES_PHASE_PRE_UPDATE" etc. Default to PRE_UPDATE if
// unrecognized (with a stderr warning) so a typo doesn't wedge the build.
static const char* zues_phase_for(const char* p) {
    if (!p) return "ZUES_PHASE_PRE_UPDATE";
    // Canonical names mirror Phase enum in zues/ecs/world.h.
    if (strcmp(p, "Input")        == 0) return "ZUES_PHASE_INPUT";
    if (strcmp(p, "PreUpdate")    == 0) return "ZUES_PHASE_PRE_UPDATE";
    if (strcmp(p, "Physics")      == 0) return "ZUES_PHASE_PHYSICS";
    if (strcmp(p, "PostUpdate")   == 0) return "ZUES_PHASE_POST_UPDATE";
    if (strcmp(p, "NetReplicate") == 0) return "ZUES_PHASE_NET_REPLICATE";
    if (strcmp(p, "UiInput")      == 0) return "ZUES_PHASE_UI_INPUT";
    if (strcmp(p, "UiLayout")     == 0) return "ZUES_PHASE_UI_LAYOUT";
    if (strcmp(p, "Render")       == 0) return "ZUES_PHASE_RENDER";
    if (strcmp(p, "UiRender")     == 0) return "ZUES_PHASE_UI_RENDER";
    // Friendly aliases. "Logic" + "Update" are common synonyms used in
    // tutorials / templates -> map to PreUpdate (the standard tick phase).
    if (strcmp(p, "Logic")        == 0) return "ZUES_PHASE_PRE_UPDATE";
    if (strcmp(p, "Update")       == 0) return "ZUES_PHASE_PRE_UPDATE";
    fprintf(stderr, "[zues] unknown phase '%s', defaulting to PreUpdate\n", p);
    return "ZUES_PHASE_PRE_UPDATE";
}

static const char* zues_domain_for(const char* d) {
    if (!d) return "ZUES_DOMAIN_BOTH";
    if (strcmp(d, "Editor") == 0) return "ZUES_DOMAIN_EDITOR";
    if (strcmp(d, "Game")   == 0) return "ZUES_DOMAIN_GAME";
    if (strcmp(d, "Both")   == 0) return "ZUES_DOMAIN_BOTH";
    fprintf(stderr, "[zues] unknown domain '%s', defaulting to Both\n", d);
    return "ZUES_DOMAIN_BOTH";
}

// Find an attribute by name; returns NULL if absent.
static LyncAttr* find_attr(const LyncApi* api, LyncDecl* d, const char* name) {
    const int n = api->decl_attr_count(d);
    for (int i = 0; i < n; ++i) {
        LyncAttr* a = api->decl_attr_at(d, i);
        if (strcmp(api->attr_name(a), name) == 0) return a;
    }
    return NULL;
}

// Lenient lookup: accept either spelling. Used so old-style lowercase
// attributes ([component], [on_load], [system]) keep compiling alongside
// the preferred PascalCase ([Component], [OnLoad], [System]).
static LyncAttr* find_attr_either(const LyncApi* api, LyncDecl* d,
                                   const char* a, const char* b) {
    LyncAttr* x = find_attr(api, d, a);
    return x ? x : find_attr(api, d, b);
}

// Lazy emit of #include <zues/project_api.h> + the static state pointers
// that all per-call wrappers reference. Done on first attribute we
// recognise so it lands at the start of the plugin's top section.
static void emit_header_once(LyncContext* ctx, const LyncApi* api) {
    if (g_emitted_header) return;
    g_emitted_header = 1;
    api->emit_top(ctx,
        "#include <stddef.h>\n"
        "#include <string.h>  /* memcpy — used by ref unpacking */\n"
        "#include <zues/project_api.h>\n"
        "\n"
        "/* Cached host pointer + engine handle. Populated by __zues_on_load\n"
        " * before any [component] register or user [on_load] runs. The\n"
        " * Lync-callable wrappers below dereference these. */\n"
        "static ZuesEngine*        __zues_engine = NULL;\n"
        "static const ZuesHostApi* __zues_host   = NULL;\n"
        "/* Most-recent dt fed to a system / on_update tick. Each<T> callbacks\n"
        " * read this so user code receives the live frame dt as its 3rd arg\n"
        " * without us having to thread it through QueryEach's user pointer. */\n"
        "static float              __zues_dt     = 0.0f;\n"
        "\n"
        "/* EntityRef is the Lync-side type. ZuesEntity is the C ABI type\n"
        " * the host API uses. They have identical layout (uint32 index +\n"
        " * uint32 generation), but C treats them as distinct types. These\n"
        " * helpers shuttle between them at the host boundary so the rest\n"
        " * of the wrapper code can speak in EntityRef alone. */\n"
        "static inline ZuesEntity __zues_to_z(EntityRef e) {\n"
        "    return (ZuesEntity){ .index = (uint32_t)e.index,\n"
        "                          .generation = (uint32_t)e.generation };\n"
        "}\n"
        "static inline EntityRef __zues_from_z(ZuesEntity z) {\n"
        "    return (EntityRef){ .index = (int)z.index,\n"
        "                         .generation = (int)z.generation };\n"
        "}\n"
        "\n"
        "/* ---- Lync-callable host wrappers ---------------------------------- *\n"
        " * Lync code can't dereference function pointers in a struct, so the\n"
        " * plugin emits these as plain C functions. The matching Lync extern\n"
        " * declarations live in zues_api.lync (auto-generated next to the\n"
        " * compiled DLL; pass --prelude=zues_api.lync to lync.exe).\n"
        " *\n"
        " * Entity refs cross the boundary as ZuesEntity (idx + generation).\n"
        " * Lync sees the same struct under the name `EntityRef` thanks to\n"
        " * the prelude's struct decl — layout-identical, so passing one\n"
        " * compiles to a plain by-value 8-byte struct copy on the C ABI. */\n"
        "EntityRef zues_create_entity(void) {\n"
        "    EntityRef z = {0, 0};\n"
        "    if (!__zues_host) return z;\n"
        "    return __zues_from_z(__zues_host->create_entity(__zues_engine));\n"
        "}\n"
        "void zues_set_transform(EntityRef e, float x, float y,\n"
        "                                float rot, float sx, float sy) {\n"
        "    if (!__zues_host) return;\n"
        "    __zues_host->set_transform(__zues_engine, __zues_to_z(e), x, y, rot, sx, sy);\n"
        "}\n"
        "void zues_set_transform_position(EntityRef e, float x, float y) {\n"
        "    if (!__zues_host) return;\n"
        "    __zues_host->set_transform_position(__zues_engine, __zues_to_z(e), x, y);\n"
        "}\n"
        "void zues_add_sprite_default(EntityRef e, float w, float h,\n"
        "                                     float r, float g, float b, float a) {\n"
        "    if (!__zues_host) return;\n"
        "    __zues_host->add_sprite_default(__zues_engine, __zues_to_z(e), w, h, r, g, b, a);\n"
        "}\n"
        "void zues_destroy_entity(EntityRef e) {\n"
        "    if (!__zues_host) return;\n"
        "    __zues_host->destroy_entity(__zues_engine, __zues_to_z(e));\n"
        "}\n"
        "bool zues_is_key_down(int key) {\n"
        "    if (!__zues_host) return false;\n"
        "    return __zues_host->is_key_down(__zues_engine, key) != 0;\n"
        "}\n"
        "bool zues_is_key_pressed(int key) {\n"
        "    if (!__zues_host) return false;\n"
        "    return __zues_host->is_key_pressed(__zues_engine, key) != 0;\n"
        "}\n"
        "void zues_log_info(char* msg) {\n"
        "    if (__zues_host && __zues_host->log)\n"
        "        __zues_host->log(__zues_engine, ZUES_LOG_INFO, msg);\n"
        "}\n"
        "void zues_log_warn(char* msg) {\n"
        "    if (__zues_host && __zues_host->log)\n"
        "        __zues_host->log(__zues_engine, ZUES_LOG_WARN, msg);\n"
        "}\n"
        "void zues_log_error(char* msg) {\n"
        "    if (__zues_host && __zues_host->log)\n"
        "        __zues_host->log(__zues_engine, ZUES_LOG_ERROR, msg);\n"
        "}\n"
        "void zues_log_debug(char* msg) {\n"
        "    if (__zues_host && __zues_host->log)\n"
        "        __zues_host->log(__zues_engine, ZUES_LOG_DEBUG, msg);\n"
        "}\n"
        "/* Typed log helpers, dispatched through Lync templates. The user\n"
        " * writes  Log<int>(42)  /  Log<float>(3.14)  /  Log<bool>(true)\n"
        " * which Lync mangles to Log__int / Log__float / Log__bool. The\n"
        " * plugin emits each mangled name as a concrete C extern below;\n"
        " * lync's template drain (with the \"concrete extern already\n"
        " * declared\" short-circuit) treats those exactly like a\n"
        " * monomorphised template body. Pure C-level overloading on the\n"
        " * back end, full template ergonomics on the front end.\n"
        " *\n"
        " * Same shape for Warn/Error/Debug. The string variant keeps the\n"
        " * plain LogInfo / LogWarn / LogError / LogDebug names so common\n"
        " * `LogInfo(\"booting up\")` calls don't need a `<string>` slot. */\n"
        "#define ZUES__LOG_VAL_FN(Name, Lvl, FmtSpec, CType) \\\n"
        "    void Name(CType v) { \\\n"
        "        if (!__zues_host || !__zues_host->log) return; \\\n"
        "        char buf[256]; \\\n"
        "        snprintf(buf, sizeof(buf), FmtSpec, v); \\\n"
        "        __zues_host->log(__zues_engine, Lvl, buf); \\\n"
        "    }\n"
        "ZUES__LOG_VAL_FN(Log__int,         ZUES_LOG_INFO,  \"%d\",  int)\n"
        "ZUES__LOG_VAL_FN(Log__float,       ZUES_LOG_INFO,  \"%g\",  float)\n"
        "ZUES__LOG_VAL_FN(Log__double,      ZUES_LOG_INFO,  \"%g\",  double)\n"
        "ZUES__LOG_VAL_FN(LogWarn__int,     ZUES_LOG_WARN,  \"%d\",  int)\n"
        "ZUES__LOG_VAL_FN(LogWarn__float,   ZUES_LOG_WARN,  \"%g\",  float)\n"
        "ZUES__LOG_VAL_FN(LogWarn__double,  ZUES_LOG_WARN,  \"%g\",  double)\n"
        "ZUES__LOG_VAL_FN(LogError__int,    ZUES_LOG_ERROR, \"%d\",  int)\n"
        "ZUES__LOG_VAL_FN(LogError__float,  ZUES_LOG_ERROR, \"%g\",  float)\n"
        "ZUES__LOG_VAL_FN(LogError__double, ZUES_LOG_ERROR, \"%g\",  double)\n"
        "ZUES__LOG_VAL_FN(LogDebug__int,    ZUES_LOG_DEBUG, \"%d\",  int)\n"
        "ZUES__LOG_VAL_FN(LogDebug__float,  ZUES_LOG_DEBUG, \"%g\",  float)\n"
        "ZUES__LOG_VAL_FN(LogDebug__double, ZUES_LOG_DEBUG, \"%g\",  double)\n"
        "/* Bool prints as the literal \"true\"/\"false\" — no format spec. */\n"
        "void Log__bool(bool v) {\n"
        "    if (!__zues_host || !__zues_host->log) return;\n"
        "    __zues_host->log(__zues_engine, ZUES_LOG_INFO,  v ? \"true\" : \"false\");\n"
        "}\n"
        "void LogWarn__bool(bool v) {\n"
        "    if (!__zues_host || !__zues_host->log) return;\n"
        "    __zues_host->log(__zues_engine, ZUES_LOG_WARN,  v ? \"true\" : \"false\");\n"
        "}\n"
        "void LogError__bool(bool v) {\n"
        "    if (!__zues_host || !__zues_host->log) return;\n"
        "    __zues_host->log(__zues_engine, ZUES_LOG_ERROR, v ? \"true\" : \"false\");\n"
        "}\n"
        "void LogDebug__bool(bool v) {\n"
        "    if (!__zues_host || !__zues_host->log) return;\n"
        "    __zues_host->log(__zues_engine, ZUES_LOG_DEBUG, v ? \"true\" : \"false\");\n"
        "}\n"
        "/* String pass-through: Log<string>(s) -> same as LogInfo(s). The\n"
        " * mangled name keeps the template path consistent across types. */\n"
        "void Log__string(char* msg) {\n"
        "    if (!__zues_host || !__zues_host->log) return;\n"
        "    __zues_host->log(__zues_engine, ZUES_LOG_INFO,  msg);\n"
        "}\n"
        "void LogWarn__string(char* msg) {\n"
        "    if (!__zues_host || !__zues_host->log) return;\n"
        "    __zues_host->log(__zues_engine, ZUES_LOG_WARN,  msg);\n"
        "}\n"
        "void LogError__string(char* msg) {\n"
        "    if (!__zues_host || !__zues_host->log) return;\n"
        "    __zues_host->log(__zues_engine, ZUES_LOG_ERROR, msg);\n"
        "}\n"
        "void LogDebug__string(char* msg) {\n"
        "    if (!__zues_host || !__zues_host->log) return;\n"
        "    __zues_host->log(__zues_engine, ZUES_LOG_DEBUG, msg);\n"
        "}\n"
        "\n"
        "/* ---- PascalCase aliases for the same wrappers --------------------- *\n"
        " * Same C symbols, cleaner Lync-side names. */\n"
        "EntityRef CreateEntity(void) { return zues_create_entity(); }\n"
        "void SetTransform(EntityRef e, float x, float y, float r, float sx, float sy) {\n"
        "    zues_set_transform(e, x, y, r, sx, sy);\n"
        "}\n"
        "void SetTransformPosition(EntityRef e, float x, float y) {\n"
        "    zues_set_transform_position(e, x, y);\n"
        "}\n"
        "void AddSpriteDefault(EntityRef e, float w, float h,\n"
        "                              float r, float g, float b, float a) {\n"
        "    zues_add_sprite_default(e, w, h, r, g, b, a);\n"
        "}\n"
        "void DestroyEntity(EntityRef e) { zues_destroy_entity(e); }\n"
        "bool IsKeyDown(int key)    { return zues_is_key_down(key); }\n"
        "bool IsKeyPressed(int key) { return zues_is_key_pressed(key); }\n"
        "void LogInfo(char* msg)    { zues_log_info(msg); }\n"
        "void LogWarn(char* msg)    { zues_log_warn(msg); }\n"
        "void LogError(char* msg)   { zues_log_error(msg); }\n"
        "void LogDebug(char* msg)   { zues_log_debug(msg); }\n"
        "\n"
        "/* ---- Physics wrappers --------------------------------------------- *\n"
        " * Forward to IPhysics_v1 looked up at call time. The host populates a\n"
        " * vtable pointer (`__zues_phys`) on first use; safe to call before any\n"
        " * RigidBody exists - the underlying ops are silent no-ops then. */\n"
        "struct IPhysics_v1 {\n"
        "    uint32_t abi_version;\n"
        "    void (*set_gravity)(struct IPhysics_v1*, float, float);\n"
        "    void (*get_gravity)(struct IPhysics_v1*, float*, float*);\n"
        "    void (*apply_impulse)(struct IPhysics_v1*, int, float, float);\n"
        "    void (*apply_force)  (struct IPhysics_v1*, int, float, float);\n"
        "    void (*set_velocity) (struct IPhysics_v1*, int, float, float);\n"
        "    void (*get_velocity) (struct IPhysics_v1*, int, float*, float*);\n"
        "    void (*set_position) (struct IPhysics_v1*, int, float, float, float);\n"
        "    void (*wake_body)    (struct IPhysics_v1*, int);\n"
        "    void (*pre_step) (struct IPhysics_v1*, void*, float);\n"
        "    void (*step)     (struct IPhysics_v1*, float);\n"
        "    void (*post_step)(struct IPhysics_v1*, void*, float);\n"
        "    int  (*raycast)(struct IPhysics_v1*, float, float, float, float,\n"
        "                    int*, float*, float*);\n"
        "    void (*set_collision_handlers)(struct IPhysics_v1*, void*,\n"
        "        void(*)(void*, int, int),\n"
        "        void(*)(void*, int, int),\n"
        "        void(*)(void*, int, int));\n"
        "};\n"
        "static struct IPhysics_v1* __zues_phys = NULL;\n"
        "static void __zues_phys_resolve(void) {\n"
        "    if (__zues_phys || !__zues_host || !__zues_host->get_service) return;\n"
        "    __zues_phys = (struct IPhysics_v1*)__zues_host->get_service(\n"
        "        __zues_engine, \"zues.physics\", 1);\n"
        "}\n"
        "/* Physics service uses raw int slot indices internally — extract\n"
        " * `e.index` at the boundary. The generation half travels with the\n"
        " * EntityRef but Box2D's contact event drain only needs the slot. */\n"
        "void ApplyImpulse(EntityRef e, float fx, float fy) {\n"
        "    __zues_phys_resolve();\n"
        "    if (__zues_phys && __zues_phys->apply_impulse)\n"
        "        __zues_phys->apply_impulse(__zues_phys, (int)e.index, fx, fy);\n"
        "}\n"
        "void ApplyForce(EntityRef e, float fx, float fy) {\n"
        "    __zues_phys_resolve();\n"
        "    if (__zues_phys && __zues_phys->apply_force)\n"
        "        __zues_phys->apply_force(__zues_phys, (int)e.index, fx, fy);\n"
        "}\n"
        "void SetVelocity(EntityRef e, float vx, float vy) {\n"
        "    __zues_phys_resolve();\n"
        "    if (__zues_phys && __zues_phys->set_velocity)\n"
        "        __zues_phys->set_velocity(__zues_phys, (int)e.index, vx, vy);\n"
        "}\n"
        "Vec2 GetVelocity(EntityRef e) {\n"
        "    Vec2 v = { 0.0f, 0.0f };\n"
        "    __zues_phys_resolve();\n"
        "    if (__zues_phys && __zues_phys->get_velocity)\n"
        "        __zues_phys->get_velocity(__zues_phys, (int)e.index, &v.x, &v.y);\n"
        "    return v;\n"
        "}\n"
        "void SetVelocityV(EntityRef e, Vec2 v) {\n"
        "    __zues_phys_resolve();\n"
        "    if (__zues_phys && __zues_phys->set_velocity)\n"
        "        __zues_phys->set_velocity(__zues_phys, (int)e.index, v.x, v.y);\n"
        "}\n"
        "void SetBodyPosition(EntityRef e, float x, float y, float rot) {\n"
        "    __zues_phys_resolve();\n"
        "    if (__zues_phys && __zues_phys->set_position)\n"
        "        __zues_phys->set_position(__zues_phys, (int)e.index, x, y, rot);\n"
        "}\n"
        "void WakeBody(EntityRef e) {\n"
        "    __zues_phys_resolve();\n"
        "    if (__zues_phys && __zues_phys->wake_body)\n"
        "        __zues_phys->wake_body(__zues_phys, (int)e.index);\n"
        "}\n"
        "\n"
        "/* ---- Prefab instantiation ----------------------------------------- *\n"
        " * Two flavours, same destination:\n"
        " *   - Instantiate(path, x, y)        -> string-keyed; pragmatic\n"
        " *                                       and survives any rename of\n"
        " *                                       the registry.\n"
        " *   - InstantiatePrefab(ref, x, y)   -> guid-keyed; what you get\n"
        " *                                       when a [Component] field is\n"
        " *                                       PrefabRef and the editor\n"
        " *                                       drag-drop populated it.\n"
        " * Both return the spawned root entity index, or 0 on failure. */\n"
        "EntityRef Instantiate(char* path, float x, float y) {\n"
        "    EntityRef z = {0, 0};\n"
        "    if (!__zues_host || !__zues_host->instantiate_prefab) return z;\n"
        "    return __zues_from_z(__zues_host->instantiate_prefab(\n"
        "        __zues_engine, path, x, y));\n"
        "}\n"
        "EntityRef InstantiatePrefab(PrefabRef p, float x, float y) {\n"
        "    EntityRef z = {0, 0};\n"
        "    if (!__zues_host || !__zues_host->instantiate_prefab_guid) return z;\n"
        "    /* The inspector wrote the Guid bytes via memcpy of\n"
        "     * Engine::Guid {u64 hi; u64 lo}. We reconstruct in the same\n"
        "     * order, so the four int fields' interior names are irrelevant\n"
        "     * — the byte sequence is what matters. */\n"
        "    uint64_t hi = 0, lo = 0;\n"
        "    memcpy(&hi, (char*)&p + 0, 8);\n"
        "    memcpy(&lo, (char*)&p + 8, 8);\n"
        "    return __zues_from_z(__zues_host->instantiate_prefab_guid(\n"
        "        __zues_engine, hi, lo, x, y));\n"
        "}\n"
        "\n"
        "/* ---- Timers --------------------------------------------------------- *\n"
        " * Lync sees only the user-facing form: pass a parameterless function\n"
        " * `def my_cb(): void` straight to SetTimeout / SetInterval. The host's\n"
        " * cb takes a `void* user` we don't use here -- the user's Lync\n"
        " * function reads world state through the cached __zues_engine /\n"
        " * __zues_host pointers, same as systems do. If a project later wants\n"
        " * per-cb context, we can layer a (cb, user) overload on top without\n"
        " * breaking this one. */\n"
        "typedef void (*__zues_timer_user_cb)(void);\n"
        "static void __zues_timer_thunk(void* user) {\n"
        "    __zues_timer_user_cb f = (__zues_timer_user_cb)user;\n"
        "    if (f) f();\n"
        "}\n"
        "int SetTimeout(float seconds, void* cb) {\n"
        "    if (!__zues_host || !__zues_host->set_timeout || !cb) return 0;\n"
        "    return (int)__zues_host->set_timeout(__zues_engine, seconds,\n"
        "                                          __zues_timer_thunk, cb);\n"
        "}\n"
        "int SetInterval(float seconds, void* cb) {\n"
        "    if (!__zues_host || !__zues_host->set_interval || !cb) return 0;\n"
        "    return (int)__zues_host->set_interval(__zues_engine, seconds,\n"
        "                                           __zues_timer_thunk, cb);\n"
        "}\n"
        "int CancelTimer(int handle) {\n"
        "    if (!__zues_host || !__zues_host->cancel_timer) return 0;\n"
        "    return __zues_host->cancel_timer(__zues_engine, (uint32_t)handle);\n"
        "}\n"
        "\n"
        "/* ---- Random --------------------------------------------------------- *\n"
        " * Engine-side mt19937, seeded at first use. Reseed via RandomSeed for\n"
        " * deterministic playthroughs. Range / int variants follow the same\n"
        " * inclusive-low/exclusive-or-inclusive-high conventions as std:\n"
        " *   RandomFloat()       -> [0, 1)\n"
        " *   RandomRange(lo, hi) -> [lo, hi)   (float)\n"
        " *   RandomInt(lo, hi)   -> [lo, hi]   (int, BOTH inclusive) */\n"
        "float RandomFloat(void) {\n"
        "    if (!__zues_host || !__zues_host->random_float) return 0.0f;\n"
        "    return __zues_host->random_float(__zues_engine);\n"
        "}\n"
        "float RandomRange(float lo, float hi) {\n"
        "    if (!__zues_host || !__zues_host->random_range) return lo;\n"
        "    return __zues_host->random_range(__zues_engine, lo, hi);\n"
        "}\n"
        "int RandomInt(int lo, int hi) {\n"
        "    if (!__zues_host || !__zues_host->random_int) return lo;\n"
        "    return __zues_host->random_int(__zues_engine, lo, hi);\n"
        "}\n"
        "void RandomSeed(int seed) {\n"
        "    if (!__zues_host || !__zues_host->random_seed) return;\n"
        "    __zues_host->random_seed(__zues_engine, (uint64_t)(uint32_t)seed);\n"
        "}\n"
        "\n"
        "/* ---- Hierarchy queries ---------------------------------------------- *\n"
        " * All return EntityRef by value. A returned EntityRef with\n"
        " * generation == 0 means \"no such relation\" -- walk that with\n"
        " * IsNull(e) instead of trying to match-unwrap, since EntityRef is\n"
        " * a value type with a built-in null sentinel. Use ChildCount /\n"
        " * GetChild(idx) for indexed access, or GetFirstChild +\n"
        " * GetNextSibling for linked-list iteration. */\n"
        "EntityRef GetParent(EntityRef e) {\n"
        "    EntityRef z = {0, 0};\n"
        "    if (!__zues_host || !__zues_host->get_parent) return z;\n"
        "    return __zues_from_z(__zues_host->get_parent(__zues_engine, __zues_to_z(e)));\n"
        "}\n"
        "EntityRef GetFirstChild(EntityRef e) {\n"
        "    EntityRef z = {0, 0};\n"
        "    if (!__zues_host || !__zues_host->get_first_child) return z;\n"
        "    return __zues_from_z(__zues_host->get_first_child(__zues_engine, __zues_to_z(e)));\n"
        "}\n"
        "EntityRef GetNextSibling(EntityRef e) {\n"
        "    EntityRef z = {0, 0};\n"
        "    if (!__zues_host || !__zues_host->get_next_sibling) return z;\n"
        "    return __zues_from_z(__zues_host->get_next_sibling(__zues_engine, __zues_to_z(e)));\n"
        "}\n"
        "int ChildCount(EntityRef e) {\n"
        "    if (!__zues_host || !__zues_host->get_child_count) return 0;\n"
        "    return (int)__zues_host->get_child_count(__zues_engine, __zues_to_z(e));\n"
        "}\n"
        "EntityRef GetChild(EntityRef e, int idx) {\n"
        "    EntityRef z = {0, 0};\n"
        "    if (!__zues_host || !__zues_host->get_child_at || idx < 0) return z;\n"
        "    return __zues_from_z(__zues_host->get_child_at(\n"
        "        __zues_engine, __zues_to_z(e), (uint32_t)idx));\n"
        "}\n"
        "/* generation == 0 IS the null sentinel for EntityRef. Centralised\n"
        " * here so user code never has to remember the convention. */\n"
        "bool IsNull(EntityRef e) { return e.generation == 0; }\n"
        "\n"
        "/* ---- HUD / Text helpers ----------------------------------------------\n"
        " * `Text` carries a fixed char[256] buffer that doesn't have a clean\n"
        " * Lync representation, so user code goes through these accessors\n"
        " * instead of touching the struct directly. AddTextDefault attaches\n"
        " * a Text component (the entity must already exist + have UIAnchor\n"
        " * for the HUD pass to render it); SetText overwrites the buffer\n"
        " * each frame as numeric labels tick. */\n"
        "void AddTextDefault(EntityRef e, char* utf8, float size_px,\n"
        "                     float r, float g, float b, float a) {\n"
        "    if (!__zues_host || !__zues_host->add_text_default) return;\n"
        "    __zues_host->add_text_default(__zues_engine, __zues_to_z(e),\n"
        "                                   utf8, size_px, r, g, b, a);\n"
        "}\n"
        "void SetText(EntityRef e, char* utf8) {\n"
        "    if (!__zues_host || !__zues_host->set_text) return;\n"
        "    __zues_host->set_text(__zues_engine, __zues_to_z(e), utf8);\n"
        "}\n"
        "void SetTextColor(EntityRef e, float r, float g, float b, float a) {\n"
        "    if (!__zues_host || !__zues_host->set_text_color) return;\n"
        "    __zues_host->set_text_color(__zues_engine, __zues_to_z(e),\n"
        "                                  r, g, b, a);\n"
        "}\n"
        "/* Typed SetText variants. User code calls `SetText<int>(label, score)`\n"
        " * which Lync mangles to `SetText__int`; we format into a small stack\n"
        " * buffer and forward to set_text. Float uses %g for compact output\n"
        " * (no trailing zeros); for fixed precision the user can write their\n"
        " * own helper or fall through to a future SetTextFmt(...). */\n"
        "void SetText__int(EntityRef e, int v) {\n"
        "    char buf[32]; snprintf(buf, sizeof(buf), \"%d\", v);\n"
        "    SetText(e, buf);\n"
        "}\n"
        "void SetText__float(EntityRef e, float v) {\n"
        "    char buf[32]; snprintf(buf, sizeof(buf), \"%g\", (double)v);\n"
        "    SetText(e, buf);\n"
        "}\n"
        "void SetText__double(EntityRef e, double v) {\n"
        "    char buf[32]; snprintf(buf, sizeof(buf), \"%g\", v);\n"
        "    SetText(e, buf);\n"
        "}\n"
        "void SetText__bool(EntityRef e, bool v) {\n"
        "    SetText(e, v ? \"true\" : \"false\");\n"
        "}\n"
        "\n"
        "/* ---- Animator playback (v15) -------------------------------- *\n"
        " * Drive an Animator component's currently-playing clip from\n"
        " * gameplay code. The clip TABLE itself is built in the editor\n"
        " * (Animator inspector); these forwarders just pick which entry\n"
        " * plays + pause/resume/seek. */\n"
        "int PlayByName(EntityRef e, char* name) {\n"
        "    if (!__zues_host || !__zues_host->animator_play_by_name) return 0;\n"
        "    return __zues_host->animator_play_by_name(__zues_engine,\n"
        "                                                __zues_to_z(e), name);\n"
        "}\n"
        "void SetPlaying(EntityRef e, bool playing) {\n"
        "    if (!__zues_host || !__zues_host->animator_set_playing) return;\n"
        "    __zues_host->animator_set_playing(__zues_engine,\n"
        "                                       __zues_to_z(e), playing ? 1 : 0);\n"
        "}\n"
        "void Seek(EntityRef e, float seconds) {\n"
        "    if (!__zues_host || !__zues_host->animator_seek) return;\n"
        "    __zues_host->animator_seek(__zues_engine,\n"
        "                                __zues_to_z(e), seconds);\n"
        "}\n"
        "\n"
        "/* ---- Particles control (v16) ---------------------------------*\n"
        " * Drive a Particles emitter from gameplay code -- the editor\n"
        " * sets the emitter up, gameplay decides when bursts happen and\n"
        " * whether the emitter is currently spawning. */\n"
        "void EmitBurst(EntityRef e, int count) {\n"
        "    if (!__zues_host || !__zues_host->particles_emit_burst) return;\n"
        "    __zues_host->particles_emit_burst(__zues_engine,\n"
        "                                       __zues_to_z(e), count);\n"
        "}\n"
        "void SetEmitting(EntityRef e, bool playing) {\n"
        "    if (!__zues_host || !__zues_host->particles_set_playing) return;\n"
        "    __zues_host->particles_set_playing(__zues_engine,\n"
        "                                        __zues_to_z(e),\n"
        "                                        playing ? 1 : 0);\n"
        "}\n"
        "void RestartEmitter(EntityRef e) {\n"
        "    if (!__zues_host || !__zues_host->particles_restart) return;\n"
        "    __zues_host->particles_restart(__zues_engine,\n"
        "                                    __zues_to_z(e));\n"
        "}\n"
        "\n"
        "/* ---- v17: Particle pool / extras ------------------------------ *\n"
        " * Per-particle iteration + named-float scratch slot access. The\n"
        " * eight names live in Particles.extra_names (one per row); these\n"
        " * thunks resolve a name -> slot via the inspector's stored list. */\n"
        "int ParticleCount(EntityRef e) {\n"
        "    if (!__zues_host || !__zues_host->particles_count) return 0;\n"
        "    return __zues_host->particles_count(__zues_engine, __zues_to_z(e));\n"
        "}\n"
        "/* Lync's `ptr` lowers to `void*` so these out-pointer params\n"
        " * take void* in the prototype and cast internally. */\n"
        "void ParticleGetPos(EntityRef e, int idx, void* out_x, void* out_y) {\n"
        "    if (!__zues_host || !__zues_host->particles_get_pos) return;\n"
        "    __zues_host->particles_get_pos(__zues_engine, __zues_to_z(e),\n"
        "                                    idx, (float*)out_x, (float*)out_y);\n"
        "}\n"
        "void ParticleSetPos(EntityRef e, int idx, float x, float y) {\n"
        "    if (!__zues_host || !__zues_host->particles_set_pos) return;\n"
        "    __zues_host->particles_set_pos(__zues_engine, __zues_to_z(e),\n"
        "                                    idx, x, y);\n"
        "}\n"
        "void ParticleGetVel(EntityRef e, int idx, void* out_vx, void* out_vy) {\n"
        "    if (!__zues_host || !__zues_host->particles_get_vel) return;\n"
        "    __zues_host->particles_get_vel(__zues_engine, __zues_to_z(e),\n"
        "                                    idx, (float*)out_vx, (float*)out_vy);\n"
        "}\n"
        "void ParticleSetVel(EntityRef e, int idx, float vx, float vy) {\n"
        "    if (!__zues_host || !__zues_host->particles_set_vel) return;\n"
        "    __zues_host->particles_set_vel(__zues_engine, __zues_to_z(e),\n"
        "                                    idx, vx, vy);\n"
        "}\n"
        "float ParticleGet(EntityRef e, int idx, char* field) {\n"
        "    if (!__zues_host || !__zues_host->particles_get_field) return 0.0f;\n"
        "    return __zues_host->particles_get_field(__zues_engine, __zues_to_z(e),\n"
        "                                             idx, field);\n"
        "}\n"
        "void ParticleSet(EntityRef e, int idx, char* field, float value) {\n"
        "    if (!__zues_host || !__zues_host->particles_set_field) return;\n"
        "    __zues_host->particles_set_field(__zues_engine, __zues_to_z(e),\n"
        "                                      idx, field, value);\n"
        "}\n"
        "void ParticleKill(EntityRef e, int idx) {\n"
        "    if (!__zues_host || !__zues_host->particles_kill) return;\n"
        "    __zues_host->particles_kill(__zues_engine, __zues_to_z(e), idx);\n"
        "}\n"
        "\n"
        "/* ---- v18: Spatial query / bulk movement / iterator -------- *\n"
        " * Nearest-neighbor uses a per-emitter uniform grid (cell size\n"
        " * = max_radius) rebuilt lazily. ParticleStepToward saves 3\n"
        " * host calls vs the get_pos/compute/set_pos pattern.\n"
        " * ParticleForEach drives the loop in C++ so per-call host\n"
        " * lookups inside the callback hit the cached pool ptr. */\n"
        "int ParticleNearestNeighbor(EntityRef e, float x, float y, float max_radius) {\n"
        "    if (!__zues_host || !__zues_host->particles_nearest_neighbor) return -1;\n"
        "    return __zues_host->particles_nearest_neighbor(__zues_engine,\n"
        "        __zues_to_z(e), x, y, max_radius);\n"
        "}\n"
        "int ParticleStepToward(EntityRef e, int idx, float tx, float ty,\n"
        "                        float max_speed, float dt) {\n"
        "    if (!__zues_host || !__zues_host->particles_step_toward) return 0;\n"
        "    return __zues_host->particles_step_toward(__zues_engine,\n"
        "        __zues_to_z(e), idx, tx, ty, max_speed, dt);\n"
        "}\n"
        "/* The user callback type is `void (EntityRef, int, float)`. We\n"
        " * stash it in a static and dispatch from a thunk that pulls\n"
        " * the engine's current __zues_dt for the dt arg. */\n"
        "typedef void (*__zues_pe_user_t)(EntityRef e, int idx, float dt);\n"
        "static __zues_pe_user_t __zues_pe_user = NULL;\n"
        "static void __zues_pe_thunk(ZuesEntity emitter, int idx,\n"
        "                              float dt_from_host, void* user) {\n"
        "    (void)dt_from_host; (void)user;\n"
        "    if (__zues_pe_user)\n"
        "        __zues_pe_user(__zues_from_z(emitter), idx, __zues_dt);\n"
        "}\n"
        "void ParticleForEach(EntityRef e, void* cb) {\n"
        "    if (!__zues_host || !__zues_host->particles_for_each || !cb) return;\n"
        "    /* Save/restore so nested ForEach doesn't clobber outer. */\n"
        "    __zues_pe_user_t prev = __zues_pe_user;\n"
        "    __zues_pe_user = (__zues_pe_user_t)cb;\n"
        "    __zues_host->particles_for_each(__zues_engine,\n"
        "        __zues_to_z(e), __zues_pe_thunk, NULL);\n"
        "    __zues_pe_user = prev;\n"
        "}\n"
        "\n"
        "/* Raw SoA pointer slices. Each returns a writable float* into\n"
        " * the pool's parallel arrays -- frame-local, invalidated by\n"
        " * pool resize/erase. Use to skip ParticleGet/Set inside hot\n"
        " * inner loops where per-call overhead dominates. */\n"
        "void* ParticleSlicePx(EntityRef e) {\n"
        "    if (!__zues_host || !__zues_host->particles_slice_px) return NULL;\n"
        "    return __zues_host->particles_slice_px(__zues_engine, __zues_to_z(e));\n"
        "}\n"
        "void* ParticleSlicePy(EntityRef e) {\n"
        "    if (!__zues_host || !__zues_host->particles_slice_py) return NULL;\n"
        "    return __zues_host->particles_slice_py(__zues_engine, __zues_to_z(e));\n"
        "}\n"
        "void* ParticleSliceVx(EntityRef e) {\n"
        "    if (!__zues_host || !__zues_host->particles_slice_vx) return NULL;\n"
        "    return __zues_host->particles_slice_vx(__zues_engine, __zues_to_z(e));\n"
        "}\n"
        "void* ParticleSliceVy(EntityRef e) {\n"
        "    if (!__zues_host || !__zues_host->particles_slice_vy) return NULL;\n"
        "    return __zues_host->particles_slice_vy(__zues_engine, __zues_to_z(e));\n"
        "}\n"
        "void* ParticleSliceAge(EntityRef e) {\n"
        "    if (!__zues_host || !__zues_host->particles_slice_age) return NULL;\n"
        "    return __zues_host->particles_slice_age(__zues_engine, __zues_to_z(e));\n"
        "}\n"
        "void* ParticleSliceField(EntityRef e, char* field) {\n"
        "    if (!__zues_host || !__zues_host->particles_slice_field) return NULL;\n"
        "    return __zues_host->particles_slice_field(__zues_engine,\n"
        "        __zues_to_z(e), field);\n"
        "}\n"
        "\n"
        "/* ---- v19: Audio playback (2D + 3D) -------------------------- *\n"
        " * Path-based one-shots route through miniaudio's resource manager\n"
        " * (paths dedupe by string, so repeated plays of the same clip\n"
        " * never re-decode). Source-bound playback toggles the\n"
        " * AudioSource.playing flag; the audio system picks it up next\n"
        " * tick and starts/stops the underlying voice with 3D positioning\n"
        " * pulled from the entity's Transform2D. */\n"
        "int AudioPlayOneShot(char* path, float volume, float pitch) {\n"
        "    if (!__zues_host || !__zues_host->audio_play_one_shot) return 0;\n"
        "    return (int)__zues_host->audio_play_one_shot(__zues_engine,\n"
        "        path, volume, pitch);\n"
        "}\n"
        "int AudioPlayOneShotAt(char* path, float x, float y,\n"
        "                         float min_distance, float max_distance,\n"
        "                         float volume) {\n"
        "    if (!__zues_host || !__zues_host->audio_play_one_shot_at) return 0;\n"
        "    return (int)__zues_host->audio_play_one_shot_at(__zues_engine,\n"
        "        path, x, y, min_distance, max_distance, volume);\n"
        "}\n"
        "void AudioStopVoice(int voice) {\n"
        "    if (!__zues_host || !__zues_host->audio_stop_voice) return;\n"
        "    __zues_host->audio_stop_voice(__zues_engine, (uint32_t)voice);\n"
        "}\n"
        "void AudioPlay(EntityRef e) {\n"
        "    if (!__zues_host || !__zues_host->audio_source_play) return;\n"
        "    __zues_host->audio_source_play(__zues_engine, __zues_to_z(e));\n"
        "}\n"
        "void AudioStop(EntityRef e) {\n"
        "    if (!__zues_host || !__zues_host->audio_source_stop) return;\n"
        "    __zues_host->audio_source_stop(__zues_engine, __zues_to_z(e));\n"
        "}\n"
        "void AudioPause(EntityRef e, bool paused) {\n"
        "    if (!__zues_host || !__zues_host->audio_source_pause) return;\n"
        "    __zues_host->audio_source_pause(__zues_engine, __zues_to_z(e),\n"
        "                                     paused ? 1 : 0);\n"
        "}\n"
        "bool AudioIsPlaying(EntityRef e) {\n"
        "    if (!__zues_host || !__zues_host->audio_source_is_playing) return 0;\n"
        "    return __zues_host->audio_source_is_playing(__zues_engine,\n"
        "        __zues_to_z(e)) ? 1 : 0;\n"
        "}\n"
        "void AudioSetMasterVolume(float v) {\n"
        "    if (!__zues_host || !__zues_host->audio_set_master_volume) return;\n"
        "    __zues_host->audio_set_master_volume(__zues_engine, v);\n"
        "}\n"
        "float AudioMasterVolume(void) {\n"
        "    if (!__zues_host || !__zues_host->audio_master_volume) return 0.0f;\n"
        "    return __zues_host->audio_master_volume(__zues_engine);\n"
        "}\n"
        "void AudioMute(bool muted) {\n"
        "    if (!__zues_host || !__zues_host->audio_set_muted) return;\n"
        "    __zues_host->audio_set_muted(__zues_engine, muted ? 1 : 0);\n"
        "}\n"
        "EntityRef SpawnAudio(AudioCueRef cue) {\n"
        "    EntityRef z = {0, 0};\n"
        "    if (!__zues_host || !__zues_host->audio_spawn) return z;\n"
        "    /* Same Guid byte order as InstantiatePrefab -- the four\n"
        "     * int fields hold a 16-byte guid. memcpy halves into hi/lo. */\n"
        "    uint64_t hi = 0, lo = 0;\n"
        "    memcpy(&hi, (char*)&cue + 0, 8);\n"
        "    memcpy(&lo, (char*)&cue + 8, 8);\n"
        "    return __zues_from_z(\n"
        "        __zues_host->audio_spawn(__zues_engine, hi, lo));\n"
        "}\n"
        "EntityRef SpawnAudio3D(AudioCueRef cue, float x, float y,\n"
        "                        float max_distance) {\n"
        "    EntityRef z = {0, 0};\n"
        "    if (!__zues_host || !__zues_host->audio_spawn_3d) return z;\n"
        "    uint64_t hi = 0, lo = 0;\n"
        "    memcpy(&hi, (char*)&cue + 0, 8);\n"
        "    memcpy(&lo, (char*)&cue + 8, 8);\n"
        "    return __zues_from_z(\n"
        "        __zues_host->audio_spawn_3d(__zues_engine, hi, lo,\n"
        "                                      x, y, max_distance));\n"
        "}\n"
        "\n"
        "/* ---- KeyCode singleton -------------------------------------------- *\n"
        " * GLFW key codes packed into a single struct so user lync code can\n"
        " * write `IsKeyDown(KeyCode().W)` instead of memorizing magic\n"
        " * numbers. Static const init lets the compiler hoist the initializer\n"
        " * outside the call - the per-call cost is effectively zero in\n"
        " * optimized builds. */\n"
        "KeyCodeT KeyCode(void) {\n"
        "    static const KeyCodeT k = {\n"
        "        .A=65,.B=66,.C=67,.D=68,.E=69,.F=70,.G=71,.H=72,\n"
        "        .I=73,.J=74,.K=75,.L=76,.M=77,.N=78,.O=79,.P=80,\n"
        "        .Q=81,.R=82,.S=83,.T=84,.U=85,.V=86,.W=87,.X=88,\n"
        "        .Y=89,.Z=90,\n"
        "        .Num0=48,.Num1=49,.Num2=50,.Num3=51,.Num4=52,\n"
        "        .Num5=53,.Num6=54,.Num7=55,.Num8=56,.Num9=57,\n"
        "        .Space=32,.Enter=257,.Escape=256,.Tab=258,.Backspace=259,\n"
        "        .Left=263,.Right=262,.Up=265,.Down=264,\n"
        "        .LShift=340,.RShift=344,.LCtrl=341,.RCtrl=345,\n"
        "        .LAlt=342,.RAlt=346,\n"
        "        .F1=290,.F2=291,.F3=292,.F4=293,.F5=294,.F6=295,\n"
        "        .F7=296,.F8=297,.F9=298,.F10=299,.F11=300,.F12=301,\n"
        "    };\n"
        "    return k;\n"
        "}\n"
        "\n"
        "/* ---- Engine built-in component wrappers --------------------------- *\n"
        " * The engine's renderer / physics / transform modules register these\n"
        " * components C++-side at module on_load. Their layouts are the source\n"
        " * of truth; we don't redeclare them in lync. These thin wrappers route\n"
        " * `Get<X> / Has<X> / Remove<X> / Each<X>` (UFCS) through the host's\n"
        " * find_component_id + get/has/remove, so user lync code can match on,\n"
        " * test for, or detach engine components without per-type C glue.       \n"
        " * The component_id lookup is cached per name on first call. */\n"
        "#define ZUES_ENGINE_COMP_WRAPPERS(Name) \\\n"
        "    static ZuesComponentId __zues_id_engine_##Name = 0; \\\n"
        "    static ZuesComponentId __zues_resolve_engine_##Name(void) { \\\n"
        "        if (__zues_id_engine_##Name) return __zues_id_engine_##Name; \\\n"
        "        if (!__zues_host || !__zues_host->find_component_id) return 0; \\\n"
        "        __zues_id_engine_##Name = __zues_host->find_component_id(__zues_engine, #Name); \\\n"
        "        return __zues_id_engine_##Name; \\\n"
        "    } \\\n"
        "    Name* Get__##Name(EntityRef e) { \\\n"
        "        ZuesComponentId cid = __zues_resolve_engine_##Name(); \\\n"
        "        if (!cid || !__zues_host) return NULL; \\\n"
        "        return (Name*)__zues_host->get_component(__zues_engine, __zues_to_z(e), cid); \\\n"
        "    } \\\n"
        "    bool Has__##Name(EntityRef e) { \\\n"
        "        ZuesComponentId cid = __zues_resolve_engine_##Name(); \\\n"
        "        if (!cid || !__zues_host) return 0; \\\n"
        "        return __zues_host->has_component(__zues_engine, __zues_to_z(e), cid); \\\n"
        "    } \\\n"
        "    void Remove__##Name(EntityRef e) { \\\n"
        "        ZuesComponentId cid = __zues_resolve_engine_##Name(); \\\n"
        "        if (!cid || !__zues_host) return; \\\n"
        "        __zues_host->remove_component(__zues_engine, __zues_to_z(e), cid); \\\n"
        "    } \\\n"
        "    void Each__##Name(void* cb) { (void)cb; /* TODO: query iteration */ }\n"
        "ZUES_ENGINE_COMP_WRAPPERS(Transform2D)\n"
        "ZUES_ENGINE_COMP_WRAPPERS(Sprite)\n"
        "ZUES_ENGINE_COMP_WRAPPERS(RigidBody)\n"
        "ZUES_ENGINE_COMP_WRAPPERS(BoxCollider)\n"
        "ZUES_ENGINE_COMP_WRAPPERS(CircleCollider)\n"
        "ZUES_ENGINE_COMP_WRAPPERS(AudioSource)\n"
        "ZUES_ENGINE_COMP_WRAPPERS(AudioListener)\n"
        "\n");
}

// ---- on_decl handler ------------------------------------------------------

// v2 pre-analyze hook: for each [component] struct, synthesize a Lync extern
// block declaring the matching `zues_add_<Name>` helper. The C-side definition
// is emitted later by on_decl_struct_component (post-analyze); this just lets
// the analyzer accept user calls to the helper without hand-written boilerplate.
//
// Lync's C-type names (float/int/double/char + struct names) round-trip through
// the Lync type names unchanged, so we can paste struct_field_type() directly.
// Map a C type spelling (as struct_field_type returns) to its lync
// equivalent. Lync's primitives use a slightly different spelling than C
// (`string` vs `char*`), so we have to translate before pasting into the
// synthesized extern decls. Anything not in the table passes through
// unchanged - covers `int`, `float`, `bool`, plus user-defined struct
// names which are spelled the same in both languages.
static const char* lync_type_for_c(const char* c_type) {
    if (!c_type) return "int";
    // C `char*` -> lync `string`. Without this mapping the synthesized
    // `def AddX(e: int, vec: char*): void;` fails to parse on the `*`.
    if (strcmp(c_type, "char*") == 0)        return "string";
    // `_Bool` is C99 spelling for bool. Some codepaths emit it.
    if (strcmp(c_type, "_Bool") == 0)        return "bool";
    // Engine math types: lync sees them via the prelude's struct decls.
    // The C side uses `Engine::math::vec2` etc; map back to lync names
    // when emitted via struct_field_type. Round-tripping is identity for
    // primitives that share a spelling (int / float / double / char).
    if (strcmp(c_type, "Engine::math::vec2") == 0)  return "Vec2";
    if (strcmp(c_type, "Engine::math::vec3") == 0)  return "Vec3";
    if (strcmp(c_type, "Engine::math::color") == 0) return "Color";
    return c_type;
}

static void on_decl_pre_analyze(LyncContext* ctx, const LyncApi* api, LyncDecl* d) {
    if (api->decl_kind(d) != LYNC_DECL_STRUCT) return;
    // Accept either [Component] (preferred) or [component] (legacy).
    if (!find_attr_either(api, d, "Component", "component")) return;

    const char* name = api->decl_name(d);
    const int fc = api->struct_field_count(d);
    char buf[4096];
    int off = 0;
    off += snprintf(buf + off, sizeof(buf) - off,
        "// auto-injected by zues plugin: helpers for [Component] %s\n", name);
    // `<zues/project_api.h>` (not stddef.h) so Lync emits real C
    // forward declarations — the plugin's emit_top output that defines
    // these symbols lands at the END of the .c file, so the call sites
    // need explicit forward decls or they fall back to C99 implicit-int.
    off += snprintf(buf + off, sizeof(buf) - off, "extern <zues/project_api.h> {\n");

    // [Singleton] components get a parameterless cached getter that returns
    // a typed pointer. Emitted here in the pre-analyze synth so the analyzer
    // can resolve `Singleton<Name>()` at call sites BEFORE post-analysis
    // codegen runs. The C body comes later (in on_decl_struct_component).
    if (find_attr_either(api, d, "Singleton", "singleton")) {
        // `ref T` (non-null borrow): the contract is "[Singleton] components
        // exist after on_load" — the plugin's __zues_on_load calls
        // ensure_singleton for every singleton component, and the cached
        // getter has its own ensure_singleton fallback. Returning `ref T`
        // saves the user a match-unwrap on a path that essentially never
        // fires; if the host pointer really is unset, dereferencing crashes
        // loudly rather than silently entering the dead `null:` arm.
        off += snprintf(buf + off, sizeof(buf) - off,
            "    def Singleton__%s(): ref %s;\n", name, name);
    }

    // Legacy: zues_add_<Name>(e, fields...).
    off += snprintf(buf + off, sizeof(buf) - off,
        "    def zues_add_%s(e: EntityRef", name);
    for (int i = 0; i < fc; ++i) {
        off += snprintf(buf + off, sizeof(buf) - off, ", %s: %s",
                        api->struct_field_name(d, i),
                        lync_type_for_c(api->struct_field_type(d, i)));
    }
    off += snprintf(buf + off, sizeof(buf) - off, "): void;\n");

    // PascalCase: Add<Name>(e, fields...).
    off += snprintf(buf + off, sizeof(buf) - off,
        "    def Add%s(e: EntityRef", name);
    for (int i = 0; i < fc; ++i) {
        off += snprintf(buf + off, sizeof(buf) - off, ", %s: %s",
                        api->struct_field_name(d, i),
                        lync_type_for_c(api->struct_field_type(d, i)));
    }
    off += snprintf(buf + off, sizeof(buf) - off, "): void;\n");

    // QueryEach<Name>(cb) — function pointer is opaque (`ptr`); the
    // callback signature it expects is `(EntityRef, T?) -> void`.
    off += snprintf(buf + off, sizeof(buf) - off,
        "    def QueryEach%s(cb: ptr): void;\n", name);

    // Verb-prefix family — Get returns a nullable borrowed pointer
    // (`ref? T`) so `match e.Get<T>() { some(p): p.field; ... }` propagates
    // the type AND the null arm is real (the entity may not have T).
    off += snprintf(buf + off, sizeof(buf) - off,
        "    def Each%s(cb: ptr): void;\n",                name);
    off += snprintf(buf + off, sizeof(buf) - off,
        "    def Has%s(e: EntityRef): bool;\n",            name);
    off += snprintf(buf + off, sizeof(buf) - off,
        "    def Get%s(e: EntityRef): ref? %s;\n",         name, name);
    off += snprintf(buf + off, sizeof(buf) - off,
        "    def Remove%s(e: EntityRef): void;\n",         name);

    // ---- Template-friendly forms ------------------------------------------
    // These use the lync mangled-name convention (`Op__Name`) so user code
    // can write `e.Add<Name>(...)`, `e.Get<Name>()`, etc. The template-call
    // parser mangles `Add<Name>` to `Add__Name` and the analyzer finds these
    // extern decls — no template body is needed because the C symbol exists
    // per-component on the host side.
    // Add__Name takes a struct by value, matching the documented
    // `Add<Name>(e, Name{ ... })` user pattern.
    off += snprintf(buf + off, sizeof(buf) - off,
        "    def Add__%s(e: EntityRef, value: %s): void;\n", name, name);
    off += snprintf(buf + off, sizeof(buf) - off,
        "    def Has__%s(e: EntityRef): bool;\n",           name);
    off += snprintf(buf + off, sizeof(buf) - off,
        "    def Get__%s(e: EntityRef): ref? %s;\n",        name, name);
    off += snprintf(buf + off, sizeof(buf) - off,
        "    def Each__%s(cb: ptr): void;\n",               name);
    off += snprintf(buf + off, sizeof(buf) - off,
        "    def Remove__%s(e: EntityRef): void;\n",        name);

    off += snprintf(buf + off, sizeof(buf) - off, "}\n");
    api->emit_lync_decls(ctx, buf);
}

static void on_decl_struct_component(LyncContext* ctx, const LyncApi* api,
                                      LyncDecl* d) {
    const char* name = api->decl_name(d);
    if (g_component_count >= MAX_COMPONENTS) {
        fprintf(stderr, "[zues] too many [component] structs (max %d)\n",
                MAX_COMPONENTS);
        return;
    }
    snprintf(g_components[g_component_count].type_name,
             sizeof(g_components[0].type_name), "%s", name);

    // Optional [Category("...")] attribute. Stored as-is; the editor's Add
    // Component picker treats it as a slash-separated path.
    g_components[g_component_count].category[0] = '\0';
    LyncAttr* cat_attr = find_attr(api, d, "Category");
    if (cat_attr && api->attr_arg_count(cat_attr) > 0
            && api->attr_arg_kind(cat_attr, 0) == LYNC_ATTR_STRING) {
        const char* path = api->attr_arg_string(cat_attr, 0);
        if (path) snprintf(g_components[g_component_count].category,
                           sizeof(g_components[0].category), "%s", path);
    }
    // [Singleton] presence drives two pieces of generated code: an
    // ensure_singleton call inside __zues_on_load (so the entity exists
    // before any system ticks), and a per-type cached getter `Name()` that
    // every consumer system uses. See emit_zues_on_load + emit_singleton_getter.
    g_components[g_component_count].is_singleton =
        find_attr_either(api, d, "Singleton", "singleton") ? 1 : 0;
    ++g_component_count;

    // Emit the per-component reflection array. References the struct
    // (defined earlier in the .c by Lync's codegen) via offsetof + sizeof.
    char buf[2048];
    int n = snprintf(buf, sizeof(buf),
        "static const ZuesFieldInfo __zues_fields_%s[] = {\n", name);
    api->emit_top(ctx, buf);

    const int fc = api->struct_field_count(d);
    for (int i = 0; i < fc; ++i) {
        const char* fname = api->struct_field_name(d, i);
        const char* ftype = api->struct_field_type(d, i);
        snprintf(buf, sizeof(buf),
            "    { .name = \"%s\", .kind = %s, "
            ".offset = (uint32_t)offsetof(%s, %s), "
            ".size = (uint32_t)sizeof(((%s*)0)->%s) },\n",
            fname, zues_kind_for(ftype),
            name, fname, name, fname);
        api->emit_top(ctx, buf);
    }
    api->emit_top(ctx, "};\n");

    snprintf(buf, sizeof(buf),
        "static ZuesComponentId __zues_id_%s = 0;\n", name);
    api->emit_top(ctx, buf);

    // Per-component helper: `zues_add_<Name>(entity, ...fields)`. Takes
    // a ZuesEntity by value (Lync sees it as EntityRef) plus the
    // component's fields in declaration order.
    snprintf(buf, sizeof(buf),
        "void zues_add_%s(EntityRef e", name);
    api->emit_top(ctx, buf);
    for (int i = 0; i < fc; ++i) {
        snprintf(buf, sizeof(buf), ", %s %s",
                 api->struct_field_type(d, i),
                 api->struct_field_name(d, i));
        api->emit_top(ctx, buf);
    }
    api->emit_top(ctx,
        ") {\n"
        "    if (!__zues_host) return;\n");
    snprintf(buf, sizeof(buf), "    %s data = { ", name);
    api->emit_top(ctx, buf);
    for (int i = 0; i < fc; ++i) {
        snprintf(buf, sizeof(buf), "%s%s",
                 i == 0 ? "" : ", ",
                 api->struct_field_name(d, i));
        api->emit_top(ctx, buf);
    }
    api->emit_top(ctx, " };\n");
    snprintf(buf, sizeof(buf),
        "    __zues_host->add_component(__zues_engine, __zues_to_z(e), __zues_id_%s, &data);\n"
        "}\n\n", name);
    api->emit_top(ctx, buf);

    // PascalCase alias `Add<Name>(entity, ...fields)` -> same impl, just a
    // thin wrapper. Both share the underlying add_component call.
    snprintf(buf, sizeof(buf), "void Add%s(EntityRef e", name);
    api->emit_top(ctx, buf);
    for (int i = 0; i < fc; ++i) {
        snprintf(buf, sizeof(buf), ", %s %s",
                 api->struct_field_type(d, i),
                 api->struct_field_name(d, i));
        api->emit_top(ctx, buf);
    }
    api->emit_top(ctx, ") {\n    zues_add_");
    api->emit_top(ctx, name);
    api->emit_top(ctx, "(e");
    for (int i = 0; i < fc; ++i) {
        snprintf(buf, sizeof(buf), ", %s",
                 api->struct_field_name(d, i));
        api->emit_top(ctx, buf);
    }
    api->emit_top(ctx, ");\n}\n\n");

    // QueryEach<Name>(callback): iterate every entity that has this
    // component and invoke the callback with (entity_idx, component*).
    // Implemented by emitting a thunk that wraps host->query_each's
    // (entity, component_ptr_array) callback into a per-component cb.
    // QueryEach signature wires through ZuesQueryFn: the host calls our
    // thunk for each match with column_ptrs[]; we extract column 0 (this
    // component) and forward to the user's per-component callback.
    // QueryEach takes the cb as `void*` (matches Lync's `ptr` type) and
    // casts internally. Calls host->query_each (engine, required, n_required,
    // excluded, n_excluded, fn, user) -- 7 args including the NULL excluded
    // arrays.
    // QueryEach<Name>(callback): callback signature is
    //   (ZuesEntity, T*) -> void.
    // The Lync-side declaration in the synthesized prelude types this as
    //   def cb(e: EntityRef, v: T?): void;
    // with T? unwrap-required at the user side. Inside the thunk the
    // pointer is never null (host->query_each only invokes for matching
    // archetypes), but Lync forces the user to spell the unwrap so the
    // type is propagated for further field access.
    snprintf(buf, sizeof(buf),
        "typedef void (*__zues_qe_cb_%s)(EntityRef e, %s* c, float dt);\n"
        "static __zues_qe_cb_%s __zues_qe_user_%s = NULL;\n"
        "static void __zues_qe_thunk_%s(ZuesEntity e,\n"
        "                                void** column_ptrs,\n"
        "                                uint32_t column_count,\n"
        "                                void* user) {\n"
        "    (void)user; (void)column_count;\n"
        "    if (__zues_qe_user_%s && column_ptrs && column_ptrs[0])\n"
        "        __zues_qe_user_%s(__zues_from_z(e), (%s*)column_ptrs[0], __zues_dt);\n"
        "}\n"
        "void QueryEach%s(void* cb) {\n"
        "    if (!__zues_host || !cb) return;\n"
        "    /* Save the previous user callback so a NESTED Each<T>\n"
        "     * call (e.g. AI's per-soldier nearest-enemy search inside\n"
        "     * an outer Each<Soldier>) doesn't clobber the outer's\n"
        "     * iteration. Without this the inner's NULL clear caused\n"
        "     * the outer thunk to skip every entity after the first. */\n"
        "    __zues_qe_cb_%s prev = __zues_qe_user_%s;\n"
        "    __zues_qe_user_%s = (__zues_qe_cb_%s)cb;\n"
        "    ZuesComponentId ids[1] = { __zues_id_%s };\n"
        "    __zues_host->query_each(__zues_engine, ids, 1, NULL, 0,\n"
        "                            __zues_qe_thunk_%s, NULL);\n"
        "    __zues_qe_user_%s = prev;\n"
        "}\n\n",
        name, name,         /* typedef */
        name, name,         /* static cb storage */
        name,               /* thunk decl */
        name, name, name,   /* thunk body */
        name,               /* QueryEach decl */
        name, name,         /* save prev */
        name, name,         /* assign user cb (with cast) */
        name,               /* ids array init */
        name,               /* call thunk */
        name);              /* restore prev */
    api->emit_top(ctx, buf);

    // Verb-prefix family — the readable / Unity-ish names. EntityRef
    // taken by-value (passed as ZuesEntity at the C ABI). Get returns
    // T* — the Lync-side decl types this as T? so users get a typed
    // nullable handle that round-trips through `match v { some(p): ... }`.
    snprintf(buf, sizeof(buf),
        "void Each%s(void* cb) { QueryEach%s(cb); }\n"
        "bool Has%s(EntityRef e) {\n"
        "    if (!__zues_host) return false;\n"
        "    return __zues_host->has_component(__zues_engine, __zues_to_z(e), __zues_id_%s) != 0;\n"
        "}\n"
        "%s* Get%s(EntityRef e) {\n"
        "    if (!__zues_host) return NULL;\n"
        "    return (%s*)__zues_host->get_component(__zues_engine, __zues_to_z(e), __zues_id_%s);\n"
        "}\n"
        "void Remove%s(EntityRef e) {\n"
        "    if (!__zues_host) return;\n"
        "    __zues_host->remove_component(__zues_engine, __zues_to_z(e), __zues_id_%s);\n"
        "}\n\n",
        name, name,             /* Each -> QueryEach */
        name, name,             /* Has  body */
        name, name, name, name, /* Get  body (typed return) */
        name, name);            /* Remove body */
    api->emit_top(ctx, buf);

    // Template-friendly aliases: same C bodies under the mangled names that
    // user code generates with `Add<Name>(...)`, `e.Get<Name>()`, etc.
    // Add__Name takes a Name struct by value and unpacks its fields into
    // the legacy positional Add%s call.
    int wpos = snprintf(buf, sizeof(buf),
        "void Add__%s(EntityRef e, %s value) {\n    Add%s(e",
        name, name, name);
    {
        const int fcc = api->struct_field_count(d);
        for (int i = 0; i < fcc; ++i) {
            wpos += snprintf(buf + wpos, sizeof(buf) - wpos, ", value.%s",
                              api->struct_field_name(d, i));
        }
        wpos += snprintf(buf + wpos, sizeof(buf) - wpos, ");\n}\n");
    }
    wpos += snprintf(buf + wpos, sizeof(buf) - wpos,
        "bool  Has__%s(EntityRef e)          { return Has%s(e); }\n"
        "%s*   Get__%s(EntityRef e)          { return Get%s(e); }\n"
        "void  Each__%s(void* cb)             { Each%s(cb); }\n"
        "void  Remove__%s(EntityRef e)       { Remove%s(e); }\n",
        name, name,
        name, name, name,
        name, name,
        name, name);
    api->emit_top(ctx, buf);

    // [Singleton] components get a Unity-style cached getter `Name() -> Name*`.
    // Steady-state cost is one u64 compare against the world's archetype
    // version + one cached pointer return. Refresh path is one
    // find_singleton call + one get_component call when archetypes have
    // moved (component add/remove anywhere) since last access.
    //
    // The cache is per-translation-unit static — fine for our single-DLL
    // project model. If/when multiple project DLLs land we'll need to make
    // this thread_local OR scope it to the host engine pointer.
    const int is_singleton = g_components[g_component_count - 1].is_singleton;
    if (is_singleton) {
        // C-symbol naming: the getter HAS to live in a different name
        // than the struct (ordinary identifiers + typedef share C's
        // identifier namespace -- `TimeManager* TimeManager(void)` is a
        // declaration conflict). We use the Lync template-mangled form
        // `Singleton__<Name>`, so user-side code reads:
        //     match Singleton<TimeManager>() { some(t): t.dt = dt; null: { } }
        // Lync's template machinery mangles `Singleton<T>` to
        // `Singleton__T` and finds this concrete extern via the
        // already-existing "concrete-extern-already-declared"
        // short-circuit in tpl_drain_pending. Same pattern Log<T>
        // already uses.
        snprintf(buf, sizeof(buf),
            "static %s*     __zues_singleton_%s   = NULL;\n"
            "static uint64_t __zues_singleton_%s_v = 0;\n"
            "%s* Singleton__%s(void) {\n"
            "    if (!__zues_host || !__zues_host->world_version) return NULL;\n"
            "    uint64_t v = __zues_host->world_version(__zues_engine);\n"
            "    if (v != __zues_singleton_%s_v || __zues_singleton_%s == NULL) {\n"
            "        ZuesEntity e = __zues_host->find_singleton(\n"
            "            __zues_engine, __zues_id_%s);\n"
            "        if (e.generation == 0) {\n"
            "            /* Plugin's on_load should have spawned it; emergency\n"
            "             * fallback covers a buggy host that calls into the\n"
            "             * project before on_load completes. */\n"
            "            e = __zues_host->ensure_singleton(\n"
            "                __zues_engine, __zues_id_%s);\n"
            "        }\n"
            "        __zues_singleton_%s = (%s*)__zues_host->get_component(\n"
            "            __zues_engine, e, __zues_id_%s);\n"
            "        __zues_singleton_%s_v = v;\n"
            "    }\n"
            "    return __zues_singleton_%s;\n"
            "}\n\n",
            name, name,           /* cache + version vars */
            name,
            name, name,           /* getter signature: T* Singleton__T(void) */
            name, name,           /* version compare + null check */
            name,                 /* find_singleton id */
            name,                 /* ensure_singleton id (fallback) */
            name, name, name,     /* get_component cast + id */
            name,                 /* version write */
            name);                /* return cached */
        api->emit_top(ctx, buf);

        // The Lync-side extern decl is emitted by on_decl_pre_analyze
        // (alongside the per-component Add/Get/Has/Each helpers) so the
        // analyzer can resolve Singleton<Name>() at call sites. Just the
        // C body lives here.
    }
}

// Caller has already confirmed the attribute is present. `hook_attr_name`
// is informational only (used in the overflow message); pass NULL when the
// caller already gated on find_attr_either.
static void on_decl_func_hook(LyncContext* ctx, const LyncApi* api,
                               LyncDecl* d, const char* hook_attr_name,
                               HookEntry* table, int* count) {
    (void)ctx;
    if (hook_attr_name && !find_attr(api, d, hook_attr_name)) return;
    if (*count >= MAX_HOOKS) {
        fprintf(stderr, "[zues] too many [%s] funcs (max %d)\n",
                hook_attr_name ? hook_attr_name : "hook", MAX_HOOKS);
        return;
    }
    api->func_mangled_name(d, table[*count].mangled_name,
                            sizeof(table[0].mangled_name));
    (*count)++;
}

static void on_decl_func_system(LyncContext* ctx, const LyncApi* api, LyncDecl* d) {
    LyncAttr* a = find_attr_either(api, d, "System", "system");
    if (!a) return;
    if (g_system_count >= MAX_SYSTEMS) {
        fprintf(stderr, "[zues] too many [system] funcs (max %d)\n", MAX_SYSTEMS);
        return;
    }

    SystemEntry* e = &g_systems[g_system_count];
    api->func_mangled_name(d, e->mangled_name, sizeof(e->mangled_name));
    snprintf(e->user_name, sizeof(e->user_name), "%s", api->decl_name(d));

    // Args: [system(phase, domain)] — both string literals.
    const int args = api->attr_arg_count(a);
    const char* phase  = (args > 0 && api->attr_arg_kind(a, 0) == LYNC_ATTR_STRING)
                            ? api->attr_arg_string(a, 0) : NULL;
    const char* domain = (args > 1 && api->attr_arg_kind(a, 1) == LYNC_ATTR_STRING)
                            ? api->attr_arg_string(a, 1) : NULL;
    snprintf(e->phase,  sizeof(e->phase),  "%s", phase  ? phase  : "PreUpdate");
    snprintf(e->domain, sizeof(e->domain), "%s", domain ? domain : "Both");

    // Emit the thunk now (forward decl). It calls the user's function with
    // the (eng, dt, user) signature ZuesSystemFn requires. We accept the
    // user wrote a matching signature in Lync; if not, the C compiler will
    // catch the call-site mismatch.
    char buf[512];
    snprintf(buf, sizeof(buf),
        "static void __zues_thunk_%s(ZuesEngine* eng, float dt, void* user) {\n"
        "    __zues_dt = dt;\n"
        "    (void)%s((void*)eng, dt, user);\n"
        "}\n\n",
        e->user_name, e->mangled_name);
    api->emit_top(ctx, buf);

    ++g_system_count;
}

static void on_decl(LyncContext* ctx, const LyncApi* api, LyncDecl* d) {
    if (api->decl_attr_count(d) == 0) return;
    emit_header_once(ctx, api);

    if (api->decl_kind(d) == LYNC_DECL_STRUCT) {
        if (find_attr_either(api, d, "Component", "component")) {
            on_decl_struct_component(ctx, api, d);
        }
    } else {  // LYNC_DECL_FUNC
        // Accept both PascalCase (preferred) and snake_case (legacy) forms.
        if (find_attr_either(api, d, "OnLoad", "on_load"))
            on_decl_func_hook(ctx, api, d, NULL, g_on_load, &g_on_load_count);
        if (find_attr_either(api, d, "OnUpdate", "on_update"))
            on_decl_func_hook(ctx, api, d, NULL, g_on_update, &g_on_update_count);
        if (find_attr_either(api, d, "OnUnload", "on_unload"))
            on_decl_func_hook(ctx, api, d, NULL, g_on_unload, &g_on_unload_count);
        if (find_attr_either(api, d, "OnCollision", "on_collision"))
            on_decl_func_hook(ctx, api, d, NULL, g_on_collision,
                              &g_on_collision_count);
        if (find_attr_either(api, d, "OnTriggerEnter", "on_trigger_enter"))
            on_decl_func_hook(ctx, api, d, NULL, g_on_trigger_enter,
                              &g_on_trigger_enter_count);
        if (find_attr_either(api, d, "OnTriggerExit", "on_trigger_exit"))
            on_decl_func_hook(ctx, api, d, NULL, g_on_trigger_exit,
                              &g_on_trigger_exit_count);
        on_decl_func_system(ctx, api, d);
    }
}

// ---- on_finalize: stitch everything into the project entry point ---------

static void emit_zues_on_load(LyncContext* ctx, const LyncApi* api) {
    api->emit_top(ctx,
        "static void __zues_on_load(ZuesEngine* eng, const ZuesHostApi* host) {\n"
        "    /* Stash so wrappers can call back later. */\n"
        "    __zues_engine = eng;\n"
        "    __zues_host   = host;\n"
        "    /* Immediate signal via engine log — survives stdout buffering */\n"
        "    if (host && host->log)\n"
        "        host->log(eng, ZUES_LOG_INFO, \"[lync] zues_project_entry on_load\");\n");

    // Register every [component] struct.
    char buf[1024];
    for (int i = 0; i < g_component_count; ++i) {
        const char* n = g_components[i].type_name;
        snprintf(buf, sizeof(buf),
            "    __zues_id_%s = host->register_component(eng, \"%s\",\n"
            "        (uint32_t)sizeof(%s), (uint32_t)_Alignof(%s),\n"
            "        __zues_fields_%s,\n"
            "        (uint32_t)(sizeof(__zues_fields_%s) / sizeof(__zues_fields_%s[0])),\n"
            "        NULL /* default_bytes - zero-init; Lync defaults TBD */);\n",
            n, n, n, n, n, n, n);
        api->emit_top(ctx, buf);
        // v8: set_component_category if [Category("...")] was present.
        // Always default to "Project" so user components don't land under
        // "Engine" alongside built-ins.
        const char* cat = g_components[i].category[0] ? g_components[i].category
                                                       : "Project";
        snprintf(buf, sizeof(buf),
            "    if (host->set_component_category)\n"
            "        host->set_component_category(eng, __zues_id_%s, \"%s\");\n",
            n, cat);
        api->emit_top(ctx, buf);
        // Auto-spawn the designated entity for [Singleton] components so
        // user systems can read the singleton on the very first tick — no
        // boot-order ceremony, no null checks. ensure_singleton is
        // idempotent (adopts an existing entity from a loaded world).
        if (g_components[i].is_singleton) {
            snprintf(buf, sizeof(buf),
                "    if (host->ensure_singleton)\n"
                "        (void)host->ensure_singleton(eng, __zues_id_%s);\n",
                n);
            api->emit_top(ctx, buf);
        }
    }

    // Register every [system(phase, domain)] function via its thunk.
    for (int i = 0; i < g_system_count; ++i) {
        SystemEntry* s = &g_systems[i];
        snprintf(buf, sizeof(buf),
            "    host->add_system_with_domain(eng, \"%s\",\n"
            "        %s, %s,\n"
            "        __zues_thunk_%s, NULL);\n",
            s->user_name, zues_phase_for(s->phase), zues_domain_for(s->domain),
            s->user_name);
        api->emit_top(ctx, buf);
    }

    // Then call each user [on_load] in source order. Explicit casts to
    // void* strip the const that the host API has on `host` — the user
    // declared their parameters as opaque `ptr` (= void* in C), and C
    // refuses const→non-const without an explicit cast.
    for (int i = 0; i < g_on_load_count; ++i) {
        snprintf(buf, sizeof(buf), "    (void)%s((void*)eng, (void*)host);\n",
                 g_on_load[i].mangled_name);
        api->emit_top(ctx, buf);
    }
    api->emit_top(ctx, "}\n\n");
}

static void emit_zues_on_update(LyncContext* ctx, const LyncApi* api) {
    api->emit_top(ctx,
        "static void __zues_on_update(ZuesEngine* eng, float dt) {\n"
        "    (void)eng;\n"
        "    __zues_dt = dt;\n");
    char buf[256];
    for (int i = 0; i < g_on_update_count; ++i) {
        snprintf(buf, sizeof(buf), "    (void)%s((void*)eng, dt);\n",
                 g_on_update[i].mangled_name);
        api->emit_top(ctx, buf);
    }
    if (g_on_update_count == 0) api->emit_top(ctx, "    (void)dt;\n");
    api->emit_top(ctx, "}\n\n");
}

static void emit_zues_on_unload(LyncContext* ctx, const LyncApi* api) {
    api->emit_top(ctx,
        "static void __zues_on_unload(ZuesEngine* eng) {\n"
        "    (void)eng;\n");
    char buf[256];
    for (int i = 0; i < g_on_unload_count; ++i) {
        snprintf(buf, sizeof(buf), "    (void)%s((void*)eng);\n",
                 g_on_unload[i].mangled_name);
        api->emit_top(ctx, buf);
    }
    api->emit_top(ctx, "}\n\n");
}

// Emit one fan-out dispatch fn for each physics-event hook table. Caller
// passes the C function name + the hook table + parameter list. Same
// shape across all three (entity-int args), so we factor.
static void emit_phys_dispatch(LyncContext* ctx, const LyncApi* api,
                                const char* fn_name,
                                const char* arg_decl_after_eng,
                                const char* arg_call_after_eng,
                                HookEntry* hooks, int count) {
    char buf[1024];
    snprintf(buf, sizeof(buf),
        "static void %s(ZuesEngine* eng, %s) {\n"
        "    (void)eng;\n",
        fn_name, arg_decl_after_eng);
    api->emit_top(ctx, buf);
    for (int i = 0; i < count; ++i) {
        snprintf(buf, sizeof(buf), "    (void)%s((void*)eng, %s);\n",
                 hooks[i].mangled_name, arg_call_after_eng);
        api->emit_top(ctx, buf);
    }
    api->emit_top(ctx, "}\n\n");
}

static void emit_zues_phys_dispatch(LyncContext* ctx, const LyncApi* api) {
    // Project API v20: callbacks now receive ZuesEntity (index +
    // generation) per side. The physics module fishes the live
    // generation out of the world's slot table at drain time, so the
    // EntityRef the user sees stays valid even after slots get
    // recycled across many spawn/destroy cycles. EntityRef and
    // ZuesEntity have identical layout but are distinct C types --
    // we shuttle through __zues_from_z to satisfy the type checker.
    emit_phys_dispatch(ctx, api, "__zues_on_collision",
        "ZuesEntity e_a, ZuesEntity e_b",
        "__zues_from_z(e_a), __zues_from_z(e_b)",
        g_on_collision, g_on_collision_count);
    emit_phys_dispatch(ctx, api, "__zues_on_trigger_enter",
        "ZuesEntity e_self, ZuesEntity e_other",
        "__zues_from_z(e_self), __zues_from_z(e_other)",
        g_on_trigger_enter, g_on_trigger_enter_count);
    emit_phys_dispatch(ctx, api, "__zues_on_trigger_exit",
        "ZuesEntity e_self, ZuesEntity e_other",
        "__zues_from_z(e_self), __zues_from_z(e_other)",
        g_on_trigger_exit, g_on_trigger_exit_count);
}

static void on_finalize(LyncContext* ctx, const LyncApi* api) {
    // Always emit the project entry — even when the user's source has no
    // Zues attributes yet. An empty project (or one with Game.lync
    // deleted) should still produce a loadable DLL with empty hook
    // stubs; otherwise the editor errors out with
    //   "project DLL missing zues_project_entry"
    // and the user is stuck whenever they want to clear out the
    // template starter content.
    //
    // emit_header_once is idempotent: it sets `g_emitted_header` on
    // first call. We force it here so the wrapper symbols + entry point
    // are present even without attributes.
    emit_header_once(ctx, api);

    emit_zues_on_load(ctx, api);
    emit_zues_on_update(ctx, api);
    emit_zues_on_unload(ctx, api);
    emit_zues_phys_dispatch(ctx, api);

    // The exported entry point.
    api->emit_top(ctx,
        "static const ZuesProjectApi __zues_api = {\n"
        "    .abi_version = ZUES_PROJECT_API_VERSION,\n"
        "    .on_load            = __zues_on_load,\n"
        "    .on_update          = __zues_on_update,\n"
        "    .on_unload          = __zues_on_unload,\n"
        "    .on_collision       = __zues_on_collision,\n"
        "    .on_trigger_enter   = __zues_on_trigger_enter,\n"
        "    .on_trigger_exit    = __zues_on_trigger_exit,\n"
        "};\n\n"
        "ZUES_PROJECT_EXPORT const ZuesProjectApi* zues_project_entry(void) {\n"
        "    return &__zues_api;\n"
        "}\n");

    fprintf(stderr,
        "[zues] generated entry point: %d component(s), %d on_load, %d on_update, "
        "%d on_unload, %d on_collision, %d on_trigger_enter, %d on_trigger_exit, "
        "%d system(s)\n",
        g_component_count, g_on_load_count, g_on_update_count,
        g_on_unload_count,
        g_on_collision_count, g_on_trigger_enter_count, g_on_trigger_exit_count,
        g_system_count);
}

// ---- Plugin descriptor ----------------------------------------------------

static const LyncPlugin g_plugin = {
    .abi_version         = LYNC_PLUGIN_ABI_VERSION,
    .name                = "zues",
    .version             = "0.1",
    .on_decl             = on_decl,
    .on_decl_pre_analyze = on_decl_pre_analyze,
    .on_finalize         = on_finalize,
};

LYNC_PLUGIN_EXPORT const LyncPlugin* lync_plugin_entry(void) {
    return &g_plugin;
}
