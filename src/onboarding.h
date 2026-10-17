#pragma once
#ifndef CATA_SRC_ONBOARDING_H
#define CATA_SRC_ONBOARDING_H

#include <string>

// Baby mode: guided hints for new players, an overlay on a normal game.  Lessons are JSON
// (type "onboarding_lesson", data/json/onboarding/); the rules live in onboarding_core.h.  This
// header is the game's side: loading, the hooks the game calls, and saving.  Nothing here changes
// game state; it reads what happens and writes messages, popups and its own progress.
// See doc/ONBOARDING_BABY_MODE.md.

class JsonObject;
class JsonOut;
class event_subscriber;

namespace onboarding
{

// JSON loading, called by DynamicDataLoader.
void load_lesson( const JsonObject &jo, const std::string &src );
void finalize_lessons();
void check_lessons();
void reset_lessons();

// The event bus subscriber that feeds game events to the lessons.
event_subscriber &subscriber();

// Forgets the progress of the previous character.  Called when a game is set up.
void reset_progress();
// Called once a new character is made, before the game starts.  Characters made with
// Play Now get guided hints under the default option.
void prepare_new_game( bool play_now );

// The scenario a Play Now (Default Scenario) character starts in when guided hints and the
// guided start kit are on, or an empty string for the usual default scenario.  Call it after
// prepare_new_game().
std::string start_scenario();

// Whether hints are on for this game at all.
bool enabled();

// Called whenever the player is about to choose an action: checks lessons and shows any
// pending popup or hint.
void before_input();
// Called when the player chose an action, with the action's id from keybindings.json.
void on_action( const std::string &action_id );
// A one-off thing the game reports, such as "safe_mode_spotted".
void on_signal( const std::string &name );

// The character died: shows the death lesson, before the death screens.
void on_death();
// Default-mode actions handled here ("onboarding_checklist", "EXPLAIN_SCREEN"); returns
// whether the action was one of them.
bool handle_action( const std::string &action );
// The checklist of all lessons, to read them again, show them again or mark them done.
void show_checklist();
// Explains the screen whose input context has the given category.  Returns false, doing
// nothing, when hints are off.
bool explain_screen( const std::string &category );
// JSON loading of "onboarding_screen" (the explain-this-screen texts).
void load_screen( const JsonObject &jo, const std::string &src );

// The sidebar's "Next" line, empty when there is nothing to show.
// Wrapped to the given width, at most three lines.
std::string sidebar_text( int width );

// Saved with the avatar.
void serialize( JsonOut &json );
void deserialize( const JsonObject &jo );

} // namespace onboarding

#endif // CATA_SRC_ONBOARDING_H
