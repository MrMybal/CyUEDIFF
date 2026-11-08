#include <cyuediff/cooked_skeletal.hpp>
#include "cooked_skeletal_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
unsigned checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void refused(F run, const char *code) {
    try {
        run();
    } catch (const Error &e) {
        check(e.code == code, (std::string("Wrong refusal ") + e.code + ": " + e.what()).c_str());
        return;
    }
    throw std::runtime_error("Invalid profile accepted");
}
} // namespace
int main(int argc, char **argv) {
    const auto root = fs::temp_directory_path() /
                      ("cyuediff-cooked-profiles-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root);
        CookedSkeletalOptions o;
        o.profile = CookedSkeletalProfile::unreal_5_3_windows;
        o.keep_mobile_min_lod_setting = false;
        o.has_vertex_colors = false;
        const auto context = skeletal_context();
        auto read = [&](const SkeletalFixture &f, const CookedSkeletalOptions &options) {
            texture_container(root / "mesh", f.storage);
            return read_zen_skeletal_mesh(Container::open(root / "mesh.utoc"), 0, context, {}, options);
        };
        auto make = [](bool wb, bool ww, const std::vector<SkeletalWeightProfileSpec> &specs,
                       bool variable = false) {
            return skeletal_profiles(cooked_skeletal_fixture(variable, wb, ww), wb, ww, specs);
        };
        SkeletalWeightProfileSpec sparse;
        sparse.records = {{{1, 192}, {0, 63}}};
        sparse.vertices = {{0, 0}, {2, 0}};
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            auto out = fs::u8path(argv[2]);
            fs::create_directories(out);
            texture_container(out / "profiles", make(false, false, {sparse}).storage);
            auto changed = sparse;
            changed.records[0][0][1] = 193;
            changed.records[0][1][1] = 62;
            texture_container(out / "profiles-changed", make(false, false, {changed}).storage);
            auto renamed = make(false, false, {changed});
            const std::string source = "/Game/Texture", target = "/Game/Altered";
            auto &bytes = renamed.storage.package.data;
            const auto at = std::search(bytes.begin(), bytes.end(), source.begin(), source.end());
            check(at != bytes.end(), "Original package name exists");
            std::copy(target.begin(), target.end(), at);
            texture_container(out / "profiles-renamed-changed", renamed.storage);
            texture_container(out / "profiles-streamed",
                              cooked_skeletal_streaming_fixture(true, false, false, false, false, false,
                                                                false, false, {sparse})
                                  .storage);
            fs::remove_all(root);
            return 0;
        }
        for (bool wb : {false, true})
            for (bool ww : {false, true}) {
                auto spec = sparse;
                if (ww) {
                    spec.records[0][0][1] = 49345;
                    spec.records[0][1][1] = 16190;
                }
                auto f = make(wb, ww, {spec});
                auto out = read(f, o);
                auto &lod = out.lods[0];
                const auto &p = lod.weight_profiles.at(0);
                check(p.name == "Texture_0" && p.skin_profile_key == "profile:Texture_0" &&
                          p.vertex_to_record.size() == 2,
                      "Exact FName/sparse record provenance");
                check(p.overrides.at(0).at(0).bone == 0 &&
                          p.overrides.at(0).at(0).raw_weight == (ww ? 49345 : 192 * 257),
                      "Stored slots resolve section bones and exact weight width");
                const auto &skin = *lod.geometry->skin;
                const auto &profile = skin.profiles.at(p.skin_profile_key);
                check(profile.at(0).at(0).raw_weight == p.overrides.at(0).at(0).raw_weight &&
                          profile.at(1).at(0).raw_weight == 128 * 257,
                      "Sparse profile preserves base weights on unmodified vertices");
                check(profile.at(2).at(0).raw_weight == profile.at(0).at(0).raw_weight,
                      "Shared profile records resolve for each owning vertex");
                auto metadata = o;
                metadata.geometry = false;
                const auto info = read(f, metadata);
                check(!info.lods[0].geometry &&
                          info.lods[0].weight_profiles[0].overrides.at(0).at(0).raw_weight ==
                              p.overrides.at(0).at(0).raw_weight,
                      "Metadata-only reader retains typed replacement slots");
                check(lod.buffers["profileNameBytes"] == 8 &&
                          lod.buffers["serializedBytes"].get<std::uint64_t>() -
                                  lod.buffers["declaredBuffersSize"].get<std::uint64_t>() ==
                              8,
                      "Inline native counter excludes serialized FName bytes");
                const auto json = out.to_json(true);
                check(json["lods"][0]["weightProfiles"][0]["overrides"]["0"][0]["bone"] == 0,
                      "Focused original raw overrides are public JSON");
                check(!out.to_json()["lods"][0]["weightProfiles"][0].contains("overrides"),
                      "Default metadata JSON is bounded separately from full values");
                for (bool external : {false, true}) {
                    auto stream = cooked_skeletal_streaming_fixture(external, false, wb, ww, false, false,
                                                                    false, false, {spec});
                    auto loaded = read(stream, o);
                    check(loaded.lods.size() == 2 &&
                              loaded.lods[0]
                                      .geometry->skin->profiles.at(p.skin_profile_key)
                                      .at(0)
                                      .at(0)
                                      .raw_weight == p.overrides.at(0).at(0).raw_weight,
                          "Inline/external bulk profiles match cached FNames and stored values");
                    check(loaded.lods[0].bulk["availability"]["weightProfiles"] == Json::array({"Texture_0"}),
                          "Cached profile declaration preserves identity");
                }
            }
        for (bool external : {false, true}) {
            auto wide = cooked_skeletal_streaming_fixture(external, false, false, false, false, false, false,
                                                          false, {sparse}, true);
            auto loaded = read(wide, o);
            check(loaded.lods[0].weight_profiles[0].name == "Texture_0" &&
                      loaded.lods[0].buffers["profileNameBytes"] == 24,
                  "Streamed FMemoryArchive UTF16 profile names differ from inline FNames");
            auto capped = o;
            capped.max_profile_name_bytes = 4;
            refused([&] { read(wide, capped); }, "cooked-skeletal-limit");
            auto &bytes = external ? wide.storage.bulk : wide.storage.package;
            bytes.data[wide.profile + 8] = 0;
            bytes.data[wide.profile + 9] = 0xdc;
            refused([&] { read(wide, o); }, "cooked-skeletal-layout");
        }
        auto bad_counter = make(false, false, {sparse});
        std::uint32_t counted;
        std::memcpy(&counted, bad_counter.storage.package.data.data() + bad_counter.size, 4);
        bad_counter.storage.package.patch(static_cast<int>(bad_counter.size), static_cast<int>(counted + 8));
        refused([&] { read(bad_counter, o); }, "cooked-skeletal-layout");
        auto partial = sparse;
        partial.records = {{{0, 200}}};
        partial.vertices = {{1, 0}};
        for (bool variable : {false, true}) {
            auto out = read(make(false, false, {partial}, variable), o);
            const auto &v = out.lods[0].geometry->skin->profiles.at("profile:Texture_0").at(1);
            check(v.size() == 2 && v[0].bone == 1 && v[0].raw_weight == 200 * 257 && v[1].bone == 0 &&
                      v[1].raw_weight == 127 * 257,
                  "Partial slot replacement retains exact base tail without normalization");
        }
        auto clear = sparse;
        clear.records = {{{0, 0}, {0, 0}}};
        clear.vertices = {{0, 0}};
        auto cleared = read(make(false, false, {clear}), o);
        check(cleared.lods[0].geometry->skin->profiles.at("profile:Texture_0").at(0).empty() &&
                  cleared.lods[0].weight_profiles[0].overrides.at(0).size() == 2,
              "Zero padding retained in stored slots and omitted from semantic skin");
        SkeletalWeightProfileSpec empty;
        auto no_changes = read(make(false, false, {empty}), o);
        check(no_changes.lods[0].geometry->skin->profiles.at("profile:Texture_0").at(1).size() == 2,
              "Empty profile inherits complete stored base weights");
        auto other = sparse;
        other.number = 2;
        auto multiple = read(make(false, false, {sparse, other}), o);
        check(multiple.lods[0].weight_profiles.size() == 2 &&
                  multiple.lods[0].geometry->skin->profiles.count("profile:Texture_1"),
              "Several distinct suffixed profile names remain separate");
        refused([&] { read(make(false, false, {sparse, sparse}), o); }, "cooked-skeletal-layout");
        for (unsigned bad = 0; bad < 8; ++bad) {
            auto spec = sparse;
            switch (bad) {
            case 0:
                spec.name = 99;
                break;
            case 1:
                spec.vertices = {{3, 0}};
                break;
            case 2:
                spec.vertices = {{0, 1}};
                break;
            case 3:
                spec.vertices = {{0, 0}, {0, 0}};
                break;
            case 4:
                spec.records[0][0][0] = 2;
                break;
            case 5:
                spec.records[0][0][0] = 0;
                break;
            case 6:
                spec.slots = 3;
                break;
            case 7:
                spec.slots = 0;
                break;
            }
            refused([&] { read(make(false, false, {spec}), o); }, "cooked-skeletal-layout");
        }
        auto truncated = make(false, false, {sparse});
        const auto original = truncated.storage.package.data;
        for (std::size_t end = truncated.profile + 4; end < truncated.morph; ++end) {
            truncated.storage.package.data.assign(original.begin(), original.begin() + end);
            skeletal_export_size(truncated);
            refused([&] { read(truncated, o); }, "cooked-skeletal-layout");
        }
        for (unsigned mode = 0; mode < 3; ++mode) {
            auto options = o;
            if (mode == 0)
                options.max_weight_profiles = 0;
            else if (mode == 1)
                options.max_profile_overrides = 1;
            else
                options.max_output_bytes = 1000;
            refused([&] { read(make(false, false, {sparse}), options); }, "cooked-skeletal-limit");
        }
        auto cached = cooked_skeletal_streaming_fixture(true, false, false, false, false, false, false, false,
                                                        {sparse});
        cached.storage.package.patch(static_cast<int>(cached.availability + 69),
                                     1); // first cached FName index
        refused([&] { read(cached, o); }, "cooked-skeletal-layout");
        for (const char *phase : {"cooked-skeletal-profiles", "cooked-skeletal-profile-weights"}) {
            bool stop = false;
            auto options = o;
            options.control.cancelled = [&] { return stop; };
            options.control.progress = [&](const auto &p) {
                if (p.phase == phase)
                    stop = true;
            };
            refused([&] { read(make(false, false, {sparse}), options); }, "cancelled");
        }
        check(fs::weakly_canonical(root).parent_path() == fs::weakly_canonical(fs::temp_directory_path()),
              "Fixture cleanup stays in temporary root");
        fs::remove_all(root);
        std::cout << checks << " cooked skeletal profile checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << checks << ": " << e.what() << " fixtures " << root << '\n';
        return 1;
    }
}
