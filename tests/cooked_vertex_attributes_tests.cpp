#include <cyuediff/cooked_skeletal.hpp>
#include "cooked_vertex_attributes_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main(int argc, char **argv) {
    unsigned checks = 0;
    const auto check = [&](bool v, const char *m) {
        ++checks;
        if (!v)
            throw std::runtime_error(m);
    };
    const auto refused = [&](auto action, const std::string &code = std::string()) {
        try {
            action();
        } catch (const Error &e) {
            check(code.empty() || e.code == code, (e.code + ": " + e.what()).c_str());
            return;
        }
        throw std::runtime_error("Invalid custom attribute accepted");
    };
    const auto root = fs::temp_directory_path() /
                      ("cyuediff-attributes-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root);
        auto z = skeletal_context();
        CookedSkeletalOptions o;
        o.profile = CookedSkeletalProfile::unreal_5_3_windows;
        o.keep_mobile_min_lod_setting = false;
        o.has_vertex_colors = false;
        const auto read = [&](AttributeFixture f, const CookedSkeletalOptions &opts) {
            texture_container(root / "mesh", f.base.storage);
            return read_zen_skeletal_mesh(Container::open(root / "mesh.utoc"), 0, z, {}, opts);
        };
        auto base = attribute_spec();
        auto changed = base;
        float v = .5f;
        std::memcpy(changed.bytes.data(), &v, 4);
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            auto out = fs::u8path(argv[2]);
            fs::create_directories(out);
            for (const auto &pair : std::vector<std::pair<std::string, AttributeFixture>>{
                     {"attributes", cooked_attributes_fixture({base})},
                     {"attributes-changed", cooked_attributes_fixture({changed})},
                     {"attributes-streamed", cooked_attributes_fixture({base}, true)},
                     {"attributes-empty", cooked_attributes_fixture({})}})
                texture_container(out / pair.first, pair.second.base.storage);
            auto renamed = cooked_attributes_fixture({changed});
            const std::string old_name = "/Game/Texture", new_name = "/Game/Altered";
            auto &bytes = renamed.base.storage.package.data;
            const auto found = std::search(bytes.begin(), bytes.end(), old_name.begin(), old_name.end());
            check(found != bytes.end(), "Original package identity found");
            std::copy(new_name.begin(), new_name.end(), found);
            texture_container(out / "attributes-renamed-changed", renamed.base.storage);
            fs::remove_all(root);
            return 0;
        }
        for (auto pixel : {13u, 21u, 71u})
            for (auto components : {1u, 2u, 3u, 4u}) {
                auto spec = attribute_spec(pixel, components);
                auto f = cooked_attributes_fixture({spec});
                auto mesh = read(f, o);
                const auto &channel = *mesh.lods[0].vertex_attributes->channels.at("Texture");
                check(channel.vertex_count() == 3 && channel.components() == components &&
                          channel.encoded() == spec.bytes,
                      "Exact native width/component bytes");
                check(mesh.lods[0].geometry->vertex_attributes == mesh.lods[0].vertex_attributes,
                      "Shared immutable typed attribute model");
                check(channel.vertex(0).size() == components &&
                          channel.to_json()["sha256"] == sha256_bytes(spec.bytes),
                      "Focused decoded vector and exact payload SHA");
                if (pixel == 13)
                    check(channel.vertex(0)[0] == -1.f, "Original float32 attribute");
                if (pixel == 21)
                    check(channel.vertex(0)[0] == .5f, "Original half attribute");
                if (pixel == 71)
                    check(channel.vertex(0)[0] == 0, "Original UNorm attribute");
                auto meta = o;
                meta.geometry = false;
                auto m = read(f, meta);
                check(!m.lods[0].geometry &&
                          m.lods[0].vertex_attributes->channels.at("Texture")->vertex(2) == channel.vertex(2),
                      "Metadata-only typed values");
            }
        auto a = read(cooked_attributes_fixture({base}), o),
             b = read(cooked_attributes_fixture({changed}), o);
        auto diff = compare_meshes(*a.lods[0].geometry, *b.lods[0].geometry);
        check(diff.complete && diff.changes.size() == 1 &&
                  diff.changes[0]["path"] == "/vertexAttributes/channels/Texture/vertices/0/0",
              "Named exact decoded scalar difference");
        auto empty = read(cooked_attributes_fixture({}), o);
        check(!compare_meshes(*a.lods[0].geometry, *empty.lods[0].geometry).changes.empty(),
              "Known empty channels prove removals");
        auto unknown = *a.lods[0].geometry;
        unknown.vertex_attributes.reset();
        auto unavailable = compare_meshes(*a.lods[0].geometry, unknown);
        check(!unavailable.complete && unavailable.changes.empty(),
              "Unknown attribute coverage is not removed data");
        for (auto pixel : {21u, 71u}) {
            auto x = read(cooked_attributes_fixture({attribute_spec(13, 2, true)}), o);
            auto y = read(cooked_attributes_fixture({attribute_spec(pixel, 2, true)}), o);
            check(compare_meshes(*x.lods[0].geometry, *y.lods[0].geometry).changes.empty(),
                  "Exact decoded identity across storage formats");
        }
        auto second = attribute_spec(21, 2);
        second.name_number = 1;
        second.streamed_name = "Texture_0";
        auto third = attribute_spec(71, 4);
        third.name_number = 2;
        third.streamed_name = "Texture_1";
        const std::vector<AttributeSpec> specs = {base, second, third};
        for (bool utf16 : {false, true}) {
            auto f = cooked_attributes_fixture(specs, true, utf16);
            auto m = read(f, o);
            check(!m.lods[0].inlined && m.lods[0].vertex_attributes->channels.size() == 3,
                  "Streamed named attribute inventory");
            check(m.lods[0].vertex_attributes->channels.at("Texture_1")->vertex(2).size() == 4,
                  "Streamed multi-component focused values");
            check(m.lods[0].buffers["serializedBytes"].get<std::uint64_t>() ==
                      m.lods[0].buffers["declaredBuffersSize"].get<std::uint64_t>() +
                          m.lods[0].buffers["attributeNameBytes"].get<std::uint64_t>(),
                  "Native counted size excludes only actual name bytes");
            check(m.lods[1].vertex_attributes && m.lods[1].vertex_attributes->channels.empty(),
                  "Independent known empty inline LOD");
        }
        const auto original = base.bytes;
        auto bytes = original;
        MeshVertexAttribute owned("Texture", VertexAttributeFormat::float32, 1, bytes);
        bytes[0] ^= 1;
        check(owned.encoded() == original, "Caller storage is owned independently");
        VertexAttributeOptions opts;
        opts.max_vertices = 2;
        refused([&] { MeshVertexAttribute x("A", VertexAttributeFormat::float32, 1, original, opts); },
                "mesh-attribute-limit");
        opts = {};
        opts.max_output_bytes = 2;
        refused([&] { MeshVertexAttribute x("A", VertexAttributeFormat::float32, 1, original, opts); },
                "mesh-attribute-limit");
        refused([&] { MeshVertexAttribute x("", VertexAttributeFormat::float32, 1, original); },
                "mesh-attribute-layout");
        refused([&] { MeshVertexAttribute x("A", VertexAttributeFormat::float32, 0, original); },
                "mesh-attribute-layout");
        refused([&] { MeshVertexAttribute x("A", static_cast<VertexAttributeFormat>(99), 1, original); },
                "mesh-attribute-format");
        refused([&] { owned.vertex(3); }, "mesh-attribute-selection");
        for (unsigned length = 1; length < original.size(); ++length)
            if (length % 4) {
                std::vector<std::uint8_t> short_bytes(original.begin(), original.begin() + length);
                refused([&] { MeshVertexAttribute x("A", VertexAttributeFormat::float32, 1, short_bytes); },
                        "mesh-attribute-layout");
            }
        auto f = cooked_attributes_fixture({base});
        for (auto mutation : std::vector<std::pair<unsigned, std::uint32_t>>{
                 {0, 0}, {0, 5}, {4, 37}, {8, 2}, {12, 1}, {16, 2}, {20, UINT32_MAX}}) {
            auto bad = f;
            bad.base.storage.package.patch(static_cast<int>(bad.headers[0] + mutation.first),
                                           mutation.second);
            refused([&] { read(bad, o); });
        }
        auto nan = f;
        nan.base.storage.package.patch(static_cast<int>(nan.data[0]), 0x7fc00001);
        refused([&] { read(nan, o); }, "mesh-attribute-layout");
        auto half = cooked_attributes_fixture({attribute_spec(21)});
        half.base.storage.package.data[half.data[0]] = 0;
        half.base.storage.package.data[half.data[0] + 1] = 0x7c;
        refused([&] { read(half, o); }, "mesh-attribute-layout");
        refused([&] { read(cooked_attributes_fixture({base, base}), o); }, "cooked-skeletal-layout");
        auto limits = o;
        limits.max_vertex_attributes = 0;
        refused([&] { read(f, limits); }, "cooked-skeletal-limit");
        limits = o;
        limits.max_attribute_values = 2;
        refused([&] { read(f, limits); }, "cooked-skeletal-limit");
        limits = o;
        limits.max_attribute_name_bytes = 2;
        refused([&] { read(f, limits); }, "cooked-skeletal-limit");
        limits = o;
        limits.control.cancelled = [] { return true; };
        refused([&] { read(f, limits); }, "cancelled");
        OperationControl cancel;
        cancel.cancelled = [] { return true; };
        refused([&] { owned.vertex(0, cancel); }, "cancelled");
        for (std::size_t end = f.base.native; end < a.tail_offset; ++end) {
            auto truncated = f;
            truncated.base.storage.package.data.resize(end);
            skeletal_export_size(truncated.base);
            refused([&] { read(truncated, o); });
        }
        fs::remove_all(root);
        std::cout << checks << " native vertex attribute checks\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << checks << ": " << e.what() << '\n';
        return 1;
    }
}
