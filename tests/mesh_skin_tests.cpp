#include <cyuediff/mesh_skin.hpp>
#include "mesh_skin_fixture.hpp"
#include "../src/mesh.hpp"
#include <chrono>
#include <iostream>
#include <limits>
using namespace cyuediff;
namespace sf = cyuediff::test::skin_fixture;
namespace {
int checks = 0;
void check(bool b, const char *message) {
    ++checks;
    if (!b)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f, const char *code = nullptr) {
    try {
        f();
    } catch (const Error &e) {
        check(!code || e.code == code, e.what());
        return;
    }
    throw std::runtime_error("Expected rejected skin data");
}
} // namespace
int main(int argc, char **argv) {
    const auto root =
        fs::current_path() / (".test-tmp-mesh-skin-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            const auto out = fs::u8path(argv[2]);
            sf::package(out, "skin-before");
            sf::package(out, "skin-after", true);
            sf::package(out, "skin-virtualized", false, true);
            auto bad = sf::fixture();
            bad.bytes.patch<std::uint32_t>(bad.weight, 7u << 16 | 32768);
            bad.bytes.save(out / "invalid-skin.bin");
            sf::package(out, "skin-invalid", false, false, &bad);
            return 0;
        }
        fs::create_directories(root);
        auto source = sf::profile();
        MeshDecodeOptions options;
        auto decode = [&](const sf::Fixture &f) {
            return detail::decode_mesh_description(f.bytes.data, source, options);
        };
        const auto bytes = sf::fixture();
        const auto mesh = decode(bytes);
        check(mesh.skin && mesh.skin->bones.size() == 2, "Sparse source bone table");
        check(mesh.skin->bones[1].id == 2 && mesh.skin->bones[1].parent == 0, "Source bone IDs retained");
        check(mesh.skin->bones[1].translation[0] == 10, "Stored double source pose");
        check(mesh.skin->profiles.size() == 2 && mesh.skin->profiles.at("Default").size() == 3,
              "Only live vertex weights projected");
        check(mesh.skin->profiles.at("Default").at(0)[1].raw_weight == 32767, "Exact uint16 weights");
        const auto skin = mesh.skin->to_json();
        check(skin["bones"]["arm"]["parent"] == "root", "Named parent projection");
        check(skin["vertices"]["0"]["Default"]["arm"] == 32767, "Named skin profile projection");
        check(!skin["vertices"].contains("1"), "Inactive vertex slots excluded");
        const auto changed = decode(sf::fixture(true));
        const auto comparison = compare_meshes(mesh, changed).to_json();
        bool named = false;
        for (const auto &change : comparison["changes"])
            if (change["path"] == "/skin/vertices/0/Default/root")
                named = true;
        check(named, "Weight change has a named semantic path");
        check(compare_meshes(mesh, mesh).changes.empty(), "Source skin self comparison");
        auto bad = bytes;
        bad.bytes.patch<std::uint32_t>(bad.weight, (1u << 16) | 32768);
        rejects([&] { decode(bad); }, "mesh-skin-reference");
        bad = bytes;
        bad.bytes.patch<std::uint32_t>(bad.weight + 4, 32767);
        rejects([&] { decode(bad); }, "mesh-skin-identity");
        bad = bytes;
        bad.bytes.patch<std::int32_t>(bad.parent, 2);
        rejects([&] { decode(bad); }, "mesh-skin-hierarchy");
        bad = bytes;
        bad.bytes.patch<std::int32_t>(bad.parent + 8, 1);
        rejects([&] { decode(bad); }, "mesh-skin-reference");
        bad = bytes;
        bad.bytes.patch<double>(bad.pose, std::numeric_limits<double>::quiet_NaN());
        rejects([&] { decode(bad); }, "mesh-number");
        bad = bytes;
        bad.bytes.patch<double>(bad.pose + 24, 0);
        rejects([&] { decode(bad); }, "mesh-skin-rotation");
        bad = bytes;
        bad.bytes.patch<std::int32_t>(bad.ranges, 3);
        rejects([&] { decode(bad); }, "mesh-attribute");
        source = sf::profile(false);
        check(decode(sf::fixture(false, false)).skin->bones[1].translation[0] == 10,
              "Float source pose wire layout");
        source = sf::profile();
        for (int release = 44; release <= 51; ++release) {
            source.custom_versions["9c54d522a8264fbe9421074661b482d0"] = release;
            check(decode(bytes).skin->to_json() == skin,
                  "Known UE5 release profiles retain the same source skin wire layout");
        }
        source.custom_versions["9c54d522a8264fbe9421074661b482d0"] = 52;
        rejects([&] { decode(bytes); }, "mesh-profile");
        source = sf::profile();
        source.ue5_version = 0;
        rejects([&] { decode(bytes); }, "mesh-profile");
        source = sf::profile();
        {
            auto empty_transform = cyuediff::test::mesh_fixture::fixture();
            empty_transform.bytes.patch<int>(0, 8);
            auto &b = empty_transform.bytes;
            b.string("ExtraTransforms");
            b.i(1);
            b.i(0);
            b.i(0);
            b.i(0);
            b.i(1);
            b.string("Transform");
            b.i(7);
            b.i(1);
            b.i(0);
            b.i(1);
            b.i(1);
            b.i(96);
            b.i(0);
            for (const double value : {0., 0., 0., 1., 0., 0., 0., 1., 1., 1.})
                b.put(value);
            b.i(0);
            auto legacy_source = sf::profile();
            legacy_source.custom_versions["601d1886ac644f84aa16d3de0deac7d6"] = 132;
            check(
                !detail::decode_mesh_description(b.data, legacy_source, {}).skin,
                "Legacy empty transform attributes retain explicit ABI marker without reading native memory");
        }
        const auto many = decode(sf::fixture(false, true, 257));
        check(many.skin->profiles.at("Default").size() == 4 &&
                  many.skin->profiles.at("Default").at(256)[1].bone == 2 &&
                  many.skin->profiles.at("Default").at(256)[1].raw_weight == 32767,
              "A live vertex resolves its weights from the second 256-element storage chunk");
        options.max_attribute_values = 10;
        rejects([&] { decode(bytes); }, "limit");
        options = {};
        unsigned calls = 0;
        options.control.cancelled = [&] { return ++calls > 20; };
        rejects([&] { decode(bytes); }, "cancelled");
        options = {};
        MeshSkin custom = *mesh.skin;
        custom.bones[0].id = 20;
        custom.bones[1].id = 30;
        custom.bones[1].parent = 20;
        for (auto &profile : custom.profiles)
            for (auto &vertex : profile.second)
                for (auto &influence : vertex.second)
                    influence.bone = influence.bone == 0 ? 20 : 30;
        check(custom.to_json() == skin, "Bone renumbering preserves named skin values");
        OperationControl cancel;
        cancel.cancelled = [] { return true; };
        rejects([&] { custom.validate(cancel); }, "cancelled");
        for (std::size_t i = 0; i < bytes.bytes.data.size(); ++i) {
            auto prefix = bytes;
            prefix.bytes.data.resize(i);
            rejects([&] { decode(prefix); });
        }
        const auto file = sf::package(root, "source");
        const auto package = Package::open(file);
        check(package.mesh_sources.size() == 1, "Native SkeletalMeshDescriptionBulkData recognized");
        check(package.exports[0].native_coverage == "mesh-description-source", "Native source coverage");
        MeshDecodeOptions checked;
        checked.verify_raw_hash = true;
        check(decode_mesh(package.mesh_source(), checked).to_json() == mesh.to_json(),
              "Public standalone source package decode");
        const auto virtual_file = sf::package(root, "virtual", false, true);
        auto virtual_package = Package::open(virtual_file);
        checked.virtualized_payload_root = root / "payloads";
        check(decode_mesh(virtual_package.mesh_source(), checked).to_json() == mesh.to_json(),
              "Virtualized source skin verifies BLAKE3 and weights");
        Package owner;
        ObjectExport e;
        e.class_name = "/Script/Engine.SkeletalMesh";
        e.path = "Mesh";
        e.properties["SourceModels"]["value"] = Json::array(
            {Json{{"MeshDescriptionBulkData", {{"value", package.mesh_sources[0].object_path}}}}});
        owner.exports.push_back(e);
        owner.mesh_sources = package.mesh_sources;
        check(&owner.mesh_source() == &owner.mesh_sources[0], "SkeletalMesh source LOD reference");
        check(owner.mesh_snapshot()["meshes"]["Mesh"]["kind"] == "skeletal",
              "Skeletal source metadata is discoverable");
        std::cout << checks << " mesh skin checks passed\n";
        if (root.parent_path() != fs::current_path() ||
            root.filename().u8string().find(".test-tmp-mesh-skin-") != 0)
            throw std::runtime_error("Invalid cleanup root");
        fs::remove_all(root);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\nFixtures: " << root << '\n';
        return 1;
    }
}
