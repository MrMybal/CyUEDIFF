#include <cyuediff/mesh_morphs.hpp>
#include "mesh_morph_fixture.hpp"
#include "../src/mesh.hpp"
#include <chrono>
#include <iostream>
#include <limits>
using namespace cyuediff;
namespace mf = cyuediff::test::morph_fixture;
namespace {
unsigned checks = 0;
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
    throw std::runtime_error("Expected rejected morph data");
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            const auto root = fs::u8path(argv[2]);
            mf::package(root, "morph-before");
            mf::package(root, "morph-after", true);
            mf::package(root, "morph-virtualized", false, true);
            return 0;
        }
        const auto before = mf::fixture();
        MeshDecodeOptions opts;
        auto decode = [&](const mf::Fixture &f) {
            return detail::decode_mesh_description(f.source.bytes.data, test::skin_fixture::profile(), opts);
        };
        const auto mesh = decode(before), after = decode(mf::fixture(true));
        check(mesh.morphs && mesh.morphs->targets.size() == 2, "Named morph targets decoded");
        const auto &smile = mesh.morphs->targets.at("Smile");
        check(smile.positions.size() == 3 && !smile.positions.count(1),
              "Only live sparse vertex IDs projected");
        check(smile.positions.at(2)[1] == -.5f && smile.positions.at(4)[0] == 0,
              "Signed and stored zero deltas retained");
        check(smile.normals_present && smile.normals.at(1)[1] == .25f, "Normal deltas use instance IDs");
        check(!mesh.morphs->targets.at("Blink").normals_present &&
                  mesh.to_json()["morphs"]["targets"]["Blink"]["normals"].is_null(),
              "Absent normals remain absent");
        check(!decode(mf::fixture(false, false)).morphs->targets.at("Smile").normals_present,
              "Position-only morph supported");
        bool position = false, normal = false;
        for (const auto &c : compare_meshes(mesh, after).changes) {
            position |=
                c["path"] == "/morphs/targets/Smile/positions/0/0" && c["before"] == 1 && c["after"] == 2;
            normal |=
                c["path"] == "/morphs/targets/Smile/normals/1/1" && c["before"] == .25 && c["after"] == .5;
        }
        check(position && normal, "Named position and split-normal semantic paths");
        check(compare_meshes(mesh, mesh).changes.empty(), "Self comparison identical");
        auto f = before;
        f.source.bytes.patch<float>(f.position, std::numeric_limits<float>::quiet_NaN());
        rejects([&] { decode(f); }, "mesh-number");
        f = before;
        f.source.bytes.patch<float>(f.normal, std::numeric_limits<float>::infinity());
        rejects([&] { decode(f); }, "mesh-number");
        f = before;
        f.source.bytes.patch<float>(f.inactive, std::numeric_limits<float>::quiet_NaN());
        check(decode(f).morphs->to_json() == mesh.morphs->to_json(),
              "Inactive morph storage excluded from semantics");
        check(!compare_meshes(mesh, decode(f)).changes.empty(), "Inactive storage still fingerprinted");
        f = before;
        f.source.bytes.patch<int>(f.layout, 2);
        rejects([&] { decode(f); });
        f = before;
        f.source.bytes.data[f.name + 6] = 0;
        rejects([&] { decode(f); }, "mesh-morph-identity");
        f = before;
        f.source.bytes.data[f.normal_name + 6] = 'Q';
        rejects([&] { decode(f); }, "mesh-morph-reference");
        f = before;
        for (unsigned i = 0; i < 5; ++i)
            f.source.bytes.data[f.blink_name + 6 + i] = "smile"[i];
        rejects([&] { decode(f); }, "mesh-morph-identity");
        f = before;
        f.source.bytes.patch<int>(f.layout + 12, 2);
        const auto channel = std::vector<std::uint8_t>(f.source.bytes.data.begin() + f.layout + 16,
                                                       f.source.bytes.data.begin() + f.position + 60);
        f.source.bytes.data.insert(f.source.bytes.data.begin() + f.position + 60, channel.begin(),
                                   channel.end());
        rejects([&] { decode(f); }, "mesh-morph-layout");
        MeshGeometry empty_mesh;
        MeshMorphs empty_morphs;
        empty_morphs.targets["Empty"].normals_present = true;
        empty_morphs.validate(empty_mesh);
        check(empty_morphs.to_json()["targets"]["Empty"]["normals"].is_object(),
              "Present empty normals distinguished from absent normals");
        opts.max_attribute_values = 4;
        rejects([&] { decode(before); }, "limit");
        opts = {};
        opts.control.cancelled = [] { return true; };
        rejects([&] { decode(before); }, "cancelled");
        opts = {};
        auto custom = *mesh.morphs;
        custom.targets["smile"] = smile;
        rejects([&] { custom.validate(mesh); }, "mesh-morph-identity");
        custom = *mesh.morphs;
        custom.targets["Smile"].positions.erase(0);
        rejects([&] { custom.validate(mesh); }, "mesh-morph-reference");
        custom = *mesh.morphs;
        custom.targets["Smile"].positions.erase(0);
        custom.targets["Smile"].positions[1] = {};
        rejects([&] { custom.validate(mesh); }, "mesh-morph-reference");
        custom = *mesh.morphs;
        custom.targets["Smile"].normals_present = false;
        rejects([&] { custom.to_json(); }, "mesh-morph-layout");
        custom = *mesh.morphs;
        custom.targets["Smile"].positions[0][0] = std::numeric_limits<float>::infinity();
        rejects([&] { custom.to_json(); }, "mesh-morph-number");
        OperationControl cancel;
        cancel.cancelled = [] { return true; };
        rejects([&] { mesh.morphs->validate(mesh, cancel); }, "cancelled");
        for (std::size_t i = 0; i < before.source.bytes.data.size(); ++i) {
            f = before;
            f.source.bytes.data.resize(i);
            rejects([&] { decode(f); });
        }
        const auto root = fs::current_path() /
                          (".test-tmp-mesh-morph-" +
                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto path = mf::package(root, "source");
        auto package = Package::open(path);
        MeshDecodeOptions checked;
        checked.verify_raw_hash = true;
        check(decode_mesh(package.mesh_source(), checked).to_json() == mesh.to_json(),
              "Public skeletal source package decode");
        const auto virtual_path = mf::package(root, "virtualized", false, true);
        checked.virtualized_payload_root = root / "payloads";
        check(decode_mesh(Package::open(virtual_path).mesh_source(), checked).to_json() == mesh.to_json(),
              "Virtualized morph payload integrity");
        if (root.parent_path() != fs::current_path() ||
            root.filename().u8string().find(".test-tmp-mesh-morph-") != 0)
            throw std::runtime_error("Invalid cleanup root");
        fs::remove_all(root);
        std::cout << checks << " mesh morph checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
