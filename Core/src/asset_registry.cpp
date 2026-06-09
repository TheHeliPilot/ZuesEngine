// AssetRegistry — walks the project's assets root, indexes every recognised
// asset by guid. Owned formats (.zprefab/.zsprite/.zworld) carry their guid
// inside the JSON at top level. Foreign formats (.png/.wav/.ttf) carry it in
// a "<file>.meta" sidecar; if the sidecar is missing on first scan we mint
// a new guid and write it out so the asset becomes addressable.
//
// Paths are stored canonical: project-relative + forward slashes. Same rule
// in both directions of the (path <-> guid) maps so lookups don't need
// per-call normalisation.

#include <zues/asset.h>
#include <zues/audio_cue.h>
#include <zues/log.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <unordered_map>

namespace Engine {

namespace fs = std::filesystem;
using json   = nlohmann::json;

const char* asset_kind_name(AssetKind k) {
    switch (k) {
        case AssetKind::Prefab:    return "Prefab";
        case AssetKind::Sprite:    return "Sprite";
        case AssetKind::World:     return "World";
        case AssetKind::Texture:   return "Texture";
        case AssetKind::Audio:     return "Audio";
        case AssetKind::Font:      return "Font";
        case AssetKind::Animation: return "Animation";
        case AssetKind::AudioCue:  return "AudioCue";
        case AssetKind::Unknown:
        default:                   return "Unknown";
    }
}

AssetKind asset_kind_from_extension(const char* path) {
    if (!path) return AssetKind::Unknown;
    std::string p = path;
    auto dot = p.find_last_of('.');
    if (dot == std::string::npos) return AssetKind::Unknown;
    std::string e = p.substr(dot + 1);
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (e == "zprefab") return AssetKind::Prefab;
    if (e == "zsprite") return AssetKind::Sprite;
    if (e == "zworld")  return AssetKind::World;
    if (e == "zanim")   return AssetKind::Animation;
    if (e == "zcue")    return AssetKind::AudioCue;
    if (e == "png" || e == "jpg" || e == "jpeg" || e == "bmp" || e == "tga")
        return AssetKind::Texture;
    if (e == "wav" || e == "ogg" || e == "mp3") return AssetKind::Audio;
    if (e == "ttf" || e == "otf")               return AssetKind::Font;
    return AssetKind::Unknown;
}

bool asset_kind_embeds_guid(AssetKind k) {
    switch (k) {
        case AssetKind::Prefab:
        case AssetKind::Sprite:
        case AssetKind::World:
        case AssetKind::Animation:
        case AssetKind::AudioCue:  return true;
        default:                   return false;
    }
}

const char* sprite_filter_name(SpriteFilter f) {
    return f == SpriteFilter::Nearest ? "nearest" : "linear";
}
const char* sprite_wrap_name(SpriteWrap w) {
    switch (w) {
        case SpriteWrap::Repeat: return "repeat";
        case SpriteWrap::Mirror: return "mirror";
        case SpriteWrap::Clamp:
        default:                 return "clamp";
    }
}

namespace {
SpriteFilter parse_sprite_filter(const std::string& s) {
    return s == "nearest" ? SpriteFilter::Nearest : SpriteFilter::Linear;
}
SpriteWrap parse_sprite_wrap(const std::string& s) {
    if (s == "repeat") return SpriteWrap::Repeat;
    if (s == "mirror") return SpriteWrap::Mirror;
    return SpriteWrap::Clamp;
}

// Parse the "sprite" block of a .meta JSON. Missing fields fall back
// to the SpriteAssetSettings struct defaults.
SpriteAssetSettings parse_sprite_block(const json& j) {
    SpriteAssetSettings s;
    if (!j.is_object()) return s;
    if (j.contains("pixels_per_unit") && j["pixels_per_unit"].is_number())
        s.pixels_per_unit = j["pixels_per_unit"].get<float>();
    if (j.contains("filter") && j["filter"].is_string())
        s.filter = parse_sprite_filter(j["filter"].get<std::string>());
    if (j.contains("wrap") && j["wrap"].is_string())
        s.wrap = parse_sprite_wrap(j["wrap"].get<std::string>());
    if (j.contains("pivot") && j["pivot"].is_array() &&
        j["pivot"].size() == 2) {
        s.pivot_x = j["pivot"][0].get<float>();
        s.pivot_y = j["pivot"][1].get<float>();
    }
    if (j.contains("slices") && j["slices"].is_array()) {
        for (const auto& sj : j["slices"]) {
            if (!sj.is_object()) continue;
            SpriteSlice sl;
            if (sj.contains("name") && sj["name"].is_string())
                sl.name = sj["name"].get<std::string>();
            if (sj.contains("rect") && sj["rect"].is_array() &&
                sj["rect"].size() == 4) {
                sl.x = sj["rect"][0].get<int>();
                sl.y = sj["rect"][1].get<int>();
                sl.w = sj["rect"][2].get<int>();
                sl.h = sj["rect"][3].get<int>();
            }
            if (sj.contains("pivot") && sj["pivot"].is_array() &&
                sj["pivot"].size() == 2) {
                sl.pivot_x = sj["pivot"][0].get<float>();
                sl.pivot_y = sj["pivot"][1].get<float>();
            }
            // 9-slice borders. Optional; absent or all-zero means
            // the slice renders as a single quad (no 9-slice path).
            if (sj.contains("border") && sj["border"].is_array() &&
                sj["border"].size() == 4) {
                sl.border_l = sj["border"][0].get<int>();
                sl.border_r = sj["border"][1].get<int>();
                sl.border_t = sj["border"][2].get<int>();
                sl.border_b = sj["border"][3].get<int>();
            }
            if (sj.contains("scale_mode") && sj["scale_mode"].is_string()) {
                const auto& m = sj["scale_mode"].get<std::string>();
                if      (m == "Tile")    sl.scale_mode = SpriteScaleMode::Tile;
                else if (m == "TileFit") sl.scale_mode = SpriteScaleMode::TileFit;
                else                     sl.scale_mode = SpriteScaleMode::Stretch;
            }
            if (sj.contains("center_mode") && sj["center_mode"].is_string()) {
                const auto& m = sj["center_mode"].get<std::string>();
                if      (m == "Tile")    sl.center_mode = SpriteScaleMode::Tile;
                else if (m == "TileFit") sl.center_mode = SpriteScaleMode::TileFit;
                else                     sl.center_mode = SpriteScaleMode::Stretch;
            }
            s.slices.push_back(std::move(sl));
        }
    }
    return s;
}

// Serialise sprite settings into a JSON object suitable for the .meta
// "sprite" block. Defaults emit anyway so the file is self-describing.
json emit_sprite_block(const SpriteAssetSettings& s) {
    json j;
    j["pixels_per_unit"] = s.pixels_per_unit;
    j["filter"]          = sprite_filter_name(s.filter);
    j["wrap"]            = sprite_wrap_name(s.wrap);
    j["pivot"]           = json::array({s.pivot_x, s.pivot_y});
    if (!s.slices.empty()) {
        json arr = json::array();
        for (const auto& sl : s.slices) {
            json sj;
            sj["name"]  = sl.name;
            sj["rect"]  = json::array({sl.x, sl.y, sl.w, sl.h});
            sj["pivot"] = json::array({sl.pivot_x, sl.pivot_y});
            // Only emit 9-slice fields when they're non-default --
            // keeps existing .meta files unchanged.
            if (sl.border_l || sl.border_r || sl.border_t || sl.border_b) {
                sj["border"] = json::array({sl.border_l, sl.border_r,
                                             sl.border_t, sl.border_b});
            }
            if (sl.scale_mode != SpriteScaleMode::Stretch) {
                const char* names[3] = { "Stretch", "Tile", "TileFit" };
                sj["scale_mode"] = names[(int)sl.scale_mode];
            }
            if (sl.center_mode != SpriteScaleMode::Stretch) {
                const char* names[3] = { "Stretch", "Tile", "TileFit" };
                sj["center_mode"] = names[(int)sl.center_mode];
            }
            arr.push_back(sj);
        }
        j["slices"] = arr;
    }
    return j;
}
}  // namespace

// -----------------------------------------------------------------------------

struct AssetRegistry::Impl {
    std::unordered_map<Guid, AssetEntry, GuidHash> by_guid;
    std::unordered_map<std::string, Guid>          by_path;
    // Absolute path of the most recent assets root passed to rescan().
    // Needed by update_sprite_settings so we can write back to the
    // .meta sidecar without relying on the process CWD.
    std::string                                     assets_root_abs;

    // Runtime handle <-> guid bookkeeping. Keyed by (kind, handle) so
    // a Texture handle 7 and a Font handle 7 don't collide. Entries
    // are populated by the editor right after each load_*; consumed by
    // the world serializer to emit a stable hex string the loader can
    // re-resolve. Cleared on rescan() so a project switch starts fresh.
    struct RuntimeKey {
        u32 kind_idx;   // (u32)AssetKind
        u32 handle;
        bool operator==(const RuntimeKey& o) const {
            return kind_idx == o.kind_idx && handle == o.handle;
        }
    };
    struct RuntimeKeyHash {
        std::size_t operator()(RuntimeKey k) const noexcept {
            return std::hash<u64>{}(((u64)k.kind_idx << 32) | k.handle);
        }
    };
    std::unordered_map<RuntimeKey, Guid, RuntimeKeyHash> handle_to_guid;
    // Reverse: per-kind guid -> current handle. Editor populates this
    // alongside handle_to_guid; loaders use it to map a saved guid
    // back to whatever handle the renderer assigned this run.
    std::unordered_map<u64, u32> guid_to_handle;     // key = (kind<<32) ^ guid_hash

    // Lazy-load callback: invoked on a runtime_handle_for_guid miss
    // so the host can load the asset on demand and bind a handle.
    // Set by the editor / runtime at boot.
    AssetRegistry::HandleResolver resolver = nullptr;
    void*                          resolver_user = nullptr;
};

AssetRegistry::AssetRegistry()  : m_impl(new Impl) {}
AssetRegistry::~AssetRegistry() { delete m_impl; }

AssetRegistry& AssetRegistry::instance() {
    static AssetRegistry g;
    return g;
}

namespace {

std::string canonicalise(const fs::path& abs_path, const fs::path& root) {
    std::error_code ec;
    fs::path rel = fs::relative(abs_path, root, ec);
    if (ec) rel = abs_path;
    std::string s = rel.generic_string();   // forward slashes
    return s;
}

// Read top-level "guid" from a JSON-owned asset. Returns NULL_GUID if absent
// or unreadable; caller decides whether to mint+write.
Guid read_embedded_guid(const fs::path& p) {
    std::ifstream in(p);
    if (!in) return NULL_GUID;
    json j;
    try {
        in >> j;
    } catch (const std::exception&) {
        return NULL_GUID;
    }
    if (!j.is_object())                return NULL_GUID;
    if (!j.contains("guid"))           return NULL_GUID;
    if (!j["guid"].is_string())        return NULL_GUID;
    return guid_from_hex(j["guid"].get<std::string>());
}

// Mint a new guid and write it back into the file's top-level "guid" field.
// Used when a .zprefab/.zsprite/.zworld is found without one (e.g. an old
// world from before the registry existed).
//
// We do textual splice rather than parse-via-nlohmann + dump back -- the
// engine's world format is a compact custom JSON whose key order matters
// (the load_json reader expects "type" before "data" inside each
// component record; nlohmann's alphabetised dump would silently re-order
// it and break loads). Splicing `"guid": "<hex>",` right after the
// opening `{` preserves the rest of the file byte-for-byte.
Guid mint_embedded_guid(const fs::path& p) {
    std::string body;
    {
        std::ifstream in(p, std::ios::binary | std::ios::ate);
        if (in) {
            const auto sz = in.tellg();
            in.seekg(0);
            body.resize(static_cast<size_t>(sz));
            in.read(body.data(), sz);
        }
    }

    Guid g = guid_new();
    const std::string entry = "\"guid\":\"" + guid_to_hex(g) + "\"";

    // Find the first `{` and splice the new entry on the next line.
    // Files start with `{` then immediate `\n`; the splice keeps the
    // engine's compact-JSON format intact.
    std::string out;
    auto open = body.find('{');
    if (open == std::string::npos) {
        // Empty file or non-JSON — write a fresh stub.
        out = "{\n" + entry + "\n}\n";
    } else {
        // Insert after the `{` and any immediately-following whitespace.
        size_t after = open + 1;
        while (after < body.size() &&
               (body[after] == ' ' || body[after] == '\n' ||
                body[after] == '\r' || body[after] == '\t')) ++after;
        // Need a comma separator if there are existing keys.
        const bool has_existing = (after < body.size() && body[after] != '}');
        out  = body.substr(0, open + 1);
        out += '\n';
        out += entry;
        if (has_existing) out += ',';
        out += '\n';
        out += body.substr(after);
    }

    try {
        std::ofstream of(p, std::ios::binary);
        if (!of) return NULL_GUID;
        of.write(out.data(), static_cast<std::streamsize>(out.size()));
    } catch (const std::exception&) {
        return NULL_GUID;
    }
    return g;
}

// Read or create the sidecar .meta for a foreign-format asset. When
// `out_sprite` is non-null, populates the parsed sprite settings (or
// defaults if no "sprite" block is present in the file).
Guid read_or_create_sidecar(const fs::path& asset,
                             SpriteAssetSettings* out_sprite = nullptr) {
    fs::path meta = asset;
    meta += ".meta";

    std::error_code ec;
    if (fs::exists(meta, ec)) {
        std::ifstream in(meta);
        if (in) {
            try {
                json j;
                in >> j;
                if (j.is_object() && j.contains("guid") && j["guid"].is_string()) {
                    Guid g = guid_from_hex(j["guid"].get<std::string>());
                    if (!g.is_null()) {
                        if (out_sprite && j.contains("sprite"))
                            *out_sprite = parse_sprite_block(j["sprite"]);
                        return g;
                    }
                }
            } catch (...) {
                // fall through to mint
            }
        }
    }

    // Mint and write.
    Guid g = guid_new();
    json j;
    j["guid"]    = guid_to_hex(g);
    j["version"] = 1;
    // Bake the default sprite block on first mint so users can see/edit
    // the fields without having to know the .meta format.
    SpriteAssetSettings s;
    j["sprite"] = emit_sprite_block(s);
    try {
        std::ofstream out(meta);
        if (!out) return NULL_GUID;
        out << j.dump(2);
    } catch (const std::exception&) {
        return NULL_GUID;
    }
    if (out_sprite) *out_sprite = s;
    return g;
}

}  // namespace

u32 AssetRegistry::rescan(const char* assets_root) {
    if (!assets_root) return 0;
    m_impl->by_guid.clear();
    m_impl->by_path.clear();
    m_impl->handle_to_guid.clear();
    m_impl->guid_to_handle.clear();
    m_impl->assets_root_abs = assets_root;

    fs::path root(assets_root);
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        log_write(LogLevel::Warn, "asset_registry",
                  "rescan: assets root missing");
        return 0;
    }

    for (auto it = fs::recursive_directory_iterator(root, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec))
    {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        const fs::path& abs = it->path();

        // Skip .meta files themselves — they're loaded alongside their asset.
        if (abs.extension() == ".meta") continue;

        AssetKind kind = asset_kind_from_extension(abs.string().c_str());
        if (kind == AssetKind::Unknown) continue;

        Guid g{};
        SpriteAssetSettings sprite{};
        if (asset_kind_embeds_guid(kind)) {
            g = read_embedded_guid(abs);
            if (g.is_null()) g = mint_embedded_guid(abs);
        } else {
            // Capture sprite settings during the sidecar read so we
            // don't have to re-parse later. Non-Texture foreign kinds
            // (audio, font) ignore the result.
            g = read_or_create_sidecar(abs,
                kind == AssetKind::Texture ? &sprite : nullptr);
        }
        if (g.is_null()) {
            log_write(LogLevel::Warn, "asset_registry",
                      "skip: could not assign guid");
            continue;
        }

        AssetEntry e;
        e.guid   = g;
        e.kind   = kind;
        e.path   = canonicalise(abs, root);
        e.sprite = sprite;

        // For AudioCues, peek at the file's auto-gen flag + wraps_clip
        // so we can hide auto cues from the asset browser and resolve
        // "user dropped a .wav onto a Cue slot" without re-parsing.
        if (kind == AssetKind::AudioCue) {
            AudioCue cue;
            Guid     cue_g{};
            if (load_audio_cue(abs.string().c_str(), cue, &cue_g)) {
                e.hidden_in_browser = cue.auto_generated;
                e.wraps_clip        = cue.wraps_clip;
            }
        }

        // Defensive: drop a previous mapping for either side before insert,
        // so a duplicate-guid asset (manual file copy) doesn't leave a
        // stale path pointing nowhere.
        auto prev = m_impl->by_guid.find(g);
        if (prev != m_impl->by_guid.end()) {
            m_impl->by_path.erase(prev->second.path);
        }
        m_impl->by_path[e.path] = g;
        m_impl->by_guid[g]      = std::move(e);
    }

    // ---- Second pass: auto-generate cues for audio files that don't
    // already have one. Auto-cues live alongside the audio file as
    // `<filename>.zcue` (so `coin.wav` -> `coin.wav.zcue`) and carry
    // `auto_generated = true` so the browser hides them. They wrap a
    // single entry pointing at the audio. The user can edit the cue's
    // volume / pitch / random in the cue editor; the entries list is
    // locked because it's tied to the source file.
    {
        // Index existing auto-cues by the audio guid they wrap so we
        // don't duplicate. Pre-allocate to avoid rehash during a
        // typical project scan.
        std::unordered_map<Guid, bool, GuidHash> wrapped;
        wrapped.reserve(m_impl->by_guid.size());
        for (auto& [g, entry] : m_impl->by_guid) {
            if (entry.kind == AssetKind::AudioCue && !entry.wraps_clip.is_null())
                wrapped[entry.wraps_clip] = true;
        }

        // Snapshot audio entries first -- we mutate the map below.
        std::vector<AssetEntry> audio_entries;
        for (auto& [g, entry] : m_impl->by_guid) {
            if (entry.kind == AssetKind::Audio &&
                wrapped.find(g) == wrapped.end())
            {
                audio_entries.push_back(entry);
            }
        }

        for (auto& audio : audio_entries) {
            // <abs_audio_path>.zcue, alongside the source file.
            const fs::path abs_audio = root / audio.path;
            const fs::path abs_cue   =
                abs_audio.parent_path() /
                (abs_audio.filename().string() + ".zcue");

            AudioCue cue;
            cue.auto_generated = true;
            cue.wraps_clip     = audio.guid;
            cue.entries.push_back(AudioCueEntry{ audio.guid });
            const Guid cue_g = guid_new();
            if (!save_audio_cue(abs_cue.string().c_str(), cue, cue_g)) {
                log_write(LogLevel::Warn, "asset_registry",
                          "failed to mint auto-cue for audio asset");
                continue;
            }
            AssetEntry e;
            e.guid              = cue_g;
            e.kind              = AssetKind::AudioCue;
            e.path              = canonicalise(abs_cue, root);
            e.hidden_in_browser = true;
            e.wraps_clip        = audio.guid;
            m_impl->by_path[e.path] = cue_g;
            m_impl->by_guid[cue_g]  = std::move(e);
        }
    }

    return static_cast<u32>(m_impl->by_guid.size());
}

const char* AssetRegistry::path_for(Guid g) const {
    auto it = m_impl->by_guid.find(g);
    return it == m_impl->by_guid.end() ? nullptr : it->second.path.c_str();
}

Guid AssetRegistry::guid_for(const char* path) const {
    if (!path) return NULL_GUID;
    auto it = m_impl->by_path.find(path);
    return it == m_impl->by_path.end() ? NULL_GUID : it->second;
}

Guid AssetRegistry::guid_for_any_path(const char* path) const {
    if (!path) return NULL_GUID;
    // Normalise slashes to forward — registry's canonical paths use
    // generic_string() which is forward-slashed regardless of platform.
    std::string norm = path;
    for (auto& c : norm) if (c == '\\') c = '/';
    // Fast path: exact canonical match.
    if (auto it = m_impl->by_path.find(norm); it != m_impl->by_path.end())
        return it->second;
    // Fallback: try every stored path as a suffix of the input. The
    // registry is small (tens of entries) and this only fires on the slow
    // path of an unmatched lookup, so the linear scan is fine.
    //
    // Suffix match catches the common cases where the caller has a longer
    // form than what the registry stored:
    //   stored: "prefabs/Pipe.zprefab"
    //   input : "C:/.../MyGame/assets/prefabs/Pipe.zprefab"          (abs)
    //   input : "assets/prefabs/Pipe.zprefab"                        (project-rel)
    //   input : "prefabs/Pipe.zprefab"                               (already canonical, hits fast path)
    //
    // Require a `/` immediately before the match start (or that it's the
    // full input) so "Pipe.zprefab" doesn't match "OtherPipe.zprefab".
    for (const auto& [stored, g] : m_impl->by_path) {
        if (norm.size() < stored.size()) continue;
        const size_t off = norm.size() - stored.size();
        if (norm.compare(off, std::string::npos, stored) != 0) continue;
        if (off == 0 || norm[off - 1] == '/') return g;
    }
    return NULL_GUID;
}

const AssetEntry* AssetRegistry::find(Guid g) const {
    auto it = m_impl->by_guid.find(g);
    return it == m_impl->by_guid.end() ? nullptr : &it->second;
}

const AssetEntry* AssetRegistry::find_auto_cue_for(Guid audio_guid) const {
    if (audio_guid.is_null()) return nullptr;
    for (auto& [g, entry] : m_impl->by_guid) {
        if (entry.kind == AssetKind::AudioCue &&
            entry.wraps_clip == audio_guid) return &entry;
    }
    return nullptr;
}

void AssetRegistry::register_asset(const AssetEntry& e) {
    auto prev = m_impl->by_guid.find(e.guid);
    if (prev != m_impl->by_guid.end()) {
        m_impl->by_path.erase(prev->second.path);
    }
    m_impl->by_path[e.path] = e.guid;
    m_impl->by_guid[e.guid] = e;
}

void AssetRegistry::unregister_asset(Guid g) {
    auto it = m_impl->by_guid.find(g);
    if (it == m_impl->by_guid.end()) return;
    m_impl->by_path.erase(it->second.path);
    m_impl->by_guid.erase(it);
}

u32 AssetRegistry::entry_count() const {
    return static_cast<u32>(m_impl->by_guid.size());
}

namespace {
    inline u64 guid_kind_key(AssetKind k, Guid g) {
        // Mix the kind into the high bits of the guid hash so the same
        // guid in different kinds (rare but legal -- nothing prevents
        // a guid from naming both a Texture and a Font slot) doesn't
        // collide in the reverse map.
        return (static_cast<u64>(static_cast<u32>(k)) << 32)
             ^ GuidHash{}(g);
    }
}

void AssetRegistry::bind_runtime_handle(AssetKind kind, u32 handle, Guid g) {
    if (handle == 0 && g.is_null()) return;
    if (kind == AssetKind::Unknown) return;
    Impl::RuntimeKey k{ static_cast<u32>(kind), handle };
    if (g.is_null()) {
        // Caller asked to clear (handle, _) -> remove both directions.
        auto it = m_impl->handle_to_guid.find(k);
        if (it != m_impl->handle_to_guid.end()) {
            m_impl->guid_to_handle.erase(guid_kind_key(kind, it->second));
            m_impl->handle_to_guid.erase(it);
        }
        return;
    }
    m_impl->handle_to_guid[k] = g;
    m_impl->guid_to_handle[guid_kind_key(kind, g)] = handle;
}

Guid AssetRegistry::guid_for_runtime_handle(AssetKind kind, u32 handle) const {
    if (handle == 0 || kind == AssetKind::Unknown) return NULL_GUID;
    Impl::RuntimeKey k{ static_cast<u32>(kind), handle };
    auto it = m_impl->handle_to_guid.find(k);
    return (it == m_impl->handle_to_guid.end()) ? NULL_GUID : it->second;
}

u32 AssetRegistry::runtime_handle_for_guid(AssetKind kind, Guid g) const {
    if (g.is_null() || kind == AssetKind::Unknown) return 0;
    auto it = m_impl->guid_to_handle.find(guid_kind_key(kind, g));
    if (it != m_impl->guid_to_handle.end()) return it->second;
    // Cache miss -- ask the host to load the asset on demand. The
    // resolver should call bind_runtime_handle before returning so
    // future lookups are O(1). Returns 0 to signal still-missing.
    if (m_impl->resolver) {
        return m_impl->resolver(kind, g, m_impl->resolver_user);
    }
    return 0;
}

void AssetRegistry::set_handle_resolver(HandleResolver fn, void* user) {
    m_impl->resolver      = fn;
    m_impl->resolver_user = user;
}

void AssetRegistry::iterate(Visitor fn, void* user) const {
    if (!fn) return;
    for (const auto& kv : m_impl->by_guid) fn(kv.second, user);
}

SpriteAssetSettings AssetRegistry::sprite_settings_for(Guid g) const {
    auto it = m_impl->by_guid.find(g);
    if (it == m_impl->by_guid.end()) return {};
    return it->second.sprite;
}

bool AssetRegistry::update_sprite_settings(Guid g,
                                            const SpriteAssetSettings& s) {
    auto it = m_impl->by_guid.find(g);
    if (it == m_impl->by_guid.end()) return false;
    if (it->second.kind != AssetKind::Texture) return false;

    // Update the in-memory cache first so the editor sees the change
    // immediately, even if the disk write fails.
    it->second.sprite = s;

    // Write the updated settings back into the .meta sidecar. Read
    // the file fresh so we don't clobber any unrelated fields a user
    // (or future engine version) added.
    fs::path asset_abs;
    if (!m_impl->assets_root_abs.empty()) {
        asset_abs = fs::path(m_impl->assets_root_abs) /
                     fs::path(it->second.path);
    } else {
        asset_abs = fs::path(it->second.path);
    }
    fs::path meta = asset_abs;
    meta += ".meta";

    json doc;
    {
        std::ifstream in(meta);
        if (in) {
            try { in >> doc; } catch (...) { doc = json::object(); }
        }
        if (!doc.is_object()) doc = json::object();
    }
    doc["guid"]    = guid_to_hex(g);
    doc["version"] = doc.value("version", 1);
    doc["sprite"]  = emit_sprite_block(s);

    std::ofstream out(meta);
    if (!out) return false;
    out << doc.dump(2);
    return true;
}

}  // namespace Engine
