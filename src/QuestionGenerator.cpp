#include "QuestionGenerator.h"
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <numeric>
#include <set>

namespace {

using Rng = std::mt19937;

// ---------------------------------------------------------------- helpers

int ri(Rng& rng, int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }

template <typename T>
const T& pick(Rng& rng, const std::vector<T>& v) {
    return v[static_cast<size_t>(ri(rng, 0, static_cast<int>(v.size()) - 1))];
}

const char* kMinus = "\xE2\x88\x92";  // the "−" sign, nicer than a hyphen

// 1234567 -> "1,234,567"; negative numbers get a real minus sign
std::string commas(long long v) {
    std::string digits = std::to_string(v < 0 ? -v : v);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return v < 0 ? kMinus + out : out;
}

// A number with up to 2 decimals and no trailing zeros: 12, 7.5, 3.25
std::string num(double v) {
    double r = std::round(v * 100.0) / 100.0;
    if (std::fabs(r - std::round(r)) < 1e-9) return commas(static_cast<long long>(std::llround(r)));
    long long whole = static_cast<long long>(std::trunc(r));
    int cents = static_cast<int>(std::llround(std::fabs(r - static_cast<double>(whole)) * 100));
    std::string frac = cents % 10 == 0 ? std::to_string(cents / 10)
                                       : (cents < 10 ? "0" : "") + std::to_string(cents);
    std::string w = commas(whole);
    if (whole == 0 && r < 0) w = kMinus + std::string("0");
    return w + "." + frac;
}

// Dollars: "$54" or "$1,102.50"
std::string money(double v) {
    double r = std::round(v * 100.0) / 100.0;
    if (std::fabs(r - std::round(r)) < 1e-9) return "$" + commas(static_cast<long long>(std::llround(r)));
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.2f", r);
    std::string s(buf);
    size_t dot = s.find('.');
    return "$" + commas(std::stoll(s.substr(0, dot))) + s.substr(dot);
}

// Exponent as superscript: 12 -> "¹²"
std::string sup(int n) {
    static const char* d[] = {"\xE2\x81\xB0", "\xC2\xB9", "\xC2\xB2", "\xC2\xB3", "\xE2\x81\xB4",
                              "\xE2\x81\xB5", "\xE2\x81\xB6", "\xE2\x81\xB7", "\xE2\x81\xB8", "\xE2\x81\xB9"};
    std::string out;
    for (char c : std::to_string(n)) out += d[c - '0'];
    return out;
}

// "+ 5" or "− 5" for building expressions like 3x − 5
std::string signedTerm(int v) { return v < 0 ? std::string(kMinus) + " " + std::to_string(-v) : "+ " + std::to_string(v); }

std::string pi(int k) { return (k == 1 ? "" : std::to_string(k)) + "\xCF\x80"; }  // "16π"
const char* kRoot = "\xE2\x88\x9A";  // √

std::string fmt(const char* f, ...);
std::string plural(int n, const char* word);

std::string fmt(const char* f, ...) {
    char buf[600];
    va_list args;
    va_start(args, f);
    std::vsnprintf(buf, sizeof buf, f, args);
    va_end(args);
    return buf;
}

std::string plural(int n, const char* word) { return fmt("%d %s%s", n, word, n == 1 ? "" : "s"); }

// Builds a question: the correct answer plus the first four distinct wrong
// answers from `wrong`. Returns false if there aren't four usable ones, and the
// caller tries again with new numbers.
bool build(Question& q, const std::string& subtest, const std::string& tag, std::string prompt,
           const std::string& correct, const std::vector<std::string>& wrong, std::string explanation) {
    std::vector<std::string> choices{correct};
    std::set<std::string> seen{correct};
    for (const auto& w : wrong) {
        if (w.empty() || seen.count(w)) continue;
        seen.insert(w);
        choices.push_back(w);
        if (choices.size() == 5) break;
    }
    if (choices.size() < 5) return false;

    static unsigned counter = 0;
    q = Question{};
    q.id = "GEN-" + tag + "-" + std::to_string(++counter);
    q.subtest = subtest;
    q.prompt = std::move(prompt);
    q.choices = std::move(choices);
    q.correctIndex = 0;  // the bank shuffles choices before showing them
    q.explanation = std::move(explanation);
    return true;
}

using Template = std::function<bool(Rng&, Question&)>;

// ================================================================ Math Knowledge

const std::string MK = "math_knowledge";

bool mkLinear(Rng& rng, Question& q) {  // ax + b = c
    int a = ri(rng, 2, 9), x = ri(rng, -6, 12), b = ri(rng, -20, 20);
    if (x == 0 || b == 0) return false;
    int c = a * x + b;
    std::vector<std::string> w{num(x + 1), num(x - 1), num(-x), num(c - b)};
    if ((c + b) % a == 0) w.insert(w.begin(), num((c + b) / a));
    w.push_back(num(x + 2));
    return build(q, MK, "MK-lin", fmt("If %dx %s = %s, then x =", a, signedTerm(b).c_str(), num(c).c_str()),
                 num(x), w,
                 fmt("%s %d %s both sides: %dx = %s. Divide by %d: x = %s.",
                     b > 0 ? "Subtract" : "Add", std::abs(b), b > 0 ? "from" : "to", a, num(c - b).c_str(), a, num(x).c_str()));

}

bool mkEvaluate(Rng& rng, Question& q) {  // ax² + bx at x
    int a = ri(rng, 2, 5), b = ri(rng, -7, 7), x = ri(rng, -4, 5);
    if (b == 0 || x == 0 || x == 1) return false;
    int val = a * x * x + b * x;
    std::vector<std::string> w{num(a * x * x - b * x), num(-val), num(a * a * x * x + b * x),
                               num(2 * a * x + b * x), num(val + 2), num(val - 2)};
    return build(q, MK, "MK-eval",
                 fmt("What is the value of %dx%s %sx when x = %s?", a, sup(2).c_str(),
                     signedTerm(b).c_str(), num(x).c_str()),
                 num(val), w,
                 fmt("%d(%s)%s %s(%s) = %d(%d) %s %s = %s. Square x before multiplying by %d.", a,
                     num(x).c_str(), sup(2).c_str(), signedTerm(b).c_str(), num(x).c_str(), a, x * x,
                     b * x < 0 ? kMinus : "+", num(std::abs(b * x)).c_str(), num(val).c_str(), a));
}

bool mkRectangle(Rng& rng, Question& q) {
    int l = ri(rng, 4, 20), wd = ri(rng, 3, 15);
    if (l == wd) return false;
    bool area = ri(rng, 0, 1) == 1;
    int ar = l * wd, per = 2 * (l + wd);
    std::vector<std::string> w{num(area ? per : ar), num(l + wd), num(area ? ar * 2 : per / 2 + l),
                               num(area ? ar + l : per + 2), num(area ? ar - wd : 2 * l + wd)};
    return build(q, MK, area ? "MK-area" : "MK-perim",
                 fmt("A rectangle is %d by %d. What is its %s?", l, wd, area ? "area" : "perimeter"),
                 num(area ? ar : per), w,
                 area ? fmt("Area = length \xC3\x97 width = %d \xC3\x97 %d = %d. %d is the perimeter.", l, wd, ar, per)
                      : fmt("Perimeter = 2(length + width) = 2(%d + %d) = %d. %d is the area.", l, wd, per, ar));
}

bool mkCircle(Rng& rng, Question& q) {
    int r = ri(rng, 2, 12);
    bool area = ri(rng, 0, 1) == 1;
    bool givenDiameter = ri(rng, 0, 2) == 0;
    int d = 2 * r;
    std::string given = givenDiameter ? fmt("a diameter of %d", d) : fmt("a radius of %d", r);
    std::string correct = area ? pi(r * r) : pi(d);
    std::vector<std::string> w{area ? pi(d) : pi(r * r), area ? pi(d * d) : pi(r), pi(r * r * 2), pi(r + 2),
                               area ? pi(r * r + r) : pi(d * 2)};
    std::string expl = area ? fmt("Radius = %d, so A = \xCF\x80r%s = %s.%s", r, sup(2).c_str(), correct.c_str(),
                                  givenDiameter ? " Remember to halve the diameter first." : "")
                            : fmt("C = 2\xCF\x80r = \xCF\x80" "d = %s.", correct.c_str());
    return build(q, MK, area ? "MK-circA" : "MK-circC",
                 fmt("What is the %s of a circle with %s?", area ? "area" : "circumference", given.c_str()),
                 correct, w, expl);
}

bool mkPythagoras(Rng& rng, Question& q) {
    static const std::vector<std::array<int, 3>> triples{{3, 4, 5}, {5, 12, 13}, {8, 15, 17}, {7, 24, 25}};
    auto t = pick(rng, triples);
    int k = ri(rng, 1, t[2] > 13 ? 2 : 4);
    int a = t[0] * k, b = t[1] * k, c = t[2] * k;
    bool hyp = ri(rng, 0, 1) == 1;
    if (hyp) {
        std::vector<std::string> w{num(a + b), num(c + 1), num(c - 1), num(c * c), num(b + 1)};
        return build(q, MK, "MK-hyp",
                     fmt("A right triangle has legs of %d and %d. How long is the hypotenuse?", a, b),
                     num(c), w,
                     fmt("c%s = %d%s + %d%s = %d + %d = %d, so c = %d.", sup(2).c_str(), a, sup(2).c_str(), b,
                         sup(2).c_str(), a * a, b * b, c * c, c));
    }
    std::vector<std::string> w{num(c - a), num(c + a), num(b + 1), num(b - 1), num(c)};
    return build(q, MK, "MK-leg",
                 fmt("A right triangle has a hypotenuse of %d and one leg of %d. How long is the other leg?", c, a),
                 num(b), w,
                 fmt("b%s = %d%s %s %d%s = %d %s %d = %d, so b = %d.", sup(2).c_str(), c, sup(2).c_str(), kMinus, a,
                     sup(2).c_str(), c * c, kMinus, a * a, b * b, b));
}

bool mkExponents(Rng& rng, Question& q) {
    int m = ri(rng, 2, 7), n = ri(rng, 2, 7);
    std::string base = pick(rng, std::vector<std::string>{"x", "y", "a", "n"});
    int kind = ri(rng, 0, 2);
    if (kind == 0) {  // multiply: add exponents
        std::vector<std::string> w{base + sup(m * n), base + sup(m + n + 1), "2" + base + sup(m + n),
                                   base + sup(std::abs(m - n) + 1), base + sup(m * n + 1)};
        return build(q, MK, "MK-expmul", fmt("Simplify %s%s \xC2\xB7 %s%s.", base.c_str(), sup(m).c_str(), base.c_str(), sup(n).c_str()),
                     base + sup(m + n), w, fmt("Same base, multiplying: add the exponents, %d + %d = %d.", m, n, m + n));
    }
    if (kind == 1) {  // power of a power: multiply exponents
        std::vector<std::string> w{base + sup(m + n), std::to_string(n) + base + sup(m), base + sup(m * n + 1),
                                   base + sup(m * n - 1), base + sup(m + n + 1)};
        return build(q, MK, "MK-exppow", fmt("Simplify (%s%s)%s.", base.c_str(), sup(m).c_str(), sup(n).c_str()),
                     base + sup(m * n), w, fmt("A power raised to a power: multiply the exponents, %d \xC3\x97 %d = %d.", m, n, m * n));
    }
    int hi = std::max(m, n) + 3, lo = std::min(m, n);  // divide: subtract exponents
    std::vector<std::string> w{base + sup(hi + lo), base + sup(hi * lo), base + sup(hi - lo + 1),
                               base + sup(hi - lo - 1 > 0 ? hi - lo - 1 : hi), "1"};
    return build(q, MK, "MK-expdiv", fmt("Simplify %s%s \xC3\xB7 %s%s.", base.c_str(), sup(hi).c_str(), base.c_str(), sup(lo).c_str()),
                 base + sup(hi - lo), w, fmt("Same base, dividing: subtract the exponents, %d %s %d = %d.", hi, kMinus, lo, hi - lo));
}

bool mkSlope(Rng& rng, Question& q) {
    int m = ri(rng, -5, 5), x1 = ri(rng, -5, 6), y1 = ri(rng, -6, 8), dx = ri(rng, 1, 5);
    if (m == 0 || m == 1 || m == -1) return false;
    int x2 = x1 + dx, y2 = y1 + m * dx;
    std::string recip = std::string(m < 0 ? kMinus : "") + "1/" + std::to_string(std::abs(m));
    std::vector<std::string> w{num(-m), recip, num(m + 1), num(m - 1), num(m * dx)};
    return build(q, MK, "MK-slope",
                 fmt("What is the slope of the line through (%s, %s) and (%s, %s)?", num(x1).c_str(), num(y1).c_str(),
                     num(x2).c_str(), num(y2).c_str()),
                 num(m), w,
                 fmt("Slope = (y%s %s y%s) / (x%s %s x%s) = %s / %d = %s. Rise over run, not run over rise.",
                     "\xE2\x82\x82", kMinus, "\xE2\x82\x81", "\xE2\x82\x82", kMinus, "\xE2\x82\x81",
                     num(y2 - y1).c_str(), dx, num(m).c_str()));
}

bool mkPercent(Rng& rng, Question& q) {
    int p = pick(rng, std::vector<int>{5, 10, 12, 15, 20, 25, 30, 40, 60, 75});
    int n = ri(rng, 2, 40) * 20;
    double ans = p * n / 100.0;
    std::vector<std::string> w{num(ans * 10), num(n - ans), num(ans * 2), num(ans + p), num(ans / 2)};
    return build(q, MK, "MK-pct", fmt("What is %d%% of %s?", p, num(n).c_str()), num(ans), w,
                 fmt("%d%% = %s, and %s \xC3\x97 %s = %s.", p, num(p / 100.0).c_str(), num(p / 100.0).c_str(),
                     num(n).c_str(), num(ans).c_str()));
}

bool mkPolygon(Rng& rng, Question& q) {
    static const char* names[] = {"", "", "", "triangle", "quadrilateral", "pentagon", "hexagon", "heptagon",
                                  "octagon", "nonagon", "decagon"};
    int n = ri(rng, 5, 12);
    std::string shape = n <= 10 ? std::string(n == 8 ? "an " : "a ") + names[n]
                                : fmt("%s %d-sided polygon", n == 11 ? "an" : "a", n);
    bool each = ri(rng, 0, 2) == 0 && (360 % n == 0);
    if (each) {
        int interior = 180 - 360 / n;
        std::vector<std::string> w{fmt("%d\xC2\xB0", 360 / n), fmt("%d\xC2\xB0", (n - 2) * 180), fmt("%d\xC2\xB0", interior + 10),
                                   fmt("%d\xC2\xB0", 180 / n * 2 + 60), fmt("%d\xC2\xB0", interior - 12)};
        return build(q, MK, "MK-polyeach", fmt("What is the measure of each interior angle of a regular %s?", n <= 10 ? names[n] : fmt("%d-sided polygon", n).c_str()),
                     fmt("%d\xC2\xB0", interior), w,
                     fmt("Total = (%d %s 2) \xC3\x97 180\xC2\xB0 = %d\xC2\xB0. Divide by %d: %d\xC2\xB0. (%d\xC2\xB0 is each exterior angle.)",
                         n, kMinus, (n - 2) * 180, n, interior, 360 / n));
    }
    int sum = (n - 2) * 180;
    std::vector<std::string> w{fmt("%d\xC2\xB0", n * 180), fmt("%d\xC2\xB0", (n - 1) * 180), fmt("%d\xC2\xB0", (n - 3) * 180),
                               "360\xC2\xB0", fmt("%d\xC2\xB0", sum + 90)};
    return build(q, MK, "MK-polysum", fmt("What is the sum of the interior angles of %s?", shape.c_str()),
                 fmt("%d\xC2\xB0", sum), w,
                 fmt("Sum = (n %s 2) \xC3\x97 180\xC2\xB0 = (%d %s 2) \xC3\x97 180\xC2\xB0 = %d\xC2\xB0.", kMinus, n, kMinus, sum));
}

bool mkSystem(Rng& rng, Question& q) {
    int x = ri(rng, 3, 18), y = ri(rng, 1, 14);
    if (x == y) return false;
    int s = x + y, d = x - y;
    bool askX = ri(rng, 0, 1) == 1;
    int ans = askX ? x : y;
    std::vector<std::string> w{num(askX ? y : x), num(s), num(std::abs(d)), num(ans + 1), num(ans * 2)};
    return build(q, MK, "MK-system",
                 fmt("If x + y = %d and x %s y = %s, what is %s?", s, kMinus, num(d).c_str(), askX ? "x" : "y"),
                 num(ans), w,
                 fmt("Add the equations: 2x = %d, so x = %d. Then y = %d %s %d = %d.", 2 * x, x, s, kMinus, x, y));
}

bool mkMean(Rng& rng, Question& q) {
    std::vector<int> v(5);
    for (auto& e : v) e = ri(rng, 2, 30);
    int sum = std::accumulate(v.begin(), v.end(), 0);
    v[4] += (5 - sum % 5) % 5;  // make the mean a whole number
    sum = std::accumulate(v.begin(), v.end(), 0);
    int mean = sum / 5;
    std::vector<int> sorted = v;
    std::sort(sorted.begin(), sorted.end());
    int median = sorted[2];
    std::vector<std::string> w{num(median), num(sum), num(mean + 1), num(mean - 1), num(sum / 4.0)};
    return build(q, MK, "MK-mean",
                 fmt("What is the mean (average) of %d, %d, %d, %d, and %d?", v[0], v[1], v[2], v[3], v[4]),
                 num(mean), w, fmt("Add them: %d. Divide by 5: %d.", sum, mean));
}

bool mkRoot(Rng& rng, Question& q) {
    int k = ri(rng, 2, 9), m = pick(rng, std::vector<int>{2, 3, 5, 6, 7});
    int inside = k * k * m;
    auto r = [&](int a, int b) { return (a == 1 ? std::string() : std::to_string(a)) + kRoot + std::to_string(b); };
    std::vector<std::string> w{r(m, k), r(k * k, m), r(k, m * k), num(k * m), r(k + 1, m), r(k - 1, m)};
    return build(q, MK, "MK-root", fmt("Simplify %s%d.", kRoot, inside), r(k, m), w,
                 fmt("%s%d = %s(%d \xC3\x97 %d) = %d%s%d.", kRoot, inside, kRoot, k * k, m, k, kRoot, m));
}

// ================================================================ Arithmetic Reasoning

const std::string AR = "arithmetic_reasoning";

bool arDistance(Rng& rng, Question& q) {
    bool plane = ri(rng, 0, 1) == 1;
    int rate = plane ? ri(rng, 12, 60) * 10 : ri(rng, 30, 75);
    double t = pick(rng, std::vector<double>{1.5, 2, 2.5, 3, 3.5, 4, 4.5, 5});
    double d = rate * t;
    std::string who = plane ? "An aircraft flies" : "A convoy drives";
    std::vector<std::string> w{num(rate * std::floor(t)) + " miles", num(rate * (t + 0.5)) + " miles",
                               num(rate * (t - 0.5)) + " miles", num(d / 2) + " miles", num(d * 2) + " miles"};
    return build(q, AR, "AR-dist", fmt("%s at %s mph for %s hours. How far does it travel?", who.c_str(), num(rate).c_str(), num(t).c_str()),
                 num(d) + " miles", w,
                 fmt("Distance = rate \xC3\x97 time = %s \xC3\x97 %s = %s miles.", num(rate).c_str(), num(t).c_str(), num(d).c_str()));
}

bool arDiscountTax(Rng& rng, Question& q) {
    int p = ri(rng, 4, 40) * 5;
    int d = pick(rng, std::vector<int>{10, 20, 25, 30, 40, 50});
    int t = pick(rng, std::vector<int>{5, 6, 8, 10});
    double sale = p * (100 - d) / 100.0;
    double cents = sale * (100 + t);  // final price in cents
    if (std::fabs(cents - std::round(cents)) > 1e-6) return false;  // skip half-cent prices
    double fin = std::round(cents) / 100.0;
    std::string item = pick(rng, std::vector<std::string>{"jacket", "pair of boots", "headset", "backpack", "flight bag"});
    std::vector<std::string> w{money(sale), money(p * (100 - d + t) / 100.0), money(p * (100 + t) / 100.0),
                               money(p * d / 100.0), money(fin + 5)};
    return build(q, AR, "AR-disc",
                 fmt("A %s costs %s. It is discounted %d%%, and then %d%% sales tax is added to the sale price. What is the final cost?",
                     item.c_str(), money(p).c_str(), d, t),
                 money(fin), w,
                 fmt("Sale price: %s \xC3\x97 %s = %s. With tax: %s \xC3\x97 %s = %s.", money(p).c_str(), num((100 - d) / 100.0).c_str(),
                     money(sale).c_str(), money(sale).c_str(), num((100 + t) / 100.0).c_str(), money(fin).c_str()));
}

bool arWorkRate(Rng& rng, Question& q) {
    static const std::vector<std::array<int, 2>> pairs{{3, 6}, {4, 12}, {6, 12}, {5, 20}, {10, 15}, {12, 24}, {6, 3},
                                                       {2, 3}, {4, 6}, {8, 24}, {20, 30}, {9, 18}};
    auto pr = pick(rng, pairs);
    int a = pr[0], b = pr[1];
    double t = a * b / static_cast<double>(a + b);
    std::string who = pick(rng, std::vector<std::string>{"crew", "pump", "printer", "team"});
    std::string job = who == "pump" ? "fill a tank" : who == "printer" ? "print a batch of manuals" : "finish an inspection";
    std::vector<std::string> w{num((a + b) / 2.0) + " hours", num(a + b) + " hours", num(std::abs(a - b)) + " hours",
                               num(std::min(a, b)) + " hours", num(t + 1) + " hours"};
    return build(q, AR, "AR-work",
                 fmt("One %s can %s in %d hours. A second %s can do it in %d hours. Working together, how long will it take?",
                     who.c_str(), job.c_str(), a, who.c_str(), b),
                 num(t) + " hours", w,
                 fmt("Rates add: 1/%d + 1/%d of the job per hour. Time = (%d \xC3\x97 %d) \xC3\xB7 (%d + %d) = %s hours.",
                     a, b, a, b, a, b, num(t).c_str()));
}

bool arAverage(Rng& rng, Question& q) {
    int n = ri(rng, 3, 5), avg = ri(rng, 72, 88), target = avg + ri(rng, 1, 3);
    int need = target * (n + 1) - avg * n;
    if (need > 100) return false;
    std::vector<std::string> w{num(target), num(need - 5), num(need + 4), num(target + (target - avg)), num(100)};
    return build(q, AR, "AR-avg",
                 fmt("A cadet's average on %d tests is %d. What score is needed on the next test to raise the average to %d?",
                     n, avg, target),
                 num(need), w,
                 fmt("Needed total = %d \xC3\x97 %d = %d. Current total = %d \xC3\x97 %d = %d. %d %s %d = %d.",
                     n + 1, target, target * (n + 1), n, avg, avg * n, target * (n + 1), kMinus, avg * n, need));
}

bool arRatio(Rng& rng, Question& q) {
    int a = ri(rng, 1, 5), b = ri(rng, 2, 7);
    if (a == b || std::gcd(a, b) != 1) return false;
    int unit = ri(rng, 3, 15), total = (a + b) * unit;
    std::vector<std::string> w{num(b * unit), num(total / a), num(total - b), num(a * unit + unit), num(total / (a + b))};
    return build(q, AR, "AR-ratio",
                 fmt("A squadron of %d people has officers and enlisted members in a %d:%d ratio. How many are officers?", total, a, b),
                 num(a * unit), w,
                 fmt("%d + %d = %d parts. %d \xC3\xB7 %d = %d per part. Officers = %d \xC3\x97 %d = %d.",
                     a, b, a + b, total, a + b, unit, a, unit, a * unit));
}

bool arInterest(Rng& rng, Question& q) {
    int P = ri(rng, 5, 50) * 100, r = ri(rng, 2, 8), y = ri(rng, 2, 6);
    double I = P * r * y / 100.0;
    std::vector<std::string> w{money(P + I), money(P * r / 100.0), money(I * 2), money(I / 2), money(P * r * y / 10.0)};
    return build(q, AR, "AR-int",
                 fmt("%s is invested at %d%% simple annual interest. How much interest is earned in %d years?", money(P).c_str(), r, y),
                 money(I), w,
                 fmt("I = Prt = %s \xC3\x97 %s \xC3\x97 %d = %s. %s would be the total balance, not the interest.",
                     money(P).c_str(), num(r / 100.0).c_str(), y, money(I).c_str(), money(P + I).c_str()));
}

bool arFuel(Rng& rng, Question& q) {
    int B = pick(rng, std::vector<int>{8, 10, 12, 16, 20});
    int R = pick(rng, std::vector<int>{30, 45});
    if (R == 45 && B % 4 != 0) return false;
    double hours = pick(rng, std::vector<double>{3, 3.5, 4, 4.5, 5, 5.5, 6});
    double C = B * (hours + R / 60.0);
    if (std::fabs(C - std::round(C)) > 1e-9) return false;
    std::vector<std::string> w{num(C / B), num(hours - 0.5), num(hours + 0.5), num(hours - 1), num(hours + R / 60.0 + 0.5)};
    return build(q, AR, "AR-fuel",
                 fmt("An airplane burns %d gallons of fuel per hour and carries %s gallons. The pilot must land with a %d-minute fuel reserve. How many hours can it fly before reaching the reserve?",
                     B, num(C).c_str(), R),
                 num(hours), w,
                 fmt("Reserve = %s \xC3\x97 %d = %s gallons. Usable fuel = %s gallons. %s \xC3\xB7 %d = %s hours.",
                     num(R / 60.0).c_str(), B, num(B * R / 60.0).c_str(), num(C - B * R / 60.0).c_str(),
                     num(C - B * R / 60.0).c_str(), B, num(hours).c_str()));
}

bool arPercentChange(Rng& rng, Question& q) {
    int pct = pick(rng, std::vector<int>{5, 10, 15, 20, 25, 30, 40, 50});
    int oldv = ri(rng, 2, 40) * 20;
    bool up = ri(rng, 0, 1) == 1;
    double diff = oldv * pct / 100.0;
    double newv = up ? oldv + diff : oldv - diff;
    std::vector<std::string> w{num(diff / newv * 100) + "%", num(pct + 5) + "%", num(pct - 5 > 0 ? pct - 5 : pct + 10) + "%",
                               num(pct * 2) + "%", num(100 - pct) + "%"};
    return build(q, AR, "AR-pchg",
                 fmt("A price %s from %s to %s. What is the percent %s?", up ? "rises" : "drops", money(oldv).c_str(),
                     money(newv).c_str(), up ? "increase" : "decrease"),
                 num(pct) + "%", w,
                 fmt("Change = %s. Divide by the original price: %s \xC3\xB7 %s = %s = %d%%.", money(diff).c_str(),
                     money(diff).c_str(), money(oldv).c_str(), num(pct / 100.0).c_str(), pct));
}

bool arProportion(Rng& rng, Question& q) {
    int b = pick(rng, std::vector<int>{4, 6, 8, 12, 24}), a = ri(rng, 2, 5);
    int c = b * ri(rng, 2, 5) / 2;
    if (c == b) return false;
    double ans = a * c / static_cast<double>(b);
    std::vector<std::string> w{num(ans + 1), num(ans - 0.5 > 0 ? ans - 0.5 : ans + 2), num(ans * 2), num(a + c - b),
                               num(c / static_cast<double>(a))};
    return build(q, AR, "AR-prop",
                 fmt("A recipe uses %d cups of flour for %d servings. How many cups are needed for %d servings?", a, b, c),
                 num(ans), w,
                 fmt("%d \xC3\xB7 %d = %s batches. %s \xC3\x97 %d = %s cups.", c, b, num(c / static_cast<double>(b)).c_str(),
                     num(c / static_cast<double>(b)).c_str(), a, num(ans).c_str()));
}

bool arOpposite(Rng& rng, Question& q) {
    int s1 = ri(rng, 6, 12) * 50, s2 = ri(rng, 6, 12) * 50;
    double t = pick(rng, std::vector<double>{1.5, 2, 2.5, 3, 4});
    double D = (s1 + s2) * t;
    std::vector<std::string> w{num(t + 0.5), num(t + 1), num(t * 2), num(t - 0.5), num(t + 1.5)};
    return build(q, AR, "AR-opp",
                 fmt("Two jets leave the same base at the same time, flying in opposite directions at %d mph and %d mph. After how many hours will they be %s miles apart?",
                     s1, s2, num(D).c_str()),
                 num(t), w,
                 fmt("Moving apart, their speeds add: %d mph. %s \xC3\xB7 %d = %s hours.", s1 + s2, num(D).c_str(), s1 + s2, num(t).c_str()));
}

bool arOvertime(Rng& rng, Question& q) {
    int r = ri(rng, 14, 32), h = ri(rng, 42, 52), ot = h - 40;
    double pay = 40 * r + ot * 1.5 * r;
    std::vector<std::string> w{money(h * r), money(h * 1.5 * r), money(40 * r + ot * 2.0 * r), money(40 * r + ot * 0.5 * r), money(pay + r)};
    return build(q, AR, "AR-ot",
                 fmt("A worker earns %s per hour, with time-and-a-half for hours over 40. How much does she earn for %d hours?",
                     money(r).c_str(), h),
                 money(pay), w,
                 fmt("40 \xC3\x97 %s = %s. Overtime: %d \xC3\x97 %s = %s. Total %s.", money(r).c_str(), money(40 * r).c_str(), ot,
                     money(1.5 * r).c_str(), money(ot * 1.5 * r).c_str(), money(pay).c_str()));
}

std::string clock12(int minutes) {  // minutes after midnight -> "12:35 p.m."
    minutes %= 24 * 60;
    int h = minutes / 60, m = minutes % 60;
    int h12 = h % 12 == 0 ? 12 : h % 12;
    return fmt("%d:%02d %s", h12, m, h < 12 ? "a.m." : "p.m.");
}

bool arClock(Rng& rng, Question& q) {
    int start = ri(rng, 6 * 12, 11 * 12 + 11) * 5;  // 6:00 to 11:55 a.m.
    int dh = ri(rng, 1, 4), dm = ri(rng, 1, 11) * 5;
    int end = start + dh * 60 + dm;
    std::vector<std::string> w{clock12(end - 60), clock12(end + 60), clock12(end - 20), clock12(end + 10),
                               clock12(end - 40)};
    return build(q, AR, "AR-clock",
                 fmt("A flight departs at %s and lasts %d hour%s %d minutes. When does it arrive?", clock12(start).c_str(), dh,
                     dh == 1 ? "" : "s", dm),
                 clock12(end), w,
                 fmt("Add the hours first: %s plus %d hour%s is %s, and %d more minutes gives %s",
                     clock12(start).c_str(), dh, dh == 1 ? "" : "s", clock12(start + dh * 60).c_str(), dm,
                     clock12(end).c_str()));
}


// ---------------------------------------------------------------- more helpers

// "3x + 4", "−x − 2", "2x", "5": a linear expression with whole-number coefficients
std::string linear(int a, int b, const char* var = "x") {
    std::string out;
    if (a != 0) {
        out = a == 1 ? std::string(var) : a == -1 ? std::string(kMinus) + var : (a < 0 ? kMinus + std::to_string(-a) : std::to_string(a)) + var;
    }
    if (b != 0 || a == 0) {
        if (a == 0) out = b < 0 ? kMinus + std::to_string(-b) : std::to_string(b);
        else out += " " + signedTerm(b);
    }
    return out;
}

// A fraction in lowest terms: "7/12", "−1/3", or a whole number when it divides evenly
std::string frac(long long n, long long d) {
    if (d < 0) n = -n, d = -d;
    const long long g = std::gcd(n < 0 ? -n : n, d);
    if (g > 1) n /= g, d /= g;
    if (d == 1) return commas(n);
    return (n < 0 ? std::string(kMinus) : std::string()) + std::to_string(n < 0 ? -n : n) + "/" + std::to_string(d);
}

// Exponent with a minus sign allowed: −4 -> "⁻⁴"
std::string supSigned(int n) { return n < 0 ? "\xE2\x81\xBB" + sup(-n) : sup(n); }

const char* kTimes = "\xC3\x97";  // ×
const char* kLess = "<";
const char* kGreater = ">";

// ================================================================ Math Knowledge (more)

bool mkInequality(Rng& rng, Question& q) {  // ax + b < c
    int a = ri(rng, -6, 6), k = ri(rng, -8, 10), b = ri(rng, -15, 15);
    if (a == 0 || a == 1 || b == 0) return false;
    bool lessThan = ri(rng, 0, 1) == 1;
    int c = a * k + b;
    const char* sign = lessThan ? kLess : kGreater;
    const char* flipped = lessThan ? kGreater : kLess;
    const char* ans = a > 0 ? sign : flipped;   // dividing by a negative flips the sign
    const char* other = a > 0 ? flipped : sign;
    std::vector<std::string> w{fmt("x %s %s", other, num(k).c_str()), fmt("x %s %s", ans, num(-k).c_str()),
                               fmt("x %s %s", ans, num(k + 1).c_str()), fmt("x %s %s", other, num(-k).c_str()),
                               fmt("x %s %s", ans, num(k - 1).c_str())};
    return build(q, MK, "MK-ineq", fmt("Solve for x: %s %s %s", linear(a, b).c_str(), sign, num(c).c_str()),
                 fmt("x %s %s", ans, num(k).c_str()), w,
                 fmt("%s %d %s both sides: %s %s %s. Divide by %s: x %s %s.%s", b > 0 ? "Subtract" : "Add", std::abs(b),
                     b > 0 ? "from" : "to", linear(a, 0).c_str(), sign, num(c - b).c_str(), num(a).c_str(), ans,
                     num(k).c_str(), a < 0 ? " Dividing by a negative number flips the inequality sign." : ""));
}

bool mkQuadratic(Rng& rng, Question& q) {  // x² + bx + c = 0 with whole-number roots
    int r1 = ri(rng, -9, 9), r2 = ri(rng, -9, 9);
    if (r1 == 0 || r2 == 0 || r1 == r2 || r1 == -r2) return false;
    if (r1 > r2) std::swap(r1, r2);
    int b = -(r1 + r2), c = r1 * r2;
    auto roots = [](int x, int y) {
        if (x > y) std::swap(x, y);
        return fmt("x = %s or x = %s", num(x).c_str(), num(y).c_str());
    };
    std::vector<std::string> w{roots(-r1, -r2), roots(r1, -r2), roots(-r1, r2), roots(r1 + 1, r2 + 1), roots(b, c)};
    std::string poly = "x" + sup(2) + (b == 0 ? "" : " " + (b == 1 ? std::string("+ x") : b == -1 ? kMinus + std::string(" x") : signedTerm(b) + "x")) +
                       " " + signedTerm(c);
    return build(q, MK, "MK-quad", fmt("What are the solutions of %s = 0?", poly.c_str()), roots(r1, r2), w,
                 fmt("Find two numbers that multiply to %s and add to %s: %s and %s. So (x %s)(x %s) = 0, and x = %s or x = %s.",
                     num(c).c_str(), num(b).c_str(), num(-r1).c_str(), num(-r2).c_str(), signedTerm(-r1).c_str(),
                     signedTerm(-r2).c_str(), num(r1).c_str(), num(r2).c_str()));
}

bool mkDistribute(Rng& rng, Question& q) {  // a(x + b) − c(x − d)
    int a = ri(rng, 2, 7), b = ri(rng, 1, 9), c = ri(rng, 2, 6), d = ri(rng, 1, 9);
    if (a == c) return false;
    int x = a - c, k = a * b + c * d;
    std::vector<std::string> w{linear(x, a * b - c * d), linear(a + c, k), linear(x, a * b - d), linear(x, b + d),
                               linear(a + c, a * b - c * d)};
    return build(q, MK, "MK-dist", fmt("Simplify %d(x + %d) %s %d(x %s %d).", a, b, kMinus, c, kMinus, d), linear(x, k), w,
                 fmt("%d(x + %d) = %dx + %d, and %s%d(x %s %d) = %s%dx + %d (negative times negative is positive). "
                     "Combine: %s.", a, b, a, a * b, kMinus, c, kMinus, d, kMinus, c, c * d, linear(x, k).c_str()));
}

bool mkFractionAdd(Rng& rng, Question& q) {
    static const std::vector<int> dens{2, 3, 4, 5, 6, 8, 10, 12};
    int b = pick(rng, dens), d = pick(rng, dens);
    if (b == d) return false;
    int a = ri(rng, 1, b - 1), c = ri(rng, 1, d - 1);
    if (std::gcd(a, b) != 1 || std::gcd(c, d) != 1) return false;
    bool add = ri(rng, 0, 2) != 0;
    long long n = add ? a * d + c * b : a * d - c * b;
    long long den = static_cast<long long>(b) * d;
    if (n == 0) return false;
    std::vector<std::string> w{frac(add ? a + c : a - c, b + d), frac(a * c, b * d), frac(add ? a * d - c * b : a * d + c * b, den),
                               frac(add ? a + c : a - c, b * d), frac(n + 1, den), frac(n, den + 1)};
    const char* op = add ? "+" : kMinus;
    return build(q, MK, "MK-fadd", fmt("%d/%d %s %d/%d =", a, b, op, c, d), frac(n, den), w,
                 fmt("Use a common denominator: %d/%d %s %d/%d = %s/%lld = %s. Don't add the denominators.", a * d, b * d, op,
                     c * b, b * d, num(static_cast<double>(n)).c_str(), den, frac(n, den).c_str()));
}

bool mkFractionDivide(Rng& rng, Question& q) {
    int a = ri(rng, 1, 9), b = ri(rng, 2, 10), c = ri(rng, 1, 9), d = ri(rng, 2, 10);
    if (std::gcd(a, b) != 1 || std::gcd(c, d) != 1 || a == b || c == d) return false;
    bool divide = ri(rng, 0, 1) == 1;
    long long n = divide ? a * d : a * c, den = divide ? b * c : b * d;
    std::vector<std::string> w{divide ? frac(a * c, b * d) : frac(a * d, b * c), divide ? frac(b * c, a * d) : frac(a + c, b + d),
                               frac(n + 1, den), frac(n, den + 1), frac(a * d + c * b, b * d)};
    return build(q, MK, divide ? "MK-fdiv" : "MK-fmul",
                 fmt("%d/%d %s %d/%d =", a, b, divide ? "\xC3\xB7" : kTimes, c, d), frac(n, den), w,
                 divide ? fmt("Dividing by a fraction means multiplying by its reciprocal: %d/%d %s %d/%d = %s.", a, b, kTimes, d, c,
                              frac(n, den).c_str())
                        : fmt("Multiply straight across: (%d %s %d)/(%d %s %d) = %s.", a, kTimes, c, b, kTimes, d, frac(n, den).c_str()));
}

bool mkSciNotation(Rng& rng, Question& q) {
    int mant = ri(rng, 11, 99);  // two digits: 4.5 is mant 45
    if (mant % 10 == 0) return false;
    int e = ri(rng, 0, 1) ? ri(rng, -6, -2) : ri(rng, 3, 7);
    // The plain number, written out
    std::string plain;
    if (e < 0) {
        plain = "0." + std::string(static_cast<size_t>(-e - 1), '0') + std::to_string(mant);
    } else {
        long long v = mant;
        for (int i = 1; i < e; ++i) v *= 10;
        plain = commas(v);
    }
    std::string m = num(mant / 10.0);
    auto sci = [&](const std::string& mm, int ee) { return mm + " " + kTimes + " 10" + supSigned(ee); };
    std::vector<std::string> w{sci(m, -e), sci(m, e + 1), sci(m, e - 1), sci(std::to_string(mant), e), sci(num(mant / 100.0), e)};
    return build(q, MK, "MK-sci", fmt("Which is %s written in scientific notation?", plain.c_str()), sci(m, e), w,
                 e < 0 ? fmt("Move the decimal point %d places right to get %s, a number between 1 and 10. Moving right means a negative exponent: 10%s.",
                             -e, m.c_str(), supSigned(e).c_str())
                       : fmt("Move the decimal point %d places left to get %s, a number between 1 and 10. So the exponent is %d.", e, m.c_str(), e));
}

bool mkProbability(Rng& rng, Question& q) {
    int r = ri(rng, 1, 9), b = ri(rng, 1, 9), g = ri(rng, 1, 9);
    int total = r + b + g;
    static const std::vector<std::string> colors{"red", "blue", "green"};
    int which = ri(rng, 0, 2);
    int k = which == 0 ? r : which == 1 ? b : g;
    bool notColor = ri(rng, 0, 3) == 0;
    int good = notColor ? total - k : k;
    std::vector<std::string> w{frac(total - good, total), frac(good, total - good > 0 ? total - good : total + 1), frac(1, 3),
                               frac(good, total + 1), frac(good + 1, total)};
    return build(q, MK, "MK-prob",
                 fmt("A bag holds %d red, %d blue, and %d green marbles. If one marble is drawn at random, what is the probability it is %s%s?",
                     r, b, g, notColor ? "not " : "", colors[static_cast<size_t>(which)].c_str()),
                 frac(good, total), w,
                 fmt("Probability = favorable outcomes \xC3\xB7 total outcomes = %d/%d = %s.", good, total, frac(good, total).c_str()));
}

bool mkVolume(Rng& rng, Question& q) {
    if (ri(rng, 0, 1) == 0) {
        int l = ri(rng, 2, 12), wd = ri(rng, 2, 10), h = ri(rng, 2, 9);
        int v = l * wd * h, sa = 2 * (l * wd + l * h + wd * h);
        std::vector<std::string> w{num(sa), num(l + wd + h), num(l * wd), num(v * 2), num(v + l)};
        return build(q, MK, "MK-volbox",
                     fmt("What is the volume of a rectangular box %d inches long, %d inches wide, and %d inches high?", l, wd, h),
                     num(v) + " cubic inches", [&] { std::vector<std::string> o; for (auto& s : w) o.push_back(s + " cubic inches"); return o; }(),
                     fmt("V = length %s width %s height = %d %s %d %s %d = %d cubic inches.", kTimes, kTimes, l, kTimes, wd, kTimes, h, v));
    }
    int r = ri(rng, 2, 7), h = ri(rng, 3, 12);
    std::vector<std::string> w{pi(2 * r * h), pi(r * h), pi(4 * r * r * h), pi(r * r), pi(2 * r * r * h)};
    return build(q, MK, "MK-volcyl", fmt("What is the volume of a cylinder with radius %d and height %d?", r, h), pi(r * r * h), w,
                 fmt("V = \xCF\x80r%sh = \xCF\x80 %s %d %s %d = %s. Square the radius first.", sup(2).c_str(), kTimes, r * r, kTimes, h,
                     pi(r * r * h).c_str()));
}

bool mkTriangleAngle(Rng& rng, Question& q) {
    const char* deg = "\xC2\xB0";
    if (ri(rng, 0, 1) == 0) {
        int a = ri(rng, 20, 100), b = ri(rng, 15, 80);
        int c = 180 - a - b;
        if (c < 10) return false;
        std::vector<std::string> w{fmt("%d%s", 360 - a - b, deg), fmt("%d%s", 180 - a, deg), fmt("%d%s", a + b, deg),
                                   fmt("%d%s", 90 - (c > 90 ? 0 : c / 2), deg), fmt("%d%s", c + 10, deg)};
        return build(q, MK, "MK-tri",
                     fmt("Two angles of a triangle measure %d%s and %d%s. What is the third angle?", a, deg, b, deg),
                     fmt("%d%s", c, deg), w, fmt("The angles of a triangle add to 180%s: 180 %s %d %s %d = %d%s.", deg, kMinus, a, kMinus, b, c, deg));
    }
    int v = ri(rng, 10, 80) * 2;  // isosceles: vertex angle, find a base angle
    if (v >= 180) return false;
    int base = (180 - v) / 2;
    std::vector<std::string> w{fmt("%d%s", 180 - v, deg), fmt("%d%s", v, deg), fmt("%d%s", (360 - v) / 2, deg),
                               fmt("%d%s", base + 5, deg), fmt("%d%s", 90 - v / 2 + 10, deg)};
    return build(q, MK, "MK-iso",
                 fmt("In an isosceles triangle, the angle between the two equal sides is %d%s. What is each of the other two angles?", v, deg),
                 fmt("%d%s", base, deg), w,
                 fmt("The two base angles are equal and share 180 %s %d = %d%s, so each is %d%s.", kMinus, v, 180 - v, deg, base, deg));
}

bool mkAbsolute(Rng& rng, Question& q) {  // |x − a| = b
    int a = ri(rng, -9, 9), b = ri(rng, 1, 12);
    if (a == 0) return false;
    auto pair = [](int x, int y) {
        if (x > y) std::swap(x, y);
        return fmt("x = %s or x = %s", num(x).c_str(), num(y).c_str());
    };
    std::string inside = a > 0 ? fmt("x %s %d", kMinus, a) : fmt("x + %d", -a);
    std::vector<std::string> w{pair(-a + b, -a - b), fmt("x = %s only", num(a + b).c_str()), pair(b, -b), pair(a + b, b - a),
                               fmt("x = %s only", num(a - b).c_str())};
    return build(q, MK, "MK-abs", fmt("Solve |%s| = %d.", inside.c_str(), b), pair(a + b, a - b), w,
                 fmt("The inside can be %d or %s%d: %s = %d gives x = %s, and %s = %s%d gives x = %s.", b, kMinus, b, inside.c_str(), b,
                     num(a + b).c_str(), inside.c_str(), kMinus, b, num(a - b).c_str()));
}

bool mkOrderOps(Rng& rng, Question& q) {  // a + b × (c − d)² ÷ e, or a − b × c + d
    if (ri(rng, 0, 1) == 0) {
        int a = ri(rng, 2, 20), b = ri(rng, 2, 6), c = ri(rng, 3, 9), d = ri(rng, 1, 5);
        if (c <= d) return false;
        int s = (c - d) * (c - d);
        int v = a + b * s;
        std::vector<std::string> w{num((a + b) * s), num(a + (b * (c - d)) * (b * (c - d))), num(a + b * (c - d) * 2),
                                   num(a + b * (c * c - d * d)), num(v + b)};
        return build(q, MK, "MK-ops", fmt("Evaluate %d + %d %s (%d %s %d)%s.", a, b, kTimes, c, kMinus, d, sup(2).c_str()), num(v), w,
                     fmt("Parentheses first: %d %s %d = %d. Exponent: %d%s = %d. Multiply: %d %s %d = %d. Add: %d + %d = %d.",
                         c, kMinus, d, c - d, c - d, sup(2).c_str(), s, b, kTimes, s, b * s, a, b * s, v));
    }
    int a = ri(rng, 20, 60), b = ri(rng, 2, 9), c = ri(rng, 2, 9), d = ri(rng, 2, 12);
    int v = a - b * c + d;
    std::vector<std::string> w{num((a - b) * c + d), num(a - b * (c + d)), num(a - b * c - d), num((a - b) * (c + d)), num(v + 1)};
    return build(q, MK, "MK-ops2", fmt("Evaluate %d %s %d %s %d + %d.", a, kMinus, b, kTimes, c, d), num(v), w,
                 fmt("Multiply first: %d %s %d = %d. Then left to right: %d %s %d + %d = %s.", b, kTimes, c, b * c, a, kMinus, b * c, d,
                     num(v).c_str()));
}

bool mkGcfLcm(Rng& rng, Question& q) {
    int g = ri(rng, 2, 9), x = ri(rng, 2, 9), y = ri(rng, 2, 9);
    if (x == y || std::gcd(x, y) != 1) return false;
    int a = g * x, b = g * y;
    int lcm = a / g * b;
    bool askLcm = ri(rng, 0, 1) == 1;
    std::vector<std::string> w{num(askLcm ? g : lcm), num(a * b), num(askLcm ? lcm * 2 : g * 2), num(std::min(a, b)),
                               num(askLcm ? lcm + g : 1), num(std::max(a, b))};
    return build(q, MK, askLcm ? "MK-lcm" : "MK-gcf",
                 fmt("What is the %s of %d and %d?", askLcm ? "least common multiple" : "greatest common factor", a, b),
                 num(askLcm ? lcm : g), w,
                 askLcm ? fmt("%d = %d %s %d and %d = %d %s %d. LCM = %d %s %d %s %d = %d, the smallest number both divide into.",
                              a, g, kTimes, x, b, g, kTimes, y, g, kTimes, x, kTimes, y, lcm)
                        : fmt("%d = %d %s %d and %d = %d %s %d. The largest number that divides both is %d.", a, g, kTimes, x, b, g,
                              kTimes, y, g));
}

// ================================================================ Arithmetic Reasoning (more)

bool arUnits(Rng& rng, Question& q) {
    struct Conv { const char* big; const char* small; int factor; const char* thing; const char* after; const char* one; };
    static const std::vector<Conv> convs{{"yards", "feet", 3, "A rope is", " long", "yard"},
                                         {"hours", "minutes", 60, "A training flight lasts", "", "hour"},
                                         {"gallons", "quarts", 4, "A water tank holds", "", "gallon"},
                                         {"pounds", "ounces", 16, "A package weighs", "", "pound"},
                                         {"feet", "inches", 12, "A model airplane's wingspan is", "", "foot"},
                                         {"miles", "feet", 5280, "A ruck march covers", "", "mile"}};
    const Conv& c = pick(rng, convs);
    bool toSmall = ri(rng, 0, 1) == 1;
    int big = c.factor >= 60 ? ri(rng, 2, 9) : ri(rng, 3, 40);
    long long small = static_cast<long long>(big) * c.factor;
    std::string given = toSmall ? fmt("%s %d %s%s", c.thing, big, c.big, c.after)
                                : fmt("%s %s %s%s", c.thing, commas(small).c_str(), c.small, c.after);
    std::string ask = fmt("How many %s is that?", toSmall ? c.small : c.big);
    std::string correct = toSmall ? commas(small) : num(big);
    std::vector<std::string> w;
    if (toSmall) w = {num(big * 10), commas(small / 2), commas(small + c.factor), commas(small - c.factor), num(big + c.factor)};
    else w = {commas(small * c.factor), num(big * 2), num(big + 1), num(big - 1), num(static_cast<double>(small) / 10)};
    return build(q, AR, "AR-units", given + ". " + ask, correct, w,
                 toSmall ? fmt("1 %s = %d %s, so multiply: %d %s %d = %s %s.", c.one, c.factor, c.small, big, kTimes, c.factor,
                               commas(small).c_str(), c.small)
                         : fmt("1 %s = %d %s, so divide: %s \xC3\xB7 %d = %d %s.", c.one, c.factor, c.small, commas(small).c_str(),
                               c.factor, big, c.big));
}

bool arCoins(Rng& rng, Question& q) {
    int d = ri(rng, 3, 20), qu = ri(rng, 3, 20);
    int n = d + qu, cents = 10 * d + 25 * qu;
    bool askQ = ri(rng, 0, 1) == 1;
    int ans = askQ ? qu : d;
    std::vector<std::string> w{num(askQ ? d : qu), num(ans + 1), num(ans - 1), num(cents / 25), num(n / 2)};
    return build(q, AR, "AR-coins",
                 fmt("A jar holds %d coins, all dimes and quarters, worth %s in total. How many %s are there?", n, money(cents / 100.0).c_str(),
                     askQ ? "quarters" : "dimes"),
                 num(ans), w,
                 fmt("If all %d were dimes they'd be worth %s. Each quarter adds 15%s more, and the extra is %s, so %s \xC3\xB7 $0.15 = %d quarters and %d dimes.",
                     n, money(n * 0.10).c_str(), "\xC2\xA2", money((cents - 10 * n) / 100.0).c_str(),
                     money((cents - 10 * n) / 100.0).c_str(), qu, d));
}

bool arAges(Rng& rng, Question& q) {
    struct Case { int m, n; };  // now: m times as old; in k years: n times as old
    static const std::vector<Case> cases{{3, 2}, {4, 3}, {5, 3}, {4, 2}, {5, 2}};
    const Case& c = pick(rng, cases);
    int k = ri(rng, 2, 12);
    // m·s + k = n(s + k)  ->  s = k(n − 1)/(m − n)
    if ((k * (c.n - 1)) % (c.m - c.n) != 0) return false;
    int s = k * (c.n - 1) / (c.m - c.n);
    int big = c.m * s;
    if (big > 60 || s < 1) return false;
    static const char* times[] = {"", "", "twice", "three times", "four times", "five times"};
    std::vector<std::string> w{num(s), num(big + k), num(c.n * s), num(big - k > 0 ? big - k : big + 2), num(c.m * k)};
    return build(q, AR, "AR-ages",
                 fmt("A colonel is %s as old as her nephew. In %d years she will be %s as old as he is. How old is the colonel now?",
                     times[c.m], k, times[c.n]),
                 num(big), w,
                 fmt("Let the nephew be n. Then %dn + %d = %d(n + %d), so %dn + %d = %dn + %d and n = %d. The colonel is %d %s %d = %d.",
                     c.m, k, c.n, k, c.m, k, c.n, c.n * k, s, c.m, kTimes, s, big));
}

bool arCommission(Rng& rng, Question& q) {
    int base = ri(rng, 8, 30) * 100, pct = pick(rng, std::vector<int>{2, 3, 4, 5, 6, 8, 10}), sales = ri(rng, 10, 80) * 500;
    double comm = sales * pct / 100.0, total = base + comm;
    std::vector<std::string> w{money(comm), money(base + sales * pct / 1000.0), money(base * (100 + pct) / 100.0),
                               money(total + base * pct / 100.0), money(sales + base)};
    return build(q, AR, "AR-comm",
                 fmt("A salesperson earns %s a month plus a %d%% commission on sales. How much does she earn in a month with %s in sales?",
                     money(base).c_str(), pct, money(sales).c_str()),
                 money(total), w,
                 fmt("Commission = %d%% of %s = %s. Add the base pay: %s + %s = %s.", pct, money(sales).c_str(), money(comm).c_str(),
                     money(base).c_str(), money(comm).c_str(), money(total).c_str()));
}

bool arTipSplit(Rng& rng, Question& q) {
    int people = ri(rng, 2, 6), tip = pick(rng, std::vector<int>{10, 15, 20, 25});
    int bill = ri(rng, 6, 40) * 4 * people;  // whole dollars, easy to split
    double total = bill * (100 + tip) / 100.0, each = total / people;
    if (std::fabs(each * 100 - std::round(each * 100)) > 1e-6) return false;
    std::vector<std::string> w{money(bill / static_cast<double>(people)), money(total), money(each + tip / 10.0),
                               money(bill / static_cast<double>(people) + tip), money(bill * tip / 100.0 / people)};
    return build(q, AR, "AR-tip",
                 fmt("%d cadets split a dinner bill of %s evenly, including a %d%% tip. How much does each cadet pay?", people,
                     money(bill).c_str(), tip),
                 money(each), w,
                 fmt("Total with tip: %s %s %s = %s. Split %d ways: %s.", money(bill).c_str(), kTimes, num((100 + tip) / 100.0).c_str(),
                     money(total).c_str(), people, money(each).c_str()));
}

bool arConsecutive(Rng& rng, Question& q) {
    int kind = ri(rng, 0, 2);  // 0 = integers, 1 = even, 2 = odd
    int step = kind == 0 ? 1 : 2;
    int first = ri(rng, 5, 60);
    if (kind == 1 && first % 2) ++first;
    if (kind == 2 && first % 2 == 0) ++first;
    int n = ri(rng, 3, 4);
    int sum = 0;
    for (int i = 0; i < n; ++i) sum += first + i * step;
    int largest = first + (n - 1) * step;
    const char* what = kind == 0 ? "consecutive integers" : kind == 1 ? "consecutive even integers" : "consecutive odd integers";
    std::vector<std::string> w{num(first), num(first + step), num(largest + step), num(sum / n), num(largest - 1)};
    return build(q, AR, "AR-consec", fmt("The sum of %d %s is %d. What is the largest?", n, what, sum), num(largest), w,
                 fmt("Call the smallest n. The numbers are n, n + %d%s. Together: %dn + %d = %d, so n = %d and the largest is %d.", step,
                     n == 3 ? fmt(", n + %d", 2 * step).c_str() : fmt(", n + %d, n + %d", 2 * step, 3 * step).c_str(), n,
                     sum - n * first, sum, first, largest));
}

bool arMapScale(Rng& rng, Question& q) {
    int scale = pick(rng, std::vector<int>{5, 10, 12, 20, 25, 40, 50});
    double inches = pick(rng, std::vector<double>{1.5, 2, 2.5, 3, 3.5, 4, 4.25, 4.5, 5.5, 6, 7.5});
    double miles = scale * inches;
    std::vector<std::string> w{num(scale + inches) + " miles", num(scale / inches) + " miles", num(miles / 2) + " miles",
                               num(scale * std::floor(inches)) + " miles", num(miles + scale) + " miles"};
    return build(q, AR, "AR-map",
                 fmt("On a chart, 1 inch represents %d miles. Two airfields are %s inches apart on the chart. How far apart are they?", scale,
                     num(inches).c_str()),
                 num(miles) + " miles", w,
                 fmt("%s inches %s %d miles per inch = %s miles.", num(inches).c_str(), kTimes, scale, num(miles).c_str()));
}

bool arRoundTrip(Rng& rng, Question& q) {
    static const std::vector<std::array<int, 2>> speeds{{30, 60}, {40, 60}, {20, 30}, {60, 120}, {300, 600}, {400, 600}, {200, 300},
                                                        {45, 90}, {120, 180}};
    auto s = pick(rng, speeds);
    if (ri(rng, 0, 1)) std::swap(s[0], s[1]);
    int avg = 2 * s[0] * s[1] / (s[0] + s[1]);
    int d = s[0] * s[1] / std::gcd(s[0], s[1]) * ri(rng, 1, 2);
    std::vector<std::string> w{num((s[0] + s[1]) / 2.0) + " mph", num(s[0] + s[1]) + " mph", num(std::max(s[0], s[1])) + " mph",
                               num(avg + 5) + " mph", num(std::abs(s[0] - s[1])) + " mph"};
    return build(q, AR, "AR-round",
                 fmt("A pilot flies %s miles to a base at %d mph and returns the same distance at %d mph. What is the average speed for the whole trip?",
                     commas(d).c_str(), s[0], s[1]),
                 num(avg) + " mph", w,
                 fmt("Time out: %s hours. Time back: %s hours. Average = total distance \xC3\xB7 total time = %s \xC3\xB7 %s = %d mph. "
                     "Averaging the two speeds (%s) is wrong because more time is spent at the slower speed.",
                     num(static_cast<double>(d) / s[0]).c_str(), num(static_cast<double>(d) / s[1]).c_str(), commas(2 * d).c_str(),
                     num(static_cast<double>(d) / s[0] + static_cast<double>(d) / s[1]).c_str(), avg,
                     num((s[0] + s[1]) / 2.0).c_str()));
}

bool arCompound(Rng& rng, Question& q) {
    int P = ri(rng, 10, 80) * 100, r = pick(rng, std::vector<int>{2, 3, 4, 5, 6, 8, 10});
    double y1 = P * (100 + r) / 100.0, y2 = y1 * (100 + r) / 100.0;
    double simple = P + 2 * P * r / 100.0;
    std::vector<std::string> w{money(simple), money(y1), money(y2 - P), money(P * (100 + 2 * r) / 100.0 + r), money(P * (100 + r * r) / 100.0)};
    return build(q, AR, "AR-comp",
                 fmt("%s is deposited at %d%% interest, compounded once a year. What is the balance after 2 years?", money(P).c_str(), r),
                 money(y2), w,
                 fmt("After year 1: %s %s %s = %s. After year 2: %s %s %s = %s. Compounding earns interest on the interest, so it beats simple interest (%s).",
                     money(P).c_str(), kTimes, num((100 + r) / 100.0).c_str(), money(y1).c_str(), money(y1).c_str(), kTimes,
                     num((100 + r) / 100.0).c_str(), money(y2).c_str(), money(simple).c_str()));
}

bool arWorkers(Rng& rng, Question& q) {
    int w1 = ri(rng, 2, 12), d1 = ri(rng, 3, 20), w2 = ri(rng, 2, 16);
    if (w1 == w2 || (w1 * d1) % w2 != 0) return false;
    int d2 = w1 * d1 / w2;
    std::string task = pick(rng, std::vector<std::string>{"repaint a hangar", "build a fence", "clear a training field", "inventory a supply depot"});
    std::vector<std::string> w{num(static_cast<double>(d1) * w2 / w1), num(d1 + (w1 - w2)), num(d1), num(d2 + 1), num(w1 * d1)};
    for (auto& s : w) s += " days";
    return build(q, AR, "AR-workers",
                 fmt("%d airmen can %s in %d days. At the same rate, how many days would %d airmen take?", w1, task.c_str(), d1, w2),
                 num(d2) + " days", w,
                 fmt("The job takes %d %s %d = %d airman-days. %d \xC3\xB7 %d airmen = %d days. More workers means fewer days.", w1, kTimes,
                     d1, w1 * d1, w1 * d1, w2, d2));
}

bool arFence(Rng& rng, Question& q) {
    int l = ri(rng, 20, 120), wd = ri(rng, 10, 80), c = ri(rng, 2, 15);
    if (l == wd) return false;
    int per = 2 * (l + wd);
    std::vector<std::string> w{money(l * wd * c), money((l + wd) * c), money(per), money(per * c + c * 2), money(l * wd)};
    return build(q, AR, "AR-fence",
                 fmt("A rectangular field is %d feet by %d feet. Fencing costs %s per foot. How much will it cost to fence the whole field?", l, wd,
                     money(c).c_str()),
                 money(per * c), w,
                 fmt("Fence goes around the edge: perimeter = 2(%d + %d) = %d feet. %d %s %s = %s.", l, wd, per, per, kTimes,
                     money(c).c_str(), money(per * c).c_str()));
}

bool arTripCost(Rng& rng, Question& q) {
    int mpg = pick(rng, std::vector<int>{20, 24, 25, 28, 30, 32, 35, 40});
    int gallons = ri(rng, 4, 20), miles = mpg * gallons;
    double price = ri(rng, 280, 460) / 100.0;
    double cost = gallons * price;
    std::vector<std::string> w{money(miles * price / 10.0), money(miles / price), money(cost + price), money(gallons + price),
                               money(miles * price)};
    return build(q, AR, "AR-trip",
                 fmt("A car gets %d miles per gallon. Gas costs %s per gallon. How much does gas cost for a %s-mile trip?", mpg,
                     money(price).c_str(), commas(miles).c_str()),
                 money(cost), w,
                 fmt("Gallons needed: %s \xC3\xB7 %d = %d. Cost: %d %s %s = %s.", commas(miles).c_str(), mpg, gallons, gallons, kTimes,
                     money(price).c_str(), money(cost).c_str()));
}


// ================================================================ Table Reading

const std::string TR = "table_reading";

// "+4", "0", "−9": the sign is always shown, like on the test
std::string signedNum(int v) { return v > 0 ? "+" + std::to_string(v) : v == 0 ? "0" : kMinus + std::to_string(-v); }

std::vector<Question> tableReading(size_t count, Rng& rng) {
    // One table for the whole set, like the real test. Values rise to the
    // right and upward with a little noise, so nearby cells look alike.
    const int R = 10, N = 2 * R + 1;
    const int a = ri(rng, 1, 2), b = ri(rng, 1, 2), base = ri(rng, 42, 56);  // stays within 10 to 99
    std::vector<int> cells(static_cast<size_t>(N * N));
    for (int row = 0; row < N; ++row)
        for (int c = 0; c < N; ++c) {
            int x = c - R, y = R - row;
            cells[static_cast<size_t>(row * N + c)] = std::clamp(base + a * x + b * y + ri(rng, -3, 3), 10, 99);
        }
    auto at = [&](int x, int y) { return cells[static_cast<size_t>((R - y) * N + (x + R))]; };
    auto inRange = [&](int v) { return v >= -R && v <= R; };

    Visual vis;
    vis.kind = VisualKind::Table;
    vis.data.push_back(R);
    vis.data.insert(vis.data.end(), cells.begin(), cells.end());

    std::vector<Question> out;
    for (size_t i = 0; out.size() < count && i < count * 10; ++i) {
        int x = ri(rng, -R, R), y = ri(rng, -R, R);
        if (x == y) continue;  // keep the swapped-axes trap meaningful
        const int correct = at(x, y);
        std::vector<std::string> w;
        const bool swapOk = inRange(y) && inRange(x) && at(y, x) != correct;
        if (swapOk) w.push_back(num(at(y, x)));
        const int dx[] = {1, -1, 0, 0, 1, -1, 1, -1, 2, -2, 0, 0};
        const int dy[] = {0, 0, 1, -1, 1, -1, -1, 1, 0, 0, 2, -2};
        for (int k = 0; k < 12; ++k)
            if (inRange(x + dx[k]) && inRange(y + dy[k])) w.push_back(num(at(x + dx[k], y + dy[k])));
        for (int k = 1; k <= 4; ++k) w.push_back(num(std::clamp(correct + k * (k % 2 ? 1 : -1) * 2, 10, 99)));

        std::string expl = fmt("Go across the top to the column X = %s, then down to the row Y = %s. They meet at %d.",
                               signedNum(x).c_str(), signedNum(y).c_str(), correct);
        if (swapOk)
            expl += fmt(" Reading the axes the wrong way (X = %s, Y = %s) gives %d, a common trap.",
                        signedNum(y).c_str(), signedNum(x).c_str(), at(y, x));
        Question q;
        if (build(q, TR, "TR-cell", fmt("What value is at X = %s, Y = %s?", signedNum(x).c_str(), signedNum(y).c_str()),
                  num(correct), w, expl)) {
            q.visual = vis;
            out.push_back(std::move(q));
        }
    }
    return out;
}

// ================================================================ Instrument Comprehension

const std::string IC = "instrument_comprehension";

const char* direction(int heading) {
    static const char* names[] = {"north", "northeast", "east", "southeast", "south", "southwest", "west", "northwest"};
    return names[((heading % 360 + 360) % 360) / 45];
}

std::string attitude(int pitch, int bank, int heading) {
    std::string p = pitch > 0 ? "Climbing" : pitch < 0 ? "Diving" : "Level flight";
    std::string b = bank > 0 ? "banking right" : bank < 0 ? "banking left" : "wings level";
    return p + ", " + b + ", heading " + direction(heading);
}

int normHeading(int h) { return ((h % 360) + 360) % 360; }

// The real test shows the two dials and asks which of several aircraft pictures
// matches them. Each choice keeps its written description too: it's used in the
// results review, and as the answer text in the console version.
bool icQuestion(Rng& rng, Question& q) {
    const int bank = pick(rng, std::vector<int>{-45, -30, 0, 30, 45});
    const int pitch = pick(rng, std::vector<int>{-15, 0, 15});
    const int heading = ri(rng, 0, 7) * 45;

    // Remember the attitude behind each description, to draw it later
    std::map<std::string, std::vector<int>> attitudes;
    auto attitude = [&](int p, int b, int h) {
        std::string text = ::attitude(p, b, h);
        attitudes.emplace(text, std::vector<int>{b, p, normHeading(h)});
        return text;
    };

    // Each wrong answer gets one part wrong (sometimes two)
    std::vector<std::string> w;
    if (bank != 0) w.push_back(attitude(pitch, -bank, heading));
    if (pitch != 0) w.push_back(attitude(-pitch, bank, heading));
    w.push_back(attitude(pitch, bank, heading + 180));
    if (bank == 0) { w.push_back(attitude(pitch, 30, heading)); w.push_back(attitude(pitch, -30, heading)); }
    if (pitch == 0) { w.push_back(attitude(15, bank, heading)); w.push_back(attitude(-15, bank, heading)); }
    w.push_back(attitude(pitch, bank, heading + 90));
    w.push_back(attitude(-pitch, -bank, heading));
    w.push_back(attitude(pitch, bank, heading - 45));
    w.push_back(attitude(pitch, -bank, heading + 180));

    std::string expl;
    if (bank > 0) expl = "The horizon line is higher on the right, so the right wing is down: the aircraft is banking right. ";
    else if (bank < 0) expl = "The horizon line is higher on the left, so the left wing is down: the aircraft is banking left. ";
    else expl = "The horizon line is level, so the wings are level. ";
    if (pitch > 0) expl += "The horizon sits below the center, with more sky showing, so the nose is up: climbing. ";
    else if (pitch < 0) expl += "The horizon sits above the center, with more ground showing, so the nose is down: diving. ";
    else expl += "The horizon crosses the center, so the nose is level. ";
    expl += std::string("The compass arrow points ") + direction(heading) + ".";

    const std::string correct = attitude(pitch, bank, heading);
    if (!build(q, IC, "IC-att", "Which aircraft matches the instruments?", correct, w, expl)) return false;
    q.explanation += " Answer: " + correct + ".";
    q.visual.kind = VisualKind::Instruments;
    q.visual.data = {bank, pitch, heading};
    for (const auto& c : q.choices) q.choiceVisuals.push_back(Visual{VisualKind::Aircraft, attitudes.at(c)});
    return true;
}

// ================================================================ Block Counting

const std::string BC = "block_counting";

struct Block { int x, y, z, sx, sy, sz, label; };

// Blocks touch when they share part of a face (not just an edge).
std::vector<int> touching(const std::vector<Block>& blocks, int idx) {
    std::set<int> found;
    const Block& a = blocks[static_cast<size_t>(idx)];
    for (size_t j = 0; j < blocks.size(); ++j) {
        if (static_cast<int>(j) == idx) continue;
        const Block& b = blocks[j];
        // overlap length on each axis (negative = gap, 0 = just meeting)
        int ox = std::min(a.x + a.sx, b.x + b.sx) - std::max(a.x, b.x);
        int oy = std::min(a.y + a.sy, b.y + b.sy) - std::max(a.y, b.y);
        int oz = std::min(a.z + a.sz, b.z + b.sz) - std::max(a.z, b.z);
        int zeros = (ox == 0) + (oy == 0) + (oz == 0);
        bool apart = ox < 0 || oy < 0 || oz < 0;
        if (!apart && zeros == 1) found.insert(static_cast<int>(j));  // meet on exactly one face
    }
    return {found.begin(), found.end()};
}

std::vector<Question> blockCounting(size_t count, Rng& rng) {
    std::vector<Question> out;
    while (out.size() < count) {
        // A stack of layers, three long blocks each, crossing direction every
        // layer (like a log pile). Only the top layer may have gaps, so every
        // block's position can be seen or worked out.
        const int layers = ri(rng, 2, 4);
        std::vector<Block> blocks;
        for (int z = 0; z < layers; ++z) {
            for (int i = 0; i < 3; ++i) {
                if (z == layers - 1 && layers > 1 && ri(rng, 0, 2) == 0) continue;  // gap in the top layer
                if (z % 2 == 0) blocks.push_back({0, i, z, 3, 1, 1, 0});  // runs left-right
                else blocks.push_back({i, 0, z, 1, 3, 1, 0});            // runs front-back
            }
        }
        if (blocks.size() < 4) continue;

        // Label up to five blocks, then ask about each one
        std::vector<int> order(blocks.size());
        std::iota(order.begin(), order.end(), 0);
        std::shuffle(order.begin(), order.end(), rng);
        const int labels = std::min<int>(5, static_cast<int>(blocks.size()));
        for (int k = 0; k < labels; ++k) blocks[static_cast<size_t>(order[k])].label = k + 1;

        Visual vis;
        vis.kind = VisualKind::Blocks;
        vis.data.push_back(static_cast<int>(blocks.size()));
        for (const auto& b : blocks) vis.data.insert(vis.data.end(), {b.x, b.y, b.z, b.sx, b.sy, b.sz, b.label});

        for (int k = 0; k < labels && out.size() < count; ++k) {
            const int idx = order[k];
            const Block& me = blocks[static_cast<size_t>(idx)];
            const auto t = touching(blocks, idx);
            int same = 0, above = 0, below = 0;
            for (int j : t) {
                int dz = blocks[static_cast<size_t>(j)].z - me.z;
                (dz == 0 ? same : dz > 0 ? above : below)++;
            }
            const int n = static_cast<int>(t.size());
            std::vector<std::string> w;
            for (int d : {1, -1, 2, -2, 3, -3, 4})
                if (n + d >= 0) w.push_back(num(n + d));
            std::string expl = fmt("Block %d touches %s beside it in its own layer, %s in the layer above, and %s in the "
                                   "layer below: %d in all. Blocks in crossing layers touch wherever they cross.",
                                   me.label, plural(same, "block").c_str(), plural(above, "block").c_str(),
                                   plural(below, "block").c_str(), n);
            Question q;
            if (build(q, BC, "BC-touch", fmt("How many blocks touch block %d?", me.label), num(n), w, expl)) {
                q.visual = vis;
                out.push_back(std::move(q));
            }
        }
    }
    return out;
}

const std::vector<Template>& templatesFor(const std::string& subtest) {
    static const std::vector<Template> mk{mkLinear, mkEvaluate, mkRectangle, mkCircle, mkPythagoras, mkExponents,
                                          mkSlope, mkPercent, mkPolygon, mkSystem, mkMean, mkRoot,
                                          mkInequality, mkQuadratic, mkDistribute, mkFractionAdd, mkFractionDivide,
                                          mkSciNotation, mkProbability, mkVolume, mkTriangleAngle, mkAbsolute, mkOrderOps,
                                          mkGcfLcm};
    static const std::vector<Template> ar{arDistance, arDiscountTax, arWorkRate, arAverage, arRatio, arInterest,
                                          arFuel, arPercentChange, arProportion, arOpposite, arOvertime, arClock,
                                          arUnits, arCoins, arAges, arCommission, arTipSplit, arConsecutive, arMapScale,
                                          arRoundTrip, arCompound, arWorkers, arFence, arTripCost};
    static const std::vector<Template> ic{icQuestion};
    static const std::vector<Template> none;
    return subtest == MK ? mk : subtest == AR ? ar : subtest == IC ? ic : none;
}

}  // namespace

bool QuestionGenerator::supports(const std::string& subtestId) {
    return subtestId == TR || subtestId == BC || !templatesFor(subtestId).empty();
}

std::vector<Question> QuestionGenerator::generate(const std::string& subtestId, size_t count, Rng& rng) {
    if (subtestId == TR) return tableReading(count, rng);   // one shared table
    if (subtestId == BC) return blockCounting(count, rng);  // a few questions per figure
    const auto& templates = templatesFor(subtestId);
    std::vector<Question> out;
    if (templates.empty()) return out;

    // Cycle through the question types in a shuffled order, so a set mixes
    // many different kinds instead of repeating one.
    std::vector<size_t> order(templates.size());
    std::iota(order.begin(), order.end(), 0);
    std::shuffle(order.begin(), order.end(), rng);

    for (size_t i = 0; out.size() < count; ++i) {
        const Template& t = templates[order[i % order.size()]];
        Question q;
        for (int attempt = 0; attempt < 50; ++attempt) {  // retry with new numbers if needed
            if (t(rng, q)) {
                out.push_back(std::move(q));
                break;
            }
        }
        if (i > count * 4 + 50) break;  // safety: never loop forever
    }
    return out;
}
