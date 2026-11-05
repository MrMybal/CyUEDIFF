#include "curve_weighted.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace cyuediff::detail {
Json weighted_curve_segment(const Json &a, const Json &b, double alpha, double duration,
                            const OperationControl &control) {
    Json out = {{"ready", false}, {"method", "cubic-weighted"}, {"value", nullptr}};
    auto fail = [&](const char *reason) {
        out["reason"] = reason;
        return out;
    };
    const int left = a.at("tangentWeightMode").get<int>(), right = b.at("tangentWeightMode").get<int>();
    if (left < 0 || left > 3 || right < 0 || right > 3)
        return fail("Unknown stored tangent weight mode");
    const auto m0 = a.at("leaveTangent").get<double>(), m1 = b.at("arriveTangent").get<double>();
    auto handle = [&](bool weighted, double weight, double slope) {
        if (weighted) {
            if (weight < 0)
                return std::array<double, 2>{-1, 0};
            const auto norm = std::hypot(1., slope);
            return std::array<double, 2>{weight / norm, weight * (slope / norm)};
        }
        return std::array<double, 2>{duration / 3., slope * (duration / 3.)};
    };
    const auto h0 = handle(left == 2 || left == 3, a.at("leaveTangentWeight").get<double>(), m0);
    const auto h1 = handle(right == 1 || right == 3, b.at("arriveTangentWeight").get<double>(), m1);
    if (h0[0] < 0 || h1[0] < 0)
        return fail("Negative active tangent weights are not evaluated");
    const double x1 = h0[0] / duration, x2 = 1. - h1[0] / duration;
    const double y0 = a.at("value").get<double>(), y1 = y0 + h0[1], y3 = b.at("value").get<double>(),
                 y2 = y3 - h1[1];
    for (double v : {x1, x2, y0, y1, y2, y3})
        if (!std::isfinite(v))
            return fail("Weighted curve geometry arithmetic overflow");
    auto bezier = [](double p0, double p1, double p2, double p3, double t) {
        auto lerp = [&](double x, double y) { return (1. - t) * x + t * y; };
        return lerp(lerp(lerp(p0, p1), lerp(p1, p2)), lerp(lerp(p1, p2), lerp(p2, p3)));
    };
    auto x = [&](double t) { return bezier(0, x1, x2, 1, t); };
    // Isolate monotonic time branches at the derivative's real roots.
    double qa = 1. + 3. * x1 - 3. * x2, qb = 2. * x2 - 4. * x1, qc = x1;
    const double scale = std::max({std::abs(qa), std::abs(qb), std::abs(qc)});
    if (!std::isfinite(scale) || scale == 0)
        return fail("Weighted curve time polynomial is unresolved");
    qa /= scale;
    qb /= scale;
    qc /= scale;
    std::vector<double> cuts = {0., 1.};
    auto critical = [&](double t) {
        if (std::isfinite(t) && t > 0 && t < 1)
            cuts.push_back(t);
    };
    if (std::abs(qa) < 8 * std::numeric_limits<double>::epsilon()) {
        if (qb != 0)
            critical(-qc / qb);
    } else {
        const double discriminant = qb * qb - 4 * qa * qc;
        if (discriminant >= 0) {
            const double q = -.5 * (qb + std::copysign(std::sqrt(discriminant), qb));
            if (q == 0)
                critical(-qb / (2 * qa));
            else {
                critical(q / qa);
                critical(qc / q);
            }
        }
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    double chosen = -1;
    auto candidate = [&](double t) {
        const auto residual = x(t) - alpha;
        if (std::isfinite(residual) && std::abs(residual) <= 1e-9)
            chosen = std::max(chosen, t);
    };
    for (auto t : cuts)
        if (std::abs(x(t) - alpha) <= 1e-12)
            candidate(t);
    for (std::size_t branch = 1; branch < cuts.size(); ++branch) {
        control.check();
        double low = cuts[branch - 1], high = cuts[branch], xl = x(low), xh = x(high);
        if (!std::isfinite(xl) || !std::isfinite(xh))
            return fail("Weighted curve time arithmetic overflow");
        if (alpha < std::min(xl, xh) || alpha > std::max(xl, xh))
            continue;
        const bool increasing = xh >= xl;
        for (unsigned step = 0; step < 80; ++step) {
            control.checkpoint("Solving weighted curve time", step, 80);
            const double middle = low + (high - low) * .5;
            if (middle == low || middle == high)
                break;
            const auto xm = x(middle);
            if (!std::isfinite(xm))
                return fail("Weighted curve root arithmetic overflow");
            if ((xm < alpha) == increasing)
                low = middle;
            else
                high = middle;
        }
        candidate(low + (high - low) * .5);
    }
    if (chosen < 0)
        return fail("Weighted curve time root cannot be resolved within tolerance");
    const double value = bezier(y0, y1, y2, y3, chosen);
    if (!std::isfinite(value))
        return fail("Weighted curve value arithmetic overflow");
    out["ready"] = true;
    out["value"] = value;
    return out;
}
} // namespace cyuediff::detail
