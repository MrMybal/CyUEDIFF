#pragma once
#include "schema_collection_fixture.hpp"
#include <array>
namespace cyuediff::test {
struct MorphFixtureDelta {
    std::uint32_t vertex = 0;
    std::array<std::int32_t, 3> position{}, normal{};
};
inline Bytes compressed_morph_fixture(const std::vector<std::vector<MorphFixtureDelta>> &targets,
                                      float pp = .03125f, float np = 1.f / 2048) {
    std::vector<std::array<std::uint32_t, 10>> headers;
    std::vector<std::uint32_t> stream;
    std::vector<std::uint32_t> starts, counts;
    auto width = [](std::uint32_t n) {
        unsigned result = 0;
        while (n) {
            ++result;
            n >>= 1;
        }
        return result;
    };
    for (const auto &t : targets) {
        starts.push_back(static_cast<unsigned>(headers.size()));
        for (std::size_t first = 0; first < t.size(); first += 64) {
            const unsigned n = static_cast<unsigned>(std::min<std::size_t>(64, t.size() - first));
            auto imin = UINT32_MAX, imax = 0u;
            auto pmin = t[first].position, pmax = pmin, nmin = t[first].normal, nmax = nmin;
            bool normal = false;
            for (unsigned j = 0; j < n; ++j) {
                const auto &v = t[first + j];
                auto index = v.vertex - j;
                imin = std::min(imin, index);
                imax = std::max(imax, index);
                for (unsigned k = 0; k < 3; ++k) {
                    pmin[k] = std::min(pmin[k], v.position[k]);
                    pmax[k] = std::max(pmax[k], v.position[k]);
                    nmin[k] = std::min(nmin[k], v.normal[k]);
                    nmax[k] = std::max(nmax[k], v.normal[k]);
                    normal = normal || v.normal[k] != 0;
                }
            }
            std::array<unsigned, 3> pb{}, nb{};
            auto ib = width(imax - imin);
            std::array<std::uint32_t, 10> h{};
            h[0] = static_cast<unsigned>(stream.size() * 4);
            h[1] = ib | (normal ? 1u << 20 : 0) | (n << 21);
            h[2] = imin;
            for (unsigned k = 0; k < 3; ++k) {
                pb[k] = width(static_cast<std::uint32_t>(std::int64_t(pmax[k]) - pmin[k]));
                nb[k] = width(static_cast<std::uint32_t>(std::int64_t(nmax[k]) - nmin[k]));
                h[1] |= pb[k] << (5 + 5 * k);
                h[3 + k] = static_cast<std::uint32_t>(pmin[k]);
                h[6] |= nb[k] << (5 * k);
                h[7 + k] = static_cast<std::uint32_t>(nmin[k]);
            }
            std::uint64_t pending = 0;
            unsigned used = 0;
            auto put = [&](std::uint32_t value, unsigned bits) {
                pending |= std::uint64_t(value) << used;
                used += bits;
                while (used >= 32) {
                    stream.push_back(static_cast<std::uint32_t>(pending));
                    pending >>= 32;
                    used -= 32;
                }
            };
            for (unsigned j = 0; j < n; ++j) {
                const auto &v = t[first + j];
                put(v.vertex - j - imin, ib);
                for (unsigned k = 0; k < 3; ++k)
                    put(static_cast<std::uint32_t>(std::int64_t(v.position[k]) - pmin[k]), pb[k]);
                if (normal)
                    for (unsigned k = 0; k < 3; ++k)
                        put(static_cast<std::uint32_t>(std::int64_t(v.normal[k]) - nmin[k]), nb[k]);
            }
            if (used)
                stream.push_back(static_cast<std::uint32_t>(pending));
            headers.push_back(h);
        }
        counts.push_back(static_cast<unsigned>(headers.size()) - starts.back());
    }
    Bytes b;
    const auto nh = static_cast<unsigned>(headers.size());
    b.i(nh ? static_cast<int>(nh * 10 + stream.size() + 3) : 0);
    for (auto h : headers) {
        h[0] += nh * 40;
        for (auto word : h)
            b.put(word);
    }
    for (auto v : stream)
        b.put(v);
    if (nh)
        b.zeros(12);
    b.i(static_cast<int>(targets.size()));
    for (const auto &t : targets) {
        std::array<float, 4> v{};
        if (!t.empty()) {
            for (unsigned k = 0; k < 3; ++k) {
                v[k] = float(t[0].position[k]) * pp;
                for (const auto &d : t)
                    v[k] = std::min(v[k], float(d.position[k]) * pp);
            }
            for (const auto &d : t)
                for (auto n : d.normal)
                    v[3] = std::min(v[3], float(n) * np);
        }
        for (auto n : v)
            b.put(n);
    }
    b.i(static_cast<int>(targets.size()));
    for (const auto &t : targets) {
        std::array<float, 4> v{};
        if (!t.empty()) {
            for (unsigned k = 0; k < 3; ++k) {
                v[k] = float(t[0].position[k]) * pp;
                for (const auto &d : t)
                    v[k] = std::max(v[k], float(d.position[k]) * pp);
            }
            for (const auto &d : t)
                for (auto n : d.normal)
                    v[3] = std::max(v[3], float(n) * np);
        }
        for (auto n : v)
            b.put(n);
    }
    b.i(static_cast<int>(starts.size()));
    for (auto n : starts)
        b.put(n);
    b.i(static_cast<int>(counts.size()));
    for (auto n : counts)
        b.put(n);
    b.put(nh);
    b.put(pp);
    b.put(np);
    return b;
}
} // namespace cyuediff::test
