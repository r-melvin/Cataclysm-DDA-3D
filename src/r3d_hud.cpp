#if defined(TILES)

#include "r3d_hud.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <SDL3/SDL.h>

#include "avatar.h"
#include "color.h"
#include "cursesdef.h"
#include "line.h"
#include "options.h"
#include "output.h"

namespace r3d
{

vec3 hud_frame::clip( const vec3 &tile, float &w_out ) const
{
    const vec3 ndc = transform_point( view_proj, tile_to_world( tile ), &w_out );
    return { ndc.x * w_out, ndc.y * w_out, ndc.z * w_out };
}

std::optional<vec3> hud_frame::screen( const vec3 &tile ) const
{
    float w_clip = 0.0f;
    const vec3 ndc = transform_point( view_proj, tile_to_world( tile ), &w_clip );
    if( w_clip <= 1e-4f ) {
        return std::nullopt;
    }
    return vec3{ x + ( ndc.x * 0.5f + 0.5f ) * w, y + ( 0.5f - ndc.y * 0.5f ) * h, ndc.z };
}

vec3 hud_frame::tile_point( const tripoint_bub_ms &p, float height ) const
{
    return { static_cast<float>( p.x() - center.x() ) + 0.5f,
             static_cast<float>( p.y() - center.y() ) + 0.5f,
             static_cast<float>( p.z() - center.z() ) + height };
}

std::optional<vec3> hud_frame::screen( const tripoint_bub_ms &p, float height ) const
{
    return screen( tile_point( p, height ) );
}

namespace
{

// Same colours as the flat view's scrolling combat text (msgtype_to_tilecolor in cata_tiles.cpp).
int message_color( game_message_type type, bool old )
{
    const int bright = old ? 0 : 8;
    switch( type ) {
        case m_good:
            return bright + catacurses::green;
        case m_bad:
            return bright + catacurses::red;
        case m_mixed:
        case m_headshot:
            return bright + catacurses::magenta;
        case m_warning:
        case m_critical:
            return bright + catacurses::yellow;
        case m_info:
        case m_grazing:
            return bright + catacurses::blue;
        case m_neutral:
        default:
            return bright + catacurses::white;
    }
}

// Scrolling combat text over the 3D picture. The flat view moves it across tiles; here it
// rises from the head of whoever it is about, and texts to the side (damage numbers to the right,
// health to the left) keep their side.
void add_combat_text( const hud_frame &frame, std::vector<hud_text> &texts )
{
    if( !frame.sct ) {
        return;
    }
    const int z = get_avatar().pos_bub().z();
    constexpr float head_height = 1.1f;
    constexpr float rise_per_step = 7.0f; // logical pixels
    for( const scrollingcombattext::cSCT &sct : SCT.vSCT ) {
        const tripoint_bub_ms at( sct.getInitPosX(), sct.getInitPosY(), z );
        const std::optional<vec3> p = frame.screen( at, head_height );
        if( !p ) {
            continue;
        }
        const int steps = sct.getStep() + sct.getStepOffset();
        const bool old = sct.getStep() >= scrollingcombattext::iMaxSteps / 2;
        const point side = displace_XY( sct.getDirection() );
        const float sx = p->x + static_cast<float>( side.x ) * 18.0f;
        const float sy = p->y - static_cast<float>( steps ) * rise_per_step;
        if( sx < frame.x || sx > frame.x + frame.w || sy < frame.y || sy > frame.y + frame.h ) {
            continue;
        }
        const std::string first = sct.getText( "first" );
        const std::string second = sct.getText( "second" );
        const int align = side.x > 0 ? -1 : side.x < 0 ? 1 : 0;
        if( second.empty() ) {
            texts.push_back( { sx, sy, first, message_color( sct.getMsgType( "first" ), old ), align, false } );
        } else {
            // Two coloured parts on one line: the second starts where the first ends (the caller
            // measures text in cells, so split around x).
            texts.push_back( { sx, sy, first, message_color( sct.getMsgType( "first" ), old ), 1, false } );
            texts.push_back( { sx, sy, second, message_color( sct.getMsgType( "second" ), old ), -1, false } );
        }
    }
}

} // namespace

void draw_hud( SDL_Renderer *renderer, const hud_frame &frame, const atmosphere &air,
               std::vector<hud_text> &texts )
{
    ( void )renderer;
    ( void )air;
    if( get_option<bool>( "R3D_HUD" ) ) {
        add_combat_text( frame, texts );
    }
}

} // namespace r3d

#endif // defined(TILES)
