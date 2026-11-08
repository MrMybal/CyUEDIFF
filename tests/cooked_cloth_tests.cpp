#include <cyuediff/cooked_skeletal.hpp>
#include "cooked_cloth_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
unsigned checks = 0;
void check(bool v, const char *m) {
    ++checks;
    if (!v)
        throw std::runtime_error(m);
}
template <class F> void refused(F f, const std::string &code = {}) {
    try {
        f();
    } catch (const Error &e) {
        check(code.empty() || code == e.code, (e.code + ": " + e.what()).c_str());
        return;
    }
    throw std::runtime_error("Invalid cloth accepted");
}
} // namespace
int main(int argc, char **argv) {
    const auto root =
        fs::temp_directory_path() /
        ("cyuediff-cloth-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root);
        auto z = skeletal_context();
        CookedSkeletalOptions o;
        o.profile = CookedSkeletalProfile::unreal_5_3_windows;
        o.keep_mobile_min_lod_setting = false;
        o.has_vertex_colors = false;
        const auto read = [&](ClothFixture f, const CookedSkeletalOptions &opts) {
            texture_container(root / "mesh", f.base.storage);
            return read_zen_skeletal_mesh(Container::open(root / "mesh.utoc"), 0, z, {}, opts);
        };
        const auto alter = [&](ClothFixture f, unsigned component, std::uint32_t bits) {
            auto &b = f.base.storage.package;
            b.patch(static_cast<int>(f.section_records + component * 4), bits);
            b.patch(static_cast<int>(f.buffer + 10 + component * 4), bits);
            return f;
        };
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            const auto out = fs::u8path(argv[2]);
            fs::create_directories(out);
            auto f = cooked_cloth_fixture();
            texture_container(out / "cloth", f.base.storage);
            float v = .25f;
            std::uint32_t bits;
            std::memcpy(&bits, &v, 4);
            texture_container(out / "cloth-changed", alter(f, 3, bits).base.storage);
            auto renamed = alter(f, 3, bits);
            const std::string source = "/Game/Texture", target = "/Game/Altered";
            auto &bytes = renamed.base.storage.package.data;
            const auto found = std::search(bytes.begin(), bytes.end(), source.begin(), source.end());
            check(found != bytes.end(), "Original package identity exists");
            std::copy(target.begin(), target.end(), found);
            texture_container(out / "cloth-renamed-changed", renamed.base.storage);
            texture_container(out / "cloth-multi", cooked_cloth_fixture(5, 2).base.storage);
            texture_container(out / "cloth-corrupt", f.base.storage, 2, true);
            fs::remove_all(root);
            return 0;
        }
        for (unsigned influences : {1u, 5u})
            for (unsigned biases : {1u, 2u}) {
                auto f = cooked_cloth_fixture(influences, biases);
                auto a = read(f, o);
                const auto &m = *a.lods[0].cloth;
                check(m.buffer.size() == 3 * influences * biases && m.sections.size() == 1 &&
                          m.index_mapping[0].lod_bias_stride == 3 * influences,
                      "Complete one/five influence and LOD-bias windows");
                check(m.sections[0]->asset_guid == std::array<std::uint32_t, 4>{1, 2, 3, 4} &&
                          m.sections[0]->asset_index == 0 && m.sections[0]->asset_lod == 0,
                      "Exact raw binding GUID words/index/LOD");
                for (unsigned bi = 0; bi < biases; ++bi)
                    for (unsigned vi = 0; vi < 3; ++vi) {
                        auto v = m.vertex(0, bi, vi, true);
                        check(v.size() == influences &&
                                  v[0]["position"] == Json(f.biases[bi][vi * influences].position),
                              "Focused original barycentric coordinate and distance values");
                    }
                check(a.lods[0].geometry->cloth == a.lods[0].cloth &&
                          !m.to_json()["simulationMeshBoundsVerified"].get<bool>(),
                      "Shared model declares unverified physical mesh bounds");
                auto opts = o;
                opts.geometry = false;
                auto metadata = read(f, opts);
                check(!metadata.lods[0].geometry && metadata.lods[0].cloth->buffer.size() == m.buffer.size(),
                      "Metadata-only path keeps typed raw mappings");
                float v = .25f;
                std::uint32_t bits;
                std::memcpy(&bits, &v, 4);
                auto b = read(alter(f, 3, bits), o);
                auto diff = compare_meshes(*a.lods[0].geometry, *b.lods[0].geometry);
                check(diff.complete && diff.changes.size() == 1 &&
                          diff.changes[0]["path"] == "/cloth/sections/0/lodBiases/0/0/0/position/3",
                      "Exact focused active mapping difference");
                auto padding = read(alter(f, 15, 0xfedcba98), o);
                check(compare_meshes(*a.lods[0].geometry, *padding.lods[0].geometry).changes.empty() &&
                          padding.lods[0].cloth->buffer[0].padding == 0xfedcba98 &&
                          padding.lods[0].cloth->buffer_info["recordSha256"] != m.buffer_info["recordSha256"],
                      "Alignment bytes retained/fingerprinted without semantic differences");
                refused([&] { m.vertex(0, biases, 0); }, "cloth-selection");
                refused([&] { m.vertex(0, 0, 3); }, "cloth-selection");
                refused([&] { m.section(1); }, "cloth-selection");
            }
        auto f = cooked_cloth_fixture();
        for (unsigned mode = 0; mode < 4; ++mode) {
            auto bad = f;
            if (mode == 0)
                bad.base.storage.package.patch(static_cast<int>(bad.buffer + 2), 63);
            if (mode == 1)
                bad.base.storage.package.patch(static_cast<int>(bad.index_mapping + 4), 1);
            if (mode == 2)
                bad.base.storage.package.patch(static_cast<int>(bad.index_mapping + 8), 2);
            if (mode == 3)
                bad.base.storage.package.patch(static_cast<int>(bad.buffer + 10), 0x7fc12345);
            refused([&] { read(bad, o); });
        }
        auto inactive = cooked_cloth_fixture(1, 1, true);
        auto a = read(inactive, o), b = read(alter(inactive, 0, 0x7fc12345), o);
        check(compare_meshes(*a.lods[0].geometry, *b.lods[0].geometry).changes.empty() &&
                  b.lods[0].cloth->vertex(0, 0, 0, true)[0]["position"][0]["nonFiniteBits"] == 0x7fc12345,
              "Inactive nonfinite producer bytes preserved without false semantic changes");
        auto empty = read(cooked_cloth_fixture(0, 1), o);
        check(empty.lods[0].cloth && !empty.lods[0].cloth->buffer_stored &&
                  empty.lods[0].cloth->buffer.empty(),
              "Stored binding without render mappings remains explicit");
        for (unsigned mode = 0; mode < 3; ++mode) {
            auto limited = o;
            if (mode == 0)
                limited.max_cloth_mappings = 5;
            if (mode == 1)
                limited.max_cloth_influences = 0;
            if (mode == 2)
                limited.max_output_bytes = 1;
            refused([&] { read(f, limited); });
        }
        bool cancel = false;
        auto options = o;
        options.control.cancelled = [&] { return cancel; };
        options.control.progress = [&](const auto &p) {
            if (p.phase == "cooked-cloth-mappings")
                cancel = true;
        };
        refused([&] { read(f, options); }, "cancelled");
        for (std::size_t end = f.base.native; end < f.base.inline_count + 2; end += 7) {
            auto prefix = f;
            prefix.base.storage.package.data.resize(end);
            skeletal_export_size(prefix.base);
            refused([&] { read(prefix, o); });
        }
        fs::remove_all(root);
        std::cout << checks << " cooked cloth checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << checks << ": " << e.what() << '\n';
        fs::remove_all(root);
        return 1;
    }
}
