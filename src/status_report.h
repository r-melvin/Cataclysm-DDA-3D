#pragma once
#ifndef CATA_SRC_STATUS_REPORT_H
#define CATA_SRC_STATUS_REPORT_H

#include <string>

class avatar;

/**
 * One paragraph of plain text describing what the character perceives right now: time, weather,
 * wind, temperature, the moon, where they are, needs and conditions that are not normal, hostiles
 * in sight with their direction and distance, and sounds heard. The text equivalent of what the
 * 3D view shows in the world, built from the same sources as the sidebar widgets (so the same
 * gating: an exact time needs a clock, a temperature a thermometer). Meant for the message log
 * and screen readers.
 */
std::string status_report( const avatar &you );

#endif // CATA_SRC_STATUS_REPORT_H
