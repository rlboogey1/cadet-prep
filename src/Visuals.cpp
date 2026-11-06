#define IMGUI_DEFINE_MATH_OPERATORS  // lets us write ImVec2 + ImVec2
#include "Visuals.h"
#include "JetShape.h"
#include "Widgets.h"
#include "imgui_internal.h"  // ShadeVertsLinearColorGradientKeepAlpha
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace ui {

namespace {

constexpr float kPi = 3.14159265f;

ImU32 u32(ImVec4 c, float alphaMul = 1.0f) {
    c.w *= alphaMul;
    return ImGui::ColorConvertFloat4ToU32(c);
}

void textCentered(ImDrawList* dl, ImFont* f, ImVec2 c, ImU32 color, const char* text) {
    ImVec2 s = textSize(f, text);
    dl->AddText(f, fontPx(f), ImVec2(std::floor(c.x - s.x * 0.5f), std::floor(c.y - s.y * 0.5f)), color, text);
}

std::string signedNum(int v) {
    return v > 0 ? "+" + std::to_string(v) : v == 0 ? "0" : "\xE2\x88\x92" + std::to_string(-v);
}

// ---------------------------------------------------------------- Table Reading

float tableCell(int n, float available) { return std::clamp(available * 0.6f / static_cast<float>(n + 1), 20.0f, 27.0f); }

void drawTable(const Visual& v, float width) {
    const int R = v.data[0], N = 2 * R + 1;
    const float cw = width / static_cast<float>(N + 1);
    const float ch = std::max(19.0f, cw * 0.82f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 size(width, ch * static_cast<float>(N + 1));
    ImGui::Dummy(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Which cell is the mouse over? Highlight its row and column to help line things up.
    int hr = -1, hc = -1;
    if (ImGui::IsItemHovered()) {
        ImVec2 m = ImGui::GetMousePos() - p;
        hc = static_cast<int>(m.x / cw) - 1;
        hr = static_cast<int>(m.y / ch) - 1;
        if (hc < 0 || hc >= N || hr < 0 || hr >= N) hr = hc = -1;
    }

    dl->AddRectFilled(p, p + size, u32(col::Card), 8.0f);
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + ch), u32(col::AccentSoft), 8.0f, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilled(p, ImVec2(p.x + cw, p.y + size.y), u32(col::AccentSoft), 8.0f, ImDrawFlags_RoundCornersLeft);
    for (int r = 0; r < N; ++r) {
        if (r % 2 == 1)
            dl->AddRectFilled(ImVec2(p.x + cw, p.y + ch * (r + 1)), ImVec2(p.x + size.x, p.y + ch * (r + 2)), u32(col::Bg));
    }
    if (hr >= 0) {
        const ImVec4 hl(0.247f, 0.373f, 0.855f, 0.10f);
        dl->AddRectFilled(ImVec2(p.x, p.y + ch * (hr + 1)), ImVec2(p.x + size.x, p.y + ch * (hr + 2)), u32(hl));
        dl->AddRectFilled(ImVec2(p.x + cw * (hc + 1), p.y), ImVec2(p.x + cw * (hc + 2), p.y + size.y), u32(hl));
    }
    // Headers: X across the top, Y down the side
    const char* corner = "Y\\X";
    textCentered(dl, fonts.label, p + ImVec2(cw * 0.5f, ch * 0.5f), u32(col::Accent), corner);
    for (int c = 0; c < N; ++c)
        textCentered(dl, fonts.label, p + ImVec2(cw * (c + 1.5f), ch * 0.5f), u32(c == hc ? col::Accent : col::InkSoft),
                     signedNum(c - R).c_str());
    for (int r = 0; r < N; ++r)
        textCentered(dl, fonts.label, p + ImVec2(cw * 0.5f, ch * (r + 1.5f)), u32(r == hr ? col::Accent : col::InkSoft),
                     signedNum(R - r).c_str());
    // Values
    for (int r = 0; r < N; ++r)
        for (int c = 0; c < N; ++c) {
            const bool here = r == hr && c == hc;
            ImVec2 cc = p + ImVec2(cw * (c + 1.5f), ch * (r + 1.5f));
            if (here) dl->AddRect(cc - ImVec2(cw * 0.5f, ch * 0.5f), cc + ImVec2(cw * 0.5f, ch * 0.5f), u32(col::Accent), 3.0f, 0, 1.5f);
            textCentered(dl, fonts.small, cc, u32(here ? col::Accent : col::Ink),
                         std::to_string(v.data[static_cast<size_t>(1 + r * N + c)]).c_str());
        }
    dl->AddLine(ImVec2(p.x + cw, p.y), ImVec2(p.x + cw, p.y + size.y), u32(col::CardBorder), 1.0f);
    dl->AddLine(ImVec2(p.x, p.y + ch), ImVec2(p.x + size.x, p.y + ch), u32(col::CardBorder), 1.0f);
    dl->AddRect(p, p + size, u32(col::CardBorder), 8.0f, 0, 1.2f);
}

// ---------------------------------------------------------------- Instrument Comprehension

void attitudeIndicator(ImDrawList* dl, ImVec2 c, float r, int bankDeg, int pitchDeg) {
    const ImVec4 navy(0.110f, 0.149f, 0.255f, 1.0f);
    const ImVec4 sky(0.373f, 0.627f, 0.925f, 1.0f), ground(0.612f, 0.443f, 0.275f, 1.0f);
    dl->AddCircleFilled(c, r, u32(navy), 96);
    const float ri = r - 10.0f;

    // The miniature airplane stays fixed; the horizon moves. Banking right makes
    // the horizon rise on the right; nose up makes the horizon drop.
    const float phi = static_cast<float>(bankDeg) * kPi / 180.0f;
    const ImVec2 up(-std::sin(phi), -std::cos(phi));  // points toward the sky
    const ImVec2 along(std::cos(phi), -std::sin(phi));
    const ImVec2 h0 = c - up * (static_cast<float>(pitchDeg) * ri * 0.035f);
    auto side = [&](ImVec2 q) { return (q.x - h0.x) * up.x + (q.y - h0.y) * up.y; };

    dl->AddCircleFilled(c, ri, u32(ground), 96);
    std::vector<ImVec2> skyPoly, cuts;
    const int K = 128;
    for (int i = 0; i < K; ++i) {
        float t0 = 2.0f * kPi * i / K, t1 = 2.0f * kPi * (i + 1) / K;
        ImVec2 a = c + ImVec2(std::cos(t0), std::sin(t0)) * ri, b = c + ImVec2(std::cos(t1), std::sin(t1)) * ri;
        float sa = side(a), sb = side(b);
        if (sa >= 0) skyPoly.push_back(a);
        if ((sa >= 0) != (sb >= 0)) {
            ImVec2 x = a + (b - a) * (sa / (sa - sb));
            skyPoly.push_back(x);
            cuts.push_back(x);
        }
    }
    if (skyPoly.size() >= 3) dl->AddConvexPolyFilled(skyPoly.data(), static_cast<int>(skyPoly.size()), u32(sky));
    if (cuts.size() == 2) dl->AddLine(cuts[0], cuts[1], u32(col::White), 2.5f);

    // Pitch ladder: short lines 10 degrees above and below the horizon
    for (int d : {-10, 10}) {
        ImVec2 m = h0 + up * (static_cast<float>(d) * ri * 0.035f);
        if (std::hypot(m.x - c.x, m.y - c.y) < ri - 12)
            dl->AddLine(m - along * (ri * 0.22f), m + along * (ri * 0.22f), u32(col::White, 0.75f), 1.5f);
    }
    // Bank marks on the bezel, and a fixed pointer at the top
    for (int d : {-60, -30, -20, -10, 0, 10, 20, 30, 60}) {
        float a = (static_cast<float>(d) - 90.0f) * kPi / 180.0f;
        ImVec2 dir(std::cos(a), std::sin(a));
        float len = (d % 30 == 0) ? 9.0f : 5.0f;
        dl->AddLine(c + dir * (r - 1.0f), c + dir * (r - 1.0f - len), u32(col::White), 1.6f);
    }
    // Sky pointer: turns with the horizon and shows the bank angle against the marks
    ImVec2 tip = c + up * (ri - 2.0f), base = c + up * (ri - 13.0f);
    dl->AddTriangleFilled(tip, base + along * 6.0f, base - along * 6.0f, u32(col::White));
    // Miniature airplane (fixed)
    const ImU32 gold = u32(col::Gold);
    const float w = ri * 0.62f;
    dl->AddLine(c + ImVec2(-w, 0), c + ImVec2(-ri * 0.2f, 0), gold, 4.0f);
    dl->AddLine(c + ImVec2(-ri * 0.2f, 0), c + ImVec2(0, ri * 0.12f), gold, 4.0f);
    dl->AddLine(c + ImVec2(0, ri * 0.12f), c + ImVec2(ri * 0.2f, 0), gold, 4.0f);
    dl->AddLine(c + ImVec2(ri * 0.2f, 0), c + ImVec2(w, 0), gold, 4.0f);
    dl->AddCircleFilled(c, 4.0f, gold);
}

void compass(ImDrawList* dl, ImVec2 c, float r, int headingDeg) {
    const ImVec4 navy(0.110f, 0.149f, 0.255f, 1.0f), face(0.165f, 0.216f, 0.345f, 1.0f);
    dl->AddCircleFilled(c, r, u32(navy), 96);
    dl->AddCircleFilled(c, r - 10.0f, u32(face), 96);
    for (int d = 0; d < 360; d += 30) {
        float a = (static_cast<float>(d) - 90.0f) * kPi / 180.0f;
        ImVec2 dir(std::cos(a), std::sin(a));
        float len = d % 90 == 0 ? 10.0f : 6.0f;
        dl->AddLine(c + dir * (r - 11.0f), c + dir * (r - 11.0f - len), u32(col::White, 0.85f), 1.6f);
    }
    const char* letters[] = {"N", "E", "S", "W"};
    for (int i = 0; i < 4; ++i) {
        float a = (static_cast<float>(i) * 90.0f - 90.0f) * kPi / 180.0f;
        textCentered(dl, fonts.strong, c + ImVec2(std::cos(a), std::sin(a)) * (r - 34.0f),
                     u32(i == 0 ? col::Gold : col::White), letters[i]);
    }
    // Arrow pointing the way the aircraft is heading (0 = north, at the top)
    const float h = static_cast<float>(headingDeg) * kPi / 180.0f;
    const ImVec2 dir(std::sin(h), -std::cos(h)), perp(-dir.y, dir.x);
    const float len = r - 48.0f;
    const ImU32 arrow = u32(col::Gold);
    dl->AddLine(c - dir * (len * 0.55f), c + dir * (len - 12.0f), arrow, 4.0f);
    dl->AddTriangleFilled(c + dir * (len + 4.0f), c + dir * (len - 14.0f) + perp * 9.0f, c + dir * (len - 14.0f) - perp * 9.0f, arrow);
    dl->AddCircleFilled(c, 5.0f, u32(col::White));
}

float dialRadius(float available) { return std::clamp((available * 0.48f - 24.0f) / 4.0f, 66.0f, 92.0f); }

void drawInstruments(const Visual& v, float width) {
    const float r = (width - 24.0f) / 4.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, r * 2 + 30));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c1 = p + ImVec2(r, r), c2 = p + ImVec2(r * 3 + 24, r);
    attitudeIndicator(dl, c1, r, v.data[0], v.data[1]);
    compass(dl, c2, r, v.data[2]);
    textCentered(dl, fonts.label, ImVec2(c1.x, p.y + r * 2 + 16), u32(col::Muted), "ARTIFICIAL HORIZON");
    textCentered(dl, fonts.label, ImVec2(c2.x, p.y + r * 2 + 16), u32(col::Muted), "COMPASS");
}

// ---------------------------------------------------------------- Block Counting

float blockUnit(float available) { return std::clamp(available * 0.42f / 5.6f, 34.0f, 54.0f); }

void drawBlocks(const Visual& v, float width) {
    const float s = width / 5.6f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, s * 7.2f));
    ImDrawList* dl = ImGui::GetWindowDrawList();

    struct B { int x, y, z, sx, sy, sz, label; };
    std::vector<B> blocks;
    const int n = v.data[0];
    for (int i = 0; i < n; ++i) {
        const int* d = &v.data[static_cast<size_t>(1 + i * 7)];
        blocks.push_back({d[0], d[1], d[2], d[3], d[4], d[5], d[6]});
    }
    // Isometric projection: x runs down-right, y runs down-left, z runs up
    const ImVec2 origin(p.x + width * 0.5f, p.y + s * 4.1f);
    auto P = [&](float x, float y, float z) {
        return ImVec2(origin.x + (x - y) * s * 0.866f, origin.y + (x + y) * s * 0.5f - z * s);
    };
    // Draw far blocks first: lower layers, then back to front
    std::vector<int> order(blocks.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const B &A = blocks[static_cast<size_t>(a)], &Bb = blocks[static_cast<size_t>(b)];
        if (A.z != Bb.z) return A.z < Bb.z;
        return A.x + A.y < Bb.x + Bb.y;
    });
    const ImU32 top = u32(ImVec4(0.93f, 0.95f, 1.0f, 1)), left = u32(ImVec4(0.77f, 0.83f, 0.97f, 1)),
                right = u32(ImVec4(0.62f, 0.70f, 0.91f, 1)), edge = u32(ImVec4(0.16f, 0.22f, 0.40f, 1));
    for (int i : order) {
        const B& b = blocks[static_cast<size_t>(i)];
        const float x0 = b.x, y0 = b.y, z0 = b.z, x1 = b.x + b.sx, y1 = b.y + b.sy, z1 = b.z + b.sz;
        ImVec2 t[4] = {P(x0, y0, z1), P(x1, y0, z1), P(x1, y1, z1), P(x0, y1, z1)};        // top
        ImVec2 l[4] = {P(x0, y1, z1), P(x1, y1, z1), P(x1, y1, z0), P(x0, y1, z0)};        // front-left (+y)
        ImVec2 rr[4] = {P(x1, y0, z1), P(x1, y1, z1), P(x1, y1, z0), P(x1, y0, z0)};       // front-right (+x)
        dl->AddConvexPolyFilled(l, 4, left);
        dl->AddConvexPolyFilled(rr, 4, right);
        dl->AddConvexPolyFilled(t, 4, top);
        dl->AddPolyline(t, 4, edge, ImDrawFlags_Closed, 1.5f);
        dl->AddPolyline(l, 4, edge, ImDrawFlags_Closed, 1.5f);
        dl->AddPolyline(rr, 4, edge, ImDrawFlags_Closed, 1.5f);
    }
    // Number labels: on the top face if nothing sits on the block, otherwise on its visible end
    for (const B& b : blocks) {
        if (!b.label) continue;
        bool covered = false;
        for (const B& o : blocks)
            if (o.z == b.z + b.sz && o.x < b.x + b.sx && b.x < o.x + o.sx && o.y < b.y + b.sy && b.y < o.y + o.sy) covered = true;
        ImVec2 at;
        if (!covered) at = P(b.x + b.sx * 0.5f, b.y + b.sy * 0.5f, b.z + b.sz);
        else if (b.sx > b.sy) at = P(b.x + b.sx, b.y + b.sy * 0.5f, b.z + b.sz * 0.5f);   // end facing right
        else at = P(b.x + b.sx * 0.5f, b.y + b.sy, b.z + b.sz * 0.5f);                    // end facing left
        dl->AddCircleFilled(at, 12.0f, u32(col::White));
        dl->AddCircle(at, 12.0f, u32(col::Accent), 0, 2.0f);
        textCentered(dl, fonts.strong, at, u32(col::Ink), std::to_string(b.label).c_str());
    }
}

}  // namespace

float visualWidth(const Visual& v, float available) {
    switch (v.kind) {
        case VisualKind::Table: {
            const int n = 2 * v.data[0] + 1;
            return tableCell(n, available) * static_cast<float>(n + 1);
        }
        case VisualKind::Instruments: return dialRadius(available) * 4.0f + 24.0f;
        case VisualKind::Blocks: return blockUnit(available) * 5.6f;
        default: return 0.0f;
    }
}

void drawVisual(const Visual& v, float width) {
    switch (v.kind) {
        case VisualKind::Table: drawTable(v, width); break;
        case VisualKind::Instruments: drawInstruments(v, width); break;
        case VisualKind::Blocks: drawBlocks(v, width); break;
        default: break;
    }
}

// ---------------------------------------------------------------- logo

void drawJet(ImDrawList* dl, ImVec2 c, float size, float angle, ImU32 color, ImU32 detail) {
    const float ca = std::cos(angle), sa = std::sin(angle);
    auto X = [&](float x, float y) { return ImVec2(c.x + (x * ca - y * sa) * size, c.y + (x * sa + y * ca) * size); };
    ImVec2 pts[jetshape::kPoints];
    for (int i = 0; i < jetshape::kPoints; ++i) pts[i] = X(jetshape::kOutline[i][0], jetshape::kOutline[i][1]);
    dl->AddConcavePolyFilled(pts, jetshape::kPoints, color);
    if (!detail) return;
    ImVec2 can[jetshape::kCanopyPoints];
    for (int i = 0; i < jetshape::kCanopyPoints; ++i) can[i] = X(jetshape::kCanopy[i][0], jetshape::kCanopy[i][1]);
    dl->AddConvexPolyFilled(can, jetshape::kCanopyPoints, detail);
    for (const auto& t : jetshape::kTails)
        dl->AddLine(X(t[0], t[1]), X(t[2], t[3]), detail, std::max(1.0f, size * 0.025f));
}

void drawLogo(ImDrawList* dl, ImVec2 c, float r) {
    // Blue circle with a diagonal gradient, a thin inner ring, and the jet
    const int v0 = dl->VtxBuffer.Size;
    dl->AddCircleFilled(c, r, IM_COL32_WHITE, 64);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, c - ImVec2(r, r), c + ImVec2(r, r),
                                                   IM_COL32(79, 120, 234, 255), IM_COL32(27, 50, 102, 255));
    dl->AddCircle(c, r * 0.9f, IM_COL32(255, 255, 255, 90), 64, std::max(1.0f, r * 0.035f));
    const float js = r * 0.609f, ang = 40.0f * kPi / 180.0f;
    drawJet(dl, c + ImVec2(r * 0.03f, r * 0.045f), js, ang, IM_COL32(8, 18, 50, 90));  // soft shadow
    drawJet(dl, c, js, ang, IM_COL32_WHITE, IM_COL32(35, 62, 140, 255));
}

}  // namespace ui
