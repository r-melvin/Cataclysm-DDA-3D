#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "cata_catch.h"
#include "onboarding_core.h"

// Tests for the rules of guided hints (baby mode).  They use hand-made lessons, not the JSON,
// so they cover the state machine on its own.

static ob_matcher match( ob_input_kind kind, const std::string &name, int count = 1 )
{
    ob_matcher m;
    m.kind = kind;
    m.name = name;
    m.count = count;
    return m;
}

static ob_matcher after( int turns )
{
    ob_matcher m;
    m.kind = ob_input_kind::after_turns;
    m.count = turns;
    return m;
}

static ob_lesson_def lesson( const std::string &id, int order,
                             const std::vector<std::string> &prerequisites,
                             const std::vector<ob_matcher> &trigger,
                             const std::vector<ob_matcher> &done_when,
                             ob_presentation presentation = ob_presentation::hint )
{
    ob_lesson_def d;
    d.id = id;
    d.phase = 1;
    d.order = order;
    d.prerequisites = prerequisites;
    d.trigger = trigger;
    d.done_when = done_when;
    d.presentation = presentation;
    d.snooze_turns = 100;
    return d;
}

static size_t count_notices( const std::vector<ob_notice> &notices, ob_notice_type type )
{
    size_t n = 0;
    for( const ob_notice &no : notices ) {
        if( no.type == type ) {
            ++n;
        }
    }
    return n;
}

// Polls nothing: every state and condition is false.
static bool poll_nothing( size_t, const ob_matcher & )
{
    return false;
}

TEST_CASE( "onboarding_start_trigger_and_prerequisites", "[onboarding]" )
{
    onboarding_tracker t( {
        lesson( "move", 1, {}, { match( ob_input_kind::start, "" ) },
                { match( ob_input_kind::event, "avatar_moves", 3 ) } ),
        lesson( "pause", 2, { "move" }, { match( ob_input_kind::start, "" ) },
                { match( ob_input_kind::action, "pause" ) } )
    } );
    const size_t move = t.find( "move" );
    const size_t pause = t.find( "pause" );
    REQUIRE( move < t.size() );
    REQUIRE( pause < t.size() );

    t.update( 0, poll_nothing );
    CHECK( t.state( move ).status == ob_status::active );
    CHECK( t.state( pause ).status == ob_status::locked );
    CHECK( t.current() == move );
    std::vector<ob_notice> notices = t.take_notices();
    REQUIRE( notices.size() == 1 );
    CHECK( notices[0].type == ob_notice_type::shown );
    CHECK( notices[0].lesson == move );

    // Two moves are not enough, the third completes it.
    t.on_input( ob_input_kind::event, "avatar_moves", 1 );
    t.on_input( ob_input_kind::event, "avatar_moves", 2 );
    CHECK( t.state( move ).status == ob_status::active );
    t.on_input( ob_input_kind::event, "avatar_moves", 3 );
    CHECK( t.state( move ).status == ob_status::done );
    CHECK_FALSE( t.state( move ).quiet );
    notices = t.take_notices();
    CHECK( count_notices( notices, ob_notice_type::done ) == 1 );

    // The next lesson unlocks and starts on the next update.
    t.update( 4, poll_nothing );
    CHECK( t.state( pause ).status == ob_status::active );
    CHECK( t.current() == pause );
    CHECK( t.count_done( 1, 1 ) == 1 );
    CHECK( t.count_total( 1, 1 ) == 2 );
}

TEST_CASE( "onboarding_quiet_completion", "[onboarding]" )
{
    // A player who already did the thing is never taught it.
    onboarding_tracker t( {
        lesson( "doors", 1, {}, { match( ob_input_kind::state, "adjacent_door" ) },
                { match( ob_input_kind::action, "close" ) } )
    } );
    t.update( 0, poll_nothing );
    CHECK( t.state( 0 ).status == ob_status::waiting );
    t.on_input( ob_input_kind::action, "close", 1 );
    CHECK( t.state( 0 ).status == ob_status::done );
    CHECK( t.state( 0 ).quiet );
    CHECK( t.take_notices().empty() );
}

TEST_CASE( "onboarding_polled_trigger_and_done", "[onboarding]" )
{
    onboarding_tracker t( {
        lesson( "pickup", 1, {}, { match( ob_input_kind::state, "items_nearby" ) },
                { match( ob_input_kind::action, "pickup" ) } ),
        lesson( "storage", 2, {}, { after( 10 ) },
                { match( ob_input_kind::state, "worn_storage" ) } )
    } );
    std::set<std::string> states;
    const auto poll = [&states]( size_t, const ob_matcher & m ) {
        return states.count( m.name ) > 0;
    };
    t.update( 0, poll );
    CHECK( t.state( 0 ).status == ob_status::waiting );
    CHECK( t.state( 1 ).status == ob_status::waiting );

    states.insert( "items_nearby" );
    t.update( 1, poll );
    CHECK( t.state( 0 ).status == ob_status::active );

    // after_turns counts from when the prerequisites were done (turn 0 here).
    t.update( 9, poll );
    CHECK( t.state( 1 ).status == ob_status::waiting );
    t.update( 10, poll );
    CHECK( t.state( 1 ).status == ob_status::active );

    // A polled done condition completes it.
    states.insert( "worn_storage" );
    t.update( 11, poll );
    CHECK( t.state( 1 ).status == ob_status::done );
    CHECK_FALSE( t.state( 1 ).quiet );
}

TEST_CASE( "onboarding_done_while_shown", "[onboarding]" )
{
    ob_matcher not_bleeding = match( ob_input_kind::state, "not_bleeding" );
    not_bleeding.while_shown = true;
    onboarding_tracker t( {
        lesson( "bleeding", 1, {}, { match( ob_input_kind::state, "bleeding" ) }, { not_bleeding } )
    } );
    bool bleeding = false;
    const auto poll = [&bleeding]( size_t, const ob_matcher & m ) {
        return m.name == "bleeding" ? bleeding : !bleeding;
    };
    // Not bleeding before the lesson was ever due does not complete it.
    t.update( 0, poll );
    CHECK( t.state( 0 ).status == ob_status::waiting );
    bleeding = true;
    t.update( 1, poll );
    CHECK( t.state( 0 ).status == ob_status::active );
    bleeding = false;
    t.update( 2, poll );
    CHECK( t.state( 0 ).status == ob_status::done );
}

TEST_CASE( "onboarding_done_after_turns_counts_from_shown", "[onboarding]" )
{
    onboarding_tracker t( {
        lesson( "sidebar", 1, {}, { after( 5 ) }, { after( 20 ) } )
    } );
    t.update( 0, poll_nothing );
    t.update( 5, poll_nothing );
    REQUIRE( t.state( 0 ).status == ob_status::active );
    t.update( 24, poll_nothing );
    CHECK( t.state( 0 ).status == ob_status::active );
    t.update( 25, poll_nothing );
    CHECK( t.state( 0 ).status == ob_status::done );
}

TEST_CASE( "onboarding_at_most_three_active_and_queue", "[onboarding]" )
{
    std::vector<ob_lesson_def> defs;
    for( int i = 0; i < 5; ++i ) {
        defs.push_back( lesson( "l" + std::to_string( i ), i, {},
        { match( ob_input_kind::start, "" ) },
        { match( ob_input_kind::action, "a" + std::to_string( i ) ) } ) );
    }
    // A popup is never queued.
    defs.push_back( lesson( "popup", 9, {}, { match( ob_input_kind::signal, "spotted" ) }, {},
                            ob_presentation::popup ) );
    onboarding_tracker t( defs );
    t.update( 0, poll_nothing );
    int active = 0;
    int queued = 0;
    for( size_t i = 0; i < t.size(); ++i ) {
        active += t.state( i ).status == ob_status::active;
        queued += t.state( i ).status == ob_status::queued;
    }
    CHECK( active == onboarding_tracker::max_active );
    CHECK( t.state( t.find( "popup" ) ).status == ob_status::waiting );
    CHECK( queued == 2 );
    // The first lessons in order are the ones shown.
    CHECK( t.state( t.find( "l0" ) ).status == ob_status::active );
    CHECK( t.state( t.find( "l3" ) ).status == ob_status::queued );

    t.on_input( ob_input_kind::signal, "spotted", 1 );
    CHECK( t.state( t.find( "popup" ) ).status == ob_status::active );

    // Finishing one shows the next queued lesson.
    t.take_notices();
    t.on_input( ob_input_kind::action, "a0", 2 );
    CHECK( t.state( t.find( "l3" ) ).status == ob_status::active );
    const std::vector<ob_notice> notices = t.take_notices();
    CHECK( count_notices( notices, ob_notice_type::done ) == 1 );
    CHECK( count_notices( notices, ob_notice_type::shown ) == 1 );
}

TEST_CASE( "onboarding_reminders_snooze_and_complete", "[onboarding]" )
{
    onboarding_tracker t( {
        lesson( "look", 1, {}, { match( ob_input_kind::start, "" ) },
                { match( ob_input_kind::action, "look" ) } )
    } );
    t.update( 0, poll_nothing );
    t.take_notices();
    // snooze_turns is 100: a reminder at 100 and 200, then no more.
    t.update( 99, poll_nothing );
    CHECK( t.take_notices().empty() );
    t.update( 100, poll_nothing );
    CHECK( count_notices( t.take_notices(), ob_notice_type::reminder ) == 1 );
    t.snooze( 0, 150 );
    t.update( 200, poll_nothing );
    CHECK( t.take_notices().empty() );
    t.update( 250, poll_nothing );
    CHECK( count_notices( t.take_notices(), ob_notice_type::reminder ) == 1 );
    t.update( 1000, poll_nothing );
    CHECK( t.take_notices().empty() );

    // "Got it" completes without a done notice.
    t.complete( 0, 1001 );
    CHECK( t.state( 0 ).status == ob_status::done );
    CHECK( t.take_notices().empty() );

    // The checklist can show it again.
    t.reopen( 0, 1002 );
    CHECK( t.state( 0 ).status == ob_status::active );
    CHECK( count_notices( t.take_notices(), ob_notice_type::shown ) == 1 );
}

TEST_CASE( "onboarding_unavailable_lessons", "[onboarding]" )
{
    onboarding_tracker t( {
        lesson( "camera", 1, {}, { match( ob_input_kind::start, "" ) },
                { match( ob_input_kind::action, "r3d_rotate_left" ) } ),
        lesson( "after_camera", 2, { "camera" }, { match( ob_input_kind::start, "" ) },
                { match( ob_input_kind::action, "x" ) } )
    } );
    // The 3D view is off: the camera lesson does not apply, is not counted, and does not block
    // the lesson after it.
    t.set_available( 0, false );
    t.update( 0, poll_nothing );
    CHECK( t.state( 0 ).status != ob_status::active );
    CHECK( t.state( 1 ).status == ob_status::active );
    CHECK( t.count_total( 1, 1 ) == 1 );
    CHECK( t.current() == 1 );
    // Its actions do not count while it does not apply.
    t.on_input( ob_input_kind::action, "r3d_rotate_left", 1 );
    CHECK( t.state( 0 ).status != ob_status::done );

    // Turning the 3D view on brings it back.
    t.set_available( 0, true );
    t.update( 2, poll_nothing );
    CHECK( t.state( 0 ).status == ob_status::active );
    CHECK( t.count_total( 1, 1 ) == 2 );
}

TEST_CASE( "onboarding_save_and_load", "[onboarding]" )
{
    const std::vector<ob_lesson_def> defs = {
        lesson( "a", 1, {}, { match( ob_input_kind::start, "" ) },
                { match( ob_input_kind::event, "avatar_moves", 5 ) } ),
        lesson( "b", 2, { "a" }, { match( ob_input_kind::start, "" ) },
                { match( ob_input_kind::action, "pause" ) } )
    };
    onboarding_tracker t( defs );
    t.update( 0, poll_nothing );
    t.on_input( ob_input_kind::event, "avatar_moves", 1 );
    t.on_input( ob_input_kind::event, "avatar_moves", 2 );
    std::map<std::string, ob_lesson_state> saved = t.export_states();
    // A lesson that no longer exists is ignored.
    saved["gone"] = ob_lesson_state();

    onboarding_tracker loaded( defs );
    loaded.import_states( saved );
    CHECK( loaded.state( 0 ).status == ob_status::active );
    REQUIRE( loaded.state( 0 ).progress.size() == 1 );
    CHECK( loaded.state( 0 ).progress[0] == 2 );
    loaded.on_input( ob_input_kind::event, "avatar_moves", 3 );
    loaded.on_input( ob_input_kind::event, "avatar_moves", 4 );
    loaded.on_input( ob_input_kind::event, "avatar_moves", 5 );
    CHECK( loaded.state( 0 ).status == ob_status::done );
}
