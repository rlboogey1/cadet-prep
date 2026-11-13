#define IMGUI_DEFINE_MATH_OPERATORS  // lets us write ImVec2 + ImVec2
#include "Visuals.h"
#include "JetShape.h"
#include "Widgets.h"
#include "imgui_internal.h"  // ShadeVertsLinearColorGradientKeepAlpha
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <map>
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

    dl->AddRectFilled(p, p + size, u32(col::Card), 8.0f);
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + ch), u32(col::AccentSoft), 8.0f, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilled(p, ImVec2(p.x + cw, p.y + size.y), u32(col::AccentSoft), 8.0f, ImDrawFlags_RoundCornersLeft);
    // Headers: X across the top, Y down the side
    const char* corner = "Y\\X";
    textCentered(dl, fonts.label, p + ImVec2(cw * 0.5f, ch * 0.5f), u32(col::Accent), corner);
    for (int c = 0; c < N; ++c)
        textCentered(dl, fonts.label, p + ImVec2(cw * (c + 1.5f), ch * 0.5f), u32(col::InkSoft),
                     signedNum(c - R).c_str());
    for (int r = 0; r < N; ++r)
        textCentered(dl, fonts.label, p + ImVec2(cw * 0.5f, ch * (r + 1.5f)), u32(col::InkSoft),
                     signedNum(R - r).c_str());
    // Values
    for (int r = 0; r < N; ++r)
        for (int c = 0; c < N; ++c) {
            const ImVec2 cc = p + ImVec2(cw * (c + 1.5f), ch * (r + 1.5f));
            textCentered(dl, fonts.small, cc, u32(col::Ink),
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

// ---------------------------------------------------------------- aircraft (Instrument Comprehension answers)

struct V3 { float x, y, z; };
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 normalize(V3 a) {
    float l = std::sqrt(dot(a, a));
    return l > 0 ? a * (1.0f / l) : a;
}

enum class Part { Body, Wing, Fin, Canopy };
struct Face { std::vector<V3> pts; Part part; bool twoSided; };

// A simple jet in body coordinates: x = right wing, y = up, z = nose.
const std::vector<Face>& aircraftModel() {
    static const std::vector<Face> faces = [] {
        std::vector<Face> f;
        // Fuselage: rings of 8 points along its length, closed to a point at the nose
        const float ring[][2] = {{-1.05f, 0.05f}, {-0.70f, 0.10f}, {0.30f, 0.13f}, {0.75f, 0.11f}, {1.00f, 0.06f}};
        const int K = 8, R = 5;
        auto at = [&](int s, int k) {
            float a = 2.0f * kPi * (static_cast<float>(k) + 0.5f) / K;
            return V3{std::cos(a) * ring[s][1], std::sin(a) * ring[s][1] * 0.9f, ring[s][0]};
        };
        for (int s = 0; s + 1 < R; ++s)
            for (int k = 0; k < K; ++k)
                f.push_back({{at(s, k), at(s, (k + 1) % K), at(s + 1, (k + 1) % K), at(s + 1, k)}, Part::Body, false});
        const V3 nose{0, 0, 1.28f};
        for (int k = 0; k < K; ++k) f.push_back({{at(R - 1, k), at(R - 1, (k + 1) % K), nose}, Part::Body, false});
        std::vector<V3> tail;
        for (int k = K - 1; k >= 0; --k) tail.push_back(at(0, k));
        f.push_back({tail, Part::Body, false});

        // Wings and tailplanes: flat, split into strips so they sort well against the body
        auto surface = [&](float x0, float x1, float lead0, float trail0, float lead1, float trail1, float y0,
                           float dihedral, Part part, int strips) {
            for (int side : {-1, 1})
                for (int i = 0; i < strips; ++i) {
                    float t0 = static_cast<float>(i) / strips, t1 = static_cast<float>(i + 1) / strips;
                    auto pt = [&](float t, bool lead) {
                        float x = x0 + (x1 - x0) * t;
                        float z = lead ? lead0 + (lead1 - lead0) * t : trail0 + (trail1 - trail0) * t;
                        return V3{side * x, y0 + dihedral * (x - x0), z};
                    };
                    f.push_back({{pt(t0, true), pt(t1, true), pt(t1, false), pt(t0, false)}, part, true});
                }
        };
        surface(0.10f, 1.15f, 0.35f, -0.25f, -0.12f, -0.38f, 0.0f, 0.05f, Part::Wing, 3);
        surface(0.06f, 0.46f, -0.62f, -1.00f, -0.88f, -1.05f, 0.02f, 0.0f, Part::Wing, 1);
        // Tail fin
        f.push_back({{{0, 0.08f, -0.55f}, {0, 0.52f, -0.92f}, {0, 0.52f, -1.06f}, {0, 0.05f, -1.05f}}, Part::Fin, true});
        // Canopy
        const V3 cf{0, 0.10f, 0.95f}, cp{0, 0.20f, 0.62f}, cb{0, 0.12f, 0.32f}, cl{-0.08f, 0.10f, 0.62f}, cr{0.08f, 0.10f, 0.62f};
        f.push_back({{cf, cp, cl}, Part::Canopy, false});
        f.push_back({{cf, cr, cp}, Part::Canopy, false});
        f.push_back({{cb, cl, cp}, Part::Canopy, false});
        f.push_back({{cb, cp, cr}, Part::Canopy, false});
        return f;
    }();
    return faces;
}

ImVec4 shade(ImVec4 c, float k) { return ImVec4(c.x * k, c.y * k, c.z * k, c.w); }

}  // namespace

void drawAircraft(ImDrawList* dl, ImVec2 a, ImVec2 b, int bankDeg, int pitchDeg, int headingDeg) {
    // Backdrop: sky, and a strip of ground at the bottom so "down" is obvious
    const float groundY = b.y - (b.y - a.y) * 0.18f;
    const int v0 = dl->VtxBuffer.Size;
    dl->AddRectFilled(a, ImVec2(b.x, groundY), IM_COL32_WHITE);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, a, ImVec2(a.x, groundY),
                                                   IM_COL32(205, 225, 250, 255), IM_COL32(240, 246, 253, 255));
    dl->AddRectFilled(ImVec2(a.x, groundY), b, IM_COL32(222, 210, 186, 255));
    dl->AddLine(ImVec2(a.x, groundY), ImVec2(b.x, groundY), IM_COL32(176, 156, 120, 255), 1.0f);

    // Orientation: start from the heading, raise the nose by the pitch, then roll by the bank.
    // World axes: x = east, y = up, z = north.
    const float h = headingDeg * kPi / 180.0f, th = pitchDeg * kPi / 180.0f, ph = bankDeg * kPi / 180.0f;
    const V3 up0{0, 1, 0}, fwd0{std::sin(h), 0, std::cos(h)}, right0{std::cos(h), 0, -std::sin(h)};
    const V3 fwd = fwd0 * std::cos(th) + up0 * std::sin(th);
    const V3 upP = up0 * std::cos(th) - fwd0 * std::sin(th);
    const V3 right = right0 * std::cos(ph) - upP * std::sin(ph);  // bank right = right wing down
    const V3 up = upP * std::cos(ph) + right0 * std::sin(ph);
    auto toWorld = [&](V3 p) { return right * p.x + up * p.y + fwd * p.z; };

    // Camera looking north, tilted 12 degrees down, with a little perspective
    const float e = 12.0f * kPi / 180.0f, D = 5.0f;
    auto toCam = [&](V3 w) { return V3{w.x, w.y * std::cos(e) + w.z * std::sin(e), -w.y * std::sin(e) + w.z * std::cos(e)}; };
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + groundY) * 0.5f + (b.y - a.y) * 0.04f);
    const float S = std::min((b.x - a.x) * 0.42f, (groundY - a.y) * 0.50f);
    auto project = [&](V3 cam) {
        float k = S * D / (D + cam.z);
        return ImVec2(c.x + cam.x * k, c.y - cam.y * k);
    };

    const V3 light = normalize({-0.35f, 0.85f, -0.4f});
    const ImVec4 topCol(0.93f, 0.94f, 0.97f, 1), underCol(0.45f, 0.52f, 0.66f, 1), bodyCol(0.80f, 0.83f, 0.90f, 1),
                 finCol(0.247f, 0.373f, 0.855f, 1), glass(0.10f, 0.16f, 0.32f, 1), edge(0.16f, 0.22f, 0.40f, 1);

    struct Drawn { std::vector<ImVec2> pts; float depth; ImU32 fill; };
    std::vector<Drawn> drawn;
    for (const Face& f : aircraftModel()) {
        std::vector<V3> cam;
        for (const V3& p : f.pts) cam.push_back(toCam(toWorld(p)));
        V3 n = normalize(cross(cam[1] - cam[0], cam[2] - cam[0]));
        // The camera looks along +z, so a face points at it when n.z < 0
        bool facing = n.z < 0;
        if (!f.twoSided && !facing) continue;
        if (!facing) n = n * -1.0f;

        ImVec4 base = bodyCol;
        if (f.part == Part::Wing) {
            // Which side are we seeing? Compare with the aircraft's "up", in camera space
            const V3 upCam = toCam(up);
            base = dot(n, upCam) > 0 ? topCol : underCol;
        } else if (f.part == Part::Fin) {
            base = finCol;
        } else if (f.part == Part::Canopy) {
            base = glass;
        }
        const V3 lightCam = toCam(light);
        float k = 0.62f + 0.38f * std::max(0.0f, dot(n, lightCam));
        Drawn d;
        d.fill = u32(shade(base, k));
        d.depth = 0;
        for (const V3& p : cam) {
            d.pts.push_back(project(p));
            d.depth += p.z;
        }
        d.depth /= static_cast<float>(cam.size());
        drawn.push_back(std::move(d));
    }
    std::sort(drawn.begin(), drawn.end(), [](const Drawn& x, const Drawn& y) { return x.depth > y.depth; });
    const ImU32 line = u32(edge, 0.55f);
    for (const Drawn& d : drawn) {
        const int n = static_cast<int>(d.pts.size());
        dl->AddConvexPolyFilled(d.pts.data(), n, d.fill);
        dl->AddPolyline(d.pts.data(), n, line, ImDrawFlags_Closed, 0.8f);
    }
}

namespace {

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

float visualWidth(const Visual& v, float available, bool stacked) {
    if (stacked) {  // the picture gets the whole width (phones): fill it, within limits
        switch (v.kind) {
            case VisualKind::Table: {
                const int n = 2 * v.data[0] + 2;
                return std::clamp(available / static_cast<float>(n), 20.0f, 27.0f) * static_cast<float>(n);
            }
            case VisualKind::Instruments: return std::clamp((available - 24.0f) / 4.0f, 60.0f, 92.0f) * 4.0f + 24.0f;
            case VisualKind::Blocks: return std::clamp(available / 5.6f, 30.0f, 54.0f) * 5.6f;
            default: return 0.0f;
        }
    }
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

float visualHeight(const Visual& v, float width) {
    switch (v.kind) {
        case VisualKind::Table: {
            const int n = 2 * v.data[0] + 2;
            return std::max(19.0f, width / static_cast<float>(n) * 0.82f) * static_cast<float>(n);
        }
        case VisualKind::Instruments: return (width - 24.0f) / 2.0f + 30.0f;
        case VisualKind::Blocks: return width / 5.6f * 7.2f;
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

// ---------------------------------------------------------------- rank insignia
// Warrior Knowledge rank cards show pictures cut from the charts the user sent
// (data/insignia/*.png, background made transparent), loaded once as textures.

namespace {

struct Texture {
    ImTextureID id = 0;
    int w = 0, h = 0;
};

std::string gImageFolder;
TextureMaker gMakeTexture = nullptr;

const Texture* texture(const std::string& file) {
    static std::map<std::string, Texture> cache;  // also remembers files that failed (id 0)
    auto it = cache.find(file);
    if (it != cache.end()) return it->second.id ? &it->second : nullptr;
    Texture t;
    int n = 0;
    unsigned char* px = gMakeTexture ? stbi_load((gImageFolder + "/" + file).c_str(), &t.w, &t.h, &n, 4) : nullptr;
    if (px) {
        t.id = gMakeTexture(px, t.w, t.h);
        stbi_image_free(px);
    }
    return (cache[file] = t).id ? &cache[file] : nullptr;
}

// The picture for a rank, e.g. "officer_4.png" (see Question.h for the schemes).
std::string insigniaFile(int scheme, int index) {
    static const char* names[] = {"cadet", "officer", "af_enlisted", "sf_enlisted"};
    if (scheme < 0 || scheme > 3) return "";
    return std::string(names[scheme]) + "_" + std::to_string(index) + ".png";
}

}  // namespace

void setImageFolder(const std::string& folder) { gImageFolder = folder; }
void setTextureMaker(TextureMaker make) { gMakeTexture = make; }

void drawInsignia(const Visual& v, float height, float left, float width) {
    if (v.data.size() < 2) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float a = ImGui::GetStyle().Alpha;
    const ImVec2 cur = ImGui::GetCursorScreenPos();
    const ImVec2 c(left + width * 0.5f, cur.y + height * 0.5f);
    if (v.data[0] == 2 && v.data[1] == 1) {  // Airman Basic wears no insignia
        textCentered(dl, fonts.heading, c, u32(col::Muted, a), "No insignia");
    } else if (const Texture* t = texture(insigniaFile(v.data[0], v.data[1]))) {
        // Fit inside height x (at most 2.4 x height), never more than 3x the picture's size,
        // on a fixed light panel so dark insignia stay readable on a dark card.
        const float pad = std::max(4.0f, height * 0.07f);
        const float inner = height - 2 * pad;
        const float maxW = std::min(width - 2 * pad, inner * 2.4f);
        const float s = std::min({inner / static_cast<float>(t->h), maxW / static_cast<float>(t->w), 3.0f});
        const ImVec2 half(t->w * s * 0.5f, t->h * s * 0.5f);
        const ImVec2 p0 = c - half - ImVec2(pad, pad), p1 = c + half + ImVec2(pad, pad);
        dl->AddRectFilled(p0, p1, u32(ImVec4(0.945f, 0.953f, 0.965f, 1), a), kRadius);
        dl->AddRect(p0, p1, u32(ImVec4(0.82f, 0.84f, 0.87f, 1), a), kRadius, 0, 1.0f);
        dl->AddImage(t->id, c - half, c + half, ImVec2(0, 0), ImVec2(1, 1),
                     u32(ImVec4(1, 1, 1, 1), a));
    } else {
        textCentered(dl, fonts.small, c, u32(col::Muted, a), "Picture missing");
    }
    ImGui::SetCursorScreenPos(ImVec2(left, cur.y));
    ImGui::Dummy(ImVec2(width, height));
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

}  // namespace ui
