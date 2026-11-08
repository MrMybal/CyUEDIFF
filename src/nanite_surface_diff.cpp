#include <cyuediff/cooked_nanite.hpp>
#include <algorithm>
#include <cstring>
#include <map>
namespace cyuediff {
namespace {
using Key = std::array<std::uint32_t, 10>;
struct Sample {
    NaniteTriangleReference ref;
    std::uint8_t rotations = 1, rotation = 0, unavailable = 0;
};
struct Pair {
    std::vector<Sample> refs[2];
};
struct Budget {
    const NaniteSurfaceDiffOptions &o;
    std::uint64_t bytes = 0, working = 0, triangles = 0;
    void check(std::uint64_t n) const {
        if (bytes > o.max_output_bytes || working > o.max_output_bytes - bytes ||
            n > o.max_output_bytes - bytes - working)
            throw Error("nanite-diff-limit", "Nanite surface comparison exceeds output byte budget");
    }
    void reserve(std::uint64_t n) {
        check(n);
        bytes += n;
    }
    void work(std::uint64_t n) {
        check(n);
        working += n;
    }
    void triangle() {
        if (triangles >= o.max_triangles)
            throw Error("nanite-diff-limit", "Nanite surface comparison exceeds aggregate triangle budget");
        ++triangles;
    }
};
std::uint32_t bits(float value) {
    if (value == 0)
        value = 0.f;
    std::uint32_t out;
    std::memcpy(&out, &value, 4);
    return out;
}
std::pair<Key, std::uint8_t> key(const NaniteGeometry &m, std::size_t p, std::size_t c, std::size_t t,
                                 const OperationControl &control) {
    const auto triangle = m.triangle(p, c, t, control);
    std::array<std::array<std::uint32_t, 3>, 3> vertices{};
    for (unsigned i = 0; i < 3; ++i) {
        const auto position = m.position(p, c, triangle.vertices[i], control);
        for (unsigned k = 0; k < 3; ++k)
            vertices[i][k] = bits(position[k]);
    }
    Key best{};
    std::uint8_t rotations = 0;
    for (unsigned r = 0; r < 3; ++r) {
        Key candidate{};
        candidate[0] = triangle.material;
        for (unsigned i = 0; i < 3; ++i)
            for (unsigned k = 0; k < 3; ++k)
                candidate[1 + 3 * i + k] = vertices[(i + r) % 3][k];
        if (!rotations || candidate < best) {
            best = candidate;
            rotations = std::uint8_t(1u << r);
        } else if (candidate == best)
            rotations |= std::uint8_t(1u << r);
    }
    return {best, rotations};
}
using Corners = std::array<NaniteVertexAttributes, 3>;
Corners corners(const NaniteGeometry &model, const Sample &s, const OperationControl &control) {
    const auto t = model.triangle(s.ref.page, s.ref.cluster, s.ref.triangle, control);
    Corners out;
    for (unsigned i = 0; i < 3; ++i)
        out[i] = model.attributes(s.ref.page, s.ref.cluster, t.vertices[(i + s.rotation) % 3], control);
    return out;
}
std::vector<std::uint32_t> signature(const NaniteGeometry &m, Sample &sample,
                                     const OperationControl &control) {
    Sample unrotated = sample;
    unrotated.rotation = 0;
    const auto values = corners(m, unrotated, control);
    std::vector<std::uint32_t> best;
    bool first = true;
    for (unsigned r = 0; r < 3; ++r) {
        if (!(sample.rotations & (1u << r)))
            continue;
        std::vector<std::uint32_t> candidate;
        candidate.reserve(64);
        std::uint8_t unavailable = 0;
        for (unsigned i = 0; i < 3; ++i) {
            const auto &a = values[(i + r) % 3];
            candidate.push_back(a.normal ? 1 : 0);
            if (a.normal)
                for (auto v : *a.normal)
                    candidate.push_back(bits(v));
            else
                unavailable |= nanite_normals;
            candidate.push_back(!a.packed_tangent ? 0 : a.tangent ? 1 : 2);
            if (a.tangent)
                for (auto v : *a.tangent)
                    candidate.push_back(bits(v));
            else if (a.packed_tangent)
                unavailable |= nanite_tangents;
            std::uint32_t color = 0;
            for (unsigned k = 0; k < 4; ++k)
                color |= std::uint32_t(a.color[k]) << (8 * k);
            candidate.push_back(color);
            candidate.push_back(static_cast<std::uint32_t>(a.uvs.size()));
            for (const auto &uv : a.uvs)
                for (auto v : uv)
                    candidate.push_back(bits(v));
        }
        if (first || candidate < best) {
            best = std::move(candidate);
            sample.rotation = static_cast<std::uint8_t>(r);
            sample.unavailable = unavailable;
            first = false;
        }
    }
    return best;
}
std::pair<std::uint8_t, std::uint8_t> channels(const NaniteGeometry &a, const Sample &ar,
                                               const NaniteGeometry &b, const Sample &br,
                                               const OperationControl &control) {
    const auto before = corners(a, ar, control), after = corners(b, br, control);
    std::uint8_t changed = 0, unknown = 0;
    for (unsigned i = 0; i < 3; ++i) {
        const auto &x = before[i], &y = after[i];
        if (!x.normal || !y.normal)
            unknown |= nanite_normals;
        else if (x.normal != y.normal)
            changed |= nanite_normals;
        if ((x.packed_tangent && !x.tangent) || (y.packed_tangent && !y.tangent))
            unknown |= nanite_tangents;
        else if (x.tangent != y.tangent)
            changed |= nanite_tangents;
        if (x.color != y.color)
            changed |= nanite_colors;
        if (x.uvs != y.uvs)
            changed |= nanite_uvs;
    }
    return {changed, unknown};
}
struct Stats {
    std::uint64_t changed = 0, unavailable = 0, ambiguous = 0, changes[4]{}, unknown[4]{};
    void observe(std::uint8_t c, std::uint8_t u) {
        changed += c != 0;
        unavailable += u != 0;
        for (unsigned k = 0; k < 4; ++k) {
            changes[k] += (c & (1u << k)) != 0;
            unknown[k] += (u & (1u << k)) != 0;
        }
    }
};
void append(std::vector<NaniteTriangleReference> &out, const Sample &s, Budget &budget) {
    budget.reserve(3 * sizeof(NaniteTriangleReference));
    out.push_back(s.ref);
}
Json reference(const NaniteTriangleReference &r) {
    return {{"page", r.page}, {"cluster", r.cluster}, {"triangle", r.triangle}};
}
Json channel_names(std::uint8_t mask) {
    Json out = Json::array();
    const char *names[] = {"normals", "tangents", "colors", "uvs"};
    for (unsigned k = 0; k < 4; ++k)
        if (mask & (1u << k))
            out.push_back(names[k]);
    return out;
}
} // namespace
NaniteSurfaceComparison compare_nanite_surfaces(const CookedNaniteResource &a, const CookedNaniteResource &b,
                                                const NaniteSurfaceDiffOptions &o) {
    o.control.check();
    Budget budget{o};
    budget.reserve(4096);
    NaniteSurfaceComparison out;
    Stats stats;
    const bool geometry_complete = a.geometry_known() && b.geometry_known();
    const auto ah = a.encoding_sha256(), bh = b.encoding_sha256();
    out.summary = {
        {"schema", "cyuediff.nanite-surface-diff/1"},
        {"scope", o.attributes
                      ? "exact full-leaf native positions, oriented triangle multiplicity, numeric materials "
                        "and decoded shading on common surfaces; runtime LOD/material evaluation excluded"
                      : "exact full-leaf native positions, oriented triangle multiplicity and numeric "
                        "materials; vertex attributes/runtime LOD excluded"},
        {"complete", geometry_complete},
        {"geometryComplete", geometry_complete},
        {"encodingStatus", geometry_complete ? (ah == bh ? "identical" : "changed") : "unavailable"},
        {"surfaceStatus", geometry_complete ? "identical" : "unavailable"},
        {"status", geometry_complete ? "identical" : "incomplete"},
        {"beforeTriangles", 0},
        {"afterTriangles", 0},
        {"matchedTriangles", 0},
        {"removedTriangles", 0},
        {"addedTriangles", 0},
        {"storedAttributesCompared", false},
        {"attributeStatus", o.attributes ? "unavailable" : "not-requested"},
        {"attributeComplete", o.attributes ? Json(false) : Json(nullptr)},
        {"ambiguousAttributePairs", 0},
        {"modifiedAttributeTriangles", 0},
        {"unavailableAttributeTriangles", 0},
        {"attributeChannels", Json::object()}};
    if (!geometry_complete)
        return out;
    const std::shared_ptr<const NaniteGeometry> models[2] = {a.geometry(), b.geometry()};
    const bool attributes_available =
        (!models[0] || models[0]->attributes_decoded()) && (!models[1] || models[1]->attributes_decoded());
    std::map<Key, Pair> pairs;
    std::uint64_t counts[2]{};
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
                    auto it = pairs.find(identity.first);
                    if (it == pairs.end()) {
                        budget.reserve(192);
                        it = pairs.emplace(identity.first, Pair{}).first;
                    }
                    budget.reserve(3 * sizeof(Sample));
                    it->second.refs[side].push_back(
                        {{static_cast<std::uint32_t>(p), static_cast<std::uint32_t>(c),
                          static_cast<std::uint32_t>(t)},
                         identity.second});
                    ++counts[side];
                }
            }
    }
    std::uint64_t matched = 0;
    for (const auto &entry : pairs) {
        o.control.check();
        budget.working = 0;
        const auto &before = entry.second.refs[0], &after = entry.second.refs[1];
        const auto common = std::min(before.size(), after.size());
        matched += common;
        if (!o.attributes || !attributes_available || !common) {
            if (o.attributes && !attributes_available)
                for (std::size_t i = 0; i < common; ++i) {
                    o.control.check();
                    stats.observe(0, 15);
                }
            for (std::size_t i = common; i < before.size(); ++i)
                append(out.removed, before[i], budget);
            for (std::size_t i = common; i < after.size(); ++i)
                append(out.added, after[i], budget);
            continue;
        }
        budget.work(4096);
        if (before.size() == 1 && after.size() == 1) {
            auto ar = before[0], br = after[0];
            auto ak = signature(*models[0], ar, o.control), bk = signature(*models[1], br, o.control);
            const auto difference =
                ak == bk ? std::make_pair(std::uint8_t(0), std::uint8_t(ar.unavailable | br.unavailable))
                         : channels(*models[0], ar, *models[1], br, o.control);
            stats.observe(difference.first, difference.second);
            if (difference.first) {
                budget.reserve(3 * sizeof(NaniteAttributeChange));
                out.modified.push_back(
                    {ar.ref, br.ref, difference.first, difference.second, false, ar.rotation, br.rotation});
            }
            continue;
        }
        std::map<std::vector<std::uint32_t>, Pair> bins;
        for (unsigned side = 0; side < 2; ++side)
            for (auto sample : entry.second.refs[side]) {
                o.control.check();
                auto k = signature(*models[side], sample, o.control);
                auto it = bins.find(k);
                if (it == bins.end()) {
                    budget.work(192 + k.capacity() * sizeof(std::uint32_t));
                    it = bins.emplace(std::move(k), Pair{}).first;
                }
                budget.work(3 * sizeof(Sample));
                it->second.refs[side].push_back(sample);
            }
        std::vector<Sample> remaining[2];
        for (const auto &bin : bins) {
            o.control.check();
            const auto &ar = bin.second.refs[0], &br = bin.second.refs[1];
            const auto equal = std::min(ar.size(), br.size());
            for (std::size_t i = 0; i < equal; ++i) {
                o.control.check();
                stats.observe(0, ar[i].unavailable | br[i].unavailable);
            }
            for (unsigned side = 0; side < 2; ++side)
                for (std::size_t i = equal; i < bin.second.refs[side].size(); ++i) {
                    budget.work(3 * sizeof(Sample));
                    remaining[side].push_back(bin.second.refs[side][i]);
                }
        }
        const auto changed = std::min(remaining[0].size(), remaining[1].size());
        const bool ambiguous = changed && (remaining[0].size() > 1 || remaining[1].size() > 1);
        if (ambiguous)
            stats.ambiguous += changed;
        for (std::size_t i = 0; i < changed; ++i) {
            o.control.check();
            const auto &ar = remaining[0][i], &br = remaining[1][i];
            const auto difference = channels(*models[0], ar, *models[1], br, o.control);
            stats.observe(difference.first, difference.second);
            if (difference.first) {
                budget.reserve(3 * sizeof(NaniteAttributeChange));
                out.modified.push_back({ar.ref, br.ref, difference.first, difference.second, ambiguous,
                                        ar.rotation, br.rotation});
            }
        }
        for (std::size_t i = changed; i < remaining[0].size(); ++i)
            append(out.removed, remaining[0][i], budget);
        for (std::size_t i = changed; i < remaining[1].size(); ++i)
            append(out.added, remaining[1][i], budget);
    }
    budget.working = 0;
    const bool complete = !o.attributes || !stats.unavailable;
    out.summary["complete"] = complete;
    out.summary["beforeTriangles"] = counts[0];
    out.summary["afterTriangles"] = counts[1];
    out.summary["matchedTriangles"] = matched;
    out.summary["removedTriangles"] = out.removed.size();
    out.summary["addedTriangles"] = out.added.size();
    out.summary["surfaceStatus"] = out.removed.empty() && out.added.empty() ? "identical" : "changed";
    out.summary["storedAttributesCompared"] = o.attributes && attributes_available && matched > 0;
    out.summary["attributeComplete"] = o.attributes ? Json(!stats.unavailable) : Json(nullptr);
    out.summary["attributeStatus"] = !o.attributes       ? "not-requested"
                                     : stats.changed     ? "changed"
                                     : stats.unavailable ? "unavailable"
                                     : matched           ? "identical"
                                                         : "not-applicable";
    out.summary["ambiguousAttributePairs"] = stats.ambiguous;
    out.summary["modifiedAttributeTriangles"] = stats.changed;
    out.summary["unavailableAttributeTriangles"] = stats.unavailable;
    if (o.attributes) {
        const char *names[] = {"normals", "tangents", "colors", "uvs"};
        for (unsigned k = 0; k < 4; ++k)
            out.summary["attributeChannels"][names[k]] = {{"changedTriangles", stats.changes[k]},
                                                          {"unavailableTriangles", stats.unknown[k]},
                                                          {"complete", !stats.unknown[k]},
                                                          {"status", stats.changes[k]   ? "changed"
                                                                     : stats.unknown[k] ? "unavailable"
                                                                     : matched          ? "identical"
                                                                                        : "not-applicable"}};
    }
    out.summary["status"] = !complete ? "incomplete"
                            : !out.removed.empty() || !out.added.empty() || stats.changed || ah != bh
                                ? "changed"
                                : "identical";
    o.control.checkpoint("compare-nanite-surface", budget.triangles, budget.triangles);
    return out;
}
Json NaniteSurfaceComparison::to_json(bool include) const {
    auto out = summary;
    if (include) {
        out["removed"] = Json::array();
        out["added"] = Json::array();
        out["modified"] = Json::array();
        for (const auto &r : removed)
            out["removed"].push_back(reference(r));
        for (const auto &r : added)
            out["added"].push_back(reference(r));
        for (const auto &r : modified)
            out["modified"].push_back(
                {{"before", reference(r.before)},
                 {"after", reference(r.after)},
                 {"channels", channel_names(r.changed)},
                 {"unavailable", channel_names(r.unavailable)},
                 {"ambiguous", r.ambiguous},
                 {"beforeCornerOrder",
                  std::array<unsigned, 3>{r.before_rotation, (r.before_rotation + 1u) % 3,
                                          (r.before_rotation + 2u) % 3}},
                 {"afterCornerOrder", std::array<unsigned, 3>{r.after_rotation, (r.after_rotation + 1u) % 3,
                                                              (r.after_rotation + 2u) % 3}}});
    }
    return out;
}
} // namespace cyuediff
