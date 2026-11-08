#pragma once
#include "cooked_skeletal_fixture.hpp"
#include <cyuediff/cloth.hpp>
namespace cyuediff::test {
inline void cloth_record(Bytes &b, const ClothMapping &m) {
    for (const auto *v : {&m.position, &m.normal, &m.tangent})
        for (auto f : *v)
            b.put(f);
    for (auto i : m.source_indices)
        b.put(i);
    b.put(m.weight);
    b.put(m.padding);
}
struct ClothFixture {
    SkeletalFixture base;
    std::vector<std::vector<ClothMapping>> biases;
    std::size_t section_records = 0, buffer = 0, index_mapping = 0;
};
inline ClothFixture cooked_cloth_fixture(unsigned influences = 1, unsigned biases = 1,
                                         bool inactive = false) {
    ClothFixture f;
    f.base = cooked_skeletal_fixture();
    for (unsigned bi = 0; bi < biases; ++bi) {
        std::vector<ClothMapping> records;
        for (unsigned vi = 0; vi < 3; ++vi)
            for (unsigned k = 0; k < influences; ++k) {
                ClothMapping m;
                m.position = {.25f, .5f, .25f, float(bi) * .125f};
                m.normal = {.5f, .25f, .25f, 1.f};
                m.tangent = {.125f, .625f, .25f, -.5f};
                m.source_indices = {0, 1, 2, static_cast<std::uint16_t>(inactive ? 65535 : 100 * vi)};
                m.weight = 1.f / std::max(1u, influences);
                m.padding = 0x12340000 + k;
                records.push_back(m);
            }
        f.biases.push_back(std::move(records));
    }
    auto &b = f.base.storage.package;
    const auto shift = [&](std::size_t at, std::ptrdiff_t n) {
        for (auto *p : {&f.base.availability, &f.base.bulk_index,    &f.base.flags,      &f.base.second_lod,
                        &f.base.native,       &f.base.bone_parent,   &f.base.pose,       &f.base.map,
                        &f.base.section,      &f.base.section_bones, &f.base.positions,  &f.base.indices,
                        &f.base.weights,      &f.base.lookup,        &f.base.profile,    &f.base.morph,
                        &f.base.attributes,   &f.base.size,          &f.base.colors,     &f.base.streaming,
                        &f.base.inline_count, &f.base.buffer_start,  &f.base.weight_meta})
            if (*p >= at)
                *p = std::size_t(std::ptrdiff_t(*p) + n);
    };
    const auto section_count = f.base.section + 29;
    Bytes section;
    section.i(static_cast<int>(biases));
    for (const auto &bias : f.biases) {
        section.i(static_cast<int>(bias.size()));
        for (const auto &m : bias)
            cloth_record(section, m);
    }
    b.data.erase(b.data.begin() + section_count, b.data.begin() + section_count + 8);
    b.data.insert(b.data.begin() + section_count, section.data.begin(), section.data.end());
    shift(section_count + 8, std::ptrdiff_t(section.data.size()) - 8);
    f.section_records = section_count + 8;
    const auto binding = f.base.section_bones + 12;
    b.data[binding] = 0;
    b.data[binding + 1] = 0;
    for (unsigned i = 0; i < 4; ++i)
        b.patch(static_cast<int>(binding + 2 + i * 4), int(i + 1));
    b.patch(static_cast<int>(binding + 18), 0);
    Bytes buffer;
    if (biases && influences) {
        buffer.put<std::uint8_t>(1);
        buffer.put<std::uint8_t>(0);
        buffer.i(64);
        buffer.i(static_cast<int>(3 * influences * biases));
        for (const auto &bias : f.biases)
            for (const auto &m : bias)
                cloth_record(buffer, m);
        buffer.i(1);
        buffer.i(0);
        buffer.i(0);
        buffer.i(static_cast<int>(3 * influences));
        f.buffer = f.base.profile;
        f.index_mapping = f.buffer + buffer.data.size() - 12;
        b.data.insert(b.data.begin() + f.base.profile, buffer.data.begin(), buffer.data.end());
        shift(f.base.profile, buffer.data.size());
        std::uint32_t size;
        std::memcpy(&size, b.data.data() + f.base.size, 4);
        b.patch(static_cast<int>(f.base.size), static_cast<int>(size + buffer.data.size()));
    }
    skeletal_export_size(f.base);
    return f;
}
} // namespace cyuediff::test
