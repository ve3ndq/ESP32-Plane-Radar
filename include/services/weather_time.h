#pragma once

#include <cstddef>

namespace services::weather {

/** Start UTC NTP synchronization. Safe to call after every reconnect. */
void begin();

/**
 * Refresh current conditions and the location's UTC offset when due.
 * Returns true only when displayable data changed.
 */
bool refreshIfDue(double latitude, double longitude, bool force = false);

bool valid();
void formatWeatherLine(char* out, size_t out_len);
void formatDateTimeLine(char* out, size_t out_len);

}  // namespace services::weather
