#pragma once

// Animation asset (.zanim). A flat list of frames, each pointing at a
// texture asset (by GUID) plus an optional slice index into that
// texture's slice array, plus a duration. Loop / FPS metadata at the
// top.
//
// File format (JSON):
//   {
//     "guid": "<32-hex>",
//     "version": 1,
//     "name": "PlayerWalk",
//     "loop": true,
//     "fps":  12.0,
//     "frames": [
//       { "texture": "<guid>", "slice": 0, "duration": 0.083 },
//       ...
//     ]
//   }
//
// `slice` is the integer index into the texture's slice list (from the
// .meta sidecar). `slice = -1` means "use the whole texture, no
// slicing". `duration` is in seconds; `0` falls back to `1.0 / fps`.
//
// Loaded by the engine on demand (by GUID via the AssetRegistry); the
// editor's animation panel reads + writes the file directly.

#include <zues/api.h>
#include <zues/asset.h>
#include <zues/types.h>

#include <string>
#include <vector>

namespace Engine {

struct AnimationFrame {
    Guid texture{};       // .png asset that owns the frame's pixels
    int  slice    = -1;   // index into texture's .meta slice array, -1 = whole
    float duration = 0.0f; // seconds; 0 -> use default 1/fps
};

struct AnimationAsset {
    Guid                       guid{};
    std::string                name;
    bool                       loop = true;
    float                      fps  = 12.0f;
    std::vector<AnimationFrame> frames;
};

// Load a .zanim from disk. Returns Result::Ok on success and fills
// `out`. NotFound if the file doesn't exist, Error on parse failure.
ZUES_API Result load_animation(const char* path, AnimationAsset& out);

// Save a .zanim. Mints a fresh GUID into `out` if it's currently null
// (so first-save assets get an addressable identity).
ZUES_API Result save_animation(const char* path, AnimationAsset& asset);

}  // namespace Engine
