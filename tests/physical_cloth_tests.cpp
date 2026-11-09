#include "physical_cloth_fixture.hpp"
#include <iostream>
#include <limits>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool v, const char *m) {
    ++checks;
    if (!v)
        throw std::runtime_error(m);
}
template <class F> void rejects(F f, const char *code) {
    try {
        f();
    } catch (const Error &e) {
        check(e.code == code, e.what());
        return;
    }
    throw std::runtime_error("Expected physical rejection");
}
PhysicalClothOptions options() {
    PhysicalClothOptions o;
    o.profile = PhysicalClothProfile::unreal_5_3_windows;
    return o;
}
} // namespace
int main() {
    try {
        const auto p = physical_fixture();
        const auto original = p.to_json();
        auto o = options();
        const auto a = read_physical_cloth(p, o);
        check(a.available() && a.lods().size() == 2 && a.guid() == "00000001000000020000000300000004",
              "Typed physical identity/LODs");
        const auto full = a.to_json(true);
        check(full["lods"][0]["vertices"].size() == 3 &&
                  full["configs"][0]["values"]["DampingCoefficient"] == .125,
              "Physical/config values");
        check(a.lods()[0].geometry &&
                  a.lods()[0].geometry->triangles[0].instances == std::array<std::uint32_t, 3>{0, 1, 2},
              "Physical topology projection");
        check(a.lods()[0].bounds == Json({{"min", {0.f, 0.f, 0.f}}, {"max", {1.f, 1.f, 0.f}}}),
              "Stored physical bounds");
        const auto v = a.vertex(0, 1);
        check(v["position"][0] == 1 && v["inverseMass"] == 1 && v["boneData"]["indices"][11] == 65535,
              "Focused physical/bone values");
        check(a.transition(1, true, 0)["sourceLOD"] == 0 &&
                  a.transition(0, false, 0)["sourceBoundsVerified"] == true,
              "Adjacent physical LOD binding");
        check(a.wrap_transition(1, true)[0] == std::array<float, 3>{0, 0, .75f},
              "Selected Chaos normal-distance algebra is not an invented normalized blend");
        check(compare_physical_cloth(a, a)["status"] == "identical", "Self typed comparison");
        auto other = p;
        physical_fields(other)[std::string(PhysicalData) + ".Vertices"]["value"][1][0] = 1.25f;
        other.exports[1]
            .property_read["properties"]["/Script/ChaosCloth.ChaosClothConfig.DampingCoefficient"]["value"] =
            .25f;
        auto mapping = physical_mapping();
        mapping.position[3] = .5f;
        physical_lod(other, 1)["native"]["transitionUp"][2] = cloth_record(mapping);
        const auto b = read_physical_cloth(other, o);
        const auto diff = compare_physical_cloth(a, b);
        check(diff["status"] == "changed" && diff["complete"].get<bool>(),
              "Typed difference complete for known fields");
        bool geometry = false, config = false, transition = false;
        for (const auto &row : diff["changes"]) {
            const auto path = row["path"].get<std::string>();
            geometry = geometry || path == "/lods/0/vertices/1/0";
            config = config || path.find("DampingCoefficient") != std::string::npos;
            transition = transition || path == "/lods/1/transitionUp/values/2/position/3";
        }
        check(geometry && config && transition,
              "Named physical geometry/configuration/transition differences");
        auto padding = p;
        mapping = physical_mapping();
        mapping.padding = 123;
        physical_lod(padding, 1)["native"]["transitionUp"][0] = cloth_record(mapping);
        check(compare_physical_cloth(a, read_physical_cloth(padding, o))["changes"].empty(),
              "Nonsemantic alignment padding stays out of named differences");
        check(read_physical_cloth(padding, o).transition(1, true, 0)["padding"] == 123,
              "Raw alignment bits remain available");
        auto metadata = p;
        physical_properties(metadata).erase(
            std::string("/Script/ClothingSystemRuntimeInterface.ClothingAssetBase.AssetGuid"));
        const auto missing = read_physical_cloth(metadata, o);
        check(!missing.available() && missing.lods().empty() &&
                  compare_physical_cloth(a, missing)["status"] == "not-comparable",
              "Missing mapped scope is not an empty removed asset");
        auto unknown = p;
        physical_properties(unknown).erase(std::string(PhysicalRoot) + ".ReferenceBoneIndex");
        physical_fields(unknown).erase(std::string(PhysicalData) + ".MaxBoneWeights");
        auto partial = compare_physical_cloth(a, read_physical_cloth(unknown, o));
        check(!partial["complete"].get<bool>(), "Missing defaults retain incomplete comparison");
        for (const auto &row : partial["changes"])
            check(row["path"].get<std::string>().find("ReferenceBoneIndex") == std::string::npos &&
                      row["path"].get<std::string>().find("MaxBoneWeights") == std::string::npos,
                  "Unknown fields are not removed values");
        auto renamed = p;
        renamed.exports[0].path += "Renamed";
        const auto renamed_asset = read_physical_cloth(renamed, o);
        check(compare_physical_cloth(a, renamed_asset)["status"] == "not-comparable",
              "Different identities need explicit pairing");
        PhysicalClothComparisonOptions pair;
        pair.explicit_object_pair = true;
        check(compare_physical_cloth(a, renamed_asset, pair)["status"] == "identical",
              "Explicit named object pair");
        auto delta = p;
        auto &map = physical_fields(delta)[std::string(PhysicalData) + ".WeightMaps"]["value"];
        map["replace"] = false;
        map["removed"] = {{"1", 1}};
        map["entries"] = Json::object();
        const auto delta_asset = read_physical_cloth(delta, o);
        check(delta_asset.to_json(true)["lods"][0]["removedWeightMapEntries"] == Json::array({1}),
              "Removed map identities are preserved without invented defaults");
        const auto delta_diff = compare_physical_cloth(a, delta_asset);
        check(delta_diff["complete"] == false, "Physical map deltas do not certify absent defaults");
        for (const auto &row : delta_diff["changes"])
            check(row["path"].get<std::string>().find("weightMaps/1") == std::string::npos,
                  "Unknown map defaults are not removed values");
        auto no_geometry = o;
        no_geometry.geometry = false;
        const auto no_mesh = read_physical_cloth(p, no_geometry);
        check(!no_mesh.lods()[0].geometry && compare_physical_cloth(a, no_mesh)["changes"].empty(),
              "Preparation choices are not stored data changes");
        for (std::size_t n = 0; n < 128; ++n) {
            auto broken = p;
            physical_lod(broken, 1)["native"]["transitionUp"][0] =
                cloth_record(physical_mapping()).substr(0, n);
            rejects([&] { read_physical_cloth(broken, o); }, "physical-cloth-value");
        }
        for (const auto *field :
             {"Vertices", "Normals", "Indices", "InverseMasses", "BoneData", "SelfCollisionIndices"}) {
            auto bad = p;
            physical_fields(bad)[std::string(PhysicalData) + "." + field]["type"] = "BoolProperty";
            rejects([&] { read_physical_cloth(bad, o); }, "physical-cloth-value");
        }
        auto bad = p;
        physical_fields(bad)[std::string(PhysicalData) + ".Indices"]["value"][0] = 3;
        rejects([&] { read_physical_cloth(bad, o); }, "physical-cloth-value");
        bad = p;
        physical_fields(bad)[std::string(PhysicalData) + ".Normals"]["value"].erase(0);
        rejects([&] { read_physical_cloth(bad, o); }, "physical-cloth-value");
        bad = p;
        physical_fields(bad)[std::string(PhysicalData) + ".InverseMasses"]["value"][0] = -1.f;
        rejects([&] { read_physical_cloth(bad, o); }, "physical-cloth-value");
        bad = p;
        mapping = physical_mapping();
        mapping.source_indices[0] = 3;
        physical_lod(bad, 1)["native"]["transitionUp"][0] = cloth_record(mapping);
        rejects([&] { read_physical_cloth(bad, o); }, "physical-cloth-value");
        bad = p;
        mapping = physical_mapping();
        mapping.position[0] = std::numeric_limits<float>::quiet_NaN();
        physical_lod(bad, 1)["native"]["transitionUp"][0] = cloth_record(mapping);
        rejects([&] { read_physical_cloth(bad, o); }, "cloth-layout");
        mapping.source_indices[3] = 65535;
        physical_lod(bad, 1)["native"]["transitionUp"][0] = cloth_record(mapping);
        const auto inactive = read_physical_cloth(bad, o);
        check(inactive.transition(1, true, 0)["position"][0].contains("nonFiniteBits"),
              "Unused nonfinite coefficients retain exact bits");
        rejects([&] { inactive.wrap_transition(1, true); }, "physical-cloth-unavailable");
        bad = p;
        mapping = physical_mapping();
        mapping.source_indices[3] = 65535;
        mapping.weight = .5f;
        physical_lod(bad, 1)["native"]["transitionUp"][0] = cloth_record(mapping);
        check(read_physical_cloth(bad, o).wrap_transition(1, true)[0][2] == .75f,
              "Chaos transition does not apply the renderer flag or weight");
        bad = p;
        physical_lod(bad, 0)["native"]["transitionUp"] =
            Json::array({cloth_record(mapping), cloth_record(mapping), cloth_record(mapping)});
        const auto boundary = read_physical_cloth(bad, o);
        check(!boundary.lods()[0].up.source_lod && boundary.lods()[0].up.records.size() == 3 &&
                  !boundary.lods()[0].up.wrap_available,
              "Unused boundary records remain explicitly unbound");
        auto low = o;
        low.max_vertices = 5;
        rejects([&] { read_physical_cloth(p, low); }, "limit");
        low = o;
        low.max_model_bytes = 1;
        rejects([&] { read_physical_cloth(p, low); }, "limit");
        low = o;
        low.max_values = 1;
        rejects([&] { read_physical_cloth(p, low); }, "limit");
        low = o;
        low.max_text_bytes = 1;
        rejects([&] { read_physical_cloth(p, low); }, "limit");
        low = o;
        low.max_transitions = 5;
        rejects([&] { read_physical_cloth(p, low); }, "limit");
        low = o;
        low.max_tethers = 3;
        rejects([&] { read_physical_cloth(p, low); }, "limit");
        low = o;
        low.max_report_values = 3;
        const auto bounded = read_physical_cloth(p, low);
        rejects([&] { bounded.to_json(true); }, "limit");
        check(bounded.vertex(0, 0)["position"].size() == 3,
              "Focused queries remain independent of full report limits");
        PhysicalClothComparisonOptions small;
        small.max_values = 2;
        rejects([&] { compare_physical_cloth(a, b, small); }, "limit");
        small = {};
        small.max_changes = 1;
        rejects([&] { compare_physical_cloth(a, b, small); }, "limit");
        OperationControl cancel;
        cancel.cancelled = [] { return true; };
        low = o;
        low.control = cancel;
        rejects([&] { read_physical_cloth(p, low); }, "cancelled");
        rejects([&] { a.to_json(true, cancel); }, "cancelled");
        rejects([&] { a.wrap_transition(1, true, cancel); }, "cancelled");
        ClothSection section;
        section.section = 4;
        section.first_vertex = 10;
        section.vertices = 3;
        section.asset_index = 0;
        section.asset_lod = 0;
        section.asset_guid = {1, 2, 3, 4};
        section.lod_biases = {{physical_mapping(), physical_mapping(), physical_mapping()}};
        check(verify_cloth_source_bounds(section, 0, a, 0)["sourceIdentityVerified"] == true,
              "Stored base GUID/LOD binds physical vertex bounds");
        auto zero_guid = p;
        physical_properties(
            zero_guid)["/Script/ClothingSystemRuntimeInterface.ClothingAssetBase.AssetGuid"]["value"] =
            std::string(32, '0');
        auto zero_section = section;
        zero_section.asset_guid = {};
        const auto unregistered = read_physical_cloth(zero_guid, o);
        rejects([&] { verify_cloth_source_bounds(zero_section, 0, unregistered, 0); },
                "physical-cloth-binding");
        auto wrong = section;
        wrong.asset_guid[0] = 2;
        rejects([&] { verify_cloth_source_bounds(wrong, 0, a, 0); }, "physical-cloth-binding");
        wrong = section;
        wrong.lod_biases[0][0].source_indices[0] = 3;
        rejects([&] { verify_cloth_source_bounds(wrong, 0, a, 0); }, "physical-cloth-binding");
        wrong.lod_biases[0][0].source_indices[3] = 65535;
        check(verify_cloth_source_bounds(wrong, 0, a, 0)["inactiveRecords"] == 1,
              "Renderer inactive mappings do not dereference physical indices");
        section.lod_biases.push_back(section.lod_biases[0]);
        rejects([&] { verify_cloth_source_bounds(section, 1, a, 1); }, "physical-cloth-binding");
        const auto explicit_bound = verify_cloth_source_bounds(section, 1, a, 1, true);
        check(explicit_bound["sourceBoundsVerified"] == true &&
                  explicit_bound["sourceIdentityVerified"] == false,
              "Biased sources are explicit and never inferred by adding LOD indices");
        check(p.to_json() == original, "Input mapped package remains immutable");
        std::cout << checks << " physical cloth checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
