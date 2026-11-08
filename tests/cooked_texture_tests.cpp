#include <cyuediff/cooked_texture.hpp>
#include "cooked_texture_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool good, const char *message) {
    ++checks;
    if (!good)
        throw std::runtime_error(message);
}
template <class F> void refused(F f, const char *code) {
    try {
        f();
    } catch (const Error &e) {
        if (e.code != code)
            std::cerr << e.code << ": " << e.what() << '\n';
        check(e.code == code, "Expected cooked texture refusal");
        return;
    }
    throw std::runtime_error("Malformed cooked texture accepted");
}
} // namespace
int main(int argc, char **argv) {
    const auto root = fs::temp_directory_path() /
                      ("cyuediff-cooked-texture-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root);
        const auto stem = root / "texture";
        auto context = texture_context();
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            const auto target = fs::u8path(argv[2]);
            fs::create_directories(target);
            for (bool external : {false, true})
                texture_container(target / (external ? "external" : "inline"),
                                  cooked_texture_fixture(external));
            auto changed = cooked_texture_fixture(true);
            changed.bulk.data[3] = 224;
            changed.bulk.data[4] = 7;
            texture_container(target / "changed", changed);
            texture_container(target / "corrupt", cooked_texture_fixture(true), 2, true);
            auto map = collection_schema_fixture();
            map.save(target / "native.usmap");
            auto global = texture_scripts();
            global.save(target / "global.ucas");
            zen_toc(global, 5).save(target / "global.utoc");
            Json manifest = {
                {"format", "cyuediff.zen-schema/2"},
                {"profile", "ue5.3"},
                {"usmap", "native.usmap"},
                {"sha256", sha256_bytes(map.data)},
                {"bindings", {{"/Script/Engine.Texture2D", {{"schema", "Collections"}, {"offset", 0}}}}},
                {"nativeStructs", Json::object()},
                {"reflectedStructs", Json::array()}};
            std::ofstream(target / "context.json") << manifest.dump(2);
            fs::remove_all(root);
            return 0;
        }
        auto read = [&](const TextureFixture &f, const CookedTextureOptions &o = CookedTextureOptions{},
                        unsigned type = 2, bool corrupt = false) {
            texture_container(stem, f, type, corrupt);
            return read_zen_texture2d(Container::open(fs::path(stem.string() + ".utoc")), 0, context, {}, o);
        };
        for (bool external : {false, true}) {
            auto f = cooked_texture_fixture(external);
            auto result = read(f);
            const auto &m = result.platforms.at(0).mips.at(0);
            check(result.object_path == "/Game/Texture.Texture" && result.native_offset == f.body + 2,
                  "Native texture starts at the exact decoded property boundary");
            check(m.loaded && m.width == 4 && m.height == 4 && m.size == 8 && !m.sha256.empty(),
                  "Stored mip bytes and dimensions resolved");
            check(m.storage == (external ? "external" : "inline") &&
                      m.offset == (external ? 3 : f.mip_index + 4),
                  "Inline cursor or external bulk offset used explicitly");
            auto image = decode_cooked_mip(result.platforms[0]);
            check(image.pixels[0] == 255 && image.pixels[1] == 0 && image.pixels[3] == 255,
                  "Cooked BC1 pixels decode");
            check(result.to_json()["mappingVerified"] == false &&
                      result.to_json()["platforms"][0]["mips"][0]["loaded"] == true,
                  "Provenance remains explicit");
            CookedTextureOptions options;
            options.load_payloads = false;
            auto inventory = read(f, options);
            check(!inventory.platforms[0].mips[0].loaded, "Inventory does not publish encoded bytes");
            refused([&] { decode_cooked_mip(inventory.platforms[0]); }, "cooked-texture-unavailable");
            options = {};
            options.max_payload_bytes = 7;
            refused([&] { read(f, options); }, "cooked-texture-limit");
            options = {};
            options.max_read_bytes = f.package.data.size() + (external ? f.bulk.data.size() - 1 : 0);
            if (external)
                refused([&] { read(f, options); }, "cooked-texture-limit");
            else
                check(read(f, options).platforms.size() == 1, "Exact aggregate read budget accepted");
            options.max_read_bytes = f.package.data.size() - 1;
            refused([&] { read(f, options); }, "cooked-texture-limit");
            options = {};
            options.max_mips = 0;
            refused([&] { read(f, options); }, "cooked-texture-limit");
            options = {};
            options.max_formats = 0;
            refused([&] { read(f, options); }, "cooked-texture-limit");
            options = {};
            options.max_dimension = 3;
            refused([&] { read(f, options); }, "cooked-texture-layout");
            options = {};
            options.control.cancelled = [] { return true; };
            refused([&] { read(f, options); }, "cancelled");
            options = {};
            bool cancel = false;
            options.control.cancelled = [&] { return cancel; };
            options.control.progress = [&](const auto &p) {
                if (p.phase == "cooked-texture-mip")
                    cancel = true;
            };
            refused([&] { read(f, options); }, "cancelled");
            auto bad = f;
            bad.package.patch(f.mip_index, 1);
            refused([&] { read(bad); }, "cooked-texture-layout");
            bad = f;
            bad.package.patch(f.dimensions, 5);
            refused([&] { read(bad); }, "cooked-texture-layout");
            bad = f;
            bad.package.patch(f.format_skip, 8);
            refused([&] { read(bad); }, "cooked-texture-layout");
            bad = f;
            bad.package.patch(f.bulk_row + 16, 7);
            refused([&] { read(bad); }, "cooked-texture-layout");
            bad = f;
            bad.package.patch(f.body + 2, 2);
            refused([&] { read(bad); }, "cooked-texture-layout");
            bad = f;
            bad.package.data[f.format_skip + 8] = 1;
            refused([&] { read(bad); }, "cooked-texture-profile");
            bad = f;
            bad.package.patch(f.bulk_row + 24, 2);
            refused([&] { read(bad); }, "cooked-texture-profile");
            if (external) {
                refused([&] { read(f, {}, 2, true); }, "container-integrity");
                bad = f;
                bad.bulk.data.clear();
                auto missing = read(bad);
                check(missing.platforms[0].mips[0].unavailable == "missing-chunk",
                      "Missing external chunk is explicit and no pixels are invented");
                for (unsigned type : {3u, 4u}) {
                    auto special = cooked_texture_fixture(true, type == 3 ? 0x10d01 : 0x11501);
                    check(read(special, {}, type).platforms[0].mips[0].loaded,
                          "Optional and mapped bulk chunk types resolve");
                }
                auto duplicate = cooked_texture_fixture(true, 0x14501);
                duplicate.package.patch(duplicate.bulk_row + 8, 3);
                duplicate.package.patch(duplicate.bulk_row + 12, 0);
                check(read(duplicate, {}, 3).platforms[0].mips[0].chunk_id.substr(22) == "03",
                      "Duplicate optional source uses duplicate offset");
                check(read(duplicate).platforms[0].mips[0].chunk_id.substr(22) == "02",
                      "Duplicate source falls back to ordinary bulk");
            }
        }
        auto f = cooked_texture_fixture();
        for (std::size_t n = f.body + 2; n < f.package.data.size(); ++n) {
            auto cut = f;
            cut.package.data.resize(n);
            std::uint32_t exports = 0;
            std::memcpy(&exports, cut.package.data.data() + 32, 4);
            const auto size = std::uint64_t(n - f.body);
            std::memcpy(cut.package.data.data() + exports + 8, &size, 8);
            try {
                read(cut);
                throw std::runtime_error("Truncated native texture accepted");
            } catch (const Error &e) {
                check(e.code == "cooked-texture-layout" || e.code == "cooked-texture-profile",
                      "Truncated native input is refused");
            }
        }
        auto reserved = f;
        reserved.package.patch(f.bulk_row + 28, 7);
        refused([&] { read(reserved); }, "zen-format");
        texture_container(stem, f);
        const auto container = Container::open(fs::path(stem.string() + ".utoc"));
        auto cached_fixture = cooked_texture_fixture(true);
        texture_container(stem, cached_fixture);
        unsigned reads = 0;
        ContainerReadOptions counters;
        counters.control.progress = [&](const auto &p) {
            if (p.phase == "container-read")
                ++reads;
        };
        auto reader = CookedTextureReader::open(Container::open(fs::path(stem.string() + ".utoc")), 0,
                                                context, counters);
        auto first = reader.read();
        const auto count = reads;
        auto second = reader.read();
        check(first.to_json() == second.to_json() && count>0 && reads == count,
              "Worker session reuses one verified package and external chunk");
        auto moved = std::move(reader);
        refused([&] { reader.read(); }, "cooked-texture-layout");
        check(moved.read().platforms[0].mips[0].loaded, "Session ownership moves safely");
        texture_container(stem,f);
        context.profile = ZenProfile::unreal_5_5;
        refused([&] { read_zen_texture2d(container, 0, context); }, "cooked-texture-profile");
        context = texture_context();
        context.schema_context.reset();
        refused([&] { read_zen_texture2d(container, 0, context); }, "cooked-texture-context");
        fs::remove_all(root);
        std::cout << checks << " cooked texture checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        fs::remove_all(root);
        return 1;
    }
}
