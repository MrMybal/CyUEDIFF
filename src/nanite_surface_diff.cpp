#include <cyuediff/cooked_nanite.hpp>
#include <algorithm>
#include <cstring>
#include <map>
namespace cyuediff {
namespace {
using Key = std::array<std::uint32_t, 10>;
struct Pair {
    std::vector<NaniteTriangleReference> refs[2];
};
struct Budget {
    const NaniteSurfaceDiffOptions &o;
    std::uint64_t bytes = 0, triangles = 0;
    void reserve(std::uint64_t n) {
        if (bytes > o.max_output_bytes || n > o.max_output_bytes - bytes)
            throw Error("nanite-diff-limit", "Nanite surface comparison exceeds output byte budget");
        bytes += n;
    }
    void triangle() {
        if (triangles >= o.max_triangles)
            throw Error("nanite-diff-limit", "Nanite surface comparison exceeds aggregate triangle budget");
        ++triangles;
    }
};
Key key(const NaniteGeometry &m, std::size_t p, std::size_t c, std::size_t t,
        const OperationControl &control) {
    const auto triangle = m.triangle(p, c, t, control);
    std::array<std::array<std::uint32_t, 3>, 3> vertices{};
    for (unsigned i = 0; i < 3; ++i) {
        const auto position = m.position(p, c, triangle.vertices[i], control);
        for (unsigned k = 0; k < 3; ++k) {
            const float f = position[k] == 0 ? 0.f : position[k];
            std::memcpy(&vertices[i][k], &f, 4);
        }
    }
    Key best{};
    bool first = true;
    for (unsigned rotation = 0; rotation < 3; ++rotation) {
        Key candidate{};
        candidate[0] = triangle.material;
        for (unsigned i = 0; i < 3; ++i)
            for (unsigned k = 0; k < 3; ++k)
                candidate[1 + 3 * i + k] = vertices[(i + rotation) % 3][k];
        if (first || candidate < best) {
            best = candidate;
            first = false;
        }
    }
    return best;
}
} // namespace
NaniteSurfaceComparison compare_nanite_surfaces(const CookedNaniteResource &a, const CookedNaniteResource &b,
                                                const NaniteSurfaceDiffOptions &o) {
    o.control.check();
    Budget budget{o};
    budget.reserve(2048);
    NaniteSurfaceComparison out;
    const bool complete = a.geometry_known() && b.geometry_known();
    const auto ah = a.encoding_sha256(), bh = b.encoding_sha256();
    out.summary = {{"schema", "cyuediff.nanite-surface-diff/1"},
                   {"scope", "exact full-leaf native positions, oriented triangle multiplicity and numeric "
                             "materials; vertex attributes/runtime LOD excluded"},
                   {"complete", complete},
                   {"encodingStatus", complete ? (ah == bh ? "identical" : "changed") : "unavailable"},
                   {"surfaceStatus", complete ? "identical" : "unavailable"},
                   {"status", complete ? "identical" : "incomplete"},
                   {"beforeTriangles", 0},
                   {"afterTriangles", 0},
                   {"matchedTriangles", 0},
                   {"removedTriangles", 0},
                   {"addedTriangles", 0},
                   {"storedAttributesCompared", false}};
    if (!complete)
        return out;
    std::map<Key, Pair> pairs;
    std::uint64_t counts[2]{};
    const std::shared_ptr<const NaniteGeometry> models[2] = {a.geometry(), b.geometry()};
    for (unsigned side = 0; side < 2; ++side) {
        const auto &m = models[side];
        if (!m)
            continue;
        for (std::size_t p = 0; p < m->page_count(); ++p)
            for (std::size_t c = 0; c < m->cluster_count(p); ++c) {
                o.control.check();
                const auto metadata = m->cluster_metadata(p, c, o.control);
                if (!metadata.at("fullLeaf").get<bool>())
                    continue;
                const auto n = metadata.at("triangles").get<std::size_t>();
                for (std::size_t t = 0; t < n; ++t) {
                    o.control.check();
                    budget.triangle();
                    const auto identity = key(*m, p, c, t, o.control);
                    auto it = pairs.find(identity);
                    if (it == pairs.end()) {
                        budget.reserve(192);
                        it = pairs.emplace(identity, Pair{}).first;
                    }
                    budget.reserve(32);
                    it->second.refs[side].push_back({static_cast<std::uint32_t>(p),
                                                     static_cast<std::uint32_t>(c),
                                                     static_cast<std::uint32_t>(t)});
                    ++counts[side];
                }
            }
    }
    std::uint64_t matched = 0, removed = 0, added = 0;
    for (const auto &entry : pairs) {
        o.control.check();
        const auto &before = entry.second.refs[0];
        const auto &after = entry.second.refs[1];
        const auto n = std::min(before.size(), after.size());
        matched += n;
        removed += before.size() - n;
        added += after.size() - n;
    }
    budget.reserve((removed + added) * sizeof(NaniteTriangleReference));
    out.removed.reserve(static_cast<std::size_t>(removed));
    out.added.reserve(static_cast<std::size_t>(added));
    for (const auto &entry : pairs) {
        o.control.check();
        const auto &before = entry.second.refs[0];
        const auto &after = entry.second.refs[1];
        const auto n = std::min(before.size(), after.size());
        out.removed.insert(out.removed.end(), before.begin() + n, before.end());
        out.added.insert(out.added.end(), after.begin() + n, after.end());
    }
    out.summary["beforeTriangles"] = counts[0];
    out.summary["afterTriangles"] = counts[1];
    out.summary["matchedTriangles"] = matched;
    out.summary["removedTriangles"] = removed;
    out.summary["addedTriangles"] = added;
    out.summary["surfaceStatus"] = removed || added ? "changed" : "identical";
    out.summary["status"] = removed || added || ah != bh ? "changed" : "identical";
    o.control.checkpoint("compare-nanite-surface", budget.triangles, budget.triangles);
    return out;
}
Json NaniteSurfaceComparison::to_json(bool include) const {
    auto out = summary;
    if (include) {
        out["removed"] = Json::array();
        out["added"] = Json::array();
        for (const auto &r : removed)
            out["removed"].push_back({{"page", r.page}, {"cluster", r.cluster}, {"triangle", r.triangle}});
        for (const auto &r : added)
            out["added"].push_back({{"page", r.page}, {"cluster", r.cluster}, {"triangle", r.triangle}});
    }
    return out;
}
} // namespace cyuediff
