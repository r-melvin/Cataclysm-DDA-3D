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

// Whether hints are on for this game at all.
bool enabled();

// Called whenever the player is about to choose an action: checks lessons and shows any
// pending popup or hint.
void before_input();
// Called when the player chose an action, with the action's id from keybindings.json.
void on_action( const std::string &action_id );
// A one-off thing the game reports, such as "safe_mode_spotted".
void on_signal( const std::string &name );

// The sidebar's "Next" line, empty when there is nothing to show.
// Wrapped to the given width, at most three lines.
std::string sidebar_text( int width );

// Saved with the avatar.
void serialize( JsonOut &json );
void deserialize( const JsonObject &jo );

} // namespace onboarding

#endif // CATA_SRC_ONBOARDING_H
