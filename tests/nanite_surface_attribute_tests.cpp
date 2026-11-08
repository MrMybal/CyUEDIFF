#include <cyuediff/cooked_mesh.hpp>
#include "cooked_nanite_fixture.hpp"
#include <fstream>
#include <iostream>
#include <chrono>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
unsigned checks = 0;
void check(bool x, const char *m) {
    ++checks;
    if (!x)
        throw std::runtime_error(m);
}
template <class F> void refused(F f) {
    try {
        f();
    } catch (const Error &) {
        ++checks;
        return;
    }
    throw std::runtime_error("Invalid attribute difference accepted");
}
std::uint32_t word(const std::vector<std::uint8_t> &b, std::size_t o) {
    std::uint32_t n = 0;
    for (unsigned k = 0; k < 4; ++k)
        n |= std::uint32_t(b.at(o + k)) << (8 * k);
    return n;
}
Json table;
Json example(const char *name) {
    for (const auto &r : table.at("cases"))
        if (r.at("name") == name)
            return r;
    throw std::runtime_error("Missing original numeric example");
}
CookedNaniteFixture resource(const Json &row) {
    const bool streaming = row.at("pages").size() == 2;
    auto f = cooked_nanite_fixture(streaming, streaming);
    auto &b = f.mesh.storage.package;
    std::vector<std::vector<std::uint8_t>> pages;
    for (std::size_t i = 0; i < row.at("pages").size(); ++i) {
        auto p = row["pages"][i]["bytes"].get<std::vector<std::uint8_t>>();
        p.insert(p.begin() + 8, 16, 0);
        p[4] = 1;
        nanite_patch(p, 8, static_cast<std::uint32_t>(i));
        nanite_patch(p, 12, static_cast<std::uint32_t>(i));
        nanite_patch(p, 20, i ? 9 : 0);
        pages.push_back(std::move(p));
    }
    const auto delta = static_cast<std::int64_t>(pages[0].size()) - 304;
    b.data.erase(b.data.begin() + static_cast<std::ptrdiff_t>(f.root),
                 b.data.begin() + static_cast<std::ptrdiff_t>(f.root + 304));
    b.data.insert(b.data.begin() + static_cast<std::ptrdiff_t>(f.root), pages[0].begin(), pages[0].end());
    auto shift = [&](std::size_t &v) { v = static_cast<std::size_t>(static_cast<std::int64_t>(v) + delta); };
    for (auto v : {&f.states, &f.hierarchy, &f.roots, &f.dependencies, &f.facts, &f.end})
        shift(*v);
    b.patch(int(f.root - 4), static_cast<std::uint32_t>(pages[0].size()));
    for (std::size_t i = 0; i < pages.size(); ++i) {
        b.patch(int(f.states + 20 * i + 4), static_cast<std::uint32_t>(pages[i].size()));
        b.patch(int(f.states + 20 * i + 8), static_cast<std::uint32_t>(pages[i].size() - 24));
        b.data[f.states + 20 * i + 19] = row["pages"][i]["flags"];
    }
    const auto native = word(pages[0], 24 + 108), uvs = word(pages[0], 24 + 12);
    b.patch(int(f.facts + 8), (native >> 24) & 15);
    b.patch(int(f.facts + 20), 1 | (uvs << 16));
    f.mesh.storage.bulk.data.clear();
    const std::int64_t size = streaming ? static_cast<std::int64_t>(pages[1].size()) : 0;
    std::memcpy(b.data.data() + f.mesh.storage.bulk_row + 16, &size, 8);
    if (streaming) {
        f.mesh.storage.bulk.zeros(3);
        f.mesh.storage.bulk.data.insert(f.mesh.storage.bulk.data.end(), pages[1].begin(), pages[1].end());
    }
    nanite_export_size(f);
    return f;
}
Json changed(Json row, unsigned bit) {
    auto bytes = row["pages"][0]["bytes"].get<std::vector<std::uint8_t>>();
    const auto base = 8 + word(bytes, 8 + 48);
    bytes.at(base + bit / 8) ^= std::uint8_t(1u << (bit % 8));
    row["pages"][0]["bytes"] = bytes;
    return row;
}
Json repack(Json row, bool uv) {
    auto b = row["pages"][0]["bytes"].get<std::vector<std::uint8_t>>();
    const auto attr = 8 + word(b, 8 + 48);
    const auto old_word = word(b, 8 + 160);
    for (unsigned v = 0; v < 3; ++v) {
        std::uint64_t old = 0;
        for (unsigned k = 0; k < 8; ++k)
            old |= std::uint64_t(b[attr + 8 * v + k]) << (8 * k);
        std::uint64_t value;
        if (uv) {
            value = old & ((1ull << 42) - 1);
            for (unsigned k = 0; k < 4; ++k)
                value |= (((old >> (42 + 4 * k)) & 15) * 2) << (42 + 5 * k);
        } else
            value = (old & ((1ull << 20) - 1)) | ((((old >> 20) & 15) + 1) << 20) | ((old >> 24) << 25);
        for (unsigned k = 0; k < 8; ++k)
            b[attr + 8 * v + k] = std::uint8_t(value >> (8 * k));
    }
    if (uv) {
        nanite_patch(b, 8 + 168, 0x5555);
        for (unsigned u = 0; u < 2; ++u) {
            for (unsigned k = 0; k < 6; ++k) {
                const auto at = 8 + 192 + 32 * u + 4 * k;
                nanite_patch(b, at, word(b, at) * 2);
            }
            nanite_patch(b, 8 + 192 + 32 * u + 24, 1);
        }
    } else {
        nanite_patch(b, 8 + 88, word(b, 8 + 88) - 1);
        nanite_patch(b, 8 + 92, (word(b, 8 + 92) & ~15u) | 5);
    }
    nanite_patch(b, 8 + 160, (old_word & 0x3fffff) | ((uv ? 62u : 59u) << 22));
    row["pages"][0]["bytes"] = b;
    return row;
}
void rotate(Json &row, bool geometry) {
    auto b = row["pages"][0]["bytes"].get<std::vector<std::uint8_t>>();
    const auto pos = 8 + word(b, 8 + 44), attr = 8 + word(b, 8 + 48),
               stride = ((word(b, 8 + 160) >> 22) + 7) / 8;
    auto previous = b;
    for (unsigned v = 0; v < 3; ++v) {
        if (geometry)
            b[pos + v] = previous[pos + (v + 1) % 3];
        for (unsigned k = 0; k < stride; ++k)
            b[attr + v * stride + k] = previous[attr + ((v + 1) % 3) * stride + k];
    }
    row["pages"][0]["bytes"] = b;
}
} // namespace
int main(int argc, char **argv) {
    const auto root = fs::temp_directory_path() /
                      ("cyuediff-nanite-attribute-diff-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::ifstream input(CYUEDIFF_NANITE_ATTRIBUTES_REFERENCE);
        input >> table;
        fs::create_directories(root);
        const auto stem = root / "mesh";
        auto z = mesh_context();
        CookedMeshOptions options;
        options.profile = CookedMeshProfile::unreal_5_3_windows;
        options.keep_mobile_min_lod_setting = false;
        options.geometry = false;
        options.nanite.profile = NaniteProfile::unreal_5_3_windows;
        options.nanite.attributes = true;
        auto load = [&](const Json &row, bool attributes = true) {
            auto f = resource(row);
            texture_container(stem, f.mesh.storage);
            auto o = options;
            o.nanite.attributes = attributes;
            return read_zen_static_mesh(Container::open(fs::path(stem.string() + ".utoc")), 0, z, {}, o)
                .nanite;
        };
        auto base = example("normal-8");
        auto color = changed(base, 20), uv = changed(base, 42), tangent = changed(base, 16);
        auto normal = base;
        auto bytes = normal["pages"][0]["bytes"].get<std::vector<std::uint8_t>>();
        const auto at = 8 + word(bytes, 8 + 48);
        bytes[at] = 128;
        bytes[at + 1] = 128;
        normal["pages"][0]["bytes"] = bytes;
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            const auto destination = fs::u8path(argv[2]);
            fs::create_directories(destination);
            for (const auto &entry : std::vector<std::pair<std::string, Json>>{{"attrs-before", base},
                                                                               {"attrs-color", color},
                                                                               {"attrs-uv", uv},
                                                                               {"attrs-tangent", tangent},
                                                                               {"attrs-normal", normal}}) {
                auto f = resource(entry.second);
                texture_container(destination / entry.first, f.mesh.storage);
            }
            check(fs::weakly_canonical(root).parent_path() == fs::weakly_canonical(fs::temp_directory_path()),
                  "Fixture cleanup path");
            fs::remove_all(root);
            return 0;
        }
        auto a = load(base);
        NaniteSurfaceDiffOptions diff_options;
        diff_options.attributes = true;
        auto same = compare_nanite_surfaces(*a, *a, diff_options);
        check(same.summary["status"] == "identical" && same.summary["storedAttributesCompared"] == true &&
                  same.modified.empty(),
              "Common decoded attributes compare without invented changes");
        for (const auto &entry :
             std::vector<std::pair<Json, std::uint8_t>>{{color, nanite_colors},
                                                        {uv, nanite_uvs},
                                                        {tangent, nanite_tangents},
                                                        {normal, nanite_normals | nanite_tangents}}) {
            auto b = load(entry.first);
            auto out = compare_nanite_surfaces(*a, *b, diff_options);
            check(out.summary["complete"] == true && out.summary["surfaceStatus"] == "identical" &&
                      out.summary["attributeStatus"] == "changed" && out.modified.size() == 1 &&
                      out.modified[0].changed == entry.second,
                  "Exact decoded channel changes retain common geometry");
            check(out.removed.empty() && out.added.empty() && out.modified[0].before.page == 0 &&
                      out.modified[0].after.triangle == 0,
                  "Attribute changes retain native highlight references");
            check(!out.to_json(false).contains("modified") &&
                      out.to_json()["modified"][0]["channels"].size() > 0,
                  "Optional focused attribute reference serialization");
        }
        for (bool uv_encoding : {false, true}) {
            auto encoded = load(repack(base, uv_encoding));
            auto out = compare_nanite_surfaces(*a, *encoded, diff_options);
            check(out.summary["encodingStatus"] == "changed" && out.summary["surfaceStatus"] == "identical" &&
                      out.summary["attributeStatus"] == "identical" && out.modified.empty(),
                  "Changed color/UV packing can preserve exact decoded shading");
        }
        auto mixed_before = example("normal-0"), mixed_after = changed(mixed_before, 4);
        auto partial = compare_nanite_surfaces(*load(mixed_before), *load(mixed_after), diff_options);
        check(!partial.summary["complete"].get<bool>() && partial.summary["attributeStatus"] == "changed" &&
                  partial.modified.size() == 1 && partial.modified[0].changed == nanite_colors &&
                  partial.modified[0].unavailable == (nanite_normals | nanite_tangents),
              "Known color changes survive unavailable normal/tangent channels");
        auto opaque = load(base, false);
        auto unknown = compare_nanite_surfaces(*a, *opaque, diff_options);
        check(!unknown.summary["complete"].get<bool>() && unknown.summary["geometryComplete"] == true &&
                  unknown.summary["attributeStatus"] == "unavailable" && unknown.modified.empty(),
              "Unrequested attributes cannot prove semantic equality");
        check(compare_nanite_surfaces(*a, *opaque).summary["complete"] == true,
              "Default position/topology scope remains explicit");
        auto zero = load(example("normal-0"));
        unknown = compare_nanite_surfaces(*zero, *zero, diff_options);
        check(unknown.summary["attributeChannels"]["normals"]["complete"] == false &&
                  unknown.summary["attributeChannels"]["colors"]["status"] == "identical" &&
                  unknown.summary["status"] == "incomplete",
              "Zero-bit normal is unknown while known channels compare");
        auto cyclic = base;
        rotate(cyclic, true);
        check(compare_nanite_surfaces(*a, *load(cyclic), diff_options).modified.empty(),
              "Cyclic native vertex order preserves surface shading correspondence");
        auto changed_cyclic = load(changed(cyclic, 42));
        auto aligned = compare_nanite_surfaces(*a, *changed_cyclic, diff_options);
        check(aligned.modified.size() == 1 &&
                  aligned.modified[0].before_rotation != aligned.modified[0].after_rotation,
              "Modified attributes retain canonical corner alignment after native vertex rotation");
        const auto &alignment = aligned.modified[0];
        const auto before_triangle = a->geometry()->triangle(0, 0, 0),
                   after_triangle = changed_cyclic->geometry()->triangle(0, 0, 0);
        for (unsigned corner = 0; corner < 3; ++corner)
            check(a->geometry()->position(
                      0, 0, before_triangle.vertices[(corner + alignment.before_rotation) % 3]) ==
                      changed_cyclic->geometry()->position(
                          0, 0, after_triangle.vertices[(corner + alignment.after_rotation) % 3]),
                  "Before/after attribute corner orders locate the same actual native point");
        auto degenerate = base;
        bytes = degenerate["pages"][0]["bytes"].get<std::vector<std::uint8_t>>();
        const auto pos = 8 + word(bytes, 8 + 44);
        bytes[pos] = bytes[pos + 1] = bytes[pos + 2] = 0;
        degenerate["pages"][0]["bytes"] = bytes;
        auto degenerate_rotated = degenerate;
        rotate(degenerate_rotated, false);
        check(compare_nanite_surfaces(*load(degenerate), *load(degenerate_rotated), diff_options)
                      .summary["attributeStatus"] == "identical",
              "Coincident positions retain cyclic attribute correspondence");
        auto two = base;
        two["pages"].push_back(color["pages"][0]);
        two["pages"][1]["dependencies"] = Json::array({0});
        auto reordered = two;
        std::swap(reordered["pages"][0], reordered["pages"][1]);
        reordered["pages"][0]["dependencies"] = Json::array();
        reordered["pages"][1]["dependencies"] = Json::array({0});
        auto duplicate_before = load(two), duplicate_after = load(reordered);
        auto out = compare_nanite_surfaces(*duplicate_before, *duplicate_after, diff_options);
        check(out.summary["surfaceStatus"] == "identical" && out.summary["attributeStatus"] == "identical" &&
                  out.modified.empty(),
              "Duplicate oriented surfaces match identical attributes before pairing changes");
        out = compare_nanite_surfaces(*duplicate_before, *load(color), diff_options);
        check(out.removed.size() == 1 && out.removed[0].page == 0 && out.modified.empty(),
              "Duplicate removal retains the actual unmatched shading identity");
        auto residual = base;
        residual["pages"].push_back(uv["pages"][0]);
        residual["pages"][1]["dependencies"] = Json::array({0});
        auto residual_before = base;
        residual_before["pages"].push_back(tangent["pages"][0]);
        residual_before["pages"][1]["dependencies"] = Json::array({0});
        // Both residual pairings are deterministic; authored identity of overlapping triangles is unknown.
        residual["pages"][0] = color["pages"][0];
        auto ambiguous = compare_nanite_surfaces(*load(residual_before), *load(residual), diff_options);
        check(ambiguous.summary["ambiguousAttributePairs"] == 2 && ambiguous.modified.size() == 2 &&
                  ambiguous.modified[0].ambiguous,
              "Residual overlapping shading identities report pairing ambiguity");
        diff_options.max_output_bytes = 1;
        refused([&] { compare_nanite_surfaces(*a, *a, diff_options); });
        diff_options = {};
        diff_options.attributes = true;
        diff_options.max_triangles = 1;
        refused([&] { compare_nanite_surfaces(*a, *a, diff_options); });
        diff_options = {};
        diff_options.attributes = true;
        diff_options.control.cancelled = [] { return true; };
        refused([&] { compare_nanite_surfaces(*a, *a, diff_options); });
        check(fs::weakly_canonical(root).parent_path() == fs::weakly_canonical(fs::temp_directory_path()),
              "Cleanup remains inside temporary fixture root");
        fs::remove_all(root);
        std::cout << checks << " Nanite shading/surface correspondence checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "check " << checks << ": " << e.what() << " fixtures " << root << '\n';
        return 1;
    }
}
