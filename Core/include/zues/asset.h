#pragma once

// Asset identity and the registry that resolves it. Every asset (prefabs,
// sprites, worlds, audio, fonts...) has a stable Guid that survives moves,
// renames, and merges. Component fields hold typed *Ref structs that wrap a
// Guid plus a phantom type tag — the inspector recognises the type and
// renders a drag-target slot, Lync exposes them as first-class reference
// types, and runtime resolves them through the AssetRegistry.
//
// Storage location of the Guid depends on whether we own the format:
//   • Owned (JSON: .zprefab, .zsprite, .zworld, ...) — top-level "guid"
//     field inside the file itself. One file, one source of truth.
//   • Foreign (binary: .png, .wav, .ttf, ...) — "<asset>.meta" sidecar JSON
//     next to the asset. Required because we can't embed metadata inside
//     someone else's binary format without breaking it.
//
// AssetRegistry walks the project's assets root once at startup, indexes
// (Guid → path, path → Guid). Project hot-reload re-scans.

#include <zues/api.h>
#include <zues/ecs/reflection.h>
#include <zues/guid.h>
#include <zues/types.h>

#include <cstddef>
#include <string>
#include <vector>

namespace Engine {

// Asset kind. Used by the editor to filter drag-targets (a PrefabRef slot
// only accepts .zprefab) and by the registry to route the right loader.
// Order is stable — saved JSON references it.
enum class AssetKind : u32 {
    Unknown = 0,
    Prefab,    // .zprefab
    Sprite,    // .zsprite
    World,     // .zworld
    Texture,   // .png / .jpg
    Audio,     // .wav / .ogg / .mp3 / .flac
    Font,      // .ttf
    Animation, // .zanim (sequence of texture-slice frames)
    AudioCue,  // .zcue (list of audio refs + playback settings)
};

ZUES_API const char* asset_kind_name(AssetKind k);
ZUES_API AssetKind   asset_kind_from_extension(const char* path);

// Whether this kind embeds its guid inside the file (true) or relies on an
// external "<file>.meta" sidecar (false). All JSON-owned formats embed; all
// foreign binary formats use sidecars.
ZUES_API bool        asset_kind_embeds_guid(AssetKind k);

// Per-texture-asset settings. Lives in the .meta sidecar's "sprite"
// block. Defaults match v0.4 behaviour (linear filter, clamp wrap, 100
// PPU, centered pivot) so existing projects behave the same after the
// upgrade.
//
// `pixels_per_unit` overrides the project-wide default at draw time; a
// per-entity Sprite component can also override this on a case-by-case
// basis if its Sprite.size_overrides_asset flag is set.
//
// `pivot` is in 0..1 within the sprite -- (0,0) top-left, (1,1)
// bottom-right, (0.5,0.5) center. Renderer reads this when no
// per-entity pivot is set.
enum class SpriteFilter : u8 { Linear = 0, Nearest = 1 };
enum class SpriteWrap   : u8 { Clamp  = 0, Repeat  = 1, Mirror = 2 };

// One named sub-rect inside a sprite atlas. Coordinates are in source
// pixels, with (0, 0) at the top-left of the texture. Pivot is per-slice
// in 0..1 within the slice rect (overrides the asset-level pivot when
// the sprite reference targets this slice).
// 9-slice scale mode applied to the slice's edges + center when it's
// stretched larger than its border-implied native size.
//   Stretch -- the edges/center are linearly interpolated to fill.
//   Tile    -- the source pixels repeat; final tile may be partial.
//   TileFit -- count tiles `n = round(length / tile)` then squeeze
//              all tiles uniformly to length/n. No half-tiles, ever.
// `Stretch` is the v1 default; Tile/TileFit follow once the renderer
// supports them.
enum class SpriteScaleMode : u8 { Stretch = 0, Tile = 1, TileFit = 2 };

struct SpriteSlice {
    std::string name;     // unique within the asset, e.g. "hero_0"
    int         x = 0;
    int         y = 0;
    int         w = 0;
    int         h = 0;
    float       pivot_x = 0.5f;
    float       pivot_y = 0.5f;
    // 9-slice borders in source pixels. All zeros (default) means the
    // slice is not 9-sliced -- the renderer skips the multi-quad path
    // and emits a single quad. Otherwise these define the four guide
    // rails that split the source rect into corners (un-stretched),
    // edges (stretched along one axis), and a center (stretched on
    // both axes). Sum-of-borders along each axis must be < that axis'
    // size or the editor reduces them.
    int             border_l = 0;
    int             border_r = 0;
    int             border_t = 0;
    int             border_b = 0;
    SpriteScaleMode scale_mode  = SpriteScaleMode::Stretch;
    // Center region uses its own mode so common patterns like
    // "tile edges + stretch center" are possible. Defaults to Stretch
    // -- matches the previous single-mode behaviour for old assets.
    SpriteScaleMode center_mode = SpriteScaleMode::Stretch;
};

struct SpriteAssetSettings {
    float        pixels_per_unit = 100.0f;
    SpriteFilter filter          = SpriteFilter::Linear;
    SpriteWrap   wrap            = SpriteWrap::Clamp;
    float        pivot_x         = 0.5f;
    float        pivot_y         = 0.5f;
    // Sliced sub-rects (sprite cutter output). Empty means the texture
    // is used whole; non-empty means a SpriteRef can target a specific
    // slice by name or index. Persisted in the .meta sidecar's
    // "sprite.slices" array.
    std::vector<SpriteSlice> slices;
};

ZUES_API const char* sprite_filter_name(SpriteFilter f);
ZUES_API const char* sprite_wrap_name  (SpriteWrap w);

// One indexed asset. Path is project-relative with forward slashes
// ("assets/prefabs/Player.zprefab") so it diffs well across platforms.
//
// For Texture assets, `sprite` carries the per-asset settings parsed
// from / written to the .meta sidecar. Other kinds leave the field
// at its defaults; consumers should ignore it.
struct AssetEntry {
    Guid                guid;
    AssetKind           kind = AssetKind::Unknown;
    std::string         path;
    SpriteAssetSettings sprite{};
    // True when the entry was machine-minted by the registry (e.g.
    // auto-generated AudioCue alongside a .wav). The asset browser
    // filters these out so they don't clutter the listing -- they
    // exist to be referenced, not directly opened.
    bool                hidden_in_browser = false;
    // For auto-generated AudioCues, the guid of the audio asset they
    // wrap. Lets the editor resolve "user dropped a .wav onto a Cue
    // slot" to the right cue without a string lookup.
    Guid                wraps_clip{};
};

// -----------------------------------------------------------------------------
// Typed asset references. Phantom-typed wrapper around a Guid so a PrefabRef
// can't be silently stored in a SpriteRef field. All refs are layout-identical
// (a single Guid), which keeps reflection/serialization uniform.
// -----------------------------------------------------------------------------

template <AssetKind K>
struct TypedAssetRef {
    Guid guid;

    constexpr bool is_null() const { return guid.is_null(); }
    constexpr bool operator==(const TypedAssetRef&) const = default;
    static constexpr AssetKind kind = K;
};

using PrefabRef    = TypedAssetRef<AssetKind::Prefab>;
using SpriteRef    = TypedAssetRef<AssetKind::Sprite>;
using TextureRef   = TypedAssetRef<AssetKind::Texture>;
using AudioRef     = TypedAssetRef<AssetKind::Audio>;
using FontRef      = TypedAssetRef<AssetKind::Font>;
using AnimationRef = TypedAssetRef<AssetKind::Animation>;
using AudioCueRef  = TypedAssetRef<AssetKind::AudioCue>;

// -----------------------------------------------------------------------------
// AssetRegistry — Core-owned, editor-driven. The editor calls rescan() when a
// project loads or hot-reloads; everyone else queries via path_for/guid_for.
//
// v1 lives entirely in Core as a plain C++ object — Modules will reach it
// through a service vtable in v2 once Lync's Instantiate(prefab) needs it at
// runtime. For now only the editor consumes it.
// -----------------------------------------------------------------------------

class ZUES_API AssetRegistry {
public:
    AssetRegistry();
    ~AssetRegistry();
    AssetRegistry(const AssetRegistry&)            = delete;
    AssetRegistry& operator=(const AssetRegistry&) = delete;

    // Walk the assets root and rebuild the index. Safe to call repeatedly —
    // existing entries are replaced. Returns the entry count after scan.
    // For owned types (.zprefab/.zsprite/.zworld): reads top-level "guid".
    // For foreign types: reads <path>.meta if present, else MINTS a new guid
    // and writes the .meta back so the asset becomes addressable.
    u32 rescan(const char* assets_root);

    // Resolve a guid to project-relative path. nullptr on miss.
    const char* path_for(Guid g) const;

    // Reverse lookup. NULL_GUID on miss.
    Guid guid_for(const char* path) const;

    // Resolve any path form (absolute, project-relative, or assets-root-
    // relative) by trying suffix matches against the registry's stored
    // canonical paths. Used by the editor inspector when accepting a
    // drag-drop payload that carries an absolute filesystem path -- the
    // strict guid_for() requires the exact canonical form, which is brittle
    // when callers don't know the project's assets root layout.
    Guid guid_for_any_path(const char* path) const;

    const AssetEntry* find(Guid g) const;

    // Find the AudioCue entry whose `wraps_clip` matches `audio_guid`.
    // Used by the editor to resolve "user dropped a .wav onto a Cue
    // slot" to the matching auto-cue. Returns nullptr when no matching
    // cue is indexed (which shouldn't happen for audio assets after a
    // rescan -- the registry mints one on the spot).
    const AssetEntry* find_auto_cue_for(Guid audio_guid) const;

    // Insert/replace an entry. Used right after "Save Selected as Prefab" so
    // the new file is addressable before a full rescan.
    void register_asset(const AssetEntry& e);
    void unregister_asset(Guid g);

    // Look up the per-texture sprite settings by guid. Returns the
    // engine defaults when the guid isn't a texture or isn't indexed
    // -- callers can use the result unconditionally without null
    // checks. Updating: read, mutate, then call
    // `update_sprite_settings(guid, ...)` to persist back to the
    // .meta sidecar AND refresh the cached entry.
    SpriteAssetSettings sprite_settings_for(Guid g) const;
    bool                update_sprite_settings(Guid g,
                                                const SpriteAssetSettings& s);

    // Runtime handle <-> guid bookkeeping. The renderer assigns a u32
    // slot id every time it loads a texture (or a font, audio, ...).
    // That id is NOT stable across runs -- a saved Sprite that records
    // only the slot id breaks on next launch when the renderer hands
    // out different ids. The fix: the editor calls bind_runtime_handle
    // after each load so the registry knows the (kind, handle) -> guid
    // mapping. The world serializer then emits a guid hex string
    // alongside the handle, and the loader looks the new handle up.
    //
    // `kind` is one of AssetKind::Texture / Audio / Font; pass Unknown
    // to clear an entry. Lookups return NULL_GUID / 0 on miss; both
    // calls are safe at any time.
    void  bind_runtime_handle(AssetKind kind, u32 handle, Guid g);
    Guid  guid_for_runtime_handle(AssetKind kind, u32 handle) const;
    u32   runtime_handle_for_guid(AssetKind kind, Guid g) const;

    // Lazy-load resolver. Called by `runtime_handle_for_guid` when no
    // handle is bound yet -- gives the host a chance to load the asset
    // (e.g. r->load_texture_from_file) and call bind_runtime_handle
    // before returning. Returns 0 to signal "still missing." Set once
    // by the editor / runtime at startup; Core never dereferences host
    // state directly.
    using HandleResolver = u32(*)(AssetKind kind, Guid g, void* user);
    void  set_handle_resolver(HandleResolver fn, void* user);

    u32 entry_count() const;

    using Visitor = void (*)(const AssetEntry&, void* user);
    void iterate(Visitor fn, void* user) const;

    // Process-wide singleton owned by the editor. Modules will eventually
    // get a service vtable instead.
    static AssetRegistry& instance();

private:
    struct Impl;
    Impl* m_impl;
};

}  // namespace Engine

// Reflection trait specialisations. Live here (not reflection.h) because the
// TypedAssetRef template is defined in this header.
namespace Engine::ecs {
    template<> struct field_kind_of<::Engine::PrefabRef>  { static constexpr FieldKind value = FieldKind::PrefabRef;  };
    template<> struct field_kind_of<::Engine::SpriteRef>  { static constexpr FieldKind value = FieldKind::SpriteRef;  };
    template<> struct field_kind_of<::Engine::TextureRef> { static constexpr FieldKind value = FieldKind::TextureRef; };
    template<> struct field_kind_of<::Engine::AudioRef>     { static constexpr FieldKind value = FieldKind::AudioRef;   };
    template<> struct field_kind_of<::Engine::FontRef>      { static constexpr FieldKind value = FieldKind::FontRef;    };
    template<> struct field_kind_of<::Engine::AnimationRef> { static constexpr FieldKind value = FieldKind::AnimationRef; };
    template<> struct field_kind_of<::Engine::AudioCueRef>  { static constexpr FieldKind value = FieldKind::AudioCueRef;  };
}
