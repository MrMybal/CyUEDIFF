#include "cooked_nanite_internal.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace cyuediff {
namespace {
void need(bool value, const char *message) {
    if (!value)
        throw Error("cooked-nanite-layout", message);
}
void limit(bool value, const char *message) {
    if (!value)
        throw Error("cooked-nanite-limit", message);
}
struct Cursor {
    const std::vector<std::uint8_t> &bytes;
    std::uint64_t pos, end;
    void range(std::uint64_t n) const {
        need(pos <= end && n <= end - pos, "Nanite resource field exceeds native export window");
    }
    std::uint32_t number(unsigned n) {
        range(n);
        std::uint32_t value = 0;
        for (unsigned i = 0; i < n; ++i)
            value |= std::uint32_t(bytes[std::size_t(pos++)]) << (8 * i);
        return value;
    }
    std::uint32_t u32() {
        return number(4);
    }
    std::int32_t i32() {
        auto u = u32();
        std::int32_t value;
        std::memcpy(&value, &u, 4);
        return value;
    }
    std::uint32_t count(std::uint64_t maximum) {
        const auto n = i32();
        need(n >= 0, "Negative Nanite resource array count");
        limit(std::uint64_t(n) <= maximum, "Nanite resource array exceeds count budget");
        return static_cast<std::uint32_t>(n);
    }
};
float scalar(std::uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}
struct State {
    std::uint32_t offset = 0, bytes = 0, disk_bytes = 0, dependency_start = 0;
    std::uint16_t dependencies = 0;
    std::uint8_t depth = 0, flags = 0;
    std::string hash, unavailable;
};
struct Child {
    std::array<std::uint32_t, 13> words{};
};
struct Node {
    std::array<Child, 4> children;
};
struct Budget {
    const CookedNaniteOptions &options;
    std::uint64_t output = 0;
    void reserve(std::uint64_t n) {
        limit(output <= options.max_output_bytes && n <= options.max_output_bytes - output,
              "Nanite native/geometry output exceeds aggregate byte budget");
        output += n;
    }
};
Json floats(const Child &c, unsigned first, unsigned n) {
    Json values = Json::array();
    for (unsigned i = 0; i < n; ++i) {
        const auto v = scalar(c.words[first + i]);
        if (std::isfinite(v))
            values.push_back(v);
        else
            values.push_back({{"value", nullptr}, {"float32Bits", c.words[first + i]}});
    }
    return values;
}
} // namespace
struct CookedNaniteResource::Impl {
    Json info;
    std::vector<State> states;
    std::vector<Node> nodes;
    std::vector<std::uint32_t> roots, dependencies, leaf_pages, leaf_starts;
    std::vector<std::uint16_t> imposters;
    std::shared_ptr<const NaniteGeometry> geometry;
    std::uint32_t root_pages = 0;
    std::uint64_t accounted = 0;
    std::string encoding;
};
CookedNaniteResource::CookedNaniteResource(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
namespace detail {
struct NaniteResourceReader {
    static CookedNaniteResource read(const std::vector<std::uint8_t> &bytes, std::uint64_t &position,
                                     std::uint64_t end, ZenBulkReader &bulk, const CookedNaniteOptions &o,
                                     Json provenance) {
        o.control.check();
        if (o.profile != NaniteProfile::unreal_5_3_windows)
            throw Error("cooked-nanite-profile", "Select the exact UE 5.3 Windows Nanite resource profile");
        Cursor r{bytes, position, end};
        const auto begin = r.pos;
        Budget budget{o};
        budget.reserve(8192 + 4 * provenance.dump().size());
        auto out = std::make_shared<CookedNaniteResource::Impl>();
        out->info = {{"schema", "cyuediff.cooked-nanite/1"},
                     {"profile", "ue5.3-windows"},
                     {"source", std::move(provenance)},
                     {"nativeOffset", begin},
                     {"status", "unavailable"},
                     {"storedAttributesDecoded", false},
                     {"scope", "stored cluster position/topology/material channels; no runtime LOD cut"}};
        const auto global = r.number(1), classes = r.number(1);
        need((global & ~3u) == 0 && classes == 0, "Unsupported Nanite resource strip flags");
        out->info["stripFlags"] = {{"global", global}, {"class", classes}};
        if (global & 2) {
            out->info["status"] = "stripped";
            out->info["unavailable"] = "server-stripped";
        } else {
            const auto flags = r.u32();
            if (flags & ~15u || flags & 4)
                throw Error("cooked-nanite-profile",
                            "Cooked Nanite cannot require DDC streaming or unknown resource flags");
            out->info["resourceFlags"] = flags;
            const auto bulk_index = r.u32();
            auto stream = bulk.describe(bulk_index, r.pos, r.end);
            const auto root_bytes = r.count(o.max_read_bytes);
            r.range(root_bytes);
            const auto root_start = r.pos;
            r.pos += root_bytes;
            out->info["rootData"] = {{"offset", root_start},
                                     {"bytes", root_bytes},
                                     {"sha256", hash_range(bytes, root_start, root_bytes, o.control)}};
            const auto pages = r.count(o.max_pages);
            budget.reserve(std::uint64_t(pages) * (sizeof(State) + 256));
            r.range(20ull * pages);
            out->states.resize(pages);
            for (auto &s : out->states) {
                o.control.check();
                s.offset = r.u32();
                s.bytes = r.u32();
                s.disk_bytes = r.u32();
                s.dependency_start = r.u32();
                s.dependencies = static_cast<std::uint16_t>(r.number(2));
                s.depth = static_cast<std::uint8_t>(r.number(1));
                s.flags = static_cast<std::uint8_t>(r.number(1));
                need(s.depth <= 14 && !(s.flags & ~1u), "Invalid Nanite page state depth or flags");
            }
            const auto nodes = r.count(o.max_hierarchy_nodes);
            budget.reserve(std::uint64_t(nodes) * sizeof(Node));
            r.range(208ull * nodes);
            const auto hierarchy_start = r.pos;
            out->nodes.resize(nodes);
            for (auto &node : out->nodes)
                for (auto &child : node.children) {
                    o.control.check();
                    for (auto &word : child.words)
                        word = r.u32();
                    if (child.words[12]) {
                        for (unsigned i : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 8u, 9u, 10u})
                            need(std::isfinite(scalar(child.words[i])),
                                 "Non-finite active Nanite hierarchy bounds");
                        need(scalar(child.words[3]) >= 0 && scalar(child.words[8]) >= 0 &&
                                 scalar(child.words[9]) >= 0 && scalar(child.words[10]) >= 0,
                             "Invalid active Nanite hierarchy radius/extent");
                    }
                }
            out->info["hierarchy"] = {
                {"nodes", nodes},
                {"bytes", 208ull * nodes},
                {"sha256", hash_range(bytes, hierarchy_start, 208ull * nodes, o.control)}};
            auto integers = [&](std::vector<std::uint32_t> &target, std::uint32_t maximum) {
                const auto n = r.count(maximum);
                r.range(4ull * n);
                budget.reserve(4ull * n);
                target.reserve(n);
                for (unsigned i = 0; i < n; ++i) {
                    o.control.check();
                    target.push_back(r.u32());
                }
            };
            integers(out->roots, 65535);
            integers(out->dependencies, o.max_dependencies);
            const auto atlas = r.count(o.max_imposter_values);
            r.range(2ull * atlas);
            budget.reserve(2ull * atlas);
            out->imposters.reserve(atlas);
            const auto atlas_start = r.pos;
            for (unsigned i = 0; i < atlas; ++i) {
                o.control.check();
                out->imposters.push_back(static_cast<std::uint16_t>(r.number(2)));
            }
            out->info["imposterAtlas"] = {{"values", atlas},
                                          {"sha256", hash_range(bytes, atlas_start, 2ull * atlas, o.control)},
                                          {"decoded", false}};
            out->root_pages = r.u32();
            const auto precision = r.i32(), normal = r.i32();
            const auto input_triangles = r.u32(), input_vertices = r.u32();
            const auto input_meshes = r.number(2), input_uvs = r.number(2), clusters = r.u32();
            out->info["positionPrecision"] = precision;
            out->info["normalPrecision"] = normal;
            out->info["input"] = {{"triangles", input_triangles},
                                  {"vertices", input_vertices},
                                  {"meshes", input_meshes},
                                  {"uvChannels", input_uvs}};
            out->info["declaredClusters"] = clusters;
            out->info["rootPages"] = out->root_pages;
            limit(clusters <= o.max_clusters, "Declared Nanite clusters exceed budget");
            need(out->root_pages <= pages && input_uvs <= 4, "Invalid Nanite resource page/UV count");
            std::uint64_t root_covered = 0, stream_covered = 0, dependencies_covered = 0;
            budget.reserve(std::uint64_t(pages) * 64 + 8ull * out->dependencies.size());
            std::vector<std::uint32_t> stamps(pages, 0xffffffffu), indegree(pages), ready;
            std::vector<std::vector<std::uint32_t>> dependents(pages);
            ready.reserve(pages);
            for (std::size_t pi = 0; pi < out->states.size(); ++pi) {
                const auto &s = out->states[pi];
                o.control.check();
                need(s.bytes >= 40 && s.disk_bytes >= 32 && s.disk_bytes <= 262144 &&
                         s.bytes <= 262144 + 6152,
                     "Invalid stored Nanite page byte sizes");
                auto &covered = pi < out->root_pages ? root_covered : stream_covered;
                need(s.offset == covered,
                     "Nanite page windows do not partition their storage in resource order");
                covered += s.bytes;
                need(s.dependency_start <= out->dependencies.size() &&
                         s.dependencies <= out->dependencies.size() - s.dependency_start,
                     "Nanite page dependencies exceed resource table");
                need(s.dependency_start == dependencies_covered,
                     "Nanite dependency windows are not contiguous");
                dependencies_covered += s.dependencies;
                indegree[pi] = s.dependencies;
                if (!s.dependencies)
                    ready.push_back(static_cast<std::uint32_t>(pi));
                for (unsigned i = 0; i < s.dependencies; ++i) {
                    o.control.check();
                    const auto parent = out->dependencies[s.dependency_start + i];
                    need(parent < pages && parent != pi && stamps[parent] != pi,
                         "Invalid or duplicate Nanite resource page dependency");
                    stamps[parent] = static_cast<std::uint32_t>(pi);
                    dependents[parent].push_back(static_cast<std::uint32_t>(pi));
                }
            }
            need(dependencies_covered == out->dependencies.size(),
                 "Nanite dependency table contains unreferenced entries");
            for (std::size_t at = 0; at < ready.size(); ++at) {
                o.control.check();
                for (auto child : dependents[ready[at]])
                    if (--indegree[child] == 0)
                        ready.push_back(child);
            }
            need(ready.size() == pages, "Cyclic Nanite resource page dependencies");
            need(root_covered == root_bytes && stream_covered == stream.bytes,
                 "Nanite page windows do not cover declared root/stream storage");
            if (!pages) {
                need(!root_bytes && !out->root_pages && !clusters && !nodes && out->roots.empty() &&
                         out->dependencies.empty(),
                     "Empty Nanite page table contradicts native data");
                out->info["status"] = "empty";
            } else {
                need(out->root_pages > 0 && clusters >= pages && precision >= -8 && precision <= 23 &&
                         normal >= 0 && normal <= 15 && input_meshes > 0 &&
                         out->roots.size() == input_meshes && nodes > 0,
                     "Invalid nonempty Nanite resource declarations");
                need(out->roots.front() == 0, "Nanite hierarchy root offsets do not start at zero");
                budget.reserve(16ull * nodes);
                std::vector<std::uint32_t> node_base(nodes);
                std::vector<bool> visited(nodes);
                std::vector<std::uint32_t> queue;
                queue.reserve(nodes);
                for (std::size_t mesh = 0; mesh < out->roots.size(); ++mesh) {
                    const auto first = out->roots[mesh],
                               last = mesh + 1 < out->roots.size() ? out->roots[mesh + 1] : nodes;
                    need(first < last && last <= nodes, "Nanite hierarchy root ranges are invalid");
                    for (auto i = first; i < last; ++i)
                        node_base[i] = first;
                    visited[first] = true;
                    queue.push_back(first);
                }
                std::uint64_t leaf_slots = 0;
                for (std::size_t at = 0; at < queue.size(); ++at) {
                    o.control.check();
                    const auto ni = queue[at], base = node_base[ni];
                    for (const auto &child : out->nodes[ni].children) {
                        const auto group = child.words[12], ref = child.words[11];
                        if (!group)
                            continue;
                        if (group == 0xffffffffu) {
                            need(ref < nodes - base, "Nanite hierarchy child node exceeds resource");
                            const auto target = base + ref;
                            need(node_base[target] == base && !visited[target],
                                 "Cyclic, shared or cross-mesh Nanite hierarchy node");
                            visited[target] = true;
                            queue.push_back(target);
                        } else {
                            ++leaf_slots;
                            need(ref == 0xffffffffu && (group & 511) > 0,
                                 "Invalid uninstalled Nanite hierarchy leaf");
                            const auto first = group >> 12, num = (group >> 9) & 7;
                            if (num)
                                need(first >= out->root_pages && first <= pages && num <= pages - first,
                                     "Nanite hierarchy leaf page range exceeds resource");
                        }
                    }
                }
                need(queue.size() == nodes, "Nanite hierarchy contains unreachable nodes");
                need(leaf_slots > 0, "Nonempty Nanite hierarchy has no leaf groups");
                if (pages > out->root_pages)
                    bulk.load(stream);
                out->info["streaming"] = stream.metadata;
                out->info["streaming"]["needed"] = pages > out->root_pages;
                NaniteGeometrySource source;
                source.root_pages = out->root_pages;
                const auto copied_bytes = root_covered + (stream.unavailable.empty() ? stream_covered : 0);
                limit(copied_bytes <= o.max_read_bytes, "Nanite page payloads exceed source byte budget");
                budget.reserve(std::uint64_t(pages) * sizeof(NanitePageSource) + copied_bytes +
                               4ull * out->dependencies.size());
                source.pages.resize(pages);
                bool available = true;
                std::uint64_t available_clusters = 0;
                out->leaf_pages.assign(4ull * nodes, 0xffffffffu);
                out->leaf_starts.assign(4ull * nodes, 0xffffffffu);
                auto &leaf_targets = out->leaf_pages;
                auto &leaf_starts = out->leaf_starts;
                budget.reserve(32ull * nodes);
                for (std::size_t pi = 0; pi < source.pages.size(); ++pi) {
                    o.control.check();
                    auto &s = out->states[pi];
                    auto &target = source.pages[pi];
                    target.flags = s.flags;
                    target.dependencies.assign(out->dependencies.begin() + s.dependency_start,
                                               out->dependencies.begin() + s.dependency_start +
                                                   s.dependencies);
                    const std::vector<std::uint8_t> *data = &bytes;
                    std::uint64_t offset = root_start + s.offset;
                    if (pi >= out->root_pages) {
                        if (!stream.unavailable.empty()) {
                            s.unavailable = stream.unavailable;
                            available = false;
                            continue;
                        }
                        need(stream.data != nullptr, "Nanite streaming bulk was not loaded");
                        data = stream.data;
                        offset = stream.offset + s.offset;
                    }
                    need(offset <= data->size() && s.bytes <= data->size() - offset,
                         "Nanite page exceeds loaded storage");
                    Cursor p{*data, offset, offset + s.bytes};
                    const auto magic = p.number(2), count = p.number(2), hf = p.number(2), cf = p.number(2);
                    need(magic == 0x464e && count > 0 && count <= 256 && hf <= 256 && cf <= 256,
                         "Invalid Nanite resource page fixup header");
                    need(8 + 16ull * hf + 8ull * cf + s.disk_bytes == s.bytes,
                         "Nanite page state size disagrees with actual fixup/disk window");
                    available_clusters += count;
                    for (unsigned i = 0; i < hf; ++i) {
                        o.control.check();
                        const auto page = p.u32(), node_child = p.u32(), start = p.u32(),
                                   dependency = p.u32();
                        const auto ni = node_child >> 6, ci = node_child & 63;
                        need(page < pages && ni < nodes && ci < 4,
                             "Nanite hierarchy fixup target exceeds native hierarchy");
                        const auto &child = out->nodes[ni].children[ci];
                        need(child.words[12] && child.words[12] != 0xffffffffu,
                             "Nanite hierarchy fixup targets a non-leaf slot");
                        const auto slot = 4ull * ni + ci;
                        need(leaf_targets[slot] == 0xffffffffu ||
                                 (leaf_targets[slot] == page && leaf_starts[slot] == start),
                             "Conflicting Nanite hierarchy fixup targets");
                        leaf_targets[slot] = page;
                        leaf_starts[slot] = start;
                        const auto first = dependency >> 3, num = dependency & 7;
                        if (num)
                            need(first <= pages && num <= pages - first,
                                 "Nanite hierarchy fixup dependency exceeds resource");
                    }
                    for (unsigned i = 0; i < cf; ++i) {
                        o.control.check();
                        p.u32();
                        p.u32();
                    }
                    s.hash = hash_range(*data, offset, s.bytes, o.control);
                    target.bytes.assign(data->begin() + static_cast<std::ptrdiff_t>(offset),
                                        data->begin() + static_cast<std::ptrdiff_t>(offset + s.bytes));
                }
                need(available_clusters <= clusters,
                     "Available Nanite page clusters exceed resource declaration");
                if (!available) {
                    out->info["status"] = "unavailable";
                    out->info["unavailable"] = "streaming-page-data-unavailable";
                } else {
                    need(available_clusters == clusters,
                         "Nanite page cluster total disagrees with resource declaration");
                    auto options = static_cast<NaniteGeometryOptions>(o);
                    options.max_output_bytes = o.max_output_bytes - budget.output;
                    auto decoded = std::make_shared<NaniteGeometry>(decode_nanite_geometry(source, options));
                    budget.reserve(decoded->accounted_output_bytes());
                    for (std::size_t pi = 0; pi < decoded->page_count(); ++pi)
                        for (std::size_t ci = 0; ci < decoded->cluster_count(pi); ++ci) {
                            o.control.check();
                            const auto meta = decoded->cluster_metadata(pi, ci, o.control);
                            need(meta.at("positionPrecision") == precision &&
                                     meta.at("uvChannels") == input_uvs,
                                 "Nanite packed cluster precision/UV count contradicts resource");
                        }
                    budget.reserve(clusters);
                    std::vector<std::uint64_t> cluster_bases(pages + 1);
                    for (std::size_t pi = 0; pi < pages; ++pi)
                        cluster_bases[pi + 1] = cluster_bases[pi] + decoded->cluster_count(pi);
                    std::vector<std::uint8_t> cluster_seen(clusters);
                    std::uint64_t covered_clusters = 0;
                    for (std::size_t ni = 0; ni < out->nodes.size(); ++ni)
                        for (unsigned ci = 0; ci < 4; ++ci) {
                            o.control.check();
                            const auto group = out->nodes[ni].children[ci].words[12];
                            if (!group || group == 0xffffffffu)
                                continue;
                            const auto slot = 4ull * ni + ci;
                            need(leaf_targets[slot] != 0xffffffffu,
                                 "Nanite hierarchy leaf lacks stored fixup target");
                            const auto page = leaf_targets[slot], start = leaf_starts[slot],
                                       size = group & 511;
                            need(start <= decoded->cluster_count(page) &&
                                     size <= decoded->cluster_count(page) - start,
                                 "Nanite hierarchy leaf group exceeds target page clusters");
                            for (std::uint32_t ci = 0; ci < size; ++ci) {
                                auto &seen =
                                    cluster_seen[static_cast<std::size_t>(cluster_bases[page] + start + ci)];
                                need(!seen, "Nanite hierarchy groups overlap stored clusters");
                                seen = 1;
                                ++covered_clusters;
                            }
                        }
                    need(covered_clusters == clusters, "Nanite hierarchy does not cover all stored clusters");
                    out->geometry = std::move(decoded);
                    out->info["status"] = "decoded";
                }
            }
            if (!out->info.contains("streaming")) {
                out->info["streaming"] = stream.metadata;
                out->info["streaming"]["needed"] = false;
            }
        }
        out->info["nativeBytes"] = r.pos - begin;
        out->info["nativeSha256"] = hash_range(bytes, begin, r.pos - begin, o.control);
        if (out->info.at("status") == "decoded" || out->info.at("status") == "empty") {
            std::string identity =
                "cyuediff.cooked-nanite-encoding/1:" + out->info.at("nativeSha256").get<std::string>();
            if (out->info.at("streaming").contains("sha256"))
                identity += out->info.at("streaming").at("sha256").get<std::string>();
            out->encoding =
                sha256_bytes(std::vector<std::uint8_t>(identity.begin(), identity.end()), o.control);
        }
        out->info["encodingSha256"] = out->encoding.empty() ? Json(nullptr) : Json(out->encoding);
        out->accounted = budget.output;
        out->info["accountedOutputBytes"] = out->accounted;
        o.control.check();
        position = r.pos;
        return CookedNaniteResource(std::move(out));
    }
};
CookedNaniteResource read_nanite_resource(const std::vector<std::uint8_t> &b, std::uint64_t &p,
                                          std::uint64_t end, ZenBulkReader &bulk,
                                          const CookedNaniteOptions &o, Json source) {
    return NaniteResourceReader::read(b, p, end, bulk, o, std::move(source));
}
} // namespace detail
bool CookedNaniteResource::geometry_known() const {
    return impl_->info.at("status") == "decoded" || impl_->info.at("status") == "empty";
}
std::shared_ptr<const NaniteGeometry> CookedNaniteResource::geometry() const {
    return impl_->geometry;
}
std::string CookedNaniteResource::encoding_sha256() const {
    return impl_->encoding;
}
std::uint64_t CookedNaniteResource::accounted_output_bytes() const {
    return impl_->accounted;
}
std::size_t CookedNaniteResource::hierarchy_node_count() const {
    return impl_->nodes.size();
}
Json CookedNaniteResource::hierarchy_node(std::size_t index, const OperationControl &control) const {
    control.check();
    need(index < impl_->nodes.size(), "Nanite hierarchy node selection exceeds table");
    Json out = {{"index", index}, {"children", Json::array()}};
    for (unsigned i = 0; i < 4; ++i) {
        const auto &c = impl_->nodes[index].children[i];
        const auto group = c.words[12];
        out["children"].push_back({{"index", i},
                                   {"kind", !group                 ? "inactive"
                                            : group == 0xffffffffu ? "node"
                                                                   : "leaf"},
                                   {"lodBounds", floats(c, 0, 4)},
                                   {"boxCenter", floats(c, 4, 3)},
                                   {"packedLodErrors", c.words[7]},
                                   {"boxExtent", floats(c, 8, 3)},
                                   {"childStartReference", c.words[11]},
                                   {"packedResourceGroup", group}});
        const auto slot = 4ull * index + i;
        out["children"].back()["storedFixupTarget"] = group && group != 0xffffffffu &&
                                                              slot < impl_->leaf_pages.size() &&
                                                              impl_->leaf_pages[slot] != 0xffffffffu
                                                          ? Json{{"page", impl_->leaf_pages[slot]},
                                                                 {"firstCluster", impl_->leaf_starts[slot]},
                                                                 {"clusters", group & 511u}}
                                                          : Json(nullptr);
    }
    return out;
}
Json CookedNaniteResource::information(const OperationControl &control) const {
    control.check();
    auto out = impl_->info;
    out["geometryKnown"] = geometry_known();
    return out;
}
Json CookedNaniteResource::catalog(const OperationControl &control) const {
    control.check();
    auto out = impl_->info;
    out["geometryKnown"] = geometry_known();
    out["rootOffsets"] = impl_->roots;
    out["pages"] = Json::array();
    for (std::size_t i = 0; i < impl_->states.size(); ++i) {
        control.check();
        const auto &s = impl_->states[i];
        Json deps = Json::array();
        for (unsigned j = 0; j < s.dependencies; ++j) {
            control.check();
            deps.push_back(impl_->dependencies[s.dependency_start + j]);
        }
        out["pages"].push_back(
            {{"index", i},
             {"root", i < impl_->root_pages},
             {"bulkOffset", s.offset},
             {"bulkBytes", s.bytes},
             {"pageBytes", s.disk_bytes},
             {"dependencies", std::move(deps)},
             {"maxHierarchyDepth", s.depth},
             {"flags", s.flags},
             {"sha256", s.hash.empty() ? Json(nullptr) : Json(s.hash)},
             {"unavailable", s.unavailable.empty() ? Json(nullptr) : Json(s.unavailable)}});
    }
    out["geometry"] = impl_->geometry ? impl_->geometry->catalog(control) : Json(nullptr);
    return out;
}
} // namespace cyuediff
