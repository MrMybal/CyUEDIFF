#include "animation_transform.hpp"
#include <cmath>
#include <limits>
namespace cyuediff::detail {
void validate_animation_tolerances(double p, double r, double s) {
    if (!std::isfinite(p) || p < 0 || !std::isfinite(r) || r < 0 || r > 180 || !std::isfinite(s) || s < 0)
        throw Error(
            "animation-tolerance",
            "Expected finite nonnegative local transform tolerances; rotation must be 0..180 degrees");
}
namespace {
template <std::size_t N> bool values(const Json &point, const char *field, std::array<double, N> &out) {
    const auto it = point.find(field);
    if (it == point.end() || !it->is_array() || it->size() != N)
        return false;
    for (std::size_t i = 0; i < N; ++i) {
        if (!(*it)[i].is_number())
            return false;
        out[i] = (*it)[i].get<double>();
        if (!std::isfinite(out[i]))
            return false;
    }
    return true;
}
double norm4(const std::array<double, 4> &q) {
    return std::hypot(std::hypot(q[0], q[1]), std::hypot(q[2], q[3]));
}
} // namespace
Json compare_local_animation_values(const Json &a, const Json &b, double pt, double rt, double st,
                                    const OperationControl &control) {
    validate_animation_tolerances(pt, rt, st);
    control.check();
    Json out = {{"ready", false},
                {"changed", nullptr},
                {"positionDistance", nullptr},
                {"rotationDeltaDegrees", nullptr},
                {"scaleMaxDelta", nullptr},
                {"positionDelta", nullptr},
                {"scaleDelta", nullptr},
                {"positionChanged", nullptr},
                {"rotationChanged", nullptr},
                {"scaleChanged", nullptr},
                {"diagnostics", Json::array()}};
    auto unavailable = [&](const char *reason) {
        out["diagnostics"].push_back(reason);
        return out;
    };
    if (!a.is_object() || !b.is_object() || !a.contains("ready") || a["ready"] != true ||
        !b.contains("ready") || b["ready"] != true)
        return unavailable("A sampled local transform is unavailable");
    std::array<double, 3> ap{}, bp{}, as{}, bs{}, dp{}, ds{};
    std::array<double, 4> aq{}, bq{};
    if (!values(a, "positions", ap) || !values(b, "positions", bp) || !values(a, "scales", as) ||
        !values(b, "scales", bs) || !values(a, "rotations", aq) || !values(b, "rotations", bq))
        return unavailable("A sampled local channel is missing or non-finite");
    const double an = norm4(aq), bn = norm4(bq);
    if (!std::isfinite(an) || !std::isfinite(bn) || std::abs(an * an - 1.) > 1e-4 ||
        std::abs(bn * bn - 1.) > 1e-4)
        return unavailable("A sampled quaternion is not unit length");
    for (unsigned i = 0; i < 4; ++i) {
        aq[i] /= an;
        bq[i] /= bn;
    }
    double dot = 0;
    for (unsigned i = 0; i < 4; ++i)
        dot += aq[i] * bq[i];
    if (dot < 0)
        for (auto &v : bq)
            v = -v;
    std::array<double, 4> difference{}, sum{};
    for (unsigned i = 0; i < 4; ++i) {
        difference[i] = aq[i] - bq[i];
        sum[i] = aq[i] + bq[i];
    }
    constexpr double pi = 3.141592653589793238462643383279502884;
    const double angle = 4. * std::atan2(norm4(difference), norm4(sum)) * (180. / pi);
    double scale = 0;
    for (unsigned i = 0; i < 3; ++i) {
        dp[i] = bp[i] - ap[i];
        ds[i] = bs[i] - as[i];
        if (!std::isfinite(dp[i]) || !std::isfinite(ds[i]))
            return unavailable("Local transform delta arithmetic overflow");
        scale = std::max(scale, std::abs(ds[i]));
    }
    const double distance = std::hypot(std::hypot(dp[0], dp[1]), dp[2]);
    if (!std::isfinite(distance) || !std::isfinite(angle) || !std::isfinite(scale))
        return unavailable("Local transform metric arithmetic overflow");
    // libm normalization/atan2 rounding can straddle an inclusive angular boundary.
    // Keep raw metrics and zero-tolerance sensitivity; allow only relative double roundoff.
    const bool rotation_changed =
        angle > rt &&
        (rt == 0 || angle - rt > 8 * std::numeric_limits<double>::epsilon() * std::max(angle, rt));
    out["ready"] = true;
    out["positionDelta"] = dp;
    out["scaleDelta"] = ds;
    out["positionDistance"] = distance;
    out["rotationDeltaDegrees"] = angle;
    out["scaleMaxDelta"] = scale;
    out["positionChanged"] = distance > pt;
    out["rotationChanged"] = rotation_changed;
    out["scaleChanged"] = scale > st;
    out["changed"] = distance > pt || rotation_changed || scale > st;
    return out;
}
} // namespace cyuediff::detail
