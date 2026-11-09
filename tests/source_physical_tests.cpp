#include "source_physical_fixture.hpp"
#include <cyuediff/binary_layout.hpp>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool ok, const char *why) {
    ++checks;
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const Error &) {
        ++checks;
        return;
    }
    throw std::runtime_error("Expected rejected source physical data");
}
} // namespace
int main() {
    try {
        PhysicalClothOptions o;
        o.profile = PhysicalClothProfile::unreal_5_3_windows;
        auto p = source_physical_fixture();
        auto a = read_physical_cloth(p, o);
        auto c = read_physical_cloth(physical_fixture(), o);
        check(a.available() && a.lods().size() == 2 && a.lods()[0].geometry && a.guid() == c.guid(),
              "Source fields build an owned physical model with base geometry");
        auto source = a.to_json(true), cooked = c.to_json(true);
        check(source.at("lods").at(0).at("vertices").size() == 3 &&
                  source.at("lods").at(0).at("vertices") == cooked.at("lods").at(0).at("vertices") &&
                  source.at("lods").at(0).at("boneData").size() == 3 &&
                  source.at("lods").at(0).at("boneData") == cooked.at("lods").at(0).at("boneData"),
              "Source geometry and twelve-slot bones match equivalent original cooked fields");
        check(source["provenance"]["storage"] == "tagged-source" &&
                  !source["provenance"].contains("scriptObjectsSha256") &&
                  !source["configMapReplaced"].get<bool>() &&
                  !source["lods"][0]["weightMapReplaced"].get<bool>(),
              "Source provenance and unresolved map defaults are never forged as cooked replacements");
        check(a.wrap_transition(0, false) == c.wrap_transition(0, false),
              "Source native records use the same certified Chaos reference-pose operation");
        auto changed = p;
        source_physical_fields(changed)["Vertices"]["value"][0][0] = .5f;
        auto b = read_physical_cloth(changed, o);
        auto d = compare_physical_cloth(a, b);
        check(!d["changes"].empty() && !d["complete"].get<bool>(),
              "Known source geometry changes coexist with unavailable defaults");
        check(d.at("contextChanged") == false && d.at("rawPackageChanged") == false,
              "Source provenance compares the original source identities without missing cooked keys");
        auto origin = p;
        origin.decoder_context_sha256 = std::string(64, 'e');
        auto context = compare_physical_cloth(a, read_physical_cloth(origin, o));
        check(context.at("contextChanged") == true && context.at("changes").empty(),
              "Source native context changes remain distinct from stored values");
        origin = p;
        origin.fingerprints["extra"] = "different";
        auto raw = compare_physical_cloth(a, read_physical_cloth(origin, o));
        check(raw.at("rawPackageChanged") == true && raw.at("contextChanged") == false &&
                  raw.at("changes").empty(),
              "Source byte fingerprints compare independently");
        auto mixed = compare_physical_cloth(a, c);
        check(mixed.at("contextChanged") == true && mixed.at("rawPackageChanged").is_null() &&
                  mixed.at("rawPackageComparable") == false,
              "Source and cooked raw encodings never imply a comparable package hash");
        auto bad = p;
        bad.summary.ue5_version = 1013;
        rejects([&] { read_physical_cloth(bad, o); });
        bad = p;
        bad.summary.licensee_version = 1;
        rejects([&] { read_physical_cloth(bad, o); });
        bad = p;
        bad.decoder_context_sha256.clear();
        check(!read_physical_cloth(bad, o).available(), "Missing source native context stays unavailable");
        bad = p;
        bad.source_type_context["profileMatched"] = false;
        check(!read_physical_cloth(bad, o).available(), "Wrong metadata profile stays unavailable");
        bad = p;
        bad.exports[0].coverage = "opaque";
        check(!read_physical_cloth(bad, o).available(),
              "Opaque source export is not an empty physical asset");
        bad = p;
        bad.exports[0].properties["LodData"].erase("value");
        bad.exports[0].properties["LodData"]["opaque"] = {{"reason", "limit"}};
        check(!read_physical_cloth(bad, o).available(),
              "Unavailable required source fields preserve unknown coverage");
        bad = p;
        bad.exports[0].properties["LodData"]["resolvedType"]["parameters"][0]["schema"] =
            "/Script/Foreign.ClothLODDataCommon";
        rejects([&] { read_physical_cloth(bad, o); });
        bad = p;
        source_physical_fields(bad)["Vertices"]["value"][0][0] = .1;
        rejects([&] { read_physical_cloth(bad, o); });
        bad = p;
        source_physical_fields(bad)["WeightMaps"]["value"]["replace"] = true;
        rejects([&] { read_physical_cloth(bad, o); });
        bad = p;
        bad.exports[0].properties["LodData"]["value"][0]["prefix"] = "unknown";
        rejects([&] { read_physical_cloth(bad, o); });
        bad = p;
        source_physical_fields(bad)["GeodesicTethers"]["value"]["data"]["native"]["extra"] = 0;
        rejects([&] { read_physical_cloth(bad, o); });
        auto small = o;
        small.max_model_bytes = 16;
        rejects([&] { read_physical_cloth(p, small); });
        small = o;
        small.max_values = 1;
        rejects([&] { read_physical_cloth(p, small); });
        small = o;
        small.control.cancelled = [] { return true; };
        rejects([&] { read_physical_cloth(p, small); });
        auto profile = o;
        profile.profile = PhysicalClothProfile::unspecified;
        rejects([&] { read_physical_cloth(p, profile); });
        bad = p;
        bad.source_type_context["sha256"] = "bad";
        rejects([&] { read_physical_cloth(bad, o); });
        bad = p;
        bad.source_type_context["profileMatched"] = 1;
        rejects([&] { read_physical_cloth(bad, o); });
        bad = p;
        bad.decoder_context_sha256 = "bad";
        rejects([&] { read_physical_cloth(bad, o); });
        bad = p;
        bad.source_read_limit_failed = true;
        check(!read_physical_cloth(bad, o).available(),
              "Failed source guards do not create a specialized asset");
        auto layouts =
            BinaryLayouts::parse({{"format", "cyuediff.binary-layouts/1"}, {"layouts", Json::object()}});
        check(layouts.decoder_registry()->context_sha256() == layouts.sha256(),
              "Declarative registry captures the exact immutable catalogue fingerprint");
        std::cout << checks << " source physical checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << checks << ": " << e.what() << '\n';
        return 1;
    }
}
