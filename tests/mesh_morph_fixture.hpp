#pragma once
#include "mesh_skin_fixture.hpp"
namespace cyuediff::test::morph_fixture {
struct Fixture {
    skin_fixture::Fixture source;
    std::size_t position = 0, normal = 0, layout = 0, name = 0, normal_name = 0, inactive = 0, blink_name = 0;
};
inline Fixture fixture(bool changed = false, bool normals = true) {
    auto base = mesh_fixture::fixture();
    Fixture out;
    auto append = [](mesh_fixture::Bytes &b, const char *name, unsigned elements,
                     const std::vector<float> &values) {
        b.string(name);
        b.i(1);
        b.i(1);
        b.i(elements);
        b.i(1);
        b.i(1);
        b.i(12);
        b.i(elements);
        const auto start = b.data.size();
        for (auto v : values)
            b.f(v);
        b.f(0);
        b.f(0);
        b.f(0);
        b.i(0);
        return start;
    };
    mesh_fixture::Bytes positions, normal_data;
    out.name = 4; // First attribute's character data.
    out.position = append(positions, "Morph-Smile", 5,
                          {changed ? 2.f : 1.f, 0, 0, 9, 9, 9, 0, -.5f, 0, 9, 9, 9, 0, 0, 0});
    out.layout = 4 + std::string("Morph-Smile").size() + 1;
    out.inactive = out.position + 12;
    out.blink_name = positions.data.size() + 4;
    append(positions, "Morph-Blink", 5, std::vector<float>(15));
    if (normals) {
        out.normal_name = 4;
        out.normal = append(normal_data, "Morph-Smile", 3, {0, 0, 0, 0, changed ? .5f : .25f, 0, 0, 0, 0});
    }
    base.bytes.patch<int>(base.vertex_attributes, 3);
    if (normals)
        base.bytes.patch<int>(base.instance_attributes, 4);
    base.bytes.data.insert(base.bytes.data.begin() + base.after_instances, normal_data.data.begin(),
                           normal_data.data.end());
    base.bytes.data.insert(base.bytes.data.begin() + base.after_vertices, positions.data.begin(),
                           positions.data.end());
    out.blink_name += base.after_vertices;
    out.position += base.after_vertices;
    out.layout += base.after_vertices;
    out.name += base.after_vertices;
    out.inactive += base.after_vertices;
    out.normal += base.after_instances + positions.data.size();
    out.normal_name += base.after_instances + positions.data.size();
    out.source.bytes = std::move(base.bytes);
    return out;
}
inline fs::path package(const fs::path &root, const std::string &name, bool changed = false,
                        bool virtualized = false) {
    const auto f = fixture(changed);
    return skin_fixture::package(root, name, false, virtualized, &f.source);
}
} // namespace cyuediff::test::morph_fixture
