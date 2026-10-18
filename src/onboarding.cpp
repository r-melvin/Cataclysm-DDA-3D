#include "onboarding.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "action.h"
#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_scope_helpers.h"
#include "cata_variant.h"
#include "character.h"
#include "character_id.h"
#include "color.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "debug.h"
#include "dialogue.h"
#include "enum_conversions.h"
#include "event.h"
#include "event_subscriber.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "generic_factory.h"
#include "input_context.h"
#include "item.h"
#include "item_location.h"
#include "json.h"
#include "map.h"
#include "mapdata.h"
#include "messages.h"
#include "monster.h"
#include "onboarding_core.h"
#include "options.h"
#include "output.h"
#include "npc.h"
#include "point.h"
#include "popup.h"
#include "scenario.h"
#include "string_formatter.h"
#include "talker.h"
#include "translation.h"
#include "translations.h"
#include "type_id.h"
#include "uilist.h"
#include "ui_manager.h"
#include "units.h"
#include "worldfactory.h"

class onboarding_lesson;
using onboarding_lesson_id = string_id<onboarding_lesson>;
class onboarding_screen;
using onboarding_screen_id = string_id<onboarding_screen>;

// Text for a particular UI, such as the 3D view.  The first variant whose flags all hold
// replaces the title, short and text it sets; every variant whose flags hold adds its "extra"
// paragraph after the text.
struct ob_text_variant {
    std::vector<std::string> when;
    std::optional<translation> title;
    std::optional<translation> short_text;
    std::optional<translation> text;
    std::optional<translation> extra;
};

static void load_variants( const JsonObject &jo, std::vector<ob_text_variant> &out );
static const translation &pick_title( const translation &base,
                                      const std::vector<ob_text_variant> &variants );
static const translation &pick_short( const translation &base,
                                      const std::vector<ob_text_variant> &variants );
static std::string pick_text( const translation &base,
                              const std::vector<ob_text_variant> &variants );
static void check_ui_flags( const std::string &what, const std::vector<std::string> &flags,
                            const std::vector<ob_text_variant> &variants );

// One lesson as loaded from JSON (type "onboarding_lesson").
class onboarding_lesson
{
    public:
        onboarding_lesson_id id;
        std::vector<std::pair<onboarding_lesson_id, mod_id>> src;
        bool was_loaded = false;

        int phase = 1;
        int order = 0;
        ob_presentation presentation = ob_presentation::hint;
        translation title;
        // One line for the sidebar and for reminders.
        translation short_text;
        // The full explanation.
        translation text;
        std::vector<std::string> prerequisites;
        std::vector<ob_matcher> trigger;
        std::vector<ob_matcher> done_when;
        int snooze_turns = 600;
        // UI flags (see ui_flag()) that must all hold for the lesson to apply at all.
        std::vector<std::string> only_when;
        // An action the lesson is about; without it (a build that lacks it, or no key bound)
        // the lesson does not apply.
        std::string requires_action;
        // Text for particular UIs; see ob_text_variant.
        std::vector<ob_text_variant> variants;
        // Dialogue conditions used by "condition" matchers, by index.
        std::vector<std::function<bool( const_dialogue const & )>> conditions;

        void load( const JsonObject &jo, std::string_view );
        void check() const;
        ob_lesson_def def() const;
        // Whether the lesson applies to the current game and UI.
        bool available() const;
        // Title, short line and text for the UI in use.
        const translation &get_title() const;
        const translation &get_short() const;
        // The full text for the UI in use, with the extra paragraphs of matching variants.
        std::string get_text() const;

    private:
        void load_matchers( const JsonObject &jo, const std::string &member,
                            std::vector<ob_matcher> &out );
};

// What "Explain this screen" says for one input context (type "onboarding_screen"; the id is
// the input context's category, such as "INVENTORY").
class onboarding_screen
{
    public:
        onboarding_screen_id id;
        std::vector<std::pair<onboarding_screen_id, mod_id>> src;
        bool was_loaded = false;

        translation title;
        translation text;
        std::vector<ob_text_variant> variants;

        void load( const JsonObject &jo, std::string_view ) {
            mandatory( jo, was_loaded, "title", title );
            mandatory( jo, was_loaded, "text", text );
            if( jo.has_array( "variants" ) ) {
                load_variants( jo, variants );
            }
        }
        void check() const {
            check_ui_flags( "onboarding_screen " + id.str(), {}, variants );
        }
};

namespace
{

generic_factory<onboarding_lesson> lesson_factory( "onboarding_lesson" );
generic_factory<onboarding_screen> screen_factory( "onboarding_screen" );

// States the game can poll for, used by "state" matchers.
const std::set<std::string> &known_states()
{
    static const std::set<std::string> states = {
        "standing_on_items", "items_nearby", "adjacent_door", "adjacent_examinable",
        "hostile_in_view", "worn_storage", "dark_here", "carries_book", "low_morale",
        "wields_gun", "corpse_here", "is_wet", "adjacent_vehicle", "npc_in_view", "on_stairs"
    };
    return states;
}

// Signals the game sends, used by "signal" matchers.
const std::set<std::string> &known_signals()
{
    static const std::set<std::string> signals = {
        "safe_mode_spotted", "avatar_died"
    };
    return signals;
}

// Option helpers that treat a missing option as off, so this file builds and runs on branches
// with fewer 3D options.
bool option_on( const std::string &name )
{
    return has_option( name ) && get_option<bool>( name );
}

std::string option_value( const std::string &name )
{
    return has_option( name ) ? get_option<std::string>( name ) : std::string();
}

// UI flags lessons can branch on ("only_when", "variants").  See doc/ONBOARDING_BABY_MODE.md.
const std::set<std::string> &known_ui_flags()
{
    static const std::set<std::string> flags = {
        "classic", "r3d", "r3d_orbit", "r3d_first_person", "r3d_wasd", "r3d_cutaway",
        "r3d_compass", "r3d_edge_alert"
    };
    return flags;
}

bool ui_flag( const std::string &flag )
{
    const bool r3d = option_on( "RENDERER_3D" );
    const bool first_person = r3d && option_value( "R3D_CAMERA_MODE" ) == "first_person";
    if( flag == "classic" ) {
        return !r3d;
    } else if( flag == "r3d" ) {
        return r3d;
    } else if( flag == "r3d_orbit" ) {
        return r3d && !first_person;
    } else if( flag == "r3d_first_person" ) {
        return first_person;
    } else if( flag == "r3d_wasd" ) {
        return r3d && option_on( "R3D_WASD" );
    } else if( flag == "r3d_cutaway" ) {
        return r3d && !first_person && option_on( "R3D_CUTAWAY" );
    } else if( flag == "r3d_compass" ) {
        const std::string compass = option_value( "R3D_FP_COMPASS" );
        return first_person && !compass.empty() && compass != "off";
    } else if( flag == "r3d_edge_alert" ) {
        return first_person && option_on( "R3D_FP_EDGE_ALERT" );
    }
    return false;
}

bool all_ui_flags( const std::vector<std::string> &flags )
{
    return std::all_of( flags.begin(), flags.end(), ui_flag );
}

// Whether an action exists in this build and has a key.
bool action_bound( const std::string &action )
{
    const input_context ctxt( "DEFAULTMODE" );
    return !ctxt.keys_bound_to( action, -1, false, false ).empty();
}

} // namespace

/** @relates string_id */
template<>
const onboarding_lesson &string_id<onboarding_lesson>::obj() const
{
    return lesson_factory.obj( *this );
}

/** @relates string_id */
template<>
bool string_id<onboarding_lesson>::is_valid() const
{
    return lesson_factory.is_valid( *this );
}

/** @relates string_id */
template<>
const onboarding_screen &string_id<onboarding_screen>::obj() const
{
    return screen_factory.obj( *this );
}

/** @relates string_id */
template<>
bool string_id<onboarding_screen>::is_valid() const
{
    return screen_factory.is_valid( *this );
}

static void load_variants( const JsonObject &jo, std::vector<ob_text_variant> &out )
{
    out.clear();
    for( JsonObject vo : jo.get_array( "variants" ) ) {
        ob_text_variant v;
        vo.read( "when", v.when );
        translation t;
        if( vo.read( "title", t ) ) {
            v.title = t;
        }
        if( vo.read( "short", t ) ) {
            v.short_text = t;
        }
        if( vo.read( "text", t ) ) {
            v.text = t;
        }
        if( vo.read( "extra", t ) ) {
            v.extra = t;
        }
        out.push_back( v );
    }
}

static const translation &pick_title( const translation &base,
                                      const std::vector<ob_text_variant> &variants )
{
    for( const ob_text_variant &v : variants ) {
        if( v.title && all_ui_flags( v.when ) ) {
            return *v.title;
        }
    }
    return base;
}

static const translation &pick_short( const translation &base,
                                      const std::vector<ob_text_variant> &variants )
{
    for( const ob_text_variant &v : variants ) {
        if( v.short_text && all_ui_flags( v.when ) ) {
            return *v.short_text;
        }
    }
    return base;
}

static std::string pick_text( const translation &base,
                              const std::vector<ob_text_variant> &variants )
{
    const translation *chosen = &base;
    for( const ob_text_variant &v : variants ) {
        if( v.text && all_ui_flags( v.when ) ) {
            chosen = &*v.text;
            break;
        }
    }
    std::string ret = chosen->translated();
    for( const ob_text_variant &v : variants ) {
        if( v.extra && all_ui_flags( v.when ) ) {
            ret += "\n\n" + v.extra->translated();
        }
    }
    return ret;
}

static void check_ui_flags( const std::string &what, const std::vector<std::string> &flags,
                            const std::vector<ob_text_variant> &variants )
{
    const auto check = [&what]( const std::vector<std::string> &list ) {
        for( const std::string &f : list ) {
            if( !known_ui_flags().count( f ) ) {
                debugmsg( "%s uses unknown UI flag %s", what, f );
            }
        }
    };
    check( flags );
    for( const ob_text_variant &v : variants ) {
        check( v.when );
    }
}

void onboarding_lesson::load_matchers( const JsonObject &jo, const std::string &member,
                                       std::vector<ob_matcher> &out )
{
    if( !jo.has_member( member ) ) {
        return;
    }
    out.clear();
    for( JsonObject m : jo.get_array( member ) ) {
        ob_matcher match;
        match.count = m.get_int( "count", 1 );
        if( m.has_member( "start" ) ) {
            match.kind = ob_input_kind::start;
            m.get_bool( "start" );
        } else if( m.has_member( "action" ) ) {
            match.kind = ob_input_kind::action;
            match.name = m.get_string( "action" );
        } else if( m.has_member( "event" ) ) {
            match.kind = ob_input_kind::event;
            match.name = m.get_string( "event" );
        } else if( m.has_member( "signal" ) ) {
            match.kind = ob_input_kind::signal;
            match.name = m.get_string( "signal" );
        } else if( m.has_member( "state" ) ) {
            match.kind = ob_input_kind::state;
            match.name = m.get_string( "state" );
        } else if( m.has_member( "after_turns" ) ) {
            match.kind = ob_input_kind::after_turns;
            match.count = m.get_int( "after_turns" );
        } else if( m.has_member( "condition" ) ) {
            match.kind = ob_input_kind::condition;
            std::function<bool( const_dialogue const & )> cond;
            read_condition( m, "condition", cond, false );
            match.condition_index = static_cast<int>( conditions.size() );
            conditions.push_back( cond );
        } else {
            m.throw_error( "a matcher needs one of start, action, event, signal, state, "
                           "after_turns or condition" );
        }
        match.while_shown = m.get_bool( "while_shown", false );
        out.push_back( match );
    }
}

void onboarding_lesson::load( const JsonObject &jo, std::string_view )
{
    optional( jo, was_loaded, "phase", phase, 1 );
    optional( jo, was_loaded, "order", order, 0 );
    if( jo.has_member( "presentation" ) ) {
        const std::string p = jo.get_string( "presentation" );
        if( p == "hint" ) {
            presentation = ob_presentation::hint;
        } else if( p == "popup" ) {
            presentation = ob_presentation::popup;
        } else if( p == "silent" ) {
            presentation = ob_presentation::silent;
        } else {
            jo.throw_error_at( "presentation", "expected hint, popup or silent" );
        }
    }
    mandatory( jo, was_loaded, "title", title );
    mandatory( jo, was_loaded, "short", short_text );
    mandatory( jo, was_loaded, "text", text );
    optional( jo, was_loaded, "prerequisites", prerequisites );
    optional( jo, was_loaded, "snooze_turns", snooze_turns, 600 );
    optional( jo, was_loaded, "only_when", only_when );
    optional( jo, was_loaded, "requires_action", requires_action );
    if( jo.has_array( "variants" ) ) {
        load_variants( jo, variants );
    }
    load_matchers( jo, "trigger", trigger );
    load_matchers( jo, "done_when", done_when );
}

bool onboarding_lesson::available() const
{
    return all_ui_flags( only_when ) &&
           ( requires_action.empty() || action_bound( requires_action ) );
}

const translation &onboarding_lesson::get_title() const
{
    return pick_title( title, variants );
}

const translation &onboarding_lesson::get_short() const
{
    return pick_short( short_text, variants );
}

std::string onboarding_lesson::get_text() const
{
    return pick_text( text, variants );
}

void onboarding_lesson::check() const
{
    for( const std::string &pre : prerequisites ) {
        if( !onboarding_lesson_id( pre ).is_valid() ) {
            debugmsg( "onboarding_lesson %s has unknown prerequisite %s", id.str(), pre );
        }
    }
    const auto check_list = [this]( const std::vector<ob_matcher> &list, bool is_trigger ) {
        for( const ob_matcher &m : list ) {
            switch( m.kind ) {
                case ob_input_kind::action:
                    // Actions of lessons for a particular UI or build (such as the 3D view's
                    // first-person keys) may be missing from this build on purpose.
                    if( only_when.empty() && m.name != requires_action &&
                        look_up_action( m.name ) == ACTION_NULL ) {
                        debugmsg( "onboarding_lesson %s uses unknown action %s", id.str(), m.name );
                    }
                    break;
                case ob_input_kind::event:
                    if( !io::enum_is_valid<event_type>( m.name ) ) {
                        debugmsg( "onboarding_lesson %s uses unknown event %s", id.str(), m.name );
                    }
                    break;
                case ob_input_kind::signal:
                    if( !known_signals().count( m.name ) ) {
                        debugmsg( "onboarding_lesson %s uses unknown signal %s", id.str(), m.name );
                    }
                    break;
                case ob_input_kind::state:
                    if( !known_states().count( m.name ) ) {
                        debugmsg( "onboarding_lesson %s uses unknown state %s", id.str(), m.name );
                    }
                    break;
                case ob_input_kind::start:
                    if( !is_trigger ) {
                        debugmsg( "onboarding_lesson %s uses start in done_when", id.str() );
                    }
                    break;
                case ob_input_kind::after_turns:
                case ob_input_kind::condition:
                    break;
            }
        }
    };
    check_list( trigger, true );
    check_list( done_when, false );
    check_ui_flags( "onboarding_lesson " + id.str(), only_when, variants );
}

ob_lesson_def onboarding_lesson::def() const
{
    ob_lesson_def d;
    d.id = id.str();
    d.phase = phase;
    d.order = order;
    d.presentation = presentation;
    d.prerequisites = prerequisites;
    d.trigger = trigger;
    d.done_when = done_when;
    d.snooze_turns = snooze_turns;
    return d;
}

namespace
{

std::string status_to_string( ob_status s )
{
    switch( s ) {
        case ob_status::locked:
            return "locked";
        case ob_status::waiting:
            return "waiting";
        case ob_status::queued:
            return "queued";
        case ob_status::active:
            return "active";
        case ob_status::done:
            return "done";
    }
    return "locked";
}

ob_status status_from_string( const std::string &s )
{
    for( ob_status st : {
             ob_status::locked, ob_status::waiting, ob_status::queued, ob_status::active,
             ob_status::done
         } ) {
        if( status_to_string( st ) == s ) {
            return st;
        }
    }
    return ob_status::locked;
}

int64_t now()
{
    return to_turn<int>( calendar::turn );
}

// How much of baby mode is on for this game.
enum class ob_level : int {
    off,
    hints,
    full
};

// A friendlier name for a key, or an empty string for keys not worth naming next to the main
// one (the keypad copies of Enter and the number keys).
std::string key_name( const std::string &raw )
{
    if( raw.rfind( "KEYPAD_", 0 ) == 0 ) {
        return std::string();
    } else if( raw == "RETURN" ) {
        return _( "Enter" );
    } else if( raw == "ESC" ) {
        return _( "Esc" );
    } else if( raw == "SPACE" ) {
        return _( "Space" );
    } else if( raw == "TAB" ) {
        return _( "Tab" );
    } else if( raw == "BACKSPACE" ) {
        return _( "Backspace" );
    }
    return raw;
}

// Expands <keybind:ACTION> and <keybind:CATEGORY:ACTION> into the keys currently bound, like
// the tags in help and dialogue text, but naming at most two keys so the hints stay short.
std::string expand_keys( const std::string &raw )
{
    std::string ret = raw;
    static const std::string open = "<keybind:";
    size_t pos = 0;
    while( ( pos = ret.find( open, pos ) ) != std::string::npos ) {
        const size_t end = ret.find( '>', pos );
        if( end == std::string::npos ) {
            break;
        }
        const std::string spec = ret.substr( pos + open.size(), end - pos - open.size() );
        std::string category = "DEFAULTMODE";
        std::string action = spec;
        const size_t split = spec.find( ':' );
        if( split != std::string::npos ) {
            category = spec.substr( 0, split );
            action = spec.substr( split + 1 );
        }
        const input_context ctxt( category );
        const std::vector<input_event> keys = ctxt.keys_bound_to( action, -1, false, true );
        std::vector<std::string> names;
        // Prefer the keys as typed (capital W rather than shift+w).
        for( const input_event_t type : {
                 input_event_t::keyboard_char, input_event_t::keyboard_code
             } ) {
            for( const input_event &k : keys ) {
                const std::string name = key_name( k.long_description() );
                if( name.empty() ) {
                    continue;
                }
                if( k.type == type && names.size() < 2 &&
                    std::find( names.begin(), names.end(), name ) == names.end() ) {
                    names.push_back( name );
                }
            }
            if( !names.empty() ) {
                break;
            }
        }
        // With the 3D view's WASD controls on, w a s d walk instead of doing what they are bound
        // to, so do not offer them for anything else.
        bool taken_by_wasd = false;
        if( !names.empty() && ui_flag( "r3d_wasd" ) ) {
            names.erase( std::remove_if( names.begin(), names.end(), []( const std::string & n ) {
                return n == "w" || n == "a" || n == "s" || n == "d";
            } ), names.end() );
            taken_by_wasd = names.empty();
        }
        std::string replacement;
        if( taken_by_wasd ) {
            //~ Shown in a hint when the action's key walks in the 3D view; %s is the action menu key.
            replacement = string_format( _( "the action menu (%s)" ),
                                         expand_keys( "<keybind:action_menu>" ) );
        } else if( names.empty() ) {
            //~ Shown in a hint for an action that has no key; %s is the action's name.
            replacement = colorize( string_format( _( "(no key set for \"%s\")" ),
                                                   ctxt.get_action_name( action ) ), c_red );
        } else {
            for( size_t i = 0; i < names.size(); ++i ) {
                if( i > 0 ) {
                    //~ Between two keys in a hint: "y or 7".
                    replacement += _( " or " );
                }
                replacement += colorize( names[i], c_yellow );
            }
        }
        ret.replace( pos, end - pos + 1, replacement );
        pos += replacement.size();
    }
    return ret;
}

// Wraps text to the given width, keeping its paragraphs, for menus that do not wrap.
std::string wrap_text( const std::string &text, int width )
{
    std::string ret;
    size_t start = 0;
    while( start <= text.size() ) {
        size_t end = text.find( '\n', start );
        if( end == std::string::npos ) {
            end = text.size();
        }
        const std::string para = text.substr( start, end - start );
        if( para.empty() ) {
            ret += "\n";
        } else {
            for( const std::string &line : foldstring( para, width ) ) {
                ret += line + "\n";
            }
        }
        start = end + 1;
    }
    while( !ret.empty() && ret.back() == '\n' ) {
        ret.pop_back();
    }
    return ret;
}

// The message log shows one line per message, so paragraphs become one line.
std::string flatten( const std::string &text )
{
    std::string ret;
    ret.reserve( text.size() );
    for( size_t i = 0; i < text.size(); ++i ) {
        if( text[i] != '\n' ) {
            ret += text[i];
        } else if( !ret.empty() && ret.back() != ' ' ) {
            ret += ' ';
        }
    }
    return ret;
}

class manager : public event_subscriber
{
    public:
        void notify( const cata::event &e ) override;
        using event_subscriber::notify;

        // Builds the tracker from the loaded lessons, keeping any progress already known.
        void ensure_built();
        void invalidate();
        void reset_progress();
        // Marks lessons that do not apply to the current UI or build (see only_when).
        void refresh_availability();
        ob_level level() const;
        bool poll( size_t lesson, const ob_matcher &match ) const;
        // Shows pending notices; returns whether a popup was answered.
        bool present();
        void show_popup( size_t lesson );
        void log_hint( size_t lesson, bool reminder );

        onboarding_tracker tracker;
        // The lesson behind each tracker index.
        std::vector<onboarding_lesson_id> lesson_ids;
        bool built = false;
        // Progress waiting for the lessons to be loaded, or kept across a data reload.
        std::map<std::string, ob_lesson_state> pending_states;
        // The character was made with Play Now.
        bool guided = false;
        // The world is the Tutorial Game's.
        bool in_tutorial = false;
        // The player has had a first turn.  Events before it (the starting outfit being put
        // on, the start location being entered) are the game setting up, not the player.
        bool started = false;
};

manager &get_manager()
{
    static manager m;
    return m;
}

void manager::ensure_built()
{
    if( built ) {
        return;
    }
    std::vector<ob_lesson_def> defs;
    lesson_ids.clear();
    for( const onboarding_lesson &l : lesson_factory.get_all() ) {
        defs.push_back( l.def() );
        lesson_ids.push_back( l.id );
    }
    tracker.set_lessons( std::move( defs ) );
    tracker.import_states( pending_states );
    pending_states.clear();
    built = true;
}

void manager::refresh_availability()
{
    for( size_t i = 0; i < lesson_ids.size(); ++i ) {
        tracker.set_available( i, lesson_ids[i].is_valid() && lesson_ids[i]->available() );
    }
}

void manager::invalidate()
{
    if( built ) {
        pending_states = tracker.export_states();
    }
    built = false;
    lesson_ids.clear();
}

void manager::reset_progress()
{
    pending_states.clear();
    built = false;
    lesson_ids.clear();
    guided = false;
    started = false;
    tracker.take_notices();
    const WORLD *world = world_generator ? world_generator->active_world : nullptr;
    in_tutorial = world != nullptr &&
                  std::find( world->active_mod_order.begin(), world->active_mod_order.end(),
                             mod_id( "dda_tutorial" ) ) != world->active_mod_order.end();
}

ob_level manager::level() const
{
    if( in_tutorial ) {
        // The Tutorial Game teaches in its own way.
        return ob_level::off;
    }
    const std::string opt = get_option<std::string>( "ONBOARDING_HINTS" );
    if( opt == "full" ) {
        return ob_level::full;
    } else if( opt == "hints" ) {
        return ob_level::hints;
    } else if( opt == "play_now" ) {
        return guided ? ob_level::full : ob_level::off;
    }
    return ob_level::off;
}

bool manager::poll( size_t lesson, const ob_matcher &match ) const
{
    avatar &u = get_avatar();
    if( match.kind == ob_input_kind::condition ) {
        if( lesson >= lesson_ids.size() || !lesson_ids[lesson].is_valid() ) {
            return false;
        }
        const onboarding_lesson &l = lesson_ids[lesson].obj();
        if( match.condition_index < 0 ||
            match.condition_index >= static_cast<int>( l.conditions.size() ) ) {
            return false;
        }
        dialogue d( get_talker_for( u ), nullptr );
        return l.conditions[match.condition_index]( d );
    }

    map &here = get_map();
    const tripoint_bub_ms pos = u.pos_bub();
    const std::string &name = match.name;
    if( name == "standing_on_items" ) {
        return here.has_items( pos ) && here.accessible_items( pos );
    } else if( name == "items_nearby" ) {
        for( const tripoint_bub_ms &p : here.points_in_radius( pos, 1 ) ) {
            if( here.has_items( p ) && here.accessible_items( p ) && u.sees( here, p ) ) {
                return true;
            }
        }
    } else if( name == "adjacent_door" ) {
        for( const tripoint_bub_ms &p : here.points_in_radius( pos, 1 ) ) {
            if( p == pos ) {
                continue;
            }
            const ter_t &ter = here.ter( p ).obj();
            const furn_t &furn = here.furn( p ).obj();
            if( ter.open || ter.close || furn.open || furn.close ) {
                return true;
            }
        }
    } else if( name == "adjacent_examinable" ) {
        for( const tripoint_bub_ms &p : here.points_in_radius( pos, 1 ) ) {
            if( here.has_furn( p ) && here.furn( p ).obj().can_examine( p ) ) {
                return true;
            }
        }
    } else if( name == "hostile_in_view" ) {
        for( monster &critter : g->all_monsters() ) {
            if( critter.attitude_to( u ) == Creature::Attitude::HOSTILE && u.sees( here, critter ) ) {
                return true;
            }
        }
    } else if( name == "dark_here" ) {
        // Too dark to read or craft well.
        return u.fine_detail_vision_mod() > 4.0f;
    } else if( name == "carries_book" ) {
        return u.has_item_with( []( const item & it ) {
            return it.is_book();
        } );
    } else if( name == "low_morale" ) {
        return u.get_morale_level() < -10;
    } else if( name == "wields_gun" ) {
        const item_location wielded = u.get_wielded_item();
        return wielded && wielded->is_gun();
    } else if( name == "corpse_here" ) {
        for( const item &it : here.i_at( pos ) ) {
            if( it.is_corpse() ) {
                return true;
            }
        }
    } else if( name == "is_wet" ) {
        for( const bodypart_id &bp : u.get_all_body_parts() ) {
            if( u.get_part_wetness( bp ) > 0 ) {
                return true;
            }
        }
    } else if( name == "adjacent_vehicle" ) {
        for( const tripoint_bub_ms &p : here.points_in_radius( pos, 1 ) ) {
            if( here.veh_at( p ) ) {
                return true;
            }
        }
    } else if( name == "npc_in_view" ) {
        for( npc &guy : g->all_npcs() ) {
            if( !guy.is_hallucination() && u.sees( here, guy ) ) {
                return true;
            }
        }
    } else if( name == "on_stairs" ) {
        return here.has_flag( ter_furn_flag::TFLAG_GOES_DOWN, pos ) ||
               here.has_flag( ter_furn_flag::TFLAG_GOES_UP, pos );
    } else if( name == "worn_storage" ) {
        // Clothes have small pockets; anything that holds five liters is a bag.
        for( const item_location &worn : u.get_visible_worn_items() ) {
            if( worn->get_volume_capacity() >= units::from_liter( 5 ) ) {
                return true;
            }
        }
    }
    return false;
}

void manager::log_hint( size_t lesson, bool reminder )
{
    const onboarding_lesson &l = lesson_ids[lesson].obj();
    const std::string body = reminder ? l.get_short().translated() : l.get_text();
    //~ Prefix of a guided hint in the message log; %1$s is the lesson title, %2$s its text.
    add_msg( m_info, _( "%1$s %2$s" ),
             colorize( string_format( _( "[Hint: %s]" ), l.get_title().translated() ), c_light_green ),
             flatten( expand_keys( body ) ) );
}

void manager::show_popup( size_t lesson )
{
    const onboarding_lesson &l = lesson_ids[lesson].obj();
    // Keep a copy in the message log so it can be read again.
    add_msg( m_info, "%s %s",
             colorize( string_format( _( "[Hint: %s]" ), l.get_title().translated() ), c_light_green ),
             expand_keys( l.get_short().translated() ) );

    // On the first turn the map has not been drawn yet; draw it so the popup has a background.
    ui_manager::redraw();
    const std::string body = colorize( l.get_title().translated(), c_light_green ) + "\n\n" +
                             expand_keys( l.get_text() );
    const query_popup::result res = query_popup()
                                    .context( "ONBOARDING_POPUP" )
                                    .message( "%s", body )
                                    .option( "GOT_IT" )
                                    .option( "LATER" )
                                    .option( "STOP_HINTS" )
                                    .allow_cancel( true )
                                    .query();
    if( res.action == "LATER" ) {
        tracker.snooze( lesson, now() );
    } else if( res.action == "STOP_HINTS" ) {
        tracker.complete( lesson, now() );
        get_options().get_option( "ONBOARDING_HINTS" ).setValue( "off" );
        get_options().save();
        add_msg( m_info, _( "Guided hints are off.  You can turn them back on in Settings > "
                            "Interface > Guided hints." ) );
    } else {
        tracker.complete( lesson, now() );
    }
}

bool manager::present()
{
    bool answered = false;
    const ob_level lvl = level();
    for( const ob_notice &n : tracker.take_notices() ) {
        if( lvl == ob_level::off || n.lesson >= lesson_ids.size() ||
            !lesson_ids[n.lesson].is_valid() ) {
            continue;
        }
        const bool popup = lvl == ob_level::full &&
                           tracker.def( n.lesson ).presentation == ob_presentation::popup;
        switch( n.type ) {
            case ob_notice_type::shown:
                if( popup ) {
                    show_popup( n.lesson );
                    answered = true;
                } else {
                    log_hint( n.lesson, false );
                }
                break;
            case ob_notice_type::reminder:
                if( popup ) {
                    show_popup( n.lesson );
                    answered = true;
                } else {
                    log_hint( n.lesson, true );
                }
                break;
            case ob_notice_type::done: {
                const onboarding_lesson &l = lesson_ids[n.lesson].obj();
                const int phase = tracker.def( n.lesson ).phase;
                //~ A guided hint was completed; %1$s is the "[Hint]" tag, %2$s the title, %3$d and %4$d the lessons done and in all.
                add_msg( m_good, _( "%1$s Done: %2$s  (%3$d/%4$d)" ),
                         colorize( _( "[Hint]" ), c_light_green ), l.get_title().translated(),
                         tracker.count_done( phase, phase ), tracker.count_total( phase, phase ) );
                break;
            }
        }
    }
    return answered;
}

void manager::notify( const cata::event &e )
{
    if( !started || level() == ob_level::off ) {
        return;
    }
    // Only the player's own actions count.
    const cata_variant who = e.get_variant_or_void( "character" );
    if( who.type() == cata_variant_type::character_id &&
        who.get<character_id>() != get_avatar().getID() ) {
        return;
    }
    ensure_built();
    tracker.on_input( ob_input_kind::event, io::enum_to_string<event_type>( e.type() ), now() );
}

} // namespace

namespace onboarding
{

void load_lesson( const JsonObject &jo, const std::string &src )
{
    lesson_factory.load( jo, src );
}

void finalize_lessons()
{
    lesson_factory.finalize();
    screen_factory.finalize();
    get_manager().invalidate();
}

void check_lessons()
{
    lesson_factory.check();
    screen_factory.check();
}

void reset_lessons()
{
    get_manager().invalidate();
    lesson_factory.reset();
    screen_factory.reset();
}

event_subscriber &subscriber()
{
    return get_manager();
}

void reset_progress()
{
    get_manager().reset_progress();
}

void prepare_new_game( bool play_now )
{
    manager &m = get_manager();
    m.reset_progress();
    m.guided = play_now;
}

std::string start_scenario()
{
    static const std::string guided_scenario = "evacuee_guided";
    manager &m = get_manager();
    if( m.level() == ob_level::off || !option_on( "ONBOARDING_SOFT_START" ) ||
        !string_id<scenario>( guided_scenario ).is_valid() ) {
        return std::string();
    }
    return guided_scenario;
}

bool enabled()
{
    return get_manager().level() != ob_level::off;
}

void before_input()
{
    manager &m = get_manager();
    if( m.level() == ob_level::off ) {
        return;
    }
    m.ensure_built();
    m.refresh_availability();
    m.started = true;
    // A popup answered with "Got it" can unlock the next lesson; show it now rather than after
    // the next key press.  Bounded, in case a lesson keeps popping up.
    for( int pass = 0; pass < 3; ++pass ) {
        m.tracker.update( now(), [&m]( size_t lesson, const ob_matcher & match ) {
            return m.poll( lesson, match );
        } );
        if( !m.present() ) {
            break;
        }
    }
}

void on_action( const std::string &action_id )
{
    manager &m = get_manager();
    if( m.level() == ob_level::off ) {
        return;
    }
    m.ensure_built();
    m.tracker.on_input( ob_input_kind::action, action_id, now() );
}

void on_signal( const std::string &name )
{
    manager &m = get_manager();
    if( m.level() == ob_level::off ) {
        return;
    }
    m.ensure_built();
    m.tracker.on_input( ob_input_kind::signal, name, now() );
}

std::string sidebar_text( int width )
{
    manager &m = get_manager();
    if( m.level() == ob_level::off ) {
        return std::string();
    }
    m.ensure_built();
    const size_t cur = m.tracker.current();
    if( cur >= m.lesson_ids.size() || !m.lesson_ids[cur].is_valid() ) {
        return std::string();
    }
    const onboarding_lesson &l = m.lesson_ids[cur].obj();
    const int phase = l.phase;
    //~ Sidebar line for guided hints; %1$s is "Next:", %2$s the lesson title, %3$d and %4$d lessons done and in all.
    std::string ret = string_format( _( "%1$s %2$s  (%3$d/%4$d)" ),
                                     colorize( _( "Next:" ), c_light_green ), l.get_title().translated(),
                                     m.tracker.count_done( phase, phase ),
                                     m.tracker.count_total( phase, phase ) );
    const std::vector<std::string> lines = foldstring( expand_keys( l.get_short().translated() ),
                                           std::max( width, 10 ) );
    for( size_t i = 0; i < lines.size() && i < 2; ++i ) {
        ret += "\n" + lines[i];
    }
    return ret;
}

void on_death()
{
    manager &m = get_manager();
    if( m.level() == ob_level::off ) {
        return;
    }
    m.ensure_built();
    m.refresh_availability();
    m.tracker.on_input( ob_input_kind::signal, "avatar_died", now() );
    m.present();
}

bool handle_action( const std::string &action )
{
    if( action == "onboarding_checklist" ) {
        show_checklist();
        return true;
    } else if( action == "EXPLAIN_SCREEN" ) {
        explain_screen( "DEFAULTMODE" );
        return true;
    }
    return false;
}

void load_screen( const JsonObject &jo, const std::string &src )
{
    screen_factory.load( jo, src );
}

bool explain_screen( const std::string &category )
{
    manager &m = get_manager();
    // The popup below waits for input itself; do not explain the popup.
    static bool explaining = false;
    if( explaining || m.level() == ob_level::off ) {
        return false;
    }
    explaining = true;
    on_out_of_scope reset_explaining( []() {
        explaining = false;
    } );
    const onboarding_screen_id sid( category );
    std::string body;
    if( sid.is_valid() ) {
        body = colorize( pick_title( sid->title, sid->variants ).translated(), c_light_green ) +
               "\n\n" + expand_keys( pick_text( sid->text, sid->variants ) );
    } else {
        body = colorize( _( "This screen" ), c_light_green ) + "\n\n" +
               _( "There is no explanation for this screen yet." );
    }
    //~ Added under every "explain this screen" text.
    body += "\n\n" + expand_keys( string_format( _( "Press %s on any screen to see all of its keys." ),
                                  "<keybind:HELP_KEYBINDINGS>" ) );
    popup( body, PF_NONE );
    m.tracker.on_input( ob_input_kind::action, "EXPLAIN_SCREEN", now() );
    return true;
}

void show_checklist()
{
    manager &m = get_manager();
    if( m.level() == ob_level::off ) {
        popup( _( "Guided hints are off.  You can turn them on in Settings > Interface > Guided "
                  "hints." ) );
        return;
    }
    m.ensure_built();
    m.refresh_availability();
    m.tracker.on_input( ob_input_kind::action, "onboarding_checklist", now() );

    // Lesson phases as the checklist shows them.
    const auto phase_name = []( int phase ) -> std::string {
        switch( phase )
        {
            case 0:
            case 1:
                return _( "First steps" );
            case 2:
                return _( "The first hour" );
            case 3:
                return _( "The first days" );
            default:
                return _( "Later" );
        }
    };

    int selected = 0;
    while( true ) {
        std::vector<size_t> order;
        for( size_t i = 0; i < m.tracker.size(); ++i ) {
            if( m.tracker.available( i ) && i < m.lesson_ids.size() && m.lesson_ids[i].is_valid() ) {
                order.push_back( i );
            }
        }
        std::stable_sort( order.begin(), order.end(), [&m]( size_t a, size_t b ) {
            const ob_lesson_def &da = m.tracker.def( a );
            const ob_lesson_def &db = m.tracker.def( b );
            return da.phase != db.phase ? da.phase < db.phase : da.order < db.order;
        } );

        uilist menu;
        menu.title = _( "Guided hints" );
        menu.text = string_format( _( "Done: %1$d of %2$d.  Pick a hint to read it again." ),
                                   m.tracker.count_done( 0, 3 ), m.tracker.count_total( 0, 3 ) );
        menu.desc_enabled = true;
        menu.allow_disabled = false;
        std::string last_phase;
        for( size_t i : order ) {
            const ob_lesson_def &def = m.tracker.def( i );
            if( def.phase >= 9 ) {
                // Shown at death only.
                continue;
            }
            const std::string group = phase_name( def.phase );
            if( group != last_phase ) {
                menu.addentry( -1, false, 0, colorize( group, c_light_blue ) );
                last_phase = group;
            }
            const ob_lesson_state &st = m.tracker.state( i );
            std::string mark;
            switch( st.status ) {
                case ob_status::done:
                    mark = colorize( "[x]", c_light_green );
                    break;
                case ob_status::active:
                    mark = colorize( "[>]", c_yellow );
                    break;
                case ob_status::queued:
                case ob_status::waiting:
                    mark = "[ ]";
                    break;
                case ob_status::locked:
                    mark = colorize( "[ ]", c_dark_gray );
                    break;
            }
            const onboarding_lesson &l = m.lesson_ids[i].obj();
            menu.addentry_desc( static_cast<int>( i ), true, 0,
                                mark + " " + l.get_title().translated(),
                                expand_keys( l.get_short().translated() ) );
        }
        menu.selected = selected;
        menu.query();
        if( menu.ret < 0 || menu.ret >= static_cast<int>( m.tracker.size() ) ) {
            break;
        }
        selected = menu.selected;
        const size_t lesson = static_cast<size_t>( menu.ret );
        const onboarding_lesson &l = m.lesson_ids[lesson].obj();

        uilist detail;
        detail.title = l.get_title().translated();
        detail.text = wrap_text( expand_keys( l.get_text() ), 64 );
        detail.addentry( 0, true, 'b', _( "Back" ) );
        detail.addentry( 1, true, 's', _( "Show this hint again" ) );
        detail.addentry( 2, m.tracker.state( lesson ).status != ob_status::done, 'd',
                         _( "Mark as done" ) );
        detail.query();
        if( detail.ret == 1 ) {
            m.tracker.reopen( lesson, now() );
            // Shown as a log hint even for popup lessons: the player just read it.
            for( const ob_notice &n : m.tracker.take_notices() ) {
                if( n.type == ob_notice_type::shown && n.lesson < m.lesson_ids.size() ) {
                    m.log_hint( n.lesson, false );
                }
            }
        } else if( detail.ret == 2 ) {
            m.tracker.complete( lesson, now() );
        }
    }
}

void serialize( JsonOut &json )
{
    manager &m = get_manager();
    const std::map<std::string, ob_lesson_state> states = m.built ? m.tracker.export_states() :
            m.pending_states;
    json.start_object();
    json.member( "guided", m.guided );
    json.member( "lessons" );
    json.start_object();
    for( const auto &entry : states ) {
        const ob_lesson_state &st = entry.second;
        // Untouched lessons need no entry.
        if( st.status == ob_status::locked && st.reminders == 0 &&
        std::all_of( st.progress.begin(), st.progress.end(), []( int p ) {
        return p == 0;
    } ) ) {
            continue;
        }
        json.member( entry.first );
        json.start_object();
        json.member( "status", status_to_string( st.status ) );
        json.member( "progress", st.progress );
        json.member( "unlocked", st.unlocked_turn );
        json.member( "activated", st.activated_turn );
        json.member( "snoozed_until", st.snoozed_until );
        json.member( "reminders", st.reminders );
        json.member( "quiet", st.quiet );
        json.end_object();
    }
    json.end_object();
    json.end_object();
}

void deserialize( const JsonObject &jo )
{
    manager &m = get_manager();
    m.reset_progress();
    jo.allow_omitted_members();
    jo.read( "guided", m.guided );
    if( jo.has_object( "lessons" ) ) {
        for( const JsonMember member : jo.get_object( "lessons" ) ) {
            JsonObject lo = member.get_object();
            lo.allow_omitted_members();
            ob_lesson_state st;
            st.status = status_from_string( lo.get_string( "status", "locked" ) );
            lo.read( "progress", st.progress );
            lo.read( "unlocked", st.unlocked_turn );
            lo.read( "activated", st.activated_turn );
            lo.read( "snoozed_until", st.snoozed_until );
            lo.read( "reminders", st.reminders );
            lo.read( "quiet", st.quiet );
            m.pending_states[member.name()] = st;
        }
    }
}

} // namespace onboarding
