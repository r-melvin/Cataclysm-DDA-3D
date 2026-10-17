#pragma once
#ifndef CATA_SRC_R3D_WALL_RULES_H
#define CATA_SRC_R3D_WALL_RULES_H

#include <string>
#include <unordered_map>
#include <vector>

namespace r3d
{

/**
 * Which side texture a wall gets in the 3D view (see data/raw/r3d/wall_sides.json). Values name
 * a texture of the atlas; an empty value means "keep the tileset sprite on the sides".
 */
struct wall_texture_rules {
    std::unordered_map<std::string, std::string> by_id;
    std::unordered_map<std::string, std::string> by_prefix;
    std::string fallback; // for walls no rule matches; empty keeps the tileset sprite
};

/**
 * The texture name for a wall. chain is the terrain id followed by its looks_like ids, nearest
 * first. Exact ids are tried along the whole chain before prefixes (the longest prefix of the
 * nearest id that has one wins), then the fallback. Returns "" to keep the tileset sprite.
 */
std::string match_wall_texture( const wall_texture_rules &rules,
                                const std::vector<std::string> &chain );

struct uv_rect {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    bool valid = false;
};

/**
 * The uv rectangle of cell index in an atlas of square cells (cell pixels wide) laid out row by
 * row, inset half a texel so nearest sampling never bleeds into the neighbouring cell. Invalid
 * if the cell does not fit in the atlas.
 */
uv_rect atlas_cell( int index, int cell, int atlas_w, int atlas_h );

} // namespace r3d

#endif // CATA_SRC_R3D_WALL_RULES_H
