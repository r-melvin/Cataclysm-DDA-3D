#include "onboarding_core.h"

#include <algorithm>
#include <utility>

onboarding_tracker::onboarding_tracker( std::vector<ob_lesson_def> defs )
{
    set_lessons( std::move( defs ) );
}

void onboarding_tracker::set_lessons( std::vector<ob_lesson_def> defs )
{
    defs_ = std::move( defs );
    reset();
}

void onboarding_tracker::reset()
{
    states_.assign( defs_.size(), ob_lesson_state() );
    available_.assign( defs_.size(), true );
    for( size_t i = 0; i < defs_.size(); ++i ) {
        states_[i].progress.assign( defs_[i].done_when.size(), 0 );
    }
    notices_.clear();
}

size_t onboarding_tracker::find( const std::string &id ) const
{
    for( size_t i = 0; i < defs_.size(); ++i ) {
        if( defs_[i].id == id ) {
            return i;
        }
    }
    return defs_.size();
}

std::vector<size_t> onboarding_tracker::sorted() const
{
    std::vector<size_t> order( defs_.size() );
    for( size_t i = 0; i < order.size(); ++i ) {
        order[i] = i;
    }
    std::stable_sort( order.begin(), order.end(), [this]( size_t a, size_t b ) {
        if( defs_[a].phase != defs_[b].phase ) {
            return defs_[a].phase < defs_[b].phase;
        }
        return defs_[a].order < defs_[b].order;
    } );
    return order;
}

bool onboarding_tracker::prerequisites_done( size_t i ) const
{
    for( const std::string &pre : defs_[i].prerequisites ) {
        const size_t p = find( pre );
        // An unknown prerequisite (a lesson removed by a mod) does not block anything.
        if( p < defs_.size() && available_[p] && states_[p].status != ob_status::done ) {
            return false;
        }
    }
    return true;
}

int onboarding_tracker::active_count() const
{
    int n = 0;
    // Popups do not take a place: they are dealt with on the spot.
    for( size_t i = 0; i < states_.size(); ++i ) {
        if( available_[i] && states_[i].status == ob_status::active &&
            defs_[i].presentation != ob_presentation::popup ) {
            ++n;
        }
    }
    return n;
}

void onboarding_tracker::activate( size_t i, int64_t turn )
{
    ob_lesson_state &st = states_[i];
    // Popups are about what is happening right now, so they never wait in the queue.
    if( defs_[i].presentation != ob_presentation::popup && active_count() >= max_active ) {
        st.status = ob_status::queued;
        return;
    }
    st.status = ob_status::active;
    st.activated_turn = turn;
    st.snoozed_until = turn + defs_[i].snooze_turns;
    st.reminders = 0;
    if( defs_[i].presentation != ob_presentation::silent ) {
        notices_.push_back( { ob_notice_type::shown, i } );
    }
}

void onboarding_tracker::finish( size_t i, int64_t turn )
{
    ob_lesson_state &st = states_[i];
    if( st.status == ob_status::done ) {
        return;
    }
    const bool was_shown = st.status == ob_status::active;
    st.status = ob_status::done;
    st.quiet = !was_shown;
    if( was_shown && defs_[i].presentation != ob_presentation::silent ) {
        notices_.push_back( { ob_notice_type::done, i } );
    }
    promote_queued( turn );
}

void onboarding_tracker::promote_queued( int64_t turn )
{
    for( size_t i : sorted() ) {
        if( active_count() >= max_active ) {
            return;
        }
        if( available_[i] && states_[i].status == ob_status::queued ) {
            activate( i, turn );
        }
    }
}

void onboarding_tracker::on_input( ob_input_kind kind, const std::string &name, int64_t turn )
{
    for( size_t i : sorted() ) {
        const ob_lesson_def &def = defs_[i];
        ob_lesson_state &st = states_[i];
        if( !available_[i] || st.status == ob_status::done ) {
            continue;
        }
        // Progress counts in every state, so a player who already knows something is not
        // taught it later.
        bool completed = false;
        for( size_t m = 0; m < def.done_when.size(); ++m ) {
            const ob_matcher &match = def.done_when[m];
            if( match.kind != kind || match.name != name ) {
                continue;
            }
            st.progress[m] = std::min( st.progress[m] + 1, match.count );
            if( st.progress[m] >= match.count ) {
                completed = true;
            }
        }
        if( completed ) {
            finish( i, turn );
            continue;
        }
        if( st.status == ob_status::waiting ) {
            for( const ob_matcher &match : def.trigger ) {
                if( match.kind == kind && match.name == name ) {
                    activate( i, turn );
                    break;
                }
            }
        }
    }
}

void onboarding_tracker::update( int64_t turn, const poll_fn &poll )
{
    // Unlock lessons whose prerequisites are done.  Repeat because a lesson unlocked here can
    // already be done (quietly) and unlock others in turn.
    bool changed = true;
    while( changed ) {
        changed = false;
        for( size_t i = 0; i < defs_.size(); ++i ) {
            if( states_[i].status == ob_status::locked && prerequisites_done( i ) ) {
                states_[i].status = ob_status::waiting;
                states_[i].unlocked_turn = turn;
                changed = true;
            }
        }
    }

    for( size_t i : sorted() ) {
        const ob_lesson_def &def = defs_[i];
        ob_lesson_state &st = states_[i];
        if( !available_[i] || st.status == ob_status::done || st.status == ob_status::locked ) {
            continue;
        }

        // Polled done conditions.
        bool completed = false;
        for( size_t m = 0; m < def.done_when.size() && !completed; ++m ) {
            const ob_matcher &match = def.done_when[m];
            switch( match.kind ) {
                case ob_input_kind::state:
                case ob_input_kind::condition:
                    completed = ( !match.while_shown || st.status == ob_status::active ) &&
                                poll && poll( i, match );
                    break;
                case ob_input_kind::after_turns:
                    completed = st.status == ob_status::active &&
                                turn - st.activated_turn >= match.count;
                    break;
                default:
                    break;
            }
        }
        if( completed ) {
            finish( i, turn );
            continue;
        }

        if( st.status == ob_status::waiting ) {
            bool triggered = def.trigger.empty();
            for( const ob_matcher &match : def.trigger ) {
                switch( match.kind ) {
                    case ob_input_kind::start:
                        triggered = true;
                        break;
                    case ob_input_kind::after_turns:
                        triggered = turn - st.unlocked_turn >= match.count;
                        break;
                    case ob_input_kind::state:
                    case ob_input_kind::condition:
                        triggered = poll && poll( i, match );
                        break;
                    default:
                        break;
                }
                if( triggered ) {
                    break;
                }
            }
            if( triggered ) {
                activate( i, turn );
            }
        } else if( st.status == ob_status::active && def.snooze_turns > 0 &&
                   st.reminders < max_reminders && turn >= st.snoozed_until ) {
            st.reminders++;
            st.snoozed_until = turn + def.snooze_turns;
            if( def.presentation != ob_presentation::silent ) {
                notices_.push_back( { ob_notice_type::reminder, i } );
            }
        }
    }
    promote_queued( turn );
}

void onboarding_tracker::complete( size_t i, int64_t turn )
{
    if( i >= defs_.size() || states_[i].status == ob_status::done ) {
        return;
    }
    states_[i].status = ob_status::done;
    states_[i].quiet = false;
    promote_queued( turn );
}

void onboarding_tracker::snooze( size_t i, int64_t turn )
{
    if( i >= defs_.size() ) {
        return;
    }
    states_[i].snoozed_until = turn + defs_[i].snooze_turns;
}

void onboarding_tracker::reopen( size_t i, int64_t turn )
{
    if( i >= defs_.size() ) {
        return;
    }
    ob_lesson_state &st = states_[i];
    st.status = ob_status::active;
    st.progress.assign( defs_[i].done_when.size(), 0 );
    st.activated_turn = turn;
    st.snoozed_until = turn + defs_[i].snooze_turns;
    st.reminders = 0;
    st.quiet = false;
    notices_.push_back( { ob_notice_type::shown, i } );
}

void onboarding_tracker::set_available( size_t i, bool available )
{
    if( i < available_.size() ) {
        available_[i] = available;
    }
}

std::vector<ob_notice> onboarding_tracker::take_notices()
{
    std::vector<ob_notice> ret;
    ret.swap( notices_ );
    return ret;
}

size_t onboarding_tracker::current() const
{
    for( size_t i : sorted() ) {
        if( available_[i] && states_[i].status == ob_status::active &&
            defs_[i].presentation != ob_presentation::silent ) {
            return i;
        }
    }
    return defs_.size();
}

int onboarding_tracker::count_done( int min_phase, int max_phase ) const
{
    int n = 0;
    for( size_t i = 0; i < defs_.size(); ++i ) {
        if( available_[i] && defs_[i].phase >= min_phase && defs_[i].phase <= max_phase &&
            states_[i].status == ob_status::done ) {
            ++n;
        }
    }
    return n;
}

int onboarding_tracker::count_total( int min_phase, int max_phase ) const
{
    int n = 0;
    for( size_t i = 0; i < defs_.size(); ++i ) {
        if( available_[i] && defs_[i].phase >= min_phase && defs_[i].phase <= max_phase ) {
            ++n;
        }
    }
    return n;
}

std::map<std::string, ob_lesson_state> onboarding_tracker::export_states() const
{
    std::map<std::string, ob_lesson_state> ret;
    for( size_t i = 0; i < defs_.size(); ++i ) {
        ret[defs_[i].id] = states_[i];
    }
    return ret;
}

void onboarding_tracker::import_states( const std::map<std::string, ob_lesson_state> &states )
{
    reset();
    for( size_t i = 0; i < defs_.size(); ++i ) {
        const auto it = states.find( defs_[i].id );
        if( it == states.end() ) {
            continue;
        }
        states_[i] = it->second;
        // The lesson's done_when list may have changed since the save was made.
        states_[i].progress.resize( defs_[i].done_when.size(), 0 );
    }
}
