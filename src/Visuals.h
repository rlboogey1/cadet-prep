#pragma once
// Pictures for the pilot sections, drawn with ImDrawList:
//   Table Reading            - a grid of numbers with X across the top and Y down the side
//   Instrument Comprehension - an artificial horizon and a compass
//   Block Counting           - a stack of blocks in 3D (isometric view)
#include "Question.h"
#include "imgui.h"

namespace ui {

// How wide the picture wants to be, given the space available.
float visualWidth(const Visual& v, float available);

// Draws the picture at the cursor and advances past it.
void drawVisual(const Visual& v, float width);

// An aircraft in the given attitude, filling the box from `a` to `b` (Instrument
// Comprehension answers). The viewer is behind the aircraft's starting point,
// looking north and a little down: heading north points away, east points right.
void drawAircraft(ImDrawList* dl, ImVec2 a, ImVec2 b, int bankDeg, int pitchDeg, int headingDeg);

// The app logo: an F-22 in a blue circle.
void drawLogo(ImDrawList* dl, ImVec2 center, float radius);

// Just the jet silhouette, pointing at `angle` radians (0 = nose up).
void drawJet(ImDrawList* dl, ImVec2 center, float size, float angle, ImU32 color, ImU32 detail = 0);

}  // namespace ui
