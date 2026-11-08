#include <cyuediff/cooked_animation.hpp>
#include "cooked_animation_fixture.hpp"
#include <chrono>
#include <iostream>
#include <fstream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
unsigned checks = 0;
void check(bool v, const char *s) {
    ++checks;
    if (!v)
        throw std::runtime_error(s);
}
template <class F> void refused(F f) {
    try {
        f();
    } catch (const Error &) {
        ++checks;
        return;
    }
    throw std::runtime_error("Invalid whole curve package accepted");
}
CookedAnimation read(const fs::path &root, bool bind = true, bool curve_schema = true) {
    auto z = animation_context(curve_schema);
    CookedAnimationOptions o;
    o.profile = CookedAnimationProfile::unreal_5_3_windows;
    if (bind)
        o.curve_settings = CookedAnimationDependency{Container::open(root / "curve-settings.utoc"), 0, z, {}};
    return read_zen_animation(Container::open(root / "animation.utoc"), 0, z, {}, o);
}
} // namespace
int main(int argc, char **argv) {
    const auto root = fs::temp_directory_path() /
                      ("cyuediff-whole-curves-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            const auto target = fs::u8path(argv[2]);
            fs::create_directories(target);
            animation_scripts().save(target / "global.ucas");
            zen_toc(animation_scripts(), 5).save(target / "global.utoc");
            animation_schema(true).save(target / "mappings.usmap");
            Json ctx = {
                {"format", "cyuediff.zen-schema/3"},
                {"profile", "ue5.3"},
                {"usmap", "mappings.usmap"},
                {"sha256", sha256_bytes(animation_schema(true).data)},
                {"bindings", Json::object()},
                {"structLayouts", Json::object()},
                {"nativeStructs", Json::object()},
                {"binaryLayouts", {{"format", "cyuediff.binary-layouts/1"}, {"layouts", Json::object()}}},
                {"narrowStringEncoding", "ascii"}};
            for (const auto &name : {"AnimSequence", "Skeleton", "AnimBoneCompressionSettings",
                                     "AnimCurveCompressionSettings"}) {
                const auto n = "/Script/Engine." + std::string(name);
                ctx["bindings"][n] = {{"schema", n}, {"offset", 0}};
            }
            std::ofstream(target / "context.json") << ctx.dump(2);
            ctx["narrowStringEncoding"] = "utf8";
            std::ofstream(target / "after-context.json") << ctx.dump(2);
            for (unsigned c = 0; c < 3; ++c) {
                auto f = cooked_animation_curve_fixture(c);
                texture_container(target / ("curves-" + std::to_string(c)), f.animation.animation.storage);
                texture_container(target / ("settings-" + std::to_string(c)), f.curve_settings.storage);
                auto only = cooked_animation_curve_fixture(c, true);
                texture_container(target / ("only-" + std::to_string(c)), only.animation.animation.storage);
            }
            auto missing = cooked_animation_curve_fixture();
            const std::uint64_t plugin_class = script_root + 6;
            std::memcpy(missing.curve_settings.storage.package.data.data() + missing.curve_settings.exports +
                            2 * 72 + 32,
                        &plugin_class, 8);
            texture_container(target / "settings-plugin", missing.curve_settings.storage);
            auto empty = cooked_animation_fixture();
            texture_container(target / "empty", empty.animation.storage);
            return 0;
        }
        fs::create_directories(root);
        for (unsigned codec = 0; codec < 3; ++codec) {
            auto f = cooked_animation_curve_fixture(codec);
            save_animation_curves(root, f);
            auto a = read(root);
            check(bool(a.compressed_curves()), "Verified registered curve decoder selected");
            check(a.to_json()["curves"]["status"] == "decoded", "Decoded native curve coverage");
            check(a.compressed_curves()->curve_count() == 1 &&
                      a.sample_curves(.5)["values"]["name:CurveOne"]["value"] == 3,
                  "Named whole curve sample in seconds");
            check(!a.decoded(), "Curves do not require bone codec binding");
            auto unavailable = read(root, false);
            check(unavailable.curve_information()["status"] == "opaque" &&
                      !unavailable.sample_curves(.5)["ready"].get<bool>(),
                  "Unknown curve settings remain unavailable");
            auto d = compare_cooked_animation_curves(a, unavailable);
            check(d["status"] == "incomplete" && d["encodingChanges"] == 0 && d["valueChanges"] == 0,
                  "Unknown peer is not a removal");
            CookedAnimationCurveDiffOptions opts;
            opts.before_time = 0;
            opts.after_time = .5;
            d = compare_cooked_animation_curves(a, a, opts);
            check(d["encodingChanges"] == 0 && d["valueChanges"] == 1 &&
                      d["curves"]["name:CurveOne"]["valueDelta"] == 2,
                  "Encoding and independently selected values differ separately");
            opts.value_tolerance = 2;
            check(compare_cooked_animation_curves(a, a, opts)["status"] == "identical",
                  "Value tolerance explicit");
            auto empty_fixture = cooked_animation_fixture();
            save_animation(root, empty_fixture);
            auto empty = read(root, false, false);
            check(empty.sample_curves(0)["ready"] == true && empty.sample_curves(0)["values"].empty(),
                  "Known empty curve coverage");
            check(compare_cooked_animation_curves(a, empty)["curves"]["name:CurveOne"]["encodingStatus"] ==
                      "removed",
                  "Confirmed absent curve is removed");
            auto curve_only = cooked_animation_curve_fixture(codec, true);
            save_animation_curves(root, curve_only);
            auto only = read(root);
            check(!only.frame_count() && only.sample_curves(.25)["values"]["name:CurveOne"]["value"] == 2,
                  "Curve-only sampling has no invented bone frame domain");
        }
        auto fixture = cooked_animation_curve_fixture();
        save_animation_curves(root, fixture);
        auto original = read(root);
        check(original.curve_information()["codec"]["class"] ==
                      "/Script/Engine.AnimCurveCompressionCodec_CompressedRichCurve" &&
                  original.curve_information()["codec"]["exportIndex"] == 2,
              "Registry selects actual codec among unused exports");
        check(!original.to_json()["curves"]["decoded"]["curves"][0].contains("values"), "Keys remain opt-in");
        check(original.to_json(true)["curves"]["decoded"]["curves"][0]["values"].size() == 2,
              "Stored curve keys opt in");
        auto snapshot = original.to_json(true);
        auto plugin = fixture;
        const std::uint64_t unsupported = script_root + 6;
        std::memcpy(plugin.curve_settings.storage.package.data.data() + plugin.curve_settings.exports +
                        2 * 72 + 32,
                    &unsupported, 8);
        save_animation_curves(root, plugin);
        auto opaque = read(root);
        check(!opaque.compressed_curves() &&
                  opaque.curve_information()["unavailable"] == "unsupported-curve-codec-lookup",
              "Unknown registered lookup stays opaque");
        check(original.to_json(true) == snapshot && original.sample_curves(.5)["ready"] == true,
              "Prepared model survives changed dependency bytes");
        auto template_codec = fixture;
        template_codec.curve_settings.storage.package.patch(
            template_codec.curve_settings.exports + 2 * 72 + 64, 0x10);
        save_animation_curves(root, template_codec);
        refused([&] { read(root); });
        auto wrong_hash = fixture;
        const std::uint64_t hash = 0xaab;
        std::memcpy(wrong_hash.curve_settings.storage.package.data.data() +
                        wrong_hash.curve_settings.exports + 56,
                    &hash, 8);
        save_animation_curves(root, wrong_hash);
        refused([&] { read(root); });
        auto null_codec = fixture;
        null_codec.curve_settings.storage.package.patch(null_codec.curve_settings.storage.body + 2, 0);
        save_animation_curves(root, null_codec);
        refused([&] { read(root); });
        auto missing_codec = fixture;
        missing_codec.curve_settings.storage.package.patch(missing_codec.curve_settings.storage.body + 2, 99);
        save_animation_curves(root, missing_codec);
        auto unresolved = read(root);
        check(!unresolved.compressed_curves() &&
                  unresolved.curve_information()["unavailable"] == "curve-settings-properties-not-decoded",
              "Unresolved registration cannot select another codec");
        auto malformed = fixture;
        malformed.animation.animation.storage.package.data[malformed.curve_start] = 255;
        save_animation_curves(root, malformed);
        refused([&] { read(root); });
        save_animation_curves(root, fixture);
        auto z = animation_context(true);
        CookedAnimationOptions options;
        options.profile = CookedAnimationProfile::unreal_5_3_windows;
        options.curve_settings =
            CookedAnimationDependency{Container::open(root / "curve-settings.utoc"), 0, z, {}};
        options.max_output_bytes = 1;
        refused([&] { read_zen_animation(Container::open(root / "animation.utoc"), 0, z, {}, options); });
        options.max_output_bytes = 256ull * 1024 * 1024;
        options.max_read_bytes = fixture.animation.animation.storage.package.data.size();
        refused([&] { read_zen_animation(Container::open(root / "animation.utoc"), 0, z, {}, options); });
        options.max_read_bytes = 512ull * 1024 * 1024;
        options.curve_channels.max_read_bytes = 1;
        refused([&] { read_zen_animation(Container::open(root / "animation.utoc"), 0, z, {}, options); });
        options.curve_channels.max_read_bytes = 128ull * 1024 * 1024;
        options.curve_channels.max_keys = 1;
        refused([&] { read_zen_animation(Container::open(root / "animation.utoc"), 0, z, {}, options); });
        options.curve_channels.max_keys = 4000000;
        options.curve_settings->zen.control.cancelled = [] { return true; };
        refused([&] { read_zen_animation(Container::open(root / "animation.utoc"), 0, z, {}, options); });
        options.curve_settings->zen.control = {};
        options.curve_channels.control.cancelled = [] { return true; };
        refused([&] { read_zen_animation(Container::open(root / "animation.utoc"), 0, z, {}, options); });
        CookedAnimationCurveDiffOptions limited;
        limited.max_output_bytes = 1;
        refused([&] { compare_cooked_animation_curves(original, original, limited); });
        limited = {};
        limited.max_curve_pairs = 0;
        refused([&] { compare_cooked_animation_curves(original, original, limited); });
        limited = {};
        limited.control.cancelled = [] { return true; };
        refused([&] { compare_cooked_animation_curves(original, original, limited); });
        const auto resolved = fs::weakly_canonical(root);
        check(resolved.parent_path() == fs::weakly_canonical(fs::temp_directory_path()),
              "Cleanup stays in created temp directory");
        fs::remove_all(resolved);
        std::cout << checks << " whole cooked curve checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "check " << checks << ": " << e.what() << " fixtures " << root << '\n';
        return 1;
    }
}
