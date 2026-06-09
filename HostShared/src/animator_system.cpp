#include <zues/host/animator_system.h>

#include <zues/animation.h>
#include <zues/asset.h>
#include <zues/components/render.h>
#include <zues/log.h>
#include <zues/services/renderer_2d.h>
#include <zues/host/host_context.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>

// Default Animator system. Game-only -- ticks during Play, idle in
// Edit. For each entity with an Animator component, advances time,
// picks the active frame, and (when a Sprite is also present) writes
// the frame's texture + slice rect into the Sprite so the existing
// sprite render system draws the right pixels.
//
// Two caches keep this O(1) per entity per frame:
//   - g_anim_cache: AnimationRef.guid -> loaded AnimationAsset. Loaded
//     once on first use, kept around for the rest of the play session.
//   - g_tex_cache:  Texture guid     -> renderer texture handle. Loaded
//     lazily through IRenderer_2D_v1::load_texture_from_file.
//
// The system DOESN'T touch entities whose `playing` flag is false --
// the inspector toggle then controls auto-play with no extra plumbing.

namespace Engine::host {

namespace {

struct SystemCtx {
    ::IRenderer_2D_v1* renderer    = nullptr;
    ecs::ComponentId   animator_id = 0;
    ecs::ComponentId   sprite_id   = 0;
    // Asset-root (project_dir + assets_root_relative). Computed once
    // on register. Unused today but kept here so a future per-frame
    // path resolve doesn't have to re-derive it.
    std::string        project_dir;
    std::string        assets_root_relative;
};
SystemCtx g_ctx{};

struct GuidHashWrap {
    std::size_t operator()(const Engine::Guid& g) const noexcept {
        return Engine::GuidHash{}(g);
    }
};

// Resolved-asset cache. AnimationAssets are cheap to keep around (a
// header + a few kB of frames); we'd rather hold them than re-read
// the .zanim from disk each play session.
std::unordered_map<Engine::Guid, Engine::AnimationAsset, GuidHashWrap> g_anim_cache;
// Texture-handle cache (guid -> renderer slot id). Same idea.
std::unordered_map<Engine::Guid, Engine::u32, GuidHashWrap> g_tex_cache;
// Negative cache so a missing-on-disk reference doesn't spam the log.
std::unordered_map<Engine::Guid, bool, GuidHashWrap> g_anim_failed;

// Build "<project_dir>/<assets_root_rel>/<asset_path>" from an asset
// guid. Returns empty when the registry doesn't know the guid.
std::string asset_abs_path(const Engine::Guid& g) {
    auto& reg = Engine::AssetRegistry::instance();
    const char* path = reg.path_for(g);
    if (!path) return {};
    std::string out = g_ctx.project_dir;
    if (!out.empty() && out.back() != '/' && out.back() != '\\') out += '/';
    if (!g_ctx.assets_root_relative.empty()) {
        out += g_ctx.assets_root_relative;
        if (out.back() != '/' && out.back() != '\\') out += '/';
    }
    out += path;
    return out;
}

const Engine::AnimationAsset* lookup_anim(const Engine::Guid& g) {
    if (g.is_null()) return nullptr;
    auto hit = g_anim_cache.find(g);
    if (hit != g_anim_cache.end()) return &hit->second;
    if (g_anim_failed.count(g))    return nullptr;
    const std::string abs = asset_abs_path(g);
    if (abs.empty()) { g_anim_failed[g] = true; return nullptr; }
    Engine::AnimationAsset a{};
    if (Engine::load_animation(abs.c_str(), a) != Engine::Result::Ok) {
        g_anim_failed[g] = true;
        return nullptr;
    }
    auto [it, _] = g_anim_cache.emplace(g, std::move(a));
    return &it->second;
}

Engine::u32 lookup_texture(const Engine::Guid& g) {
    if (g.is_null()) return 0;
    auto hit = g_tex_cache.find(g);
    if (hit != g_tex_cache.end()) return hit->second;
    if (!g_ctx.renderer || !g_ctx.renderer->load_texture_from_file) return 0;
    const std::string abs = asset_abs_path(g);
    if (abs.empty()) { g_tex_cache[g] = 0; return 0; }
    const Engine::u32 tex =
        g_ctx.renderer->load_texture_from_file(g_ctx.renderer, abs.c_str());
    g_tex_cache[g] = tex;     // 0 cached as well -> negative cache
    // Apply the texture's authored filter / wrap from its .meta on
    // first load. Without this every animation frame ran through
    // GL_LINEAR by default, ignoring the user's "Nearest" pick on
    // the source texture -- pixel-art clips ended up blurry. The
    // settings are sticky on the GL texture object so we only need
    // to push them once per (guid, session).
    if (tex && g_ctx.renderer->set_texture_filter) {
        const auto sett = Engine::AssetRegistry::instance().sprite_settings_for(g);
        g_ctx.renderer->set_texture_filter(g_ctx.renderer, tex,
            sett.filter == Engine::SpriteFilter::Nearest ? 1 : 0);
        if (g_ctx.renderer->set_texture_wrap) {
            int w = 0;
            if (sett.wrap == Engine::SpriteWrap::Repeat) w = 1;
            else if (sett.wrap == Engine::SpriteWrap::Mirror) w = 2;
            g_ctx.renderer->set_texture_wrap(g_ctx.renderer, tex, w);
        }
    }
    return tex;
}

// Total length of the clip in seconds (sums per-frame durations,
// substituting 1/fps for any frame whose explicit duration is 0).
float clip_total_seconds(const Engine::AnimationAsset& a) {
    const float inv_fps = (a.fps > 0.0f) ? 1.0f / a.fps : 0.083f;
    float total = 0.0f;
    for (const auto& f : a.frames)
        total += (f.duration > 0.0f ? f.duration : inv_fps);
    return total;
}

// Pick the frame index that the clip is showing at accumulated time
// `t`. Wraps when looping; clamps to the last frame otherwise.
int frame_at_time(const Engine::AnimationAsset& a, float t) {
    if (a.frames.empty()) return -1;
    const float total = clip_total_seconds(a);
    if (total <= 0.0f) return 0;
    float u = t;
    if (a.loop) {
        u = std::fmod(t, total);
        if (u < 0.0f) u += total;
    } else if (u >= total) {
        return (int)a.frames.size() - 1;
    }
    const float inv_fps = (a.fps > 0.0f) ? 1.0f / a.fps : 0.083f;
    float acc = 0.0f;
    for (int i = 0; i < (int)a.frames.size(); ++i) {
        const float d = (a.frames[i].duration > 0.0f
                          ? a.frames[i].duration : inv_fps);
        if (u < acc + d) return i;
        acc += d;
    }
    return (int)a.frames.size() - 1;
}

void run_system(ecs::World& world, float dt, void* user) {
    auto* ctx = static_cast<SystemCtx*>(user);
    if (!ctx || !ctx->animator_id || !ctx->sprite_id) return;

    // Iterate Animator + Sprite (Sprite is a hard dep -- without it
    // there's nothing to drive). Sprite-less animators are silently
    // skipped; they can still tick `time` if needed via a future
    // per-component callback hook.
    const ecs::ComponentId required[] = { ctx->animator_id, ctx->sprite_id };

    struct Closure { SystemCtx* c; float dt; } closure{ctx, dt};

    world.iterate_query(required, 2, nullptr, 0,
        +[](void* u, ecs::Entity, void** cols, u32) {
            auto* cl = static_cast<Closure*>(u);
            auto* an = static_cast<components::Animator*>(cols[0]);
            auto* sp = static_cast<components::Sprite*>(cols[1]);
            if (!an->playing) return;

            const auto* clip = lookup_anim(an->animation.guid);
            if (!clip || clip->frames.empty()) return;

            // Advance time. Negative time_scale plays backwards.
            an->time += cl->dt * an->time_scale;

            const int idx = frame_at_time(*clip, an->time);
            if (idx < 0 || idx >= (int)clip->frames.size()) return;
            an->current = idx;

            const auto& fr = clip->frames[idx];

            // Mirror frame -> Sprite: texture handle + slice rect.
            // The slice index references the texture's own .meta slice
            // array; -1 (or out-of-range) means "use the whole texture".
            const Engine::u32 tex = lookup_texture(fr.texture);
            if (tex != 0) {
                sp->texture.index      = tex;
                sp->texture.generation = 1;
            }

            // Resolve the slice rect from the texture's .meta if a
            // slice was specified.
            if (fr.slice >= 0) {
                const auto sett = Engine::AssetRegistry::instance()
                    .sprite_settings_for(fr.texture);
                if (fr.slice < (int)sett.slices.size()) {
                    const auto& sl = sett.slices[fr.slice];
                    sp->slice_x = sl.x; sp->slice_y = sl.y;
                    sp->slice_w = sl.w; sp->slice_h = sl.h;
                } else {
                    sp->slice_x = 0; sp->slice_y = 0;
                    sp->slice_w = 0; sp->slice_h = 0;
                }
            } else {
                sp->slice_x = 0; sp->slice_y = 0;
                sp->slice_w = 0; sp->slice_h = 0;
            }
        }, &closure);
}

}  // namespace

bool AnimatorSystem::register_into(ecs::World& world,
                                    ::IRenderer_2D_v1* renderer) {
    g_ctx.renderer    = renderer;
    g_ctx.animator_id = world.find_component_id("Animator");
    g_ctx.sprite_id   = world.find_component_id("Sprite");

    // Best-effort resolve of the assets root so frame texture/path
    // joins line up with what the editor's Asset Browser uses. The
    // assets root is conventionally `<project_dir>/assets/` -- there's
    // no per-project override yet, so we hardcode it here exactly like
    // the editor does in EditorState.
    if (auto* hc = Engine::host::get_host_context()) {
        g_ctx.project_dir          = hc->project_dir;
        g_ctx.assets_root_relative = "assets";
    }

    if (!g_ctx.animator_id || !g_ctx.sprite_id) {
        ZUES_LOG_WARN("animator_system: builtins not registered yet - "
                      "system installed but will idle until they are");
    }

    // Game-only: animations should NOT advance while the user is
    // editing the scene. They tick the moment Play starts. PreUpdate
    // lets game systems read the resolved Sprite frame in the same
    // tick they make decisions on it.
    handle = world.add_system("Animator",
                              ecs::Phase::PreUpdate, run_system, &g_ctx,
                              ecs::SystemDomain::Game);
    ZUES_LOG_INFO("Animator system registered (Phase::PreUpdate Game)");
    return handle.is_valid();
}

void AnimatorSystem::unregister_from(ecs::World& world) {
    if (handle.is_valid()) {
        world.remove_system(handle);
        handle = {};
    }
    g_ctx = {};
    g_anim_cache.clear();
    g_tex_cache.clear();
    g_anim_failed.clear();
}

}  // namespace Engine::host
