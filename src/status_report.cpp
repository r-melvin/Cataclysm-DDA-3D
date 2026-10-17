#include "status_report.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "creature.h"
#include "display.h"
#include "game.h"
#include "line.h"
#include "map.h"
#include "overmapbuffer.h"
#include "output.h"
#include "sounds.h"
#include "string_formatter.h"
#include "translations.h"
#include "weather.h"

namespace
{

std::string moon_phase_name( moon_phase phase )
{
    switch( phase ) {
        case MOON_NEW:
            return _( "new moon" );
        case MOON_WAXING_CRESCENT:
            return _( "waxing crescent moon" );
        case MOON_HALF_MOON_WAXING:
            return _( "waxing half moon" );
        case MOON_WAXING_GIBBOUS:
            return _( "waxing gibbous moon" );
        case MOON_FULL:
            return _( "full moon" );
        case MOON_WANING_GIBBOUS:
            return _( "waning gibbous moon" );
        case MOON_HALF_MOON_WANING:
            return _( "waning half moon" );
        case MOON_WANING_CRESCENT:
        default:
            return _( "waning crescent moon" );
    }
}

// "Hungry" etc.: the sidebar's own text for a need, left out when it is normal (empty or a
// colour the sidebar uses for "fine").
void add_need( std::vector<std::string> &out, const std::pair<std::string, nc_color> &need )
{
    const std::string text = remove_color_tags( need.first );
    if( text.empty() || need.second == c_white || need.second == c_green ||
        need.second == c_light_green ) {
        return;
    }
    out.push_back( text );
}

std::string join( const std::vector<std::string> &parts )
{
    return enumerate_as_string( parts.begin(), parts.end(), []( const std::string & s ) {
        return s;
    } );
}

} // namespace

std::string status_report( const avatar &you )
{
    std::string out;
    const bool sky = you.pos_bub().z() >= 0 && can_creature_see_sky( you );
    // Time and date.
    out += string_format( _( "%s, %s." ), display::date_string(), display::time_string( you ) );
    // Weather overhead, wind, temperature.
    if( you.pos_bub().z() < 0 ) {
        out += " " + std::string( _( "You are underground." ) );
    } else {
        out += " " + string_format( _( "Weather: %s." ), display::weather_text_color( you ).first );
        const weather_manager &weather = get_weather_const();
        // The wind where the player stands, as the sidebar reports it (cover slows it).
        const int wind = get_local_windpower( weather.windspeed, overmap_buffer.ter( you.pos_abs_omt() ),
                                              you.pos_abs(), weather.winddirection, g->is_sheltered( you.pos_bub() ) );
        if( wind < 3 ) {
            out += " " + std::string( _( "The air is calm." ) );
        } else {
            out += " " + string_format( _( "Wind %d mph from the %s." ), wind,
                                        get_dirstring( weather.winddirection ) );
        }
        if( sky && is_night( calendar::turn ) ) {
            out += " " + string_format( _( "The sky shows a %s." ),
                                        moon_phase_name( get_moon_phase( calendar::turn ) ) );
        }
    }
    const std::string temp = display::get_temp( you );
    if( temp != "-" ) {
        out += " " + string_format( _( "Temperature %s." ), temp );
    }
    // Where.
    out += " " + string_format( _( "Place: %s." ),
                                display::current_position_text( you.pos_abs_omt() ) );
    // Needs and conditions that are not normal.
    std::vector<std::string> needs;
    add_need( needs, display::hunger_text_color( you ) );
    add_need( needs, display::thirst_text_color( you ) );
    add_need( needs, display::sleepiness_text_color( you ) );
    add_need( needs, display::pain_text_color( you ) );
    add_need( needs, display::weariness_text_color( you ) );
    out += " " + ( needs.empty() ? std::string( _( "No pressing needs." ) ) :
                   string_format( _( "You are %s." ), join( needs ) ) );
    // Hostiles in sight, nearest first.
    map &here = get_map();
    std::vector<std::pair<int, std::string>> hostiles;
    for( Creature &critter : g->all_creatures() ) {
        if( critter.is_avatar() || critter.attitude_to( you ) != Creature::Attitude::HOSTILE ||
            !you.sees( here, critter ) ) {
            continue;
        }
        const int dist = rl_dist( you.pos_bub(), critter.pos_bub() );
        std::string where = direction_name( direction_from( you.pos_bub().raw(), critter.pos_bub().raw() ) );
        hostiles.emplace_back( dist, string_format( _( "%1$s %2$d tiles %3$s" ), critter.get_name(), dist,
                               where ) );
    }
    std::sort( hostiles.begin(), hostiles.end() );
    if( hostiles.empty() ) {
        out += " " + std::string( _( "No hostiles in sight." ) );
    } else {
        std::vector<std::string> names;
        for( size_t i = 0; i < hostiles.size() && i < 6; ++i ) {
            names.push_back( hostiles[i].second );
        }
        if( hostiles.size() > 6 ) {
            names.push_back( string_format( _( "%d more" ), static_cast<int>( hostiles.size() - 6 ) ) );
        }
        out += " " + string_format( _( "Hostiles in sight: %s." ), join( names ) );
    }
    // Sounds heard this turn.
    std::vector<std::string> heard;
    for( const tripoint_bub_ms &p : sounds::get_footstep_markers() ) {
        const std::string what = sounds::sound_at( p );
        const std::string where = direction_name( direction_from( you.pos_bub().raw(), p.raw() ) );
        heard.push_back( what.empty() ? string_format( _( "something %s" ), where ) :
                         string_format( _( "%1$s %2$s" ), what, where ) );
        if( heard.size() >= 4 ) {
            break;
        }
    }
    if( !heard.empty() ) {
        out += " " + string_format( _( "You hear %s." ), join( heard ) );
    }
    return out;
}
