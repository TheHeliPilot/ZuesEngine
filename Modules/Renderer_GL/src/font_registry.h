#pragma once

// Font baking + glyph atlas storage for the GL renderer module. Each
// FontHandle owns a single GL_R8 texture (alpha mask) plus an stbtt
// packed-char table covering printable ASCII. draw_text emits one quad
// per glyph through the existing sprite batcher's add() path -- the
// shader multiplies the sampled alpha by the user-supplied tint, which
// gives us correctly-coloured text without a separate text shader.
//
// Why ASCII only for v5: keeps the atlas at ~512x512 with one bake per
// font/size pair. Localised text (extended Latin / CJK) is a Phase 6
// follow-up where we either grow the atlas or switch to dynamic glyph
// caching.

#include "gl_loader.h"
#include "stb_truetype.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace zr {

struct FontRecord {
    GLuint  texture     = 0;          // GL_R8 alpha atlas
    int     atlas_w     = 0;
    int     atlas_h     = 0;
    int     pixel_height = 0;         // bake size in pixels (vertical)
    float   ascent_px   = 0.0f;       // unscaled font ascent baked at this size
    // ASCII range we baked. 32..126 inclusive = 95 glyphs.
    static constexpr int kFirst = 32;
    static constexpr int kCount = 95;
    std::vector<stbtt_packedchar> chars;   // size kCount
};

class FontRegistry {
public:
    // Bake a TTF/OTF file into an alpha atlas at `pixel_height`. Returns
    // a non-zero handle on success (the underlying GL texture id is the
    // handle). `path` may be UTF-8 absolute or relative; failure to open
    // returns 0 and logs.
    uint32_t load_from_file(const char* path, int pixel_height);

    void     free(uint32_t handle);
    void     free_all();

    // Read access for draw_text / measure_text. Returns nullptr if the
    // handle is unknown.
    const FontRecord* get(uint32_t handle) const;

private:
    std::unordered_map<uint32_t, FontRecord> m_records;
};

}  // namespace zr
