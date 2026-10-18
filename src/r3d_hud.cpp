#if defined(TILES)

#include "r3d_hud.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <string>
#include <tuple>

#include <SDL3/SDL.h>

#include "avatar.h"
#include "color.h"
#include "cursesdef.h"
#include "calendar.h"
#include "creature.h"
#include "cursesport.h"
#include "display.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "game.h"
#include "line.h"
#include "map.h"
#include "overmap_ui.h"
#include "overmapbuffer.h"
#include "options.h"
#include "output.h"
#include "r3d_hud_math.h"
#include "sounds.h"
#include "string_formatter.h"
#include "translations.h"
#include "weather.h"
#include "widget.h"

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
    return vec3{ x + ( ndc.x * 0.5f + 0.5f ) *w, y + ( 0.5f - ndc.y * 0.5f ) *h, ndc.z };
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

struct rgba8 {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;
};

void fill_triangles( SDL_Renderer *renderer, const std::vector<SDL_FPoint> &points, rgba8 c )
{
    if( points.size() < 3 ) {
        return;
    }
    std::vector<SDL_Vertex> v( points.size() );
    const SDL_FColor color{ c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f };
    for( size_t i = 0; i < points.size(); ++i ) {
        v[i].position = points[i];
        v[i].color = color;
        v[i].tex_coord = SDL_FPoint{ 0.0f, 0.0f };
    }
    SDL_RenderGeometry( renderer, nullptr, v.data(), static_cast<int>( v.size() ), nullptr, 0 );
}

void draw_arrow_head( SDL_Renderer *renderer, float x, float y, float angle, float s, rgba8 col )
{
    const float ca = std::cos( angle );
    const float sa = std::sin( angle );
    const auto at = [ x, y, ca, sa ]( float fx, float fy ) {
        return SDL_FPoint{ x + fx *ca - fy * sa, y + fx *sa + fy * ca };
    };
    fill_triangles( renderer, { at( s, 0.0f ), at( -s * 0.6f, s * 0.7f ), at( -s * 0.6f, -s * 0.7f ) },
                    col );
}

// An arrowhead at (x, y) pointing along angle (screen radians, y down), with a dark outline so
// it reads on any picture.
void draw_arrow( SDL_Renderer *renderer, float x, float y, float angle, float size, rgba8 c )
{

    draw_arrow_head( renderer, x, y, angle, size + 2.0f, rgba8{ 0, 0, 0, 200 } );
    draw_arrow_head( renderer, x, y, angle, size, c );
}

// Pointers at the edge of the view towards hostiles the player sees and sounds the player heard
// that are outside the picture. One arrow per direction (bins of 1/32 turn), the nearest wins.
void draw_edge_arrows( SDL_Renderer *renderer, const hud_frame &frame )
{
    const avatar &you = get_avatar();
    map &here = get_map();
    struct mark {
        edge_mark m;
        bool hostile = false;
        int dist = 0;
        int dz = 0;
    };
    std::vector<mark> marks;
    // Margins: the arrows sit a little inside the picture.
    const float margin_x = 24.0f / std::max( frame.w, 1.0f ) * 2.0f;
    const float margin_y = 24.0f / std::max( frame.h, 1.0f ) * 2.0f;
    const auto consider = [ &frame, margin_x, margin_y, &marks, &you ]( const tripoint_bub_ms & p,
    bool hostile ) {
        float w_clip = 0.0f;
        const vec3 c = frame.clip( frame.tile_point( p, 0.5f ), w_clip );
        const edge_mark m = place_edge_mark( c.x, c.y, w_clip, margin_x, margin_y );
        if( !m.on_screen ) {
            marks.push_back( { m, hostile, rl_dist( you.pos_bub(), p ), p.z() - you.pos_bub().z() } );
        }
    };
    for( Creature &critter : g->all_creatures() ) {
        if( critter.is_avatar() || critter.attitude_to( you ) != Creature::Attitude::HOSTILE ||
            !you.sees( here, critter ) ) {
            continue;
        }
        consider( critter.pos_bub(), true );
    }
    for( const tripoint_bub_ms &p : sounds::get_footstep_markers() ) {
        consider( p, false );
    }
    if( marks.empty() ) {
        return;
    }
    // Hostiles first, then the nearest, so each direction shows the most urgent thing.
    std::sort( marks.begin(), marks.end(), []( const mark & a, const mark & b ) {
        return a.hostile != b.hostile ? a.hostile : a.dist < b.dist;
    } );
    constexpr int bins = 32;
    std::array<bool, bins> used{};
    for( const mark &k : marks ) {
        const float turn = k.m.angle / 6.2831853f + 0.5f;
        const int bin = std::clamp( static_cast<int>( turn * bins ), 0, bins - 1 );
        if( used[static_cast<size_t>( bin )] ) {
            continue;
        }
        used[static_cast<size_t>( bin )] = true;
        const float sx = frame.x + ( k.m.x * 0.5f + 0.5f ) * frame.w;
        const float sy = frame.y + ( 0.5f - k.m.y * 0.5f ) * frame.h;
        // Closer things get bigger arrows; another level dims the arrow.
        const float size = std::clamp( 14.0f - static_cast<float>( k.dist ) * 0.15f, 7.0f, 14.0f );
        rgba8 c = k.hostile ? rgba8{ 235, 50, 40, 255 } :
                  rgba8{ 250, 210, 60, 255 };
        if( k.dz != 0 ) {
            c.a = 170;
        }
        // NDC y is up, the screen's is down.
        draw_arrow( renderer, sx, sy, -k.m.angle, size, c );
    }
}

// Curses colour index (0-15) of an nc_color, as the curses port draws it.
int tile_color( const nc_color &c )
{
    int fg = cata_cursesport::colorpairs[static_cast<size_t>( c.to_color_pair_index() )].FG;
    if( c.is_bold() ) {
        fg += 8;
    }
    return fg;
}

// Overmap notes near the player, refreshed when the turn or the overmap tile changes.
struct near_note {
    tripoint_abs_omt pos;
    char symbol = 'N';
    int color = 7;
    int dist = 0;
};

const std::vector<near_note> &nearby_notes( const tripoint_abs_omt &here_omt )
{
    static std::vector<near_note> notes;
    static time_point last_turn = calendar::before_time_starts;
    static tripoint_abs_omt last_pos = tripoint_abs_omt::invalid;
    if( last_turn == calendar::turn && last_pos == here_omt ) {
        return notes;
    }
    last_turn = calendar::turn;
    last_pos = here_omt;
    notes.clear();
    constexpr int max_dist = 30;
    constexpr size_t max_notes = 4;
    for( const overmapbuffer::t_point_with_note &n : overmap_buffer.get_all_notes( here_omt.z() ) ) {
        const tripoint_abs_omt p( n.first, here_omt.z() );
        const int dist = rl_dist( p, here_omt );
        if( dist < 1 || dist > max_dist ) {
            continue;
        }
        const std::tuple<char, nc_color, size_t> info = overmap_ui::get_note_display_info( n.second );
        notes.push_back( { p, std::get<0>( info ), tile_color( std::get<1>( info ) ), dist } );
    }
    std::sort( notes.begin(), notes.end(), []( const near_note & a, const near_note & b ) {
        return a.dist < b.dist;
    } );
    if( notes.size() > max_notes ) {
        notes.resize( max_notes );
    }
    return notes;
}

// Map bearing from the player to the middle of an overmap tile.
float bearing_to( const tripoint_abs_omt &target )
{
    const tripoint_abs_ms from = get_avatar().pos_abs();
    const tripoint_abs_ms to = project_to<coords::ms>( target ) + tripoint_rel_ms( SEEX, SEEY, 0 );
    return map_bearing( static_cast<float>( to.x() - from.x() ),
                        static_cast<float>( to.y() - from.y() ) );
}

// A compass tape along the top of the view, centred on where the camera looks: the cardinal
// points, the sun and the moon, where the wind comes from, and places the player chose or was
// given: the mission target, the auto-travel destination and the nearest overmap notes. Threats
// are not on it: the edge arrows point at them.
void draw_compass( SDL_Renderer *renderer, const hud_frame &frame, const atmosphere &air,
                   std::vector<hud_text> &texts )
{
    constexpr float pi = 3.14159265f;
    const float half_w = std::min( frame.w * 0.3f, 240.0f );
    const float cx = frame.x + frame.w * 0.5f;
    const float top = frame.y + 4.0f;
    const float height = 18.0f;
    const float forward = camera_forward_bearing( frame.cam.yaw );
    const auto x_of = [ cx, forward, half_w ]( float bearing ) {
        return cx + bearing_delta( forward, bearing ) / pi * half_w;
    };
    SDL_SetRenderDrawBlendMode( renderer, SDL_BLENDMODE_BLEND );
    SDL_SetRenderDrawColor( renderer, 0, 0, 0, 130 );
    const SDL_FRect band{ cx - half_w, top, half_w * 2.0f, height };
    SDL_RenderFillRect( renderer, &band );
    // Ticks every 45 degrees; letters at the cardinal points.
    SDL_SetRenderDrawColor( renderer, 200, 200, 200, 160 );
    static const std::array<std::string, 4> letters = {{ "N", "E", "S", "W" }};
    for( int i = 0; i < 8; ++i ) {
        const float b = static_cast<float>( i ) * pi / 4.0f;
        const float x = x_of( b );
        if( i % 2 == 0 ) {
            texts.push_back( { x, top + 1.0f, letters[static_cast<size_t>( i / 2 )], i == 0 ? 9 : 15, 0, false } );
        } else {
            SDL_RenderLine( renderer, x, top + height - 6.0f, x, top + height );
        }
    }
    // Where the camera looks.
    SDL_SetRenderDrawColor( renderer, 255, 255, 255, 220 );
    SDL_RenderLine( renderer, cx, top + height, cx, top + height + 4.0f );

    // The sun and the moon, drawn with their own sprites just under the tape.
    if( frame.sky && frame.sky->loaded() ) {
        for( const sky_body &b : sky_bodies( frame.view_z, air ) ) {
            const float x = x_of( map_bearing( b.dir.x, -b.dir.y ) );
            const sprite_ref &s = frame.sky->cells[static_cast<size_t>( b.cell )];
            float tw = 0.0f;
            float th = 0.0f;
            SDL_GetTextureSize( frame.sky->sheet.get(), &tw, &th );
            const SDL_FRect src{ s.u0 * tw, s.v0 * th, ( s.u1 - s.u0 ) *tw, ( s.v1 - s.v0 ) *th };
            const SDL_FRect dst{ x - 6.0f, top + height + 1.0f, 12.0f, 12.0f };
            SDL_RenderTexture( renderer, frame.sky->sheet.get(), &src, &dst );
        }
    }
    // The wind: a chevron where it comes from, pointing down the way it blows past.
    const weather_manager &weather = get_weather_const();
    const avatar &you = get_avatar();
    // The wind where the player stands, as the sidebar reports it (cover slows it).
    const int wind = get_local_windpower( weather.windspeed, overmap_buffer.ter( you.pos_abs_omt() ),
                                          you.pos_abs(), weather.winddirection, g->is_sheltered( you.pos_bub() ) );
    if( frame.view_z >= 0 && wind >= 3 ) {
        const float b = static_cast<float>( weather.winddirection ) * pi / 180.0f;
        draw_arrow( renderer, x_of( b ), top + height + 6.0f, pi * 0.5f,
                    wind >= 25 ? 7.0f : 5.0f, rgba8{ 140, 210, 255, 230 } );
    }
    // Places: notes first so the mission and the destination are drawn over them.
    const tripoint_abs_omt here_omt = you.pos_abs_omt();
    for( const near_note &n : nearby_notes( here_omt ) ) {
        texts.push_back( { x_of( bearing_to( n.pos ) ), top + height + 2.0f, std::string( 1, n.symbol ),
                           n.color, 0, true } );
    }
    if( !you.omt_path.empty() && you.omt_path.front() != here_omt ) {
        texts.push_back( { x_of( bearing_to( you.omt_path.front() ) ), top + height + 2.0f, "X", 10, 0, true } );
    }
    const tripoint_abs_omt mission = you.get_active_mission_target();
    if( !mission.is_invalid() && mission.xy() != here_omt.xy() ) {
        const float x = x_of( bearing_to( mission ) );
        const float y = top + height * 0.5f;
        fill_triangles( renderer, { SDL_FPoint{ x, y - 7.0f }, SDL_FPoint{ x + 6.0f, y }, SDL_FPoint{ x - 6.0f, y },
                                    SDL_FPoint{ x - 6.0f, y }, SDL_FPoint{ x + 6.0f, y }, SDL_FPoint{ x, y + 7.0f }
                                  }, rgba8{ 255, 200, 40, 255 } );
    }
}

// Badges for needs and conditions that are not normal, along the bottom left of the view. Which
// widgets become badges, and their text and colour, come from the "r3d_status_row" widget in
// data/json/ui/sidebar-r3d.json: the thresholds stay in the widgets' own clauses. A widget whose
// text is empty or shown in a "fine" colour (white, green) is normal and has no badge.
void add_status_badges( const hud_frame &frame, std::vector<hud_text> &texts )
{
    static const widget_id row_id( "r3d_status_row" );
    if( !row_id.is_valid() ) {
        return;
    }
    const avatar &you = get_avatar();
    float x = frame.x + 8.0f;
    const float y = frame.y + frame.h - frame.cell_h - 8.0f;
    for( const widget_id &child : row_id->_widgets ) {
        if( !child.is_valid() ) {
            continue;
        }
        widget w = child.obj();
        const std::string shown = w.layout( you, 40, 0, true );
        nc_color color = c_white;
        for( const std::string &part : split_by_color( shown ) ) {
            if( part.size() > 1 && part[0] == '<' ) {
                const color_tag_parse_result tag = get_color_from_tag( part, report_color_error::no );
                if( tag.type == color_tag_parse_result::open_color_tag ) {
                    color = tag.color;
                    break;
                }
            }
        }
        std::string text = remove_color_tags( shown );
        text.erase( 0, text.find_first_not_of( ' ' ) );
        text.erase( text.find_last_not_of( ' ' ) + 1 );
        if( text.empty() || color == c_white || color == c_green || color == c_light_green ) {
            continue;
        }
        texts.push_back( { x, y, text, tile_color( color ), -1, true } );
        x += static_cast<float>( utf8_width( text ) ) * frame.cell_w + 14.0f;
    }
}

// Listens on the game's event bus for what the overlays react to: the player being hurt and the
// player entering another overmap tile.
class hud_events : public event_subscriber
{
    public:
        void notify( const cata::event &e ) override {
            if( e.type() == event_type::character_takes_damage ) {
                const avatar &you = get_avatar();
                if( e.get<character_id>( "character" ) != you.getID() || e.get<int>( "damage" ) <= 0 ) {
                    return;
                }
                hit_ticks = SDL_GetTicks();
                hit_from = guess_attacker( you );
            } else if( e.type() == event_type::avatar_enters_omt ) {
                const std::string name = display::current_position_text( tripoint_abs_omt(
                                             e.get<tripoint>( "pos" ) ) );
                if( name != place ) {
                    place = name;
                    place_ticks = SDL_GetTicks();
                }
            }
        }

        uint64_t hit_ticks = 0;
        std::optional<tripoint_bub_ms> hit_from;
        uint64_t place_ticks = 0;
        std::string place;

    private:
        // The event does not say who hit; take the nearest hostile the player can see, preferring
        // one within reach. Nothing when none is seen: then the hit has no direction.
        static std::optional<tripoint_bub_ms> guess_attacker( const avatar &you ) {
            map &here = get_map();
            std::optional<tripoint_bub_ms> best;
            int best_dist = 61;
            for( Creature &critter : g->all_creatures() ) {
                if( critter.is_avatar() || critter.attitude_to( you ) != Creature::Attitude::HOSTILE ||
                    !you.sees( here, critter ) ) {
                    continue;
                }
                const int dist = rl_dist( you.pos_bub(), critter.pos_bub() );
                if( dist < best_dist ) {
                    best_dist = dist;
                    best = critter.pos_bub();
                }
            }
            return best;
        }
};

hud_events &events()
{
    // Leaked like the renderer's state: it must outlive the game's event bus, which unsubscribes
    // it when it goes away.
    static hud_events *ev = new hud_events();
    static const event_bus *bus = nullptr;
    event_bus &current = get_event_bus();
    if( bus != &current ) {
        current.subscribe( ev );
        bus = &current;
    }
    return *ev;
}

// A red glow at the edge of the view on the side the hit came from, fading over a second; all
// around the edge when the attacker is not seen.
void draw_hit_direction( SDL_Renderer *renderer, const hud_frame &frame )
{
    const hud_events &ev = events();
    constexpr uint64_t fade_ms = 1000;
    const uint64_t now = SDL_GetTicks();
    if( ev.hit_ticks == 0 || now - ev.hit_ticks > fade_ms ) {
        return;
    }
    const float strength = 1.0f - static_cast<float>( now - ev.hit_ticks ) / fade_ms;
    const SDL_FColor outer{ 0.9f, 0.05f, 0.05f, 0.75f * strength };
    const SDL_FColor inner{ 0.9f, 0.05f, 0.05f, 0.0f };
    const auto quad = [ &outer, &inner, renderer ]( SDL_FPoint a, SDL_FPoint b, SDL_FPoint c,
    SDL_FPoint d ) {
        // a-b on the edge (opaque), c-d inside (clear).
        const SDL_Vertex v[6] = {
            { a, outer, {} }, { b, outer, {} }, { c, inner, {} },
            { a, outer, {} }, { c, inner, {} }, { d, inner, {} }
        };
        SDL_RenderGeometry( renderer, nullptr, v, 6, nullptr, 0 );
    };
    const float x0 = frame.x;
    const float y0 = frame.y;
    const float x1 = frame.x + frame.w;
    const float y1 = frame.y + frame.h;
    const avatar &you = get_avatar();
    std::optional<vec3> from_screen;
    std::optional<vec3> me = frame.screen( you.pos_bub(), 0.5f );
    if( ev.hit_from ) {
        from_screen = frame.screen( *ev.hit_from, 0.5f );
    }
    if( !from_screen || !me || ( std::fabs( from_screen->x - me->x ) < 1.0f &&
                                 std::fabs( from_screen->y - me->y ) < 1.0f ) ) {
        const float d = 24.0f;
        quad( { x0, y0 }, { x1, y0 }, { x1 - d, y0 + d }, { x0 + d, y0 + d } );
        quad( { x1, y0 }, { x1, y1 }, { x1 - d, y1 - d }, { x1 - d, y0 + d } );
        quad( { x1, y1 }, { x0, y1 }, { x0 + d, y1 - d }, { x1 - d, y1 - d } );
        quad( { x0, y1 }, { x0, y0 }, { x0 + d, y0 + d }, { x0 + d, y1 - d } );
        return;
    }
    // Where the line from the middle of the view along the attacker's direction leaves it.
    const float dx = from_screen->x - me->x;
    const float dy = from_screen->y - me->y;
    const edge_mark m = place_edge_mark( dx, -dy, 1e-3f, 0.0f, 0.0f );
    const float ex = frame.x + ( m.x * 0.5f + 0.5f ) * frame.w;
    const float ey = frame.y + ( 0.5f - m.y * 0.5f ) * frame.h;
    const float len = std::sqrt( dx * dx + dy * dy );
    const float ux = dx / len;
    const float uy = dy / len;
    // Across the direction, and inwards.
    const float half = std::min( frame.w, frame.h ) * 0.22f;
    const float depth = 40.0f;
    const SDL_FPoint a{ ex - uy * half, ey + ux * half };
    const SDL_FPoint b{ ex + uy * half, ey - ux * half };
    quad( a, b, SDL_FPoint{ b.x - ux * depth, b.y - uy * depth }, SDL_FPoint{ a.x - ux * depth, a.y - uy * depth } );
}

// "Entering: <place>" under the compass for a few seconds when the place the sidebar names
// changes (not on every overmap tile of a forest).
void add_place_notice( const hud_frame &frame, std::vector<hud_text> &texts )
{
    const hud_events &ev = events();
    constexpr uint64_t show_ms = 3500;
    if( ev.place_ticks == 0 || SDL_GetTicks() - ev.place_ticks > show_ms || ev.place.empty() ) {
        return;
    }
    texts.push_back( { frame.x + frame.w * 0.5f, frame.y + 44.0f,
                       string_format( _( "Entering: %s" ), ev.place ), 15, 0, true } );
}

} // namespace

void draw_hud( SDL_Renderer *renderer, const hud_frame &frame, const atmosphere &air,
               std::vector<hud_text> &texts )
{
    if( get_option<bool>( "R3D_HUD" ) ) {
        add_combat_text( frame, texts );
        draw_edge_arrows( renderer, frame );
        draw_compass( renderer, frame, air, texts );
        add_status_badges( frame, texts );
        draw_hit_direction( renderer, frame );
        add_place_notice( frame, texts );
    } else {
        // Keep listening so a notice is not shown late when the option comes back on.
        events();
    }
}

} // namespace r3d

#endif // defined(TILES)
