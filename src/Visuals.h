#pragma once
// Pictures for the pilot sections, drawn with ImDrawList:
//   Table Reading            - a grid of numbers with X across the top and Y down the side
//   Instrument Comprehension - an artificial horizon and a compass
//   Block Counting           - a stack of blocks in 3D (isometric view)
#include "Question.h"
#include "imgui.h"
#include <string>

namespace ui {

// How wide the picture wants to be, given the space available. `stacked` = the
// picture sits above the question instead of beside it (narrow screens), so it can
// use the whole width. A Table Reading grid can still be wider than a phone.
float visualWidth(const Visual& v, float available, bool stacked = false);

// How tall the picture is when drawn `width` wide.
float visualHeight(const Visual& v, float width);

// Draws the picture at the cursor and advances past it.
void drawVisual(const Visual& v, float width);

// An aircraft in the given attitude, filling the box from `a` to `b` (Instrument
// Comprehension answers). The viewer is behind the aircraft's starting point,
// looking north and a little down: heading north points away, east points right.
void drawAircraft(ImDrawList* dl, ImVec2 a, ImVec2 b, int bankDeg, int pitchDeg, int headingDeg);

// Rank insignia (VisualKind::Insignia): the picture from the image folder, fit in
// `height` and centered in the span from `left` to `left + width` at the cursor;
// advances past it. Fades with the style alpha. Call setImageFolder first.
void setImageFolder(const std::string& folder);  // e.g. <data>/insignia
// Turns RGBA pixels into a texture the renderer can draw. Each front end sets its
// own (OpenGL on the Mac, SDL on iPhone and iPad); without one, pictures are missing.
using TextureMaker = ImTextureID (*)(const unsigned char* rgba, int width, int height);
void setTextureMaker(TextureMaker make);
void drawInsignia(const Visual& v, float height, float left, float width);

// The app logo: an F-22 in a blue circle.
void drawLogo(ImDrawList* dl, ImVec2 center, float radius);

// Just the jet silhouette, pointing at `angle` radians (0 = nose up).
void drawJet(ImDrawList* dl, ImVec2 center, float size, float angle, ImU32 color, ImU32 detail = 0);

}  // namespace ui
