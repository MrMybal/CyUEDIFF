#pragma once
#include "cooked_skeletal_fixture.hpp"
namespace cyuediff::test {
inline SkeletalFixture cooked_overlaps_fixture(bool empty = false) {
    auto f = cooked_skeletal_fixture();
    auto &b = f.storage.package;
    const auto at = f.section_bones + 34;
    Bytes table;
    table.i(empty ? 1 : 2);
    if (empty)
        table.i(0);
    else {
        table.i(1);
        table.i(0);
    }
    table.i(3);
    for (unsigned i = 0; i < 3; ++i) {
        table.i(empty || i == 2 ? 0 : 1);
        table.i(empty ? 0 : std::min(i, 2u));
    }
    b.data[f.section + 1] = 0;
    b.data.insert(b.data.begin() + at, table.data.begin(), table.data.end());
    for (auto *v :
         {&f.availability, &f.bulk_index, &f.flags,        &f.second_lod,    &f.native,     &f.bone_parent,
          &f.pose,         &f.map,        &f.section,      &f.section_bones, &f.positions,  &f.indices,
          &f.weights,      &f.lookup,     &f.profile,      &f.morph,         &f.attributes, &f.size,
          &f.colors,       &f.streaming,  &f.inline_count, &f.buffer_start,  &f.weight_meta})
        if (*v >= at)
            *v += table.data.size();
    skeletal_export_size(f);
    return f;
}
} // namespace cyuediff::test
