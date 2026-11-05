#pragma once
#include <cyuediff/skeleton_pose.hpp>
#include <cmath>
namespace cyuediff::detail::affine {
using Matrix = std::array<double, 12>;
inline bool local_matrix(const Json &pose, Matrix &m) {
    auto q = pose.at("rotation").get<std::array<double, 4>>();
    const auto t = pose.at("translation").get<std::array<double, 3>>(),
               s = pose.at("scale").get<std::array<double, 3>>();
    const double norm = std::hypot(std::hypot(q[0], q[1]), std::hypot(q[2], q[3]));
    if (!std::isfinite(norm) || std::abs(norm - 1) > 1e-4)
        return false;
    for (auto &x : q)
        x /= norm;
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    m = {(1 - 2 * (y * y + z * z)) * s[0], 2 * (x * y - z * w) * s[1],
         2 * (x * z + y * w) * s[2],       t[0],
         2 * (x * y + z * w) * s[0],       (1 - 2 * (x * x + z * z)) * s[1],
         2 * (y * z - x * w) * s[2],       t[1],
         2 * (x * z - y * w) * s[0],       2 * (y * z + x * w) * s[1],
         (1 - 2 * (x * x + y * y)) * s[2], t[2]};
    return true;
}
inline Matrix compose(const Matrix &parent, const Matrix &local) {
    Matrix out{};
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned column = 0; column < 4; ++column) {
            for (unsigned k = 0; k < 3; ++k)
                out[row * 4 + column] += parent[row * 4 + k] * local[k * 4 + column];
            if (column == 3)
                out[row * 4 + column] += parent[row * 4 + 3];
            if (!std::isfinite(out[row * 4 + column]))
                throw Error("skeleton-pose-value", "Reference affine transform overflow");
        }
    return out;
}
} // namespace cyuediff::detail::affine
