#include <cyuediff/nanite_geometry.hpp>
#include "archive.hpp"
#include "nanite_attributes_internal.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
namespace cyuediff {
namespace {
void need(bool value, const char *message) {
    if (!value)
        throw Error("nanite-layout", message);
}
void limit(bool value, const char *message) {
    if (!value)
        throw Error("nanite-limit", message);
}
struct Bytes {
    const std::vector<std::uint8_t> &data;
    std::uint64_t start = 0, end = 0;
    void range(std::uint64_t offset, std::uint64_t n) const {
        need(offset <= end - start && n <= end - start - offset, "Nanite field exceeds its page window");
    }
    std::uint32_t u32(std::uint64_t offset) const {
        range(offset, 4);
        std::uint32_t n = 0;
        for (unsigned i = 0; i < 4; ++i)
            n |= std::uint32_t(data[std::size_t(start + offset + i)]) << (i * 8);
        return n;
    }
    std::uint16_t u16(std::uint64_t offset) const {
        range(offset, 2);
        return std::uint16_t(data[std::size_t(start + offset)] |
                             unsigned(data[std::size_t(start + offset + 1)]) << 8);
    }
    std::uint8_t u8(std::uint64_t offset) const {
        range(offset, 1);
        return data[std::size_t(start + offset)];
    }
    std::uint32_t bits(std::uint64_t base, std::uint64_t bit, unsigned n, std::uint64_t bound) const {
        need(n <= 32 && base <= bound && bound <= end - start && bit <= 8 * (bound - base) &&
                 n <= 8 * (bound - base) - bit,
             "Nanite bit stream exceeds its own window");
        std::uint32_t out = 0;
        for (unsigned i = 0; i < n; ++i)
            out |= std::uint32_t((u8(base + (bit + i) / 8) >> ((bit + i) % 8)) & 1) << i;
        return out;
    }
};
std::int32_t signed32(std::uint32_t v) {
    std::int32_t out;
    std::memcpy(&out, &v, 4);
    return out;
}
unsigned pop(std::uint32_t n) {
    unsigned c = 0;
    while (n) {
        n &= n - 1;
        ++c;
    }
    return c;
}
struct Cluster {
    std::array<std::uint32_t, 28> words{};
    std::array<std::uint32_t, 8> disk{};
    std::array<std::int32_t, 3> start{};
    std::array<unsigned, 3> position_bits{};
    std::array<std::uint32_t, 8> mask{};
    std::vector<std::array<std::int32_t, 3>> positions, coded;
    std::vector<NaniteTriangle> triangles;
    detail::NaniteAttributeLayout attribute_layout;
    std::vector<detail::NaniteAttributeCode> attributes, coded_attributes;
    bool attributes_decoded = false;
    std::uint32_t vertices = 0, flags = 0;
    int precision = 0;
};
struct Page {
    std::vector<Cluster> clusters;
    std::vector<std::uint32_t> dependencies;
    std::array<std::uint32_t, 8> header{};
    std::uint64_t disk_start = 0, stored_bytes = 0;
    std::string hash;
    std::uint16_t hierarchy_fixups = 0, cluster_fixups = 0;
    bool root = false, relative = false;
};
struct Budget {
    const NaniteGeometryOptions &o;
    std::uint64_t output = 0, read = 0, vertices = 0, triangles = 0, clusters = 0;
    void add(std::uint64_t &count, std::uint64_t n, std::uint64_t maximum, const char *m) {
        limit(count <= maximum && n <= maximum - count, m);
        count += n;
    }
    void reserve(std::uint64_t n) {
        add(output, n, o.max_output_bytes, "Nanite decoded output exceeds byte budget");
    }
};
Json metadata(const Cluster &c, std::size_t i) {
    return {{"index", i},
            {"vertices", c.vertices},
            {"triangles", c.triangles.size()},
            {"positionPrecision", c.precision},
            {"normalPrecision", (c.words[7] >> 24) & 15},
            {"tangentPrecision", (c.words[7] >> 28) & 15},
            {"colorMode", (c.words[21] >> 26) & 3},
            {"positionStart", c.start},
            {"positionBits", c.position_bits},
            {"flags", c.flags},
            {"fullLeaf", bool(c.flags & 4)},
            {"referencedVertices", c.disk[5]},
            {"storedAttributesDecoded", c.attributes_decoded},
            {"attributeBits", c.words[20] >> 22},
            {"uvChannels", (c.words[21] >> 23) & 7},
            {"hasTangents", bool(c.words[21] & (1u << 22))}};
}
void topology(const Bytes &b, const Page &p, Cluster &c, std::size_t ci, const OperationControl &control) {
    const auto &h = p.header;
    const auto &d = c.disk;
    const auto count = c.words[1] & 255u;
    const auto end = ci + 1 < p.clusters.size() ? p.clusters[ci + 1].disk[0] : h[6];
    need(d[0] <= end && end <= h[6], "Nanite cluster index streams are not ordered");
    std::uint32_t fresh = 0, refs = 0, left = 0, right = 0, head = 0;
    std::uint64_t bit = 0;
    c.triangles.reserve(count);
    for (std::uint32_t ti = 0; ti < count; ++ti) {
        control.check();
        const auto group = ti / 32, local = ti % 32;
        const auto off = h[6] + (ci * 4 + group) * 12;
        const auto sm = b.u32(off), lm = b.u32(off + 4), wm = b.u32(off + 8);
        if (local == 0) {
            need(sm & 1, "Nanite strip block does not start with a triangle");
            if (group) {
                need(((d[6] >> (10 * (group - 1))) & 1023) == refs &&
                         ((d[7] >> (10 * (group - 1))) & 1023) == fresh,
                     "Nanite strip prefix counts disagree");
            }
        }
        const bool start = (sm >> local) & 1, l = (lm >> local) & 1, ref = (wm >> local) & 1;
        NaniteTriangle t;
        auto reference = [&](std::uint32_t base) {
            const auto delta = b.bits(d[0], bit, 5, end);
            bit += 5;
            ++refs;
            need(base >= delta, "Nanite strip reference precedes first vertex");
            return base - delta;
        };
        if (start) {
            const unsigned n = 2 * unsigned(l) + unsigned(ref);
            for (unsigned k = 0; k < 3; ++k) {
                if (k < n) {
                    need(fresh > 0, "Nanite strip starts with unavailable reference");
                    t.vertices[k] = reference(fresh - 1);
                } else
                    t.vertices[k] = fresh++;
            }
            left = t.vertices[1];
            right = t.vertices[0];
            head = t.vertices[2];
        } else {
            std::uint32_t vertex;
            if (ref) {
                need(fresh > 0, "Nanite strip reference has no vertex");
                vertex = reference(fresh - 1);
            } else
                vertex = fresh++;
            if (l) {
                t.vertices = {left, vertex, head};
                right = head;
            } else {
                t.vertices = {right, head, vertex};
                left = head;
            }
            head = vertex;
        }
        for (auto v : t.vertices)
            need(v < c.vertices, "Nanite triangle exceeds cluster vertex table");
        need(t.vertices[0] != t.vertices[1] && t.vertices[0] != t.vertices[2] &&
                 t.vertices[1] != t.vertices[2],
             "Degenerate Nanite strip triangle indices");
        const auto extrema = std::minmax_element(t.vertices.begin(), t.vertices.end());
        need(*extrema.second - *extrema.first <= 31, "Nanite triangle violates GPU index delta constraint");
        c.triangles.push_back(t);
    }
    need(fresh == c.vertices, "Nanite strip vertex count disagrees with cluster");
    const auto material = c.words[23];
    if (material < 0xfe000000u) {
        const unsigned n0 = ((material >> 18) & 127) + 1, n1 = (material >> 25) & 127;
        need(n0 <= count && n1 <= count - n0, "Nanite inline material lengths exceed triangle count");
        for (unsigned i = 0; i < count; ++i)
            c.triangles[i].material = i < n0        ? material & 63
                                      : i < n0 + n1 ? (material >> 6) & 63
                                                    : (material >> 12) & 63;
    } else {
        const auto at = std::uint64_t(material & 0x7ffffu) * 4;
        const auto rows = ((material >> 19) & 63) + 1;
        const auto raw_start = 32 + 32 * p.clusters.size();
        const auto material_end = p.header[5] - raw_start;
        need(at >= 16 + 112 * p.clusters.size() && at <= material_end && 4ull * rows <= material_end - at,
             "Nanite material table exceeds raw material window");
        unsigned next = 0;
        for (unsigned i = 0; i < rows; ++i) {
            control.check();
            const auto v = b.u32(raw_start + at + 4 * i);
            const auto first = v & 255, len = (v >> 8) & 255, mat = (v >> 16) & 63;
            need(first == next && len > 0 && len <= count - next,
                 "Nanite material ranges do not partition triangles");
            for (unsigned j = 0; j < len; ++j)
                c.triangles[next++].material = mat;
        }
        need(next == count, "Nanite material table leaves triangles uncovered");
    }
}
} // namespace
struct NaniteGeometry::Impl {
    bool attributes = false;
    std::vector<Page> pages;
    std::uint64_t vertices = 0, triangles = 0, clusters = 0, accounted = 0, leaf_triangles = 0;
};
NaniteGeometry::NaniteGeometry(std::shared_ptr<const Impl> i) : impl_(std::move(i)) {}
NaniteGeometry decode_nanite_geometry(const NaniteGeometrySource &s, const NaniteGeometryOptions &o) {
    o.control.check();
    if (o.profile != NaniteProfile::unreal_5_3_windows)
        throw Error("nanite-profile", "Select the exact UE 5.3 Windows Nanite profile");
    limit(!s.pages.empty() && s.pages.size() <= o.max_pages, "Nanite page count exceeds budget");
    need(s.root_pages > 0 && s.root_pages <= s.pages.size(), "Invalid Nanite root page count");
    Budget budget{o};
    budget.reserve(s.pages.size() * sizeof(Page));
    auto out = std::make_shared<NaniteGeometry::Impl>();
    out->attributes = o.attributes;
    out->pages.reserve(s.pages.size());
    for (std::size_t pi = 0; pi < s.pages.size(); ++pi) {
        o.control.checkpoint("decode-nanite-page", pi, s.pages.size());
        const auto &source = s.pages[pi];
        budget.add(budget.read, source.bytes.size(), o.max_read_bytes,
                   "Nanite source pages exceed read budget");
        limit(source.bytes.size() <= 262144 + 6152, "Nanite stored page exceeds profile limit");
        need((source.flags & ~1u) == 0, "Unsupported Nanite page flags");
        Bytes stored{source.bytes, 0, source.bytes.size()};
        need(stored.u16(0) == 0x464e, "Invalid Nanite fixup magic");
        const auto nc = stored.u16(2), hf = stored.u16(4), cf = stored.u16(6);
        need(nc > 0 && nc <= 256 && hf <= 256 && cf <= 256, "Invalid Nanite fixup counts");
        const std::uint64_t fixup = 8 + 16ull * hf + 8ull * cf;
        stored.range(0, fixup);
        Page page;
        page.disk_start = fixup;
        page.stored_bytes = source.bytes.size();
        page.root = pi < s.root_pages;
        page.relative = source.flags & 1;
        page.hierarchy_fixups = hf;
        page.cluster_fixups = cf;
        limit(source.dependencies.size() <= s.pages.size(), "Nanite page dependency count exceeds resource");
        budget.reserve(source.dependencies.size() * 128 + 512);
        page.dependencies = source.dependencies;
        std::set<std::uint32_t> unique;
        for (auto dependency : page.dependencies) {
            o.control.check();
            need(dependency < s.pages.size() && dependency != pi && unique.insert(dependency).second,
                 "Invalid or duplicate Nanite page dependency");
        }
        for (unsigned f = 0; f < unsigned(hf) + unsigned(cf); ++f) {
            const bool hierarchy = f < hf;
            const auto at = hierarchy ? 8 + std::uint64_t(f) * 16
                                      : 8 + std::uint64_t(hf) * 16 + std::uint64_t(f - hf) * 8;
            const auto target = stored.u32(at), dep = stored.u32(at + (hierarchy ? 12 : 4));
            need((hierarchy ? target : target >> 8) < s.pages.size(),
                 "Nanite fixup page reference exceeds resource");
            const auto first = dep >> 3, num = dep & 7;
            if (num)
                need(first <= s.pages.size() && num <= s.pages.size() - first,
                     "Nanite fixup dependency range exceeds resource");
        }
        Bytes b{source.bytes, fixup, source.bytes.size()};
        for (unsigned i = 0; i < 8; ++i)
            page.header[i] = b.u32(4 * i);
        const auto &h = page.header;
        const auto raw_start = 32 + 32ull * nc, raw_bytes = 16ull * h[2], raw_end = raw_start + raw_bytes;
        need(h[1] == nc && h[0] > 0 && h[0] <= (page.root ? 32768u : 131072u) && h[0] % 4 == 0 && h[3] <= 4 &&
                 h[4] <= std::uint32_t(nc) * 256,
             "Invalid Nanite disk page header");
        need(raw_bytes >= 16 + 112ull * nc && raw_bytes <= h[0], "Invalid Nanite raw GPU prefix size");
        b.range(raw_start, raw_bytes);
        need(b.u32(raw_start) == nc, "Nanite raw page header disagrees with disk header");
        need(h[5] >= raw_start + 16 + 112ull * nc && h[5] <= raw_end && 32ull * h[3] * nc == raw_end - h[5],
             "Nanite UV decode metadata does not cover raw prefix");
        need(h[6] >= raw_end && h[6] % 4 == 0, "Invalid Nanite strip-mask offset");
        b.range(h[6], 48ull * nc);
        need(h[7] >= h[6] + 48ull * nc && h[7] % 4 == 0, "Invalid Nanite vertex-mask offset");
        b.range(h[7], 32ull * nc);
        budget.add(budget.clusters, nc, o.max_clusters, "Nanite clusters exceed budget");
        budget.reserve(std::uint64_t(nc) * (sizeof(Cluster) + 256));
        page.clusters.resize(nc);
        std::uint64_t references = 0;
        for (unsigned ci = 0; ci < nc; ++ci) {
            o.control.check();
            auto &c = page.clusters[ci];
            for (unsigned i = 0; i < 8; ++i)
                c.disk[i] = b.u32(32 + 32ull * ci + 4 * i);
            for (unsigned row = 0; row < 7; ++row)
                for (unsigned i = 0; i < 4; ++i)
                    c.words[row * 4 + i] = b.u32(raw_start + 16 + 16ull * (row * nc + ci) + 4 * i);
            c.vertices = c.words[0] & 511;
            c.flags = c.words[19];
            const auto triangles = c.words[1] & 255;
            need(c.vertices > 0 && c.vertices <= 256 && triangles > 0 && triangles <= 128 &&
                     !(c.flags & ~15u),
                 "Invalid Nanite packed cluster counts or flags");
            c.precision = int((c.words[7] >> 4) & 31) - 8;
            unsigned position_bits = 0;
            for (unsigned k = 0; k < 3; ++k) {
                c.start[k] = signed32(c.words[4 + k]);
                c.position_bits[k] = (c.words[7] >> (9 + 5 * k)) & 31;
                need(c.position_bits[k] <= 21, "Unsupported Nanite position precision");
                position_bits += c.position_bits[k];
            }
            need(((c.words[21] >> 23) & 7) == h[3], "Nanite cluster UV count disagrees with page");
            const auto uv_offset = c.words[21] & 0x3fffff;
            need(uv_offset == h[5] - raw_start + 32ull * ci * h[3],
                 "Nanite GPU/disk UV decode offsets disagree");
            const auto position_offset = c.words[0] >> 9, index_offset = c.words[1] >> 8,
                       attr_offset = c.words[20] & 0x3fffff, attr_bits = c.words[20] >> 22,
                       index_bits = c.words[7] & 15;
            need(index_bits <= 8 && index_offset <= h[0] &&
                     ((std::uint64_t(triangles) * (index_bits + 10) + 7) / 8) <= h[0] - index_offset,
                 "Nanite GPU triangle window exceeds page");
            need(position_offset <= h[0] &&
                     (std::uint64_t(c.vertices) * position_bits + 7) / 8 <= h[0] - position_offset,
                 "Nanite GPU position window exceeds page");
            need(attr_offset <= h[0] && (std::uint64_t(c.vertices) * attr_bits + 7) / 8 <= h[0] - attr_offset,
                 "Nanite GPU attribute window exceeds page");
            unsigned actual_refs = 0;
            for (unsigned i = 0; i < 8; ++i) {
                c.mask[i] = b.u32(h[7] + 32ull * ci + 4 * i);
                const auto valid = c.vertices > 32 * i ? std::min(32u, c.vertices - 32 * i) : 0u;
                need(valid == 32 || (c.mask[i] >> valid) == 0,
                     "Nanite reference mask marks nonexistent vertices");
                actual_refs += pop(c.mask[i]);
            }
            need(c.disk[5] == actual_refs && actual_refs <= c.vertices,
                 "Nanite vertex reference count disagrees with mask");
            need(c.disk[2] == h[7] + 32ull * nc + references,
                 "Nanite split reference streams are not contiguous");
            references += actual_refs;
            budget.add(budget.vertices, c.vertices, o.max_vertices, "Nanite vertices exceed budget");
            budget.add(budget.triangles, triangles, o.max_triangles, "Nanite triangles exceed budget");
            budget.reserve(std::uint64_t(c.vertices) * 2 * sizeof(std::array<std::int32_t, 3>) +
                           std::uint64_t(triangles) * sizeof(NaniteTriangle));
            c.positions.resize(c.vertices);
            c.coded.reserve(c.vertices - actual_refs);
            if (o.attributes) {
                std::array<std::array<std::uint32_t, 8>, 4> ranges{};
                for (unsigned u = 0; u < h[3]; ++u)
                    for (unsigned k = 0; k < 8; ++k)
                        ranges[u][k] = b.u32(h[5] + 32ull * (ci * h[3] + u) + 4 * k);
                c.attribute_layout = detail::nanite_attribute_layout(c.words, ranges, h[3]);
                budget.reserve(std::uint64_t(c.vertices) * 2 * sizeof(detail::NaniteAttributeCode));
                c.attributes.resize(c.vertices);
                c.coded_attributes.reserve(c.vertices - actual_refs);
                c.attributes_decoded = true;
            }
            if (c.flags & 4)
                out->leaf_triangles += triangles;
        }
        std::uint64_t gpu_at = raw_bytes;
        for (const auto &c : page.clusters) {
            unsigned minimum = 0;
            while ((1u << minimum) < c.vertices)
                ++minimum;
            const auto index_bits = c.words[7] & 15u;
            need(index_bits == minimum && (c.words[1] >> 8) == gpu_at,
                 "Nanite GPU index layout disagrees with cluster");
            gpu_at += ((std::uint64_t(c.words[1] & 255) * (index_bits + 10) + 31) / 32) * 4;
        }
        for (const auto &c : page.clusters) {
            need((c.words[0] >> 9) == gpu_at, "Nanite GPU position layout is not contiguous");
            unsigned bits = 0;
            for (auto n : c.position_bits)
                bits += n;
            gpu_at += ((std::uint64_t(c.vertices) * bits + 31) / 32) * 4;
        }
        for (const auto &c : page.clusters) {
            need((c.words[20] & 0x3fffff) == gpu_at, "Nanite GPU attribute layout is not contiguous");
            gpu_at += ((std::uint64_t(c.vertices) * (c.words[20] >> 22) + 31) / 32) * 4;
        }
        need(gpu_at == h[0], "Nanite GPU section sizes disagree with page");
        need(references == h[4], "Nanite page reference count disagrees with clusters");
        const auto reference_end = (h[7] + 32ull * nc + 2 * references + 3) & ~3ull;
        for (unsigned ci = 0; ci < nc; ++ci) {
            auto &c = page.clusters[ci];
            const auto &d = c.disk;
            const auto map_end = ci + 1 < nc ? page.clusters[ci + 1].disk[1] : h[7];
            need(d[0] >= raw_end && d[0] <= h[6] && d[1] >= h[6] + 48ull * nc && d[1] % 4 == 0 &&
                     map_end >= d[1] && map_end <= h[7] && (map_end - d[1]) % 4 == 0,
                 "Nanite cluster map exceeds its own window");
            const auto position_end = ci + 1 < nc ? page.clusters[ci + 1].disk[3] : page.clusters[0].disk[4];
            need(d[3] >= reference_end && position_end >= d[3], "Nanite position streams are not ordered");
            b.range(d[3], position_end - d[3]);
            const auto attr_end = ci + 1 < nc ? page.clusters[ci + 1].disk[4] : b.end - b.start;
            need(d[4] >= page.clusters.back().disk[3] && attr_end >= d[4],
                 "Nanite attribute streams are not ordered");
            b.range(d[4], attr_end - d[4]);
            const auto coded = c.vertices - d[5], attribute_bytes = ((c.words[20] >> 22) + 7) / 8;
            need(((std::uint64_t(coded) * attribute_bytes + 3) & ~3ull) == attr_end - d[4],
                 "Nanite stored attributes exceed cluster window");
            unsigned sum = 0;
            for (auto bits : c.position_bits)
                sum += bits;
            const auto stride = (sum + 7) / 8;
            need(((std::uint64_t(coded) * stride + 3) & ~3ull) == position_end - d[3],
                 "Nanite coded positions exceed cluster window");
            for (unsigned vi = 0; vi < c.vertices; ++vi) {
                o.control.check();
                if ((c.mask[vi / 32] >> (vi % 32)) & 1)
                    continue;
                std::array<std::int32_t, 3> q{};
                auto bit = std::uint64_t(c.coded.size()) * stride * 8;
                for (unsigned k = 0; k < 3; ++k) {
                    const auto delta = b.bits(d[3], bit, c.position_bits[k], position_end);
                    bit += c.position_bits[k];
                    const auto value = std::int64_t(c.start[k]) + delta;
                    need(value >= INT32_MIN && value <= INT32_MAX,
                         "Nanite quantized position overflows native int32");
                    q[k] = static_cast<std::int32_t>(value);
                }
                c.positions[vi] = q;
                c.coded.push_back(q);
                if (o.attributes) {
                    detail::NaniteAttributeCode code;
                    const auto &layout = c.attribute_layout;
                    auto attribute_bit = std::uint64_t(c.coded_attributes.size()) * attribute_bytes * 8;
                    auto read_bits = [&](unsigned n) {
                        const auto value = b.bits(d[4], attribute_bit, n, attr_end);
                        attribute_bit += n;
                        return value;
                    };
                    code.normal = read_bits(2 * layout.normal);
                    code.tangent = read_bits(layout.has_tangent ? layout.tangent + 1 : 0);
                    std::uint32_t color_delta = 0;
                    for (unsigned k = 0; k < 4; ++k) {
                        const auto delta = read_bits(layout.color_bits[k]);
                        need(((layout.color_min >> (8 * k)) & 255) + delta <= 255,
                             "Nanite coded color overflows byte component");
                        color_delta |= delta << (8 * k);
                    }
                    code.color = layout.color_min + color_delta;
                    for (unsigned u = 0; u < layout.uv_count; ++u)
                        for (unsigned k = 0; k < 2; ++k)
                            code.uv[u][k] = read_bits(layout.uv_bits[u][k]);
                    c.coded_attributes.push_back(code);
                    c.attributes[vi] = detail::transcode_nanite_attribute(code, layout, layout);
                }
            }
            topology(b, page, c, ci, o.control);
        }
        page.hash = detail::hash_range(source.bytes, 0, source.bytes.size(), o.control);
        out->pages.push_back(std::move(page));
    }
    for (std::size_t pi = 0; pi < out->pages.size(); ++pi) {
        const auto &p = out->pages[pi];
        Bytes stored{s.pages[pi].bytes, 0, p.stored_bytes};
        for (unsigned i = 0; i < p.cluster_fixups; ++i) {
            o.control.check();
            const auto word = stored.u32(8 + 16ull * p.hierarchy_fixups + 8ull * i);
            need((word & 255u) < out->pages[word >> 8].clusters.size(),
                 "Nanite fixup cluster exceeds target page");
        }
        for (unsigned i = 0; i < p.hierarchy_fixups; ++i) {
            o.control.check();
            const auto at = 8 + 16ull * i;
            need(stored.u32(at + 8) < out->pages[stored.u32(at)].clusters.size(),
                 "Nanite hierarchy fixup group start exceeds target page");
        }
    }
    budget.reserve(out->pages.size() * 64);
    std::vector<std::vector<std::size_t>> dependents(out->pages.size());
    std::vector<std::size_t> remaining(out->pages.size()), ready;
    ready.reserve(out->pages.size());
    for (std::size_t pi = 0; pi < out->pages.size(); ++pi) {
        o.control.check();
        remaining[pi] = out->pages[pi].dependencies.size();
        for (auto parent : out->pages[pi].dependencies)
            dependents[parent].push_back(pi);
        if (!remaining[pi])
            ready.push_back(pi);
    }
    for (std::size_t at = 0; at < ready.size(); ++at) {
        o.control.check();
        const auto pi = ready[at];
        auto &p = out->pages[pi];
        Bytes b{s.pages[pi].bytes, p.disk_start, p.stored_bytes};
        for (std::size_t ci = 0; ci < p.clusters.size(); ++ci) {
            auto &c = p.clusters[ci];
            const auto &d = c.disk;
            const auto map_end = ci + 1 < p.clusters.size() ? p.clusters[ci + 1].disk[1] : p.header[7];
            unsigned ri = 0;
            for (unsigned vi = 0; vi < c.vertices; ++vi) {
                o.control.check();
                if (!((c.mask[vi / 32] >> (vi % 32)) & 1))
                    continue;
                const auto pair_index = b.u8(d[2] + ri);
                need(4ull * pair_index + 4 <= map_end - d[1], "Nanite reference exceeds cluster page map");
                const auto pair = b.u32(d[1] + 4ull * pair_index), parent = pair >> 8, sc = pair & 255;
                need(parent <= p.dependencies.size() && (!parent || p.relative),
                     "Nanite parent reference lacks declared relative dependency");
                const auto source_page = parent ? p.dependencies[parent - 1] : pi;
                const auto &sp = out->pages[source_page];
                need(sc < sp.clusters.size(), "Nanite reference cluster exceeds source page");
                const auto &source = sp.clusters[sc];
                const auto source_vertex = b.u8(d[2] + ri + p.header[4]);
                need(source.precision == c.precision,
                     "Nanite referenced positions use incompatible quantization");
                const auto &values = parent ? source.positions : source.coded;
                need(source_vertex < values.size(), "Nanite reference vertex exceeds source cluster");
                const auto &q = values[source_vertex];
                for (unsigned k = 0; k < 3; ++k) {
                    const auto delta = std::int64_t(q[k]) - c.start[k];
                    need(delta >= 0 && std::uint64_t(delta) < (1ull << c.position_bits[k]),
                         "Nanite referenced position exceeds destination precision");
                }
                c.positions[vi] = q;
                if (o.attributes) {
                    const auto &codes = parent ? source.attributes : source.coded_attributes;
                    need(source_vertex < codes.size(), "Nanite attribute reference exceeds source vertices");
                    c.attributes[vi] = detail::transcode_nanite_attribute(
                        codes[source_vertex], source.attribute_layout, c.attribute_layout);
                }
                ++ri;
            }
        }
        for (auto child : dependents[pi])
            if (--remaining[child] == 0)
                ready.push_back(child);
    }
    need(ready.size() == out->pages.size(), "Cyclic Nanite page dependencies");
    for (auto &p : out->pages)
        for (auto &c : p.clusters) {
            std::vector<std::array<std::int32_t, 3>>().swap(c.coded);
            std::vector<detail::NaniteAttributeCode>().swap(c.coded_attributes);
        }
    out->vertices = budget.vertices;
    out->triangles = budget.triangles;
    out->clusters = budget.clusters;
    out->accounted = budget.output;
    o.control.checkpoint("decode-nanite-page", s.pages.size(), s.pages.size());
    return NaniteGeometry(std::move(out));
}
namespace {
const Cluster &selected(const NaniteGeometry::Impl &i, std::size_t page, std::size_t cluster) {
    need(page < i.pages.size() && cluster < i.pages[page].clusters.size(),
         "Nanite cluster selection exceeds model");
    return i.pages[page].clusters[cluster];
}
} // namespace
bool NaniteGeometry::attributes_decoded() const {
    return impl_->attributes;
}
NaniteVertexAttributes NaniteGeometry::attributes(std::size_t p, std::size_t c, std::size_t v,
                                                  const OperationControl &control) const {
    control.check();
    const auto &cluster = selected(*impl_, p, c);
    need(v < cluster.vertices, "Nanite attribute vertex selection exceeds cluster");
    if (!cluster.attributes_decoded)
        throw Error("nanite-attributes-unavailable", "Nanite attributes were not requested");
    return detail::decode_nanite_attribute(cluster.attributes[v], cluster.attribute_layout);
}
std::uint64_t NaniteGeometry::accounted_output_bytes() const {
    return impl_->accounted;
}
std::size_t NaniteGeometry::page_count() const {
    return impl_->pages.size();
}
std::size_t NaniteGeometry::cluster_count(std::size_t page) const {
    need(page < impl_->pages.size(), "Nanite page selection exceeds model");
    return impl_->pages[page].clusters.size();
}
Json NaniteGeometry::cluster_metadata(std::size_t p, std::size_t c, const OperationControl &control) const {
    control.check();
    return metadata(selected(*impl_, p, c), c);
}
std::array<std::int32_t, 3> NaniteGeometry::quantized_position(std::size_t p, std::size_t c, std::size_t v,
                                                               const OperationControl &control) const {
    control.check();
    const auto &cluster = selected(*impl_, p, c);
    need(v < cluster.positions.size(), "Nanite vertex selection exceeds cluster");
    return cluster.positions[v];
}
std::array<float, 3> NaniteGeometry::position(std::size_t p, std::size_t c, std::size_t v,
                                              const OperationControl &control) const {
    const auto q = quantized_position(p, c, v, control);
    const auto &cluster = selected(*impl_, p, c);
    std::array<float, 3> out{};
    for (unsigned k = 0; k < 3; ++k)
        out[k] = std::ldexp(static_cast<float>(q[k]), -cluster.precision);
    return out;
}
NaniteTriangle NaniteGeometry::triangle(std::size_t p, std::size_t c, std::size_t t,
                                        const OperationControl &control) const {
    control.check();
    const auto &cluster = selected(*impl_, p, c);
    need(t < cluster.triangles.size(), "Nanite triangle selection exceeds cluster");
    return cluster.triangles[t];
}
Json NaniteGeometry::catalog(const OperationControl &control) const {
    Json out = {{"schema", "cyuediff.nanite-geometry/1"},
                {"profile", "ue5.3-windows"},
                {"scope", "stored cluster positions, oriented triangles and materials; no runtime LOD cut"},
                {"vertices", impl_->vertices},
                {"triangles", impl_->triangles},
                {"clusters", impl_->clusters},
                {"fullLeafTriangles", impl_->leaf_triangles},
                {"accountedOutputBytes", impl_->accounted},
                {"storedAttributesDecoded", impl_->attributes},
                {"pages", Json::array()}};
    for (std::size_t pi = 0; pi < impl_->pages.size(); ++pi) {
        control.check();
        const auto &p = impl_->pages[pi];
        Json row = {{"index", pi},
                    {"root", p.root},
                    {"relativeEncoding", p.relative},
                    {"sha256", p.hash},
                    {"storedBytes", p.stored_bytes},
                    {"gpuBytes", p.header[0]},
                    {"dependencies", p.dependencies},
                    {"hierarchyFixups", p.hierarchy_fixups},
                    {"clusterFixups", p.cluster_fixups},
                    {"clusters", Json::array()}};
        for (std::size_t ci = 0; ci < p.clusters.size(); ++ci) {
            control.check();
            row["clusters"].push_back(metadata(p.clusters[ci], ci));
        }
        out["pages"].push_back(std::move(row));
    }
    return out;
}
} // namespace cyuediff
