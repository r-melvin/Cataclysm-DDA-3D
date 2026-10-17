#pragma once
#ifndef CATA_SRC_R3D_HUD_H
#define CATA_SRC_R3D_HUD_H

// Pure maths of the first-person overlay and the rotating minimap: bearings on the map, where
// they land on the compass strip, which screen edge an alert lights, and how the minimap turns.
// No SDL and no game types, so tests/r3d_test.cpp can check every convention.
//
// Conventions: map directions have x east and y south. A bearing is in radians clockwise from
// north, like a compass: north 0, east pi/2, south pi, west -pi/2. The first-person heading is in
// eighths of a turn the same way round (camera::heading), and its view yaw (camera::fp_yaw) is
// the same angle in radians.

namespace r3d
{

/** Bearing of the map offset (dx east, dy south) from north, clockwise; 0 for no offset. */
float map_bearing( float dx, float dy );

/** The same angle in (-pi, pi]. */
float wrap_angle( float radians );

/**
 * Bearing of the map offset (dx east, dy south) seen from a viewer looking along view_yaw:
 * 0 straight ahead, negative to the left, positive to the right, +-pi behind.
 */
float relative_bearing( float dx, float dy, float view_yaw );

/** Bearing of the heading (eighths of a turn clockwise from north) in radians. */
float heading_bearing( int heading );

/**
 * Horizontal position on the compass strip of something at the relative bearing rel, from the
 * strip's middle, for a strip of the given width showing span radians (the middle is straight
 * ahead). Bearings beyond the ends sit on the end they are nearer to.
 */
float compass_offset( float rel, float strip_width, float span );

/** Whether a relative bearing lies on the compass strip at all (within half its span). */
bool on_compass( float rel, float span );

enum class edge_side : int {
    none,   // in the picture
    left,
    right,
    behind, // the bottom edge
};

/**
 * Which edge of the picture an alert for something at the relative bearing rel lights, for a
 * picture whose horizontal field of view is fovx: none while it is inside the picture (its edge
 * included, so a diagonal neighbour at a 90 degree view raises no alert), the
 * bottom edge for what is behind (more than 135 degrees off the view), else the side to turn to.
 * The rear diagonals (exactly 135 degrees) light their side, which says which way to turn.
 */
edge_side edge_alert_side( float rel, float fovx );

/**
 * Turn of the heading-up minimap on screen, in radians clockwise (screen y points down): the
 * map turns the other way from the heading so that the way the player faces points up.
 */
float minimap_turn( int heading );

/** Turns the screen point (x, y) by radians clockwise (y down) about (cx, cy), in place. */
void turn_on_screen( float &x, float &y, float cx, float cy, float radians );

/**
 * First person: how far to slide an upright sprite standing at (x, y) along the view direction
 * (fx, fy), a unit vector on the ground, so that its quad (which faces the screen) lies at least
 * min_depth in front of the eye at (ex, ey). 0 when it already does or when it is behind the eye
 * (out of the picture anyway). Any 2D frame works as long as all points use the same one.
 */
float slide_from_eye( float x, float y, float ex, float ey, float fx, float fy, float min_depth );

} // namespace r3d

#endif // CATA_SRC_R3D_HUD_H
