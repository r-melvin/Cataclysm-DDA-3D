#pragma once
#ifndef CATA_SRC_ONBOARDING_CORE_H
#define CATA_SRC_ONBOARDING_CORE_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

// The rules of baby mode (guided onboarding hints), kept free of game types so they can be
// unit tested on their own.  The game side (onboarding.cpp) feeds this tracker with what the
// player does and shows the notices it produces.  See doc/ONBOARDING_BABY_MODE.md.

// What a matcher listens to.
enum class ob_input_kind : int {
    // Matches as soon as it is evaluated: a lesson whose trigger is "start" becomes active the
    // moment its prerequisites are done.
    start,
    // An action id as written in data/raw/keybindings.json, such as "pickup".
    action,
    // An event_type name, such as "character_wears_item".
    event,
    // A named one-off signal pushed by the game, such as "safe_mode_spotted".
    signal,
    // A named game state the game polls on request, such as "standing_on_items".
    state,
    // A dialogue condition held by the game side; the tracker only knows its index.
    condition,
    // A number of turns: since the prerequisites were done (in a trigger) or since the lesson
    // was shown (in a done_when).
    after_turns
};

struct ob_matcher {
    ob_input_kind kind = ob_input_kind::start;
    // The action, event, signal or state name.
    std::string name;
    // How many times it must happen before it counts; for after_turns, the number of turns.
    int count = 1;
    // For conditions, the index the game side uses to find the condition.
    int condition_index = -1;
    // For polled done_when matchers (state, condition): only count while the lesson is shown.
    // Without it, a state that already holds completes the lesson quietly before it is due,
    // which is right for "wears a bag" but wrong for "is no longer bleeding".
    bool while_shown = false;
};

enum class ob_presentation : int {
    // A line in the message log and the sidebar.
    hint,
    // A modal popup, then the same as a hint.
    popup,
    // Only listed in the checklist.
    silent
};

struct ob_lesson_def {
    std::string id;
    int phase = 1;
    int order = 0;
    ob_presentation presentation = ob_presentation::hint;
    std::vector<std::string> prerequisites;
    // Any one of these makes the lesson active.  Empty means the same as one "start" matcher.
    std::vector<ob_matcher> trigger;
    // Any one of these, once it has reached its count, completes the lesson.  Lessons with an
    // empty list are only completed by the player (a popup's "Got it", or the checklist).
    std::vector<ob_matcher> done_when;
    // Turns between reminders while the lesson stays active and not done.
    int snooze_turns = 600;
};

enum class ob_status : int {
    // Waiting for its prerequisites.
    locked,
    // Prerequisites done, waiting for its trigger.
    waiting,
    // Triggered, but too many lessons are already shown; it will be shown later.
    queued,
    // Shown to the player and not done yet.
    active,
    // Completed by doing the thing, by "Got it", or by the checklist.
    done
};

struct ob_lesson_state {
    ob_status status = ob_status::locked;
    // Progress of each done_when matcher.
    std::vector<int> progress;
    // Turn at which the prerequisites were done.
    int64_t unlocked_turn = 0;
    // Turn at which the lesson was shown.
    int64_t activated_turn = 0;
    // No reminder before this turn.
    int64_t snoozed_until = 0;
    int reminders = 0;
    // Completed before it was ever shown, so the player evidently knew it already.
    bool quiet = false;
};

enum class ob_notice_type : int {
    // Show the lesson (a hint line, or a popup for popup lessons).
    shown,
    // Remind the player of a lesson that is still active (a short line, or the popup again
    // after "Later").
    reminder,
    // The player did it.  Not sent for lessons completed quietly.
    done
};

struct ob_notice {
    ob_notice_type type = ob_notice_type::shown;
    size_t lesson = 0;
};

class onboarding_tracker
{
    public:
        // Evaluates a state or condition matcher of the given lesson.
        using poll_fn = std::function<bool( size_t lesson, const ob_matcher & )>;

        // Hint lessons shown at the same time, so the player is never flooded.  Popups do not
        // count.
        static constexpr int max_active = 3;
        // Reminders per lesson; after that the lesson stays in the sidebar and the checklist.
        static constexpr int max_reminders = 2;

        onboarding_tracker() = default;
        explicit onboarding_tracker( std::vector<ob_lesson_def> defs );

        // Replaces the lessons and forgets all progress.
        void set_lessons( std::vector<ob_lesson_def> defs );
        // Forgets all progress.
        void reset();

        // Something happened.  `name` is the action id, event name or signal name.
        void on_input( ob_input_kind kind, const std::string &name, int64_t turn );
        // Called whenever the player is about to act: unlocks lessons, evaluates polled triggers,
        // after_turns matchers and reminders.
        void update( int64_t turn, const poll_fn &poll );

        // The player pressed "Got it" or marked the lesson done in the checklist.
        void complete( size_t lesson, int64_t turn );
        // The player pressed "Later": no reminder for snooze_turns.
        void snooze( size_t lesson, int64_t turn );
        // Shows a lesson again from the checklist, whatever its state.
        void reopen( size_t lesson, int64_t turn );
        // Whether a lesson applies to the current game at all, for example a lesson about the
        // 3D view while the 3D view is off, or about an action this build does not have.
        // Unavailable lessons are not shown, not counted, and do not block lessons that
        // depend on them.  Not saved: the game sets it before every update.
        void set_available( size_t lesson, bool available );
        bool available( size_t lesson ) const {
            return lesson < available_.size() && available_[lesson];
        }

        // Notices produced since the last call.
        std::vector<ob_notice> take_notices();

        size_t size() const {
            return defs_.size();
        }
        const ob_lesson_def &def( size_t i ) const {
            return defs_[i];
        }
        const ob_lesson_state &state( size_t i ) const {
            return states_[i];
        }
        // Index of a lesson by id, or size() when there is none.
        size_t find( const std::string &id ) const;

        // The active lesson that comes first in phase and order, or size() when none is active.
        size_t current() const;
        // Lessons of the given phases that are done, and how many there are in all.
        int count_done( int min_phase, int max_phase ) const;
        int count_total( int min_phase, int max_phase ) const;

        // Saving: the state of each lesson by id.  Unknown ids are ignored when loading, and
        // lessons missing from the save start fresh.
        std::map<std::string, ob_lesson_state> export_states() const;
        void import_states( const std::map<std::string, ob_lesson_state> &states );

    private:
        bool prerequisites_done( size_t i ) const;
        void activate( size_t i, int64_t turn );
        void finish( size_t i, int64_t turn );
        void promote_queued( int64_t turn );
        int active_count() const;
        // Lesson indices in phase and order.
        std::vector<size_t> sorted() const;

        std::vector<ob_lesson_def> defs_;
        std::vector<ob_lesson_state> states_;
        std::vector<bool> available_;
        std::vector<ob_notice> notices_;
};

#endif // CATA_SRC_ONBOARDING_CORE_H
