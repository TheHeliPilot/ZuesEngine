// Audio system. miniaudio backend; one ma_engine for the process; ECS
// integration walks AudioSource + AudioListener entities each tick.
//
// Voice lifetime:
//   - Source-bound: created on first time we observe `playing == 1` for
//     an entity, destroyed when the source stops or the entity loses
//     its AudioSource. Kept across `playing` toggles to avoid reload.
//   - One-shots: created on play_one_shot, recycled when the underlying
//     ma_sound reports at_end. Stable u32 handles let callers stop a
//     specific voice mid-flight.
//
// Clip caching is delegated to miniaudio's resource manager: passing
// the same path string to ma_sound_init_from_file de-dupes the decoded
// data automatically. We just keep an id->path map so callers can use
// integer ids in the service vtable.

#define MA_NO_ENCODING               // we never write audio
#define MA_IMPLEMENTATION
#include "miniaudio.h"

#include <zues/host/audio_system.h>

#include <zues/asset.h>
#include <zues/audio_cue.h>
#include <zues/components/audio.h>
#include <zues/components/render.h>
#include <zues/components/transform.h>
#include <zues/ecs/world.h>
#include <zues/engine.h>
#include <zues/log.h>
#include <zues/service.h>
#include <zues/services/audio.h>
#include <zues/services/debug_draw.h>
#include <zues/host/host_context.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine::host {

namespace {

// Pack ecs::Entity into a u64 for hashmap keys without needing a
// custom hash for the unhashable POD.
inline Engine::u64 entity_key(ecs::Entity e) {
    return (static_cast<Engine::u64>(e.generation) << 32) | e.index;
}

struct ClipEntry {
    std::string path;     // absolute filesystem path (resolved via registry)
};

struct SourceVoice {
    ma_sound     sound{};
    bool         initialised = false;
    Engine::Guid bound_cue{};      // cue guid this voice was created for
    Engine::Guid bound_clip{};     // resolved clip guid (last picked entry)
    int          last_playing  = 0;
};

// Cached cue. Loaded lazily on first reference; reloaded only when the
// editor explicitly invalidates the cache (cue editor save).
struct GuidHashWrap {
    std::size_t operator()(const Engine::Guid& g) const noexcept {
        return Engine::GuidHash{}(g);
    }
};
using CueCache = std::unordered_map<Engine::Guid, Engine::AudioCue, GuidHashWrap>;

struct OneShot {
    std::unique_ptr<ma_sound> sound;
    Engine::u32               handle;
};

struct Backend {
    bool         engine_inited = false;
    bool         muted         = false;
    float        master        = 1.0f;
    ma_engine    engine;

    // Cached component ids (filled in init()).
    ecs::ComponentId xform_id    = 0;
    ecs::ComponentId source_id   = 0;
    ecs::ComponentId listener_id = 0;
    ecs::ComponentId camera_id   = 0;

    // Project dir + assets root for AudioRef -> absolute path.
    std::string project_dir;
    std::string assets_root_relative;

    // Clip id allocator (1..N; 0 reserved for "invalid").
    Engine::u32 next_clip_id = 1;
    std::unordered_map<Engine::u32, ClipEntry> clips;

    // Source-bound voices.
    std::unordered_map<Engine::u64, SourceVoice> sources;

    // One-shots, with monotonic handle ids (1..N).
    Engine::u32                                  next_voice = 1;
    std::vector<OneShot>                         oneshots;

    // Live listener position (world units). Updated every tick from
    // AudioListener / fallback Camera2D.
    float listener_x = 0.0f;
    float listener_y = 0.0f;

    // Editor selection — drives gizmo gating.
    ecs::Entity selected{};

    // Cached debug-draw service ptr (resolved lazily; null on runtime).
    ::IDebugDraw_v1* dbg = nullptr;

    // Cue cache. Cleared on shutdown; the editor's cue editor calls
    // `audio_api::invalidate_cue(guid)` after saving so the next play
    // re-reads the file.
    CueCache cues;

    // RNG for random pick mode + ± random. Seeded once at boot from
    // the system clock for "different every run" feel; cue authors
    // can rely on it being non-deterministic.
    std::mt19937 rng{ std::random_device{}() };
};

Backend g_b{};

// Build an absolute filesystem path for an AudioRef guid via the
// asset registry. Empty string when the guid is unknown.
std::string resolve_audio_path(Engine::Guid g) {
    if (g.is_null()) return {};
    const char* rel = Engine::AssetRegistry::instance().path_for(g);
    if (!rel) return {};
    std::string out = g_b.project_dir;
    if (!out.empty() && out.back() != '/' && out.back() != '\\') out += '/';
    if (!g_b.assets_root_relative.empty()) {
        out += g_b.assets_root_relative;
        if (out.back() != '/' && out.back() != '\\') out += '/';
    }
    out += rel;
    return out;
}

// Resolve a cue guid to a loaded AudioCue, fetching from disk on first
// use. Returns null when the guid isn't a known AudioCue or the file
// is missing / corrupt.
const Engine::AudioCue* resolve_cue(Engine::Guid g) {
    if (g.is_null()) return nullptr;
    auto it = g_b.cues.find(g);
    if (it != g_b.cues.end()) return &it->second;

    auto* entry = Engine::AssetRegistry::instance().find(g);
    if (!entry || entry->kind != Engine::AssetKind::AudioCue) return nullptr;
    std::string abs = g_b.project_dir;
    if (!abs.empty() && abs.back() != '/' && abs.back() != '\\') abs += '/';
    abs += g_b.assets_root_relative;
    if (!abs.empty() && abs.back() != '/' && abs.back() != '\\') abs += '/';
    abs += entry->path;

    Engine::AudioCue cue;
    Engine::Guid     cg{};
    if (!Engine::load_audio_cue(abs.c_str(), cue, &cg)) return nullptr;
    auto inserted = g_b.cues.emplace(g, std::move(cue));
    return &inserted.first->second;
}

// Pick one entry from a cue (random for now). Returns NULL_GUID on empty.
Engine::Guid pick_cue_clip(const Engine::AudioCue& cue) {
    if (cue.entries.empty()) return Engine::NULL_GUID;
    if (cue.entries.size() == 1) return cue.entries.front().clip;
    std::uniform_int_distribution<size_t> d(0, cue.entries.size() - 1);
    return cue.entries[d(g_b.rng)].clip;
}

// Symmetric ± random in [-r, +r]. Zero range -> always 0.
inline float roll_pm(float r) {
    if (r <= 0.0f) return 0.0f;
    std::uniform_real_distribution<float> d(-r, r);
    return d(g_b.rng);
}

// Apply 2D / 3D parameters from the AudioSource (combined with the
// resolved cue) to a ma_sound. Volume = src.volume * cue.volume + roll
// (clamped >= 0). Pitch = src.pitch * cue.pitch + roll (clamped > 0).
// Loop comes from the cue.
void apply_source_params(ma_sound& s,
                          const Engine::components::AudioSource& src,
                          const Engine::AudioCue& cue) {
    float vol = src.volume * cue.volume + roll_pm(cue.volume_random);
    if (vol < 0) vol = 0;
    float pit = src.pitch  * cue.pitch  + roll_pm(cue.pitch_random);
    if (pit <= 0) pit = 0.0001f;
    ma_sound_set_volume (&s, vol);
    ma_sound_set_pitch  (&s, pit);
    ma_sound_set_looping(&s, cue.loop ? MA_TRUE : MA_FALSE);
    if (src.is_3d) {
        ma_sound_set_spatialization_enabled(&s, MA_TRUE);
        ma_sound_set_attenuation_model(&s, ma_attenuation_model_inverse);
        ma_sound_set_min_distance(&s, src.min_distance > 0.0f ? src.min_distance : 0.01f);
        ma_sound_set_max_distance(&s, src.max_distance > src.min_distance
                                       ? src.max_distance : src.min_distance + 0.01f);
        const float blend = src.spatial_blend < 0 ? 0 : (src.spatial_blend > 1 ? 1 : src.spatial_blend);
        if (blend < 0.5f) ma_sound_set_spatialization_enabled(&s, MA_FALSE);
    } else {
        ma_sound_set_spatialization_enabled(&s, MA_FALSE);
        ma_sound_set_pan(&s, src.pan);
    }
}

// Resolve world-space position of an entity (handles parented
// hierarchies via World::world_transform_2d).
bool world_pos_of(ecs::World& world, ecs::Entity e, float& x, float& y) {
    const auto wt = world.world_transform_2d(e);
    x = wt.pos_x;
    y = wt.pos_y;
    return true;
}

// =========================================================================
// IAudio_v1 vtable functions.
// =========================================================================

float fn_master_volume(IAudio_v1*) { return g_b.master; }
void  fn_set_master_volume(IAudio_v1*, float v) {
    if (v < 0) v = 0;
    g_b.master = v;
    if (g_b.engine_inited)
        ma_engine_set_volume(&g_b.engine, g_b.muted ? 0.0f : v);
}
int   fn_is_muted(IAudio_v1*) { return g_b.muted ? 1 : 0; }
void  fn_set_muted(IAudio_v1*, int m) {
    g_b.muted = m != 0;
    if (g_b.engine_inited)
        ma_engine_set_volume(&g_b.engine, g_b.muted ? 0.0f : g_b.master);
}

Engine::u32 fn_load_clip(IAudio_v1*, const char* path) {
    if (!path || !*path) return 0;
    // Dedup by path.
    for (auto& [id, c] : g_b.clips) {
        if (c.path == path) return id;
    }
    Engine::u32 id = g_b.next_clip_id++;
    g_b.clips.emplace(id, ClipEntry{path});
    return id;
}
void fn_unload_clip(IAudio_v1*, Engine::u32 clip) { g_b.clips.erase(clip); }
void fn_unload_all_clips(IAudio_v1*)              { g_b.clips.clear(); }

ZuesVoiceHandle fn_play_one_shot(IAudio_v1* self, Engine::u32 clip,
                                  const ZuesAudioPlayParams* p) {
    if (!g_b.engine_inited) return 0;
    auto it = g_b.clips.find(clip);
    if (it == g_b.clips.end()) return 0;

    auto sound = std::make_unique<ma_sound>();
    if (ma_sound_init_from_file(&g_b.engine, it->second.path.c_str(),
                                 0, nullptr, nullptr, sound.get()) != MA_SUCCESS) {
        return 0;
    }

    ZuesAudioPlayParams defaults{1.0f, 1.0f, 0.0f, 0, 0, 0.0f, 0.0f, 1.0f, 20.0f};
    if (!p) p = &defaults;

    ma_sound_set_volume (sound.get(), p->volume);
    ma_sound_set_pitch  (sound.get(), p->pitch > 0 ? p->pitch : 1.0f);
    ma_sound_set_looping(sound.get(), p->loop ? MA_TRUE : MA_FALSE);
    if (p->is_3d) {
        ma_sound_set_spatialization_enabled(sound.get(), MA_TRUE);
        ma_sound_set_attenuation_model(sound.get(), ma_attenuation_model_inverse);
        ma_sound_set_position(sound.get(), p->x, p->y, 0.0f);
        ma_sound_set_min_distance(sound.get(),
                                   p->min_distance > 0 ? p->min_distance : 0.01f);
        ma_sound_set_max_distance(sound.get(),
                                   p->max_distance > p->min_distance
                                       ? p->max_distance : p->min_distance + 0.01f);
    } else {
        ma_sound_set_spatialization_enabled(sound.get(), MA_FALSE);
        ma_sound_set_pan(sound.get(), p->pan);
    }

    if (ma_sound_start(sound.get()) != MA_SUCCESS) {
        ma_sound_uninit(sound.get());
        return 0;
    }

    OneShot os;
    os.handle = g_b.next_voice++;
    os.sound  = std::move(sound);
    g_b.oneshots.push_back(std::move(os));
    return g_b.oneshots.back().handle;
}

void fn_stop_voice(IAudio_v1*, ZuesVoiceHandle voice) {
    for (auto it = g_b.oneshots.begin(); it != g_b.oneshots.end(); ++it) {
        if (it->handle == voice) {
            ma_sound_stop  (it->sound.get());
            ma_sound_uninit(it->sound.get());
            g_b.oneshots.erase(it);
            return;
        }
    }
}
void fn_set_voice_volume(IAudio_v1*, ZuesVoiceHandle v, float vol) {
    for (auto& os : g_b.oneshots) if (os.handle == v) ma_sound_set_volume(os.sound.get(), vol);
}
void fn_set_voice_pitch(IAudio_v1*, ZuesVoiceHandle v, float p) {
    for (auto& os : g_b.oneshots) if (os.handle == v) ma_sound_set_pitch(os.sound.get(), p > 0 ? p : 1.0f);
}
int  fn_is_voice_playing(IAudio_v1*, ZuesVoiceHandle v) {
    for (auto& os : g_b.oneshots) if (os.handle == v)
        return ma_sound_is_playing(os.sound.get()) ? 1 : 0;
    return 0;
}

void fn_set_listener(IAudio_v1*, float x, float y) {
    g_b.listener_x = x; g_b.listener_y = y;
    if (g_b.engine_inited)
        ma_engine_listener_set_position(&g_b.engine, 0, x, y, 0.0f);
}

// Source-bound playback. The ma_sound is created lazily on first play so
// changing the AudioRef on a stopped source doesn't trigger an immediate
// disk hit.
void source_voice_destroy(SourceVoice& v) {
    if (v.initialised) {
        ma_sound_stop  (&v.sound);
        ma_sound_uninit(&v.sound);
        v.initialised = false;
    }
}

// Resolve a source's cue guid to a clip path. Returns empty string when
// the cue is missing or has no entries. Picks a fresh entry every call
// (random pick mode) -- the caller owns the picked clip guid via the
// out param so we can stash it in `bound_clip` for cache invalidation.
std::string pick_source_clip_path(const Engine::components::AudioSource& src,
                                   Engine::Guid& out_clip_guid,
                                   const Engine::AudioCue*& out_cue) {
    out_clip_guid = Engine::NULL_GUID;
    out_cue       = nullptr;
    const Engine::AudioCue* cue = resolve_cue(src.cue.guid);
    if (!cue) return {};
    out_cue = cue;
    Engine::Guid clip = pick_cue_clip(*cue);
    if (clip.is_null()) return {};
    out_clip_guid = clip;
    return resolve_audio_path(clip);
}

bool source_voice_ensure(SourceVoice& v,
                          Engine::Guid cue_guid,
                          Engine::Guid clip_guid,
                          const std::string& abs_path) {
    // Re-init when either the cue or the picked clip changed -- random
    // mode legitimately re-picks each play.
    if (v.initialised && v.bound_cue == cue_guid && v.bound_clip == clip_guid)
        return true;
    if (v.initialised) source_voice_destroy(v);
    if (abs_path.empty()) return false;
    if (ma_sound_init_from_file(&g_b.engine, abs_path.c_str(),
                                 0, nullptr, nullptr, &v.sound) != MA_SUCCESS) {
        return false;
    }
    v.initialised = true;
    v.bound_cue   = cue_guid;
    v.bound_clip  = clip_guid;
    return true;
}

void fn_source_play(IAudio_v1*, ZuesAudioEntity ze) {
    auto* hc = Engine::host::get_host_context();
    if (!hc || !hc->world || !g_b.engine_inited) return;
    ecs::Entity e{ze.index, ze.generation};
    if (!hc->world->is_alive(e)) return;
    auto* src = static_cast<Engine::components::AudioSource*>(
        hc->world->get_component(e, g_b.source_id));
    if (!src) return;
    src->playing = 1;
    auto& v = g_b.sources[entity_key(e)];
    Engine::Guid picked_clip{};
    const Engine::AudioCue* cue = nullptr;
    const std::string abs = pick_source_clip_path(*src, picked_clip, cue);
    if (cue && source_voice_ensure(v, src->cue.guid, picked_clip, abs)) {
        apply_source_params(v.sound, *src, *cue);
        ma_sound_seek_to_pcm_frame(&v.sound, 0);
        ma_sound_start(&v.sound);
    }
    v.last_playing = 1;
}
void fn_source_stop(IAudio_v1*, ZuesAudioEntity ze) {
    ecs::Entity e{ze.index, ze.generation};
    auto it = g_b.sources.find(entity_key(e));
    if (it == g_b.sources.end()) return;
    source_voice_destroy(it->second);
    g_b.sources.erase(it);

    auto* hc = Engine::host::get_host_context();
    if (hc && hc->world && hc->world->is_alive(e)) {
        if (auto* src = static_cast<Engine::components::AudioSource*>(
                hc->world->get_component(e, g_b.source_id))) {
            src->playing = 0;
        }
    }
}
void fn_source_pause(IAudio_v1*, ZuesAudioEntity ze, int paused) {
    ecs::Entity e{ze.index, ze.generation};
    auto it = g_b.sources.find(entity_key(e));
    if (it == g_b.sources.end() || !it->second.initialised) return;
    if (paused) ma_sound_stop (&it->second.sound);
    else        ma_sound_start(&it->second.sound);
}
int fn_source_is_playing(IAudio_v1*, ZuesAudioEntity ze) {
    ecs::Entity e{ze.index, ze.generation};
    auto it = g_b.sources.find(entity_key(e));
    if (it == g_b.sources.end() || !it->second.initialised) return 0;
    return ma_sound_is_playing(&it->second.sound) ? 1 : 0;
}

void fn_tick_passthrough(IAudio_v1*, float /*dt*/) {
    // The system tick is owned by AudioSystem::tick (driven by the world's
    // System dispatch). The vtable entry is exposed so clients can manually
    // step audio in scenarios where the world isn't ticking (e.g. a test
    // harness). It does the same work as the system body.
}

int fn_voices_active(IAudio_v1*) {
    int n = (int)g_b.oneshots.size();
    for (auto& [k, v] : g_b.sources) if (v.initialised && ma_sound_is_playing(&v.sound)) ++n;
    return n;
}
int fn_clips_loaded(IAudio_v1*) { return (int)g_b.clips.size(); }

IAudio_v1 g_vtable{
    /* abi_version */         ZUES_SERVICE_AUDIO_VERSION,
    /* master_volume */       fn_master_volume,
    /* set_master_volume */   fn_set_master_volume,
    /* is_muted */            fn_is_muted,
    /* set_muted */           fn_set_muted,
    /* load_clip */           fn_load_clip,
    /* unload_clip */         fn_unload_clip,
    /* unload_all_clips */    fn_unload_all_clips,
    /* play_one_shot */       fn_play_one_shot,
    /* stop_voice */          fn_stop_voice,
    /* set_voice_volume */    fn_set_voice_volume,
    /* set_voice_pitch */     fn_set_voice_pitch,
    /* is_voice_playing */    fn_is_voice_playing,
    /* set_listener */        fn_set_listener,
    /* source_play */         fn_source_play,
    /* source_stop */         fn_source_stop,
    /* source_pause */        fn_source_pause,
    /* source_is_playing */   fn_source_is_playing,
    /* tick */                fn_tick_passthrough,
    /* voices_active */       fn_voices_active,
    /* clips_loaded */        fn_clips_loaded,
};

// =========================================================================
// Per-frame ECS sync.
// =========================================================================

void publish_gizmos_for_selected(ecs::World& world) {
    if (!g_b.dbg)        g_b.dbg = static_cast<::IDebugDraw_v1*>(
                                       Engine::services()->get_service(
                                           ZUES_SERVICE_DEBUG_DRAW,
                                           ZUES_SERVICE_DEBUG_DRAW_VERSION));
    if (!g_b.dbg) return;
    if (!g_b.dbg->is_enabled(g_b.dbg, ZUES_DBG_AUDIO)) return;

    // Selected entity range circles.
    const ecs::Entity sel = g_b.selected;
    if (!sel.is_null() && world.is_alive(sel) && g_b.source_id) {
        auto* src = static_cast<Engine::components::AudioSource*>(
            world.get_component(sel, g_b.source_id));
        if (src) {
            float x = 0, y = 0; world_pos_of(world, sel, x, y);
            const float a_inner = 0.85f;
            const float a_outer = 0.45f;
            if (src->is_3d) {
                g_b.dbg->circle(g_b.dbg, ZUES_DBG_AUDIO,
                                x, y, src->min_distance,
                                0.20f, 0.85f, 0.55f, a_inner);
                g_b.dbg->circle(g_b.dbg, ZUES_DBG_AUDIO,
                                x, y, src->max_distance,
                                0.20f, 0.55f, 0.85f, a_outer);
            } else {
                // 2D source: a small icon-circle so it's still findable.
                g_b.dbg->circle(g_b.dbg, ZUES_DBG_AUDIO,
                                x, y, 0.25f,
                                0.85f, 0.65f, 0.20f, 0.7f);
            }
        }
    }

    // Listener position arrow.
    g_b.dbg->circle(g_b.dbg, ZUES_DBG_AUDIO,
                    g_b.listener_x, g_b.listener_y, 0.35f,
                    0.95f, 0.95f, 0.30f, 0.8f);
}

void run_system(ecs::World& world, float /*dt*/, void* /*user*/) {
    if (!g_b.engine_inited) return;
    if (!g_b.source_id || !g_b.xform_id) return;

    // 1. Listener: prefer first active AudioListener; fallback to Camera2D.
    struct ListenerCtx {
        ecs::World* world;
        bool        set;
    } lctx{ &world, false };
    if (g_b.listener_id) {
        const ecs::ComponentId req[] = { g_b.listener_id };
        world.iterate_query(req, 1, nullptr, 0,
            +[](void* user, ecs::Entity e, void** cols, Engine::u32) {
                auto* c = static_cast<ListenerCtx*>(user);
                if (c->set) return;
                auto* l = static_cast<Engine::components::AudioListener*>(cols[0]);
                if (!l || !l->is_active) return;
                float x = 0, y = 0; world_pos_of(*c->world, e, x, y);
                g_b.listener_x = x; g_b.listener_y = y;
                ma_engine_listener_set_position(&g_b.engine, 0, x, y, 0.0f);
                c->set = true;
            }, &lctx);
    }
    if (!lctx.set && g_b.camera_id) {
        const ecs::ComponentId req[] = { g_b.camera_id };
        world.iterate_query(req, 1, nullptr, 0,
            +[](void* user, ecs::Entity e, void** cols, Engine::u32) {
                auto* c = static_cast<ListenerCtx*>(user);
                if (c->set) return;
                auto* cam = static_cast<Engine::components::Camera2D*>(cols[0]);
                if (!cam || !cam->is_active) return;
                float x = 0, y = 0; world_pos_of(*c->world, e, x, y);
                g_b.listener_x = x; g_b.listener_y = y;
                ma_engine_listener_set_position(&g_b.engine, 0, x, y, 0.0f);
                c->set = true;
            }, &lctx);
    }

    // 2. Sources. Walk every entity with AudioSource. If `playing` toggled
    //    on, ensure a voice + start it. If `playing` toggled off, stop it.
    //    For active voices, push the latest position + parameters.
    struct SourceCtx {
        ecs::World* world;
        std::vector<ecs::Entity> to_destroy;
    } sctx{ &world, {} };
    {
        const ecs::ComponentId req[] = { g_b.source_id };
        world.iterate_query(req, 1, nullptr, 0,
            +[](void* user, ecs::Entity e, void** cols, Engine::u32) {
                auto* c         = static_cast<SourceCtx*>(user);
                auto* world_ptr = c->world;
                auto* src = static_cast<Engine::components::AudioSource*>(cols[0]);
                if (!src) return;

                auto& v = g_b.sources[entity_key(e)];
                if (!v.initialised && src->autoplay && !src->playing) {
                    src->playing = 1;
                }

                const bool want_play = src->playing != 0;

                if (want_play) {
                    Engine::Guid picked_clip{};
                    const Engine::AudioCue* cue = nullptr;
                    const std::string abs =
                        pick_source_clip_path(*src, picked_clip, cue);
                    if (!cue || !source_voice_ensure(v, src->cue.guid,
                                                       picked_clip, abs)) {
                        src->playing = 0;
                        return;
                    }
                    apply_source_params(v.sound, *src, *cue);
                    if (src->is_3d) {
                        float x = 0, y = 0; world_pos_of(*world_ptr, e, x, y);
                        ma_sound_set_position(&v.sound, x, y, 0.0f);
                    }
                    if (!ma_sound_is_playing(&v.sound)) {
                        if (!cue->loop && ma_sound_at_end(&v.sound)) {
                            ma_sound_seek_to_pcm_frame(&v.sound, 0);
                            src->playing = 0;
                            // SpawnAudio* fire-and-forget cleanup --
                            // queue the entity for destruction once
                            // the iterate_query finishes (mid-walk
                            // destroy would invalidate the cursor).
                            if (src->auto_destroy && !cue->loop) {
                                c->to_destroy.push_back(e);
                            }
                        } else if (v.last_playing == 0) {
                            ma_sound_seek_to_pcm_frame(&v.sound, 0);
                            ma_sound_start(&v.sound);
                        } else {
                            ma_sound_start(&v.sound);
                        }
                    }
                } else if (v.initialised) {
                    ma_sound_stop(&v.sound);
                }
                v.last_playing = src->playing;
            }, &sctx);
        for (auto e2 : sctx.to_destroy) {
            // Tear down the source's voice before the entity vanishes
            // so the audio system's per-entity ma_sound is uninit'd
            // cleanly (otherwise the destroy_entity path leaves the
            // voice in g_b.sources as initialised but orphaned).
            fn_source_stop(nullptr, ZuesAudioEntity{ e2.index, e2.generation });
            world.destroy_entity(e2);
        }
    }

    // 3. One-shot reaping. Remove voices that finished naturally.
    for (auto it = g_b.oneshots.begin(); it != g_b.oneshots.end(); ) {
        if (ma_sound_at_end(it->sound.get()) ||
            !ma_sound_is_playing(it->sound.get())) {
            ma_sound_uninit(it->sound.get());
            it = g_b.oneshots.erase(it);
        } else {
            ++it;
        }
    }

    // 4. Gizmos.
    publish_gizmos_for_selected(world);
}

}  // namespace

// =========================================================================
// AudioSystem public API.
// =========================================================================

bool AudioSystem::init(ecs::World& world) {
    if (g_b.engine_inited) return true;

    ma_engine_config cfg = ma_engine_config_init();
    if (ma_engine_init(&cfg, &g_b.engine) != MA_SUCCESS) {
        ZUES_LOG_WARN("audio_system: ma_engine_init failed -- audio disabled");
        return false;
    }
    ma_engine_set_volume(&g_b.engine, g_b.master);
    g_b.engine_inited = true;

    g_b.xform_id    = world.find_component_id("Transform2D");
    g_b.source_id   = world.find_component_id("AudioSource");
    g_b.listener_id = world.find_component_id("AudioListener");
    g_b.camera_id   = world.find_component_id("Camera2D");

    if (auto* hc = Engine::host::get_host_context()) {
        g_b.project_dir          = hc->project_dir;
        g_b.assets_root_relative = "assets";
    }

    // Register service.
    if (auto* sr = Engine::services()) {
        sr->register_service(ZUES_SERVICE_AUDIO,
                              ZUES_SERVICE_AUDIO_VERSION, &g_vtable);
    }

    // Tick in PreUpdate / domain Both so audio advances during edit-mode
    // preview AND during gameplay. Ordering: positions read here come
    // from the previous frame's world transforms, which is fine for
    // audio (1-frame latency is inaudible).
    update_handle = world.add_system("AudioSystem",
                                      ecs::Phase::PreUpdate,
                                      run_system, nullptr,
                                      ecs::SystemDomain::Both);
    ZUES_LOG_INFO("Audio system initialised (miniaudio backend)");
    return true;
}

void AudioSystem::shutdown() {
    // Voices must die before the engine.
    for (auto& [k, v] : g_b.sources) {
        if (v.initialised) {
            ma_sound_stop  (&v.sound);
            ma_sound_uninit(&v.sound);
        }
    }
    g_b.sources.clear();

    for (auto& os : g_b.oneshots) {
        ma_sound_stop  (os.sound.get());
        ma_sound_uninit(os.sound.get());
    }
    g_b.oneshots.clear();
    g_b.clips.clear();
    g_b.cues.clear();

    if (g_b.engine_inited) {
        ma_engine_uninit(&g_b.engine);
        g_b.engine_inited = false;
    }
}

void AudioSystem::tick(float dt) {
    auto* hc = Engine::host::get_host_context();
    if (!hc || !hc->world) return;
    run_system(*hc->world, dt, nullptr);
}

void AudioSystem::set_selected_entity(ecs::Entity e) {
    g_b.selected = e;
}

// =========================================================================
// audio_api: thunks for the project-DLL host_api / Lync extern bridge.
// =========================================================================

namespace audio_api {

Engine::u32 play_one_shot_path(const char* path, float volume, float pitch) {
    if (!path || !*path || !g_b.engine_inited) return 0;
    Engine::u32 clip = fn_load_clip(nullptr, path);
    if (!clip) return 0;
    ZuesAudioPlayParams p{
        volume <= 0 ? 1.0f : volume,
        pitch  <= 0 ? 1.0f : pitch,
        0.0f, 0, 0, 0.0f, 0.0f, 1.0f, 20.0f
    };
    return fn_play_one_shot(nullptr, clip, &p);
}

Engine::u32 play_one_shot_at(const char* path, float x, float y,
                              float min_dist, float max_dist, float volume) {
    if (!path || !*path || !g_b.engine_inited) return 0;
    Engine::u32 clip = fn_load_clip(nullptr, path);
    if (!clip) return 0;
    ZuesAudioPlayParams p{
        volume <= 0 ? 1.0f : volume,
        1.0f, 0.0f, 0, 1, x, y,
        min_dist > 0 ? min_dist : 1.0f,
        max_dist > min_dist ? max_dist : (min_dist + 19.0f)
    };
    return fn_play_one_shot(nullptr, clip, &p);
}

void stop_voice(Engine::u32 v) { fn_stop_voice(nullptr, v); }

void source_play(ecs::Entity e) {
    fn_source_play(nullptr, ZuesAudioEntity{e.index, e.generation});
}
void source_stop(ecs::Entity e) {
    fn_source_stop(nullptr, ZuesAudioEntity{e.index, e.generation});
}
void source_pause(ecs::Entity e, int p) {
    fn_source_pause(nullptr, ZuesAudioEntity{e.index, e.generation}, p);
}
int  source_is_playing(ecs::Entity e) {
    return fn_source_is_playing(nullptr,
                                 ZuesAudioEntity{e.index, e.generation});
}

float master_volume()        { return fn_master_volume(nullptr); }
void  set_master_volume(float v) { fn_set_master_volume(nullptr, v); }
int   is_muted()             { return fn_is_muted(nullptr); }
void  set_muted(int m)       { fn_set_muted(nullptr, m); }
int   voices_active()        { return fn_voices_active(nullptr); }
int   clips_loaded()         { return fn_clips_loaded(nullptr); }

Engine::u32 preview_path(const char* abs_path) {
    return play_one_shot_path(abs_path, 1.0f, 1.0f);
}

void invalidate_cue(Engine::Guid g) {
    if (g.is_null()) g_b.cues.clear();
    else             g_b.cues.erase(g);
}

ecs::Entity spawn_for_cue(Engine::Guid cue_guid) {
    auto* hc = Engine::host::get_host_context();
    if (!hc || !hc->world || !g_b.engine_inited) return ecs::NULL_ENTITY;
    if (cue_guid.is_null()) return ecs::NULL_ENTITY;
    const auto src_id = hc->world->find_component_id("AudioSource");
    if (!src_id) return ecs::NULL_ENTITY;

    Engine::components::AudioSource src{};
    src.cue.guid     = cue_guid;
    src.volume       = 1.0f;
    src.pitch        = 1.0f;
    src.playing      = 1;
    src.autoplay     = 1;
    src.is_3d        = 0;
    src.auto_destroy = 1;

    const ecs::Entity e = hc->world->create_entity();
    hc->world->add_component(e, src_id, &src);
    return e;
}

ecs::Entity spawn_for_cue_3d(Engine::Guid cue_guid,
                              float x, float y, float max_distance) {
    auto* hc = Engine::host::get_host_context();
    if (!hc || !hc->world || !g_b.engine_inited) return ecs::NULL_ENTITY;
    if (cue_guid.is_null()) return ecs::NULL_ENTITY;
    const auto src_id   = hc->world->find_component_id("AudioSource");
    const auto xform_id = hc->world->find_component_id("Transform2D");
    if (!src_id) return ecs::NULL_ENTITY;

    Engine::components::AudioSource src{};
    src.cue.guid       = cue_guid;
    src.volume         = 1.0f;
    src.pitch          = 1.0f;
    src.playing        = 1;
    src.autoplay       = 1;
    src.is_3d          = 1;
    src.min_distance   = 1.0f;
    src.max_distance   = max_distance > src.min_distance
                           ? max_distance : src.min_distance + 1.0f;
    src.spatial_blend  = 1.0f;
    src.auto_destroy   = 1;

    const ecs::Entity e = hc->world->create_entity();
    if (xform_id) {
        // create_entity already auto-attaches Transform2D; just write
        // its position. Reach via raw pointer so we don't depend on
        // a header-side typed accessor here.
        if (auto* xf = static_cast<Engine::components::Transform2D*>(
                hc->world->get_component(e, xform_id))) {
            xf->position = { x, y };
        }
    }
    hc->world->add_component(e, src_id, &src);
    return e;
}

Engine::u32 preview_cue(Engine::Guid cue_guid) {
    if (!g_b.engine_inited) return 0;
    const Engine::AudioCue* cue = resolve_cue(cue_guid);
    if (!cue) return 0;
    Engine::Guid clip = pick_cue_clip(*cue);
    if (clip.is_null()) return 0;
    const std::string abs = resolve_audio_path(clip);
    if (abs.empty()) return 0;
    Engine::u32 clip_id = fn_load_clip(nullptr, abs.c_str());
    if (!clip_id) return 0;

    // Roll cue's intrinsic volume/pitch (with ± random) into the
    // one-shot params so the test button matches what a real source
    // play would produce.
    float vol = cue->volume        + roll_pm(cue->volume_random);
    float pit = cue->pitch         + roll_pm(cue->pitch_random);
    if (vol < 0)  vol = 0;
    if (pit <= 0) pit = 0.0001f;
    ZuesAudioPlayParams p{
        vol, pit, 0.0f,
        cue->loop ? 1 : 0,
        0, 0.0f, 0.0f, 1.0f, 20.0f
    };
    return fn_play_one_shot(nullptr, clip_id, &p);
}

}  // namespace audio_api

}  // namespace Engine::host
