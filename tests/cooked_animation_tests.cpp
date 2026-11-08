#include <cyuediff/cooked_animation.hpp>
#include "cooked_animation_fixture.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool b, const char *s) {
    ++checks;
    if (!b)
        throw std::runtime_error(s);
}
template <class F> void refused(F f, const char *code) {
    try {
        f();
    } catch (const Error &e) {
        if (e.code != code)
            std::cerr << e.code << ": " << e.what() << '\n';
        check(e.code == code, "Expected cooked animation refusal");
        return;
    }
    throw std::runtime_error("Invalid cooked animation accepted");
}
CookedAnimationOptions options(const fs::path &root, const ZenReadOptions &z) {
    CookedAnimationOptions o;
    o.profile = CookedAnimationProfile::unreal_5_3_windows;
    o.bone_settings = CookedAnimationDependency{Container::open(root / "settings.utoc"), 0, z, {}};
    o.skeleton = CookedAnimationDependency{Container::open(root / "skeleton.utoc"), 0, z, {}};
    return o;
}
CookedAnimation read(const fs::path &root, const ZenReadOptions &z, const CookedAnimationOptions &o) {
    return read_zen_animation(Container::open(root / "animation.utoc"), 0, z, {}, o);
}
void size(AnimationPackage &p, std::size_t n) {
    auto &b = p.storage.package;
    const auto size = std::uint64_t(n - p.storage.body);
    b.data.resize(n);
    std::memcpy(b.data.data() + p.exports + 8, &size, 8);
}
} // namespace
int main(int argc, char **argv) {
    const auto root = fs::temp_directory_path() /
                      ("cyuediff-cooked-animation-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root);
        auto z = animation_context();
        auto f = cooked_animation_fixture();
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            auto target = fs::u8path(argv[2]);
            fs::create_directories(target);
            save_animation(target, f);
            animation_scripts().save(target / "scripts.bin");
            auto script_toc = zen_toc(animation_scripts(), 5);
            script_toc.save(target / "global.utoc");
            animation_scripts().save(target / "global.ucas");
            animation_schema().save(target / "mappings.usmap");
            Json ctx = {{"format", "cyuediff.zen-schema/1"},
                        {"profile", "ue5.3"},
                        {"usmap", "mappings.usmap"},
                        {"sha256", sha256_bytes(animation_schema().data)},
                        {"bindings", Json::object()}};
            for (const auto &name : {"AnimSequence", "Skeleton", "AnimBoneCompressionSettings"}) {
                auto s = "/Script/Engine." + std::string(name);
                ctx["bindings"][s] = {{"schema", s}, {"offset", 0}};
            }
            std::ofstream(target / "context.json") << ctx.dump(2);
            auto changed = f;
            float v = 14;
            std::memcpy(changed.animation.storage.package.data.data() + changed.buffer + 16, &v, 4);
            texture_container(target / "changed", changed.animation.storage);
            auto external = cooked_animation_fixture(true, true);
            texture_container(target / "external", external.animation.storage);
            external.animation.storage.bulk.data.clear();
            texture_container(target / "missing", external.animation.storage);
            auto nointerp = cooked_animation_fixture(false, false, false);
            texture_container(target / "no-interpolation", nointerp.animation.storage);
            return 0;
        }
        save_animation(root, f);
        auto o = options(root, z);
        auto a = read(root, z, o);
        auto data = a.to_json(true);
        check(a.decoded() && a.skeleton_bound() && a.frame_count() == 5, "Decoded bound animation");
        check(data["tail"]["bytes"] == 0 && data["curves"]["status"] == "empty",
              "Exact native boundary and empty curves");
        check(data["boneCodec"]["codec"]["class"] == "/Script/Engine.AnimCompress_PerTrackCompression",
              "Class verified independently of instance name");
        check(data["boneCodec"]["handle"] == "Instance_7", "Stored codec instance handle");
        auto sample = a.sample(2.5);
        check(sample["ready"] == true && sample["identitiesComplete"] == true, "Named fractional sample");
        check(sample["tracks"]["name:Child"]["positions"][0] == 7, "Uniform fractional translation");
        check(sample["tracks"]["name:Child"]["scales"] == Json({1, 1, 1}),
              "Absent scale table retains identity");
        check(!sample["tracks"].contains("name:Root"),
              "Missing track does not borrow Skeleton reference pose");
        check(!sample["skeleton"].contains("bones") && !sample["skeleton"].contains("retargetSources"),
              "Samples do not duplicate full Skeleton payload");
        check(a.sample_track(0, 2.5)["positions"] == sample["tracks"]["name:Child"]["positions"],
              "Focused named sample equals full projection");
        check(a.catalog()["skeleton"]["bones"][1]["name"] == "Child" &&
                  !a.catalog()["skeleton"]["bones"][1].contains("translation"),
              "Lightweight catalogue keeps names without full reference poses");
        check(a.key_preview(0, 2)["translation"]["points"].size() == 2 &&
                  a.stored_key(0, CompressedAnimationChannel::translation, 1)["value"][0] == 10,
              "Focused raw keys remain available");
        auto identical = compare_cooked_animation_frames(a, a, 2.5, 2.5);
        check(identical["status"] == "identical" && identical["comparedTracks"] == 1,
              "Named stored-frame comparison");
        auto changed = compare_cooked_animation_frames(a, a, 0, .5);
        check(changed["status"] == "changed" &&
                  changed["tracks"]["name:Child"]["metrics"]["positionDistance"] == 1,
              "Independent fractional frame diff");
        auto tol = AnimationFrameDiffOptions{};
        tol.position_tolerance = 1;
        check(compare_cooked_animation_frames(a, a, 0, .5, tol)["status"] == "identical",
              "Inclusive position tolerance");
        tol.max_track_pairs = 0;
        refused([&] { compare_cooked_animation_frames(a, a, 0, 0, tol); }, "cooked-animation-limit");
        auto without_tracks = cooked_animation_fixture(false, false, true, false, false, true, true);
        save_animation(root, without_tracks);
        auto empty = read(root, z, options(root, z));
        check(empty.decoded() && empty.sample(0)["ready"] == true && empty.sample(0)["tracks"].empty(),
              "Known empty compressed track map adds no reference fallback");
        check(
            compare_cooked_animation_frames(a, empty, 0, 0)["tracks"]["name:Child"]["status"] == "removed" &&
                compare_cooked_animation_frames(empty, a, 0, 0)["tracks"]["name:Child"]["status"] == "added",
            "Verified empty map confirms stored track addition/removal");
        save_animation(root, f);
        o = options(root, z);
        auto no_skeleton = o;
        no_skeleton.skeleton.reset();
        auto numeric = read(root, z, no_skeleton);
        check(numeric.decoded() && !numeric.skeleton_bound(), "Unbound Skeleton retains numeric channels");
        check(numeric.sample(0)["numericTracks"].size() == 1 && numeric.sample(0)["tracks"].empty(),
              "No guessed names");
        auto unknown = compare_cooked_animation_frames(a, numeric, 0, 0);
        check(unknown["status"] == "incomplete" &&
                  unknown["tracks"]["name:Child"]["status"] == "unavailable" && unknown["changedTracks"] == 0,
              "Unknown binding is not removal");
        auto no_settings = o;
        no_settings.bone_settings.reset();
        auto unbound = read(root, z, no_settings);
        check(!unbound.decoded() && unbound.to_json()["unavailable"] == "bone-compression-settings-not-bound",
              "Settings dependency not guessed");
        check(unbound.to_json()["tail"]["offset"] == f.metadata, "Unknown codec metadata remains opaque");
        auto empty_map = std::make_shared<ZenSchemaContext>(*z.schema_context);
        empty_map->bindings.erase("/Script/Engine.AnimBoneCompressionSettings");
        auto unmapped = o;
        unmapped.bone_settings->zen.schema_context = empty_map;
        check(read(root, z, unmapped).to_json()["unavailable"] ==
                  "compression-settings-properties-not-decoded",
              "Class/outer alone is not codec registration proof");
        auto nested = cooked_animation_fixture(false, false, true, true);
        save_animation(root, nested);
        o = options(root, z);
        check(read(root, z, o).decoded(), "Registered nested codec uses actual outer chain");
        for (unsigned mode = 0; mode < 5; ++mode) {
            auto stream = cooked_animation_fixture(true, mode != 0);
            save_animation(root, stream, mode == 2 ? 3 : mode == 3 ? 4 : 2);
            o = options(root, z);
            if (mode == 2)
                stream.animation.storage.package.patch(stream.animation.storage.bulk_row + 24, 0x10d01);
            if (mode == 3)
                stream.animation.storage.package.patch(stream.animation.storage.bulk_row + 24, 0x11501);
            if (mode == 4) {
                stream.animation.storage.package.patch(stream.animation.storage.bulk_row + 24, 0x14501);
                std::int64_t dup = 3;
                std::memcpy(stream.animation.storage.package.data.data() + stream.animation.storage.bulk_row +
                                8,
                            &dup, 8);
            }
            texture_container(root / "animation", stream.animation.storage,
                              mode == 2 || mode == 4 ? 3
                              : mode == 3            ? 4
                                                     : 2);
            auto loaded = read(root, z, o);
            check(loaded.decoded() && loaded.sample(2.5)["tracks"]["name:Child"]["positions"][0] == 7,
                  "Inline/external/optional/mapped/duplicate bulk");
        }
        auto missing = cooked_animation_fixture(true, true);
        missing.animation.storage.bulk.data.clear();
        save_animation(root, missing);
        o = options(root, z);
        auto absent = read(root, z, o);
        check(!absent.decoded() && absent.to_json()["unavailable"] == "missing-chunk" &&
                  absent.frame_count() == 5,
              "Missing payload keeps known frame domain");
        check(compare_cooked_animation_frames(a, absent, 0, 0)["changedTracks"] == 0,
              "Missing payload is not deletion");
        auto unused = cooked_animation_fixture(true, true);
        unused.animation.storage.package.patch(unused.animation.storage.bulk_row + 24, 0x10521);
        save_animation(root, unused);
        check(read(root, z, o).to_json()["unavailable"] == "unused", "Unused bulk is not consumed");
        save_animation(root, cooked_animation_fixture(true, true), 2, true);
        refused([&] { read(root, z, o); }, "container-integrity");
        auto nointerp = cooked_animation_fixture(false, false, false);
        save_animation(root, nointerp);
        o = options(root, z);
        auto unresolved = read(root, z, o);
        check(unresolved.decoded() && !unresolved.sample(0)["ready"].get<bool>(),
              "Omitted interpolation is not a default");
        o.interpolation = CompressedAnimationInterpolation::step;
        check(read(root, z, o).sample(2.5)["tracks"]["name:Child"]["positions"][0] == 2,
              "Explicit step producer setting");
        save_animation(root, f);
        o = options(root, z);
        o.interpolation = CompressedAnimationInterpolation::step;
        refused([&] { read(root, z, o); }, "cooked-animation-context");
        auto curved = cooked_animation_fixture(false, false, true, false, true);
        save_animation(root, curved);
        o = options(root, z);
        auto curve_metadata = read(root, z, o).to_json();
        check(curve_metadata["boneChannelsDecoded"] == true &&
                  curve_metadata["curves"]["status"] == "opaque" &&
                  curve_metadata["curves"]["names"] == Json({"CurveOne"}) &&
                  curve_metadata["curves"]["bytes"] == 3,
              "Compressed curves retain names/hash without fabricated values");
        auto curve_only = cooked_animation_fixture(false, false, true, false, true, false);
        save_animation(root, curve_only);
        o = options(root, z);
        auto no_bones = read(root, z, o);
        check(!no_bones.decoded() && !no_bones.frame_count() &&
                  no_bones.to_json()["boneCodec"]["status"] == "absent" &&
                  no_bones.to_json()["tail"]["bytes"] == 0 &&
                  no_bones.to_json()["curves"]["status"] == "opaque",
              "Curve-only sequence has no invented compressed frame metadata");
        auto wide = f;
        auto &wide_bytes = wide.animation.storage.package.data;
        std::int32_t length;
        std::memcpy(&length, wide_bytes.data() + wide.handle, 4);
        const std::string text(wide_bytes.begin() + static_cast<std::ptrdiff_t>(wide.handle + 4),
                               wide_bytes.begin() +
                                   static_cast<std::ptrdiff_t>(wide.handle + 4 + length - 1));
        Bytes utf16;
        utf16.i(-length);
        for (char c : text)
            utf16.put<std::uint16_t>(std::uint8_t(c));
        utf16.put<std::uint16_t>(0);
        wide_bytes.erase(wide_bytes.begin() + static_cast<std::ptrdiff_t>(wide.handle),
                         wide_bytes.begin() + static_cast<std::ptrdiff_t>(wide.handle + 4 + length));
        wide_bytes.insert(wide_bytes.begin() + static_cast<std::ptrdiff_t>(wide.handle), utf16.data.begin(),
                          utf16.data.end());
        size(wide.animation, wide_bytes.size());
        save_animation(root, wide);
        o = options(root, z);
        check(read(root, z, o).decoded(), "UTF16 codec handle uses validated FString framing");
        wide.animation.storage.package.data[wide.handle + 4] = 0;
        save_animation(root, wide);
        refused([&] { read(root, z, o); }, "cooked-animation-layout");
        auto folded = f;
        folded.animation.storage.package.data[folded.handle + 4] = 'i';
        save_animation(root, folded);
        o = options(root, z);
        auto folded_metadata = read(root, z, o).to_json();
        check(folded_metadata["boneChannelsDecoded"] == true &&
                  folded_metadata["boneCodec"]["handle"] == "instance_7" &&
                  folded_metadata["boneCodec"]["canonicalHandle"] == "Instance_7",
              "Engine ASCII handle comparison preserves stored and canonical spellings");
        auto ansi = f;
        auto &ansi_settings = ansi.settings.storage.package.data;
        const std::string instance = "Instance_7";
        auto found_name =
            std::search(ansi_settings.begin(), ansi_settings.end(), instance.begin(), instance.end());
        check(found_name != ansi_settings.end(), "Original codec name fixture located");
        *found_name = 0xe9;
        ansi.animation.storage.package.data[ansi.handle + 4] = 0xe9;
        save_animation(root, ansi);
        o = options(root, z);
        refused([&] { read(root, z, o); }, "string-encoding");
        auto encoded_context = z;
        auto encoded_schema = std::make_shared<ZenSchemaContext>(*z.schema_context);
        encoded_schema->narrow_string_encoding = NarrowStringEncoding::windows_1252;
        encoded_context.schema_context = encoded_schema;
        check(read(root, encoded_context, o).decoded(),
              "Native codec text honors explicit schema character set");
        o.narrow_string_encoding = NarrowStringEncoding::ascii;
        refused([&] { read(root, encoded_context, o); }, "string-encoding");
        auto wrong_guid = f;
        wrong_guid.skeleton.storage.package.data[wrong_guid.guid] ^= 1;
        save_animation(root, wrong_guid);
        o = options(root, z);
        refused([&] { read(root, z, o); }, "cooked-animation-context");
        save_animation(root, f);
        o = options(root, z);
        o.bone_settings->container = Container::open(root / "skeleton.utoc");
        refused([&] { read(root, z, o); }, "cooked-animation-context");
        auto wrong_hash = f;
        std::uint64_t hash = 0x123;
        std::memcpy(wrong_hash.settings.storage.package.data.data() + wrong_hash.settings.exports + 56, &hash,
                    8);
        save_animation(root, wrong_hash);
        o = options(root, z);
        refused([&] { read(root, z, o); }, "cooked-animation-context");
        auto wrong_class = f;
        std::uint64_t cls = script_root + 6;
        std::memcpy(wrong_class.settings.storage.package.data.data() + wrong_class.settings.exports + 72 + 32,
                    &cls, 8);
        save_animation(root, wrong_class);
        o = options(root, z);
        check(read(root, z, o).to_json()["unavailable"] == "unsupported-compression-codec-lookup",
              "Plugin lookup is not emulated");
        cls = script_root + 5;
        std::memcpy(wrong_class.settings.storage.package.data.data() + wrong_class.settings.exports + 72 + 32,
                    &cls, 8);
        save_animation(root, wrong_class);
        o = options(root, z);
        check(read(root, z, o).to_json()["unavailable"] == "unsupported-bone-codec-class",
              "Known unsupported codec keeps opaque metadata");
        auto unregistered = f;
        unregistered.settings.storage.package.patch(unregistered.settings.storage.body + 6, 0);
        save_animation(root, unregistered);
        o = options(root, z);
        refused([&] { read(root, z, o); }, "cooked-animation-context");
        auto bad_track = f;
        bad_track.animation.storage.package.patch(bad_track.track_map, 2);
        save_animation(root, bad_track);
        o = options(root, z);
        refused([&] { read(root, z, o); }, "cooked-animation-layout");
        for (std::size_t n = f.native; n < f.animation.storage.package.data.size(); ++n) {
            auto cut = f;
            size(cut.animation, n);
            save_animation(root, cut);
            o = options(root, z);
            try {
                read(root, z, o);
                throw std::runtime_error("Truncated native animation accepted");
            } catch (const Error &) {
                ++checks;
            }
        }
        for (std::size_t n = f.skeleton_native; n < f.skeleton.storage.package.data.size(); ++n) {
            auto cut = f;
            size(cut.skeleton, n);
            save_animation(root, cut);
            o = options(root, z);
            try {
                read(root, z, o);
                throw std::runtime_error("Truncated native Skeleton accepted");
            } catch (const Error &) {
                ++checks;
            }
        }
        auto template_codec = f;
        template_codec.settings.storage.package.patch(template_codec.settings.exports + 72 + 64, 0x10);
        save_animation(root, template_codec);
        o = options(root, z);
        refused([&] { read(root, z, o); }, "cooked-animation-context");
        auto duplicate_hash = f;
        const std::uint64_t repeated = 0xabc;
        std::memcpy(duplicate_hash.settings.storage.package.data.data() + duplicate_hash.settings.exports +
                        72 + 56,
                    &repeated, 8);
        save_animation(root, duplicate_hash);
        o = options(root, z);
        refused([&] { read(root, z, o); }, "cooked-animation-context");
        auto nonfinite = f;
        const auto infinity = std::numeric_limits<double>::infinity();
        std::memcpy(nonfinite.skeleton.storage.package.data.data() + nonfinite.skeleton_native + 36,
                    &infinity, 8);
        save_animation(root, nonfinite);
        o = options(root, z);
        refused([&] { read(root, z, o); }, "cooked-animation-layout");
        auto legacy_scale = f;
        legacy_scale.animation.storage.package.data[legacy_scale.metadata + 7] = 0;
        save_animation(root, legacy_scale);
        o = options(root, z);
        check(read(root, z, o).sample(0)["tracks"]["name:Child"]["scales"] == Json({1, 1, 1}),
              "Unused legacy global scale tag does not override per-track scale semantics");
        auto bad_view = f;
        bad_view.animation.storage.package.patch(bad_view.metadata + 8, 16);
        save_animation(root, bad_view);
        o = options(root, z);
        refused([&] { read(root, z, o); }, "cooked-animation-layout");
        save_animation(root, f);
        o = options(root, z);
        auto no_mapping = z;
        no_mapping.schema_context.reset();
        refused([&] { read(root, no_mapping, o); }, "cooked-animation-context");
        auto disabled = o;
        disabled.profile = CookedAnimationProfile::unspecified;
        refused([&] { read(root, z, disabled); }, "cooked-animation-profile");
        for (unsigned mode = 0; mode < 6; ++mode) {
            auto bounded = o;
            if (mode == 0)
                bounded.max_read_bytes = f.animation.storage.package.data.size();
            if (mode == 1)
                bounded.max_output_bytes = 1;
            if (mode == 2)
                bounded.channels.max_keys = 1;
            if (mode == 3)
                bounded.channels.max_tracks = 0;
            if (mode == 4)
                bounded.max_bones = 1;
            if (mode == 5)
                bounded.max_codec_exports = 1;
            const char *code = mode == 2 ? "animation-compression-limit" : "cooked-animation-limit";
            refused([&] { read(root, z, bounded); }, code);
        }
        auto property_limited = o;
        property_limited.max_output_bytes = 8200;
        refused([&] { read(root, z, property_limited); }, "cooked-animation-limit");
        auto cancelled = o;
        cancelled.control.cancelled = [] { return true; };
        refused([&] { read(root, z, cancelled); }, "cancelled");
        auto cancelled_dependency = o;
        cancelled_dependency.bone_settings->zen.control.cancelled = [] { return true; };
        refused([&] { read(root, z, cancelled_dependency); }, "cancelled");
        bool stop = false;
        OperationControl control;
        control.cancelled = [&] { return stop; };
        control.progress = [&](const OperationProgress &) { stop = true; };
        refused([&] { a.sample(0, control); }, "cancelled");
        for (double value : {-1., 5., std::numeric_limits<double>::infinity()})
            refused([&] { a.sample(value); }, "cooked-animation-layout");
        const auto resolved = fs::weakly_canonical(root);
        check(resolved.parent_path() == fs::weakly_canonical(fs::temp_directory_path()) &&
                  resolved.filename().u8string().rfind("cyuediff-cooked-animation-", 0) == 0,
              "Cleanup stays in the created temporary fixture directory");
        fs::remove_all(resolved);
        std::cout << checks << " cooked animation checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "check " << checks << ": " << e.what() << " fixtures " << root << '\n';
        return 1;
    }
}
