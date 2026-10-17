#include "r3d_wall_rules.h"

namespace r3d
{

std::string match_wall_texture( const wall_texture_rules &rules,
                                const std::vector<std::string> &chain )
{
    for( const std::string &id : chain ) {
        const auto it = rules.by_id.find( id );
        if( it != rules.by_id.end() ) {
            return it->second;
        }
    }
    for( const std::string &id : chain ) {
        const std::string *best = nullptr;
        size_t best_len = 0;
        for( const auto &rule : rules.by_prefix ) {
            const std::string &prefix = rule.first;
            if( !prefix.empty() && prefix.size() > best_len &&
                id.compare( 0, prefix.size(), prefix ) == 0 ) {
                best = &rule.second;
                best_len = prefix.size();
            }
        }
        if( best ) {
            return *best;
        }
    }
    return rules.fallback;
}

uv_rect atlas_cell( int index, int cell, int atlas_w, int atlas_h )
{
    uv_rect r;
    if( index < 0 || cell <= 0 || atlas_w < cell || atlas_h < cell ) {
        return r;
    }
    const int columns = atlas_w / cell;
    const int col = index % columns;
    const int row = index / columns;
    if( ( row + 1 ) * cell > atlas_h ) {
        return r;
    }
    const float w = static_cast<float>( atlas_w );
    const float h = static_cast<float>( atlas_h );
    r.u0 = ( static_cast<float>( col * cell ) + 0.5f ) / w;
    r.v0 = ( static_cast<float>( row * cell ) + 0.5f ) / h;
    r.u1 = ( static_cast<float>( ( col + 1 ) * cell ) - 0.5f ) / w;
    r.v1 = ( static_cast<float>( ( row + 1 ) * cell ) - 0.5f ) / h;
    r.valid = true;
    return r;
}

} // namespace r3d
