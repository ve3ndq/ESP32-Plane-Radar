#pragma once

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

/** Advance the radar sweep animation when its next frame is due. */
void radarDisplayRefreshSweep();

}  // namespace ui
