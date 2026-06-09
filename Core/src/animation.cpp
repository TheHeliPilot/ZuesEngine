// Animation asset I/O. Owns the JSON shape; the asset registry just
// points at the file. Editor's animation panel calls save_animation
// after every meaningful edit; engine runtime calls load_animation
// the first time an Animator references the GUID.

#include <zues/animation.h>
#include <zues/log.h>

#include <nlohmann/json.hpp>

#include <fstream>

namespace Engine {

using json = nlohmann::json;

Result load_animation(const char* path, AnimationAsset& out) {
    if (!path) return Result::InvalidArgument;
    std::ifstream in(path);
    if (!in) return Result::NotFound;

    json doc;
    try { in >> doc; }
    catch (const std::exception&) {
        log_write(LogLevel::Error, "animation",
                  "load: JSON parse failed");
        return Result::Error;
    }
    if (!doc.is_object()) return Result::Error;

    out = {};
    if (doc.contains("guid") && doc["guid"].is_string())
        out.guid = guid_from_hex(doc["guid"].get<std::string>());
    if (doc.contains("name") && doc["name"].is_string())
        out.name = doc["name"].get<std::string>();
    if (doc.contains("loop") && doc["loop"].is_boolean())
        out.loop = doc["loop"].get<bool>();
    if (doc.contains("fps") && doc["fps"].is_number())
        out.fps = doc["fps"].get<float>();

    if (doc.contains("frames") && doc["frames"].is_array()) {
        for (const auto& fj : doc["frames"]) {
            if (!fj.is_object()) continue;
            AnimationFrame f;
            if (fj.contains("texture") && fj["texture"].is_string())
                f.texture = guid_from_hex(fj["texture"].get<std::string>());
            if (fj.contains("slice") && fj["slice"].is_number_integer())
                f.slice = fj["slice"].get<int>();
            if (fj.contains("duration") && fj["duration"].is_number())
                f.duration = fj["duration"].get<float>();
            out.frames.push_back(f);
        }
    }
    return Result::Ok;
}

Result save_animation(const char* path, AnimationAsset& asset) {
    if (!path) return Result::InvalidArgument;
    if (asset.guid.is_null()) asset.guid = guid_new();

    json doc;
    doc["guid"]    = guid_to_hex(asset.guid);
    doc["version"] = 1;
    doc["kind"]    = "animation";
    doc["name"]    = asset.name;
    doc["loop"]    = asset.loop;
    doc["fps"]     = asset.fps;

    json arr = json::array();
    for (const auto& f : asset.frames) {
        json fj;
        fj["texture"]  = guid_to_hex(f.texture);
        fj["slice"]    = f.slice;
        fj["duration"] = f.duration;
        arr.push_back(fj);
    }
    doc["frames"] = arr;

    try {
        std::ofstream out(path);
        if (!out) return Result::Error;
        out << doc.dump(2);
    } catch (const std::exception&) {
        return Result::Error;
    }
    return Result::Ok;
}

}  // namespace Engine
