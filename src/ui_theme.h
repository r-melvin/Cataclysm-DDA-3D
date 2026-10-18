#pragma once
#ifndef CATA_SRC_UI_THEME_H
#define CATA_SRC_UI_THEME_H

#include "color.h"

/**
 * The parts of the selected menu style (data/raw/imgui_styles, see doc/user-guides/COLOR.md)
 * that reach beyond ImGui's own style: the boxes of curses-drawn screens and key hints. The
 * default is the classic look; a style file opts in with its "curses" and "keycaps" members.
 */
struct ui_theme {
    // Curses screens: draw_border() boxes drawn in the classic border colour use border instead,
    // and their titles (in the classic title colour) use title, padded by a space on each side.
    bool modern_borders = false;
    nc_color border = c_light_gray;
    nc_color title = c_light_red;
    // ImGui key hints ([a] apply) are drawn as a key cap: the key in a framed box.
    bool keycaps = false;
};

const ui_theme &get_ui_theme();
void set_ui_theme( const ui_theme &theme );

#endif // CATA_SRC_UI_THEME_H
