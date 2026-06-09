#include <zues/audio_cue.h>
#include <zues/log.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <system_error>

namespace Engine {

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace {
// Helper: read a guid hex string from JSON or fall back to NULL_GUID.
Guid read_guid_field(const json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return NULL_GUID;
    const std::string s = it->get<std::string>();
    return guid_from_hex(s);
}
}  // namespace

bool load_audio_cue(const char* abs_path, AudioCue& out, Guid* out_guid) {
    if (!abs_path) return false;
    std::ifstream f(abs_path);
    if (!f) return false;

    json j;
    try { f >> j; } catch (...) { return false; }
    if (!j.is_object()) return false;

    out = AudioCue{};
    if (out_guid) *out_guid = read_guid_field(j, "guid");

    if (j.contains("auto_generated") && j["auto_generated"].is_boolean())
        out.auto_generated = j["auto_generated"].get<bool>();
    out.wraps_clip = read_guid_field(j, "wraps_clip");

    if (j.contains("volume") && j["volume"].is_number())
        out.volume = j["volume"].get<float>();
    if (j.contains("volume_random") && j["volume_random"].is_number())
        out.volume_random = j["volume_random"].get<float>();
    if (j.contains("pitch") && j["pitch"].is_number())
        out.pitch = j["pitch"].get<float>();
    if (j.contains("pitch_random") && j["pitch_random"].is_number())
        out.pitch_random = j["pitch_random"].get<float>();
    if (j.contains("loop") && j["loop"].is_boolean())
        out.loop = j["loop"].get<bool>();
    if (j.contains("pick_mode") && j["pick_mode"].is_string()) {
        // Single value today (`random`). Unknown spellings silently
        // fall back to Random so adding new modes later doesn't break
        // existing files.
        out.pick_mode = AudioCuePickMode::Random;
    }

    if (j.contains("entries") && j["entries"].is_array()) {
        for (auto& e : j["entries"]) {
            if (!e.is_object()) continue;
            AudioCueEntry entry;
            entry.clip = read_guid_field(e, "clip");
            out.entries.push_back(entry);
        }
    }
    return true;
}

bool save_audio_cue(const char* abs_path, const AudioCue& cue, Guid guid) {
    if (!abs_path) return false;
    std::error_code ec;
    fs::path p(abs_path);
    fs::create_directories(p.parent_path(), ec);   // best-effort

    json j;
    j["guid"]           = guid_to_hex(guid);
    j["auto_generated"] = cue.auto_generated;
    if (cue.auto_generated)
        j["wraps_clip"] = guid_to_hex(cue.wraps_clip);
    j["volume"]         = cue.volume;
    j["volume_random"]  = cue.volume_random;
    j["pitch"]          = cue.pitch;
    j["pitch_random"]   = cue.pitch_random;
    j["loop"]           = cue.loop;
    j["pick_mode"]      = "random";

    j["entries"] = json::array();
    for (auto& e : cue.entries) {
        json je;
        je["clip"] = guid_to_hex(e.clip);
        j["entries"].push_back(je);
    }

    std::ofstream f(abs_path);
    if (!f) return false;
    f << j.dump(2);
    return f.good();
}

}  // namespace Engine
