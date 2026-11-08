#include <cyuediff/collision_wireframe.hpp>
#include <algorithm>
#include <cmath>
#include <set>
namespace cyuediff {
namespace {
constexpr double pi = 3.14159265358979323846;
CollisionPoint point(const Json &v) {
    return {v.at(0).get<double>(), v.at(1).get<double>(), v.at(2).get<double>()};
}
void extend(CollisionBounds &bounds, const CollisionPoint &p) {
    if (!bounds.valid) {
        bounds.low = bounds.high = p;
        bounds.valid = true;
    } else
        for (unsigned i = 0; i < 3; ++i) {
            bounds.low[i] = std::min(bounds.low[i], p[i]);
            bounds.high[i] = std::max(bounds.high[i], p[i]);
        }
}
struct Builder {
    const CollisionWireframeOptions &options;
    std::size_t edges = 0;
    CollisionPoint center{}, rotation{};
    CollisionPoint transform(CollisionPoint p) const {
        // Stored Unreal Rotator is pitch, yaw, roll in degrees.
        const double pitch = std::remainder(rotation[0], 360.) * pi / 180.,
                     yaw = std::remainder(rotation[1], 360.) * pi / 180.,
                     roll = std::remainder(rotation[2], 360.) * pi / 180.;
        const double y = std::cos(roll) * p[1] + std::sin(roll) * p[2],
                     z = -std::sin(roll) * p[1] + std::cos(roll) * p[2];
        p[1] = y;
        p[2] = z;
        const double x = std::cos(pitch) * p[0] - std::sin(pitch) * p[2];
        p[2] = std::sin(pitch) * p[0] + std::cos(pitch) * p[2];
        p[0] = x;
        p = {std::cos(yaw) * p[0] - std::sin(yaw) * p[1], std::sin(yaw) * p[0] + std::cos(yaw) * p[1], p[2]};
        for (unsigned i = 0; i < 3; ++i)
            p[i] += center[i];
        return p;
    }
    void line(CollisionWireShape &shape, CollisionPoint a, CollisionPoint b) {
        options.collision.control.check();
        if (edges >= options.max_edges)
            throw Error("collision-preview-limit", "Collision wireframe edge budget exceeded");
        a = transform(a);
        b = transform(b);
        for (const auto &p : {a, b})
            for (auto x : p)
                if (!std::isfinite(x))
                    throw Error("collision-preview-value", "Collision wireframe coordinate overflow");
        shape.lines.push_back({a, b});
        ++edges;
    }
    template <typename F>
    void curve(CollisionWireShape &shape, F sample, double start = 0, double end = 2 * pi,
               unsigned steps = 0) {
        if (!steps)
            steps = options.circle_segments;
        auto previous = sample(start);
        for (unsigned i = 1; i <= steps; ++i) {
            auto next = sample(start + (end - start) * i / steps);
            line(shape, previous, next);
            previous = next;
        }
    }
    void sphere(CollisionWireShape &shape, double radius, double z = 0) {
        for (unsigned plane = 0; plane < 3; ++plane)
            curve(shape, [=](double angle) {
                CollisionPoint p{0, 0, z};
                p[plane] += radius * std::cos(angle);
                p[(plane + 1) % 3] += radius * std::sin(angle);
                return p;
            });
    }
    void capsule(CollisionWireShape &shape, double r0, double r1, double length) {
        const double half = length / 2;
        if (length <= std::abs(r0 - r1)) {
            sphere(shape, std::max(r0, r1), r0 > r1 ? half : -half);
            return;
        }
        // Convex envelope of the two stored end spheres. Radius0 is the +Z end.
        const double slope = (r1 - r0) / length;
        const double radial = std::sqrt(std::max(0., 1 - slope * slope));
        for (unsigned end = 0; end < 2; ++end) {
            const double radius = end ? r1 : r0, z = end ? -half : half;
            curve(shape, [=](double a) {
                return CollisionPoint{radius * radial * std::cos(a), radius * radial * std::sin(a),
                                      z + radius * slope};
            });
            const double boundary = std::acos(slope);
            for (unsigned plane = 0; plane < 2; ++plane)
                curve(
                    shape,
                    [=](double a) {
                        CollisionPoint p{0, 0, z + radius * std::cos(a)};
                        p[plane] = radius * std::sin(a);
                        return p;
                    },
                    end ? boundary : -boundary, end ? 2 * pi - boundary : boundary);
        }
        for (unsigned i = 0; i < 4; ++i) {
            const double a = i * pi / 2, x = radial * std::cos(a), y = radial * std::sin(a);
            line(shape, {r0 * x, r0 * y, half + r0 * slope}, {r1 * x, r1 * y, -half + r1 * slope});
        }
    }
    void convex(CollisionWireShape &shape, const Json &g) {
        for (const auto *field : {"vertices", "indices", "transform"})
            if (!g.contains(field)) {
                shape.diagnostics.push_back(std::string("Missing stored convex geometry: ") + field);
                return;
            }
        const auto &transform = g.at("transform");
        auto q = transform.at("rotation").get<std::array<double, 4>>();
        const double norm = std::hypot(std::hypot(q[0], q[1]), std::hypot(q[2], q[3]));
        if (!std::isfinite(norm) || std::abs(norm - 1.) > 1e-4) {
            shape.diagnostics.push_back("Stored convex quaternion is not unit length");
            return;
        }
        // Remove only bounded floating-point unit-length error, never repair an arbitrary quaternion.
        for (auto &component : q)
            component /= norm;
        const auto translation = point(transform.at("translation")), scale = point(transform.at("scale"));
        std::vector<CollisionPoint> vertices;
        vertices.reserve(g.at("vertices").size());
        for (const auto &value : g.at("vertices")) {
            options.collision.control.check();
            auto p = point(value);
            for (unsigned k = 0; k < 3; ++k)
                p[k] *= scale[k];
            const CollisionPoint u{q[0], q[1], q[2]};
            const auto cross = [](CollisionPoint a, CollisionPoint b) -> CollisionPoint {
                return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
            };
            auto c = cross(u, p), d = cross(u, c);
            for (unsigned k = 0; k < 3; ++k) {
                p[k] += 2 * (q[3] * c[k] + d[k]) + translation[k];
                if (!std::isfinite(p[k]))
                    throw Error("collision-preview-value", "Convex coordinate overflow");
            }
            vertices.push_back(p);
        }
        center = {};
        rotation = {};
        std::set<std::pair<std::size_t, std::size_t>> unique;
        const auto &indices = g.at("indices");
        for (std::size_t i = 0; i < indices.size(); i += 3)
            for (unsigned side = 0; side < 3; ++side) {
                const auto a = indices[i + side].get<std::size_t>(),
                           b = indices[i + (side + 1) % 3].get<std::size_t>();
                if (a == b)
                    continue;
                auto key = std::minmax(a, b);
                if (unique.insert(key).second)
                    line(shape, vertices.at(a), vertices.at(b));
            }
        if (shape.lines.empty())
            shape.diagnostics.push_back("Stored convex topology has no non-degenerate edges");
    }
    void build(CollisionWireShape &shape, const Json &source) {
        const auto &g = source.at("geometry");
        auto need = [&](std::initializer_list<const char *> names) {
            for (auto name : names)
                if (!g.contains(name)) {
                    shape.diagnostics.push_back(std::string("Missing stored geometry: ") + name);
                    return false;
                }
            return true;
        };
        if (shape.kind == "convex") {
            convex(shape, g);
            return;
        }
        if (!need({"center"}))
            return;
        center = point(g.at("center"));
        rotation = {};
        if (shape.kind != "sphere") {
            if (!need({"rotation"}))
                return;
            rotation = point(g.at("rotation"));
        }
        if (shape.kind == "sphere") {
            if (need({"radius"}))
                sphere(shape, g.at("radius").get<double>());
        } else if (shape.kind == "box") {
            if (!need({"x", "y", "z"}))
                return;
            CollisionPoint half{g.at("x").get<double>() / 2, g.at("y").get<double>() / 2,
                                g.at("z").get<double>() / 2};
            for (unsigned vertex = 0; vertex < 8; ++vertex)
                for (unsigned axis = 0; axis < 3; ++axis)
                    if (!(vertex & (1 << axis))) {
                        CollisionPoint a{}, b{};
                        for (unsigned k = 0; k < 3; ++k)
                            a[k] = b[k] = (vertex & (1 << k)) ? half[k] : -half[k];
                        b[axis] = -b[axis];
                        line(shape, a, b);
                    }
        } else if (shape.kind == "capsule") {
            if (need({"radius", "length"}))
                capsule(shape, g.at("radius").get<double>(), g.at("radius").get<double>(),
                        g.at("length").get<double>());
        } else if (shape.kind == "tapered-capsule") {
            if (need({"radius0", "radius1", "length"}))
                capsule(shape, g.at("radius0").get<double>(), g.at("radius1").get<double>(),
                        g.at("length").get<double>());
        } else
            shape.diagnostics.push_back("Unsupported stored shape kind");
    }
};
} // namespace
std::size_t CollisionWireframe::edge_count() const {
    std::size_t count = 0;
    for (const auto &shape : shapes)
        count += shape.lines.size();
    return count;
}
Json CollisionWireframe::to_json() const {
    Json entries = Json::array();
    for (const auto &shape : shapes) {
        Json lines = Json::array();
        for (const auto &line : shape.lines)
            lines.push_back({line.a, line.b});
        entries.push_back({{"body", shape.body},
                           {"identity", shape.identity},
                           {"kind", shape.kind},
                           {"ready", shape.ready()},
                           {"lines", std::move(lines)},
                           {"diagnostics", shape.diagnostics}});
    }
    return {{"schema", "cyuediff.collision-wireframe/1"},
            {"space", "body-local"},
            {"sourceComplete", source_complete},
            {"shapes", std::move(entries)},
            {"bounds", bounds.valid ? Json{{"low", bounds.low}, {"high", bounds.high}} : Json(nullptr)}};
}
static CollisionWireframe build_wireframe(const Json &snapshot, const CollisionWireframeOptions &options) {
    if (options.circle_segments < 8 || options.circle_segments > 256 || options.circle_segments % 4)
        throw Error("collision-preview-options", "Circle segments must be a multiple of four from 8 to 256");
    CollisionWireframe result;
    result.source_complete = snapshot.at("complete").get<bool>();
    Builder builder{options};
    const auto &bodies = snapshot.at("bodies");
    for (auto body = bodies.begin(); body != bodies.end(); ++body)
        for (auto it = body->at("shapes").begin(); it != body->at("shapes").end(); ++it) {
            options.collision.control.checkpoint("collision-wireframe", result.shapes.size());
            CollisionWireShape shape;
            shape.body = body.key();
            shape.identity = it.key();
            shape.kind = it->at("kind");
            builder.build(shape, *it);
            for (const auto &line : shape.lines) {
                extend(shape.bounds, line.a);
                extend(shape.bounds, line.b);
                extend(result.bounds, line.a);
                extend(result.bounds, line.b);
            }
            result.shapes.push_back(std::move(shape));
        }
    return result;
}
CollisionWireframe collision_wireframe(const Package &package, const CollisionWireframeOptions &options) {
    return build_wireframe(collision_snapshot(package, options.collision), options);
}
CollisionWireframe collision_wireframe(const ZenPackage &package, const CollisionWireframeOptions &options) {
    return build_wireframe(collision_snapshot(package, options.collision), options);
}

} // namespace cyuediff
