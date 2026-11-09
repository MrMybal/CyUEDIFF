#pragma once
#include "physical_cloth_fixture.hpp"
namespace cyuediff::test {
inline Package source_physical_fixture() {
    const auto mapped = physical_fixture();
    Package p;
    p.summary.ue4_version = 522;
    p.summary.ue5_version = 1009;
    p.summary.package_name = "/Game/Test";
    p.source_type_context = {
        {"sha256", std::string(64, 'b')}, {"profileMatched", true}, {"defaultsResolved", false}};
    p.decoder_context_sha256 = std::string(64, 'd');
    p.fingerprints = {{".uasset", std::string(64, 'a')}};
    const std::map<std::string, std::string> owners = {
        {"LodData", PhysicalLod},
        {"PhysicalMeshData", PhysicalData},
        {"BoneData", PhysicalBone},
        {"WeightMaps", "/Script/ClothingSystemRuntimeCommon.PointWeightMap"},
        {"EuclideanTethers", "/Script/ClothingSystemRuntimeCommon.ClothTetherData"},
        {"GeodesicTethers", "/Script/ClothingSystemRuntimeCommon.ClothTetherData"},
        {"AssetGuid", "/Script/CoreUObject.Guid"},
        {"Vertices", "/Script/CoreUObject.Vector3f"},
        {"Normals", "/Script/CoreUObject.Vector3f"},
        {"EdgeStiffnessWeighted", "/Script/ChaosCloth.ChaosClothWeightedValue"}};
    auto structure = [](const std::string &owner) {
        return Json{{"name", "StructProperty"}, {"schema", owner}};
    };
    std::function<Json(const Json &)> convert = [&](const Json &j) -> Json {
        if (j.is_array()) {
            Json out = Json::array();
            for (const auto &v : j)
                out.push_back(convert(v));
            return out;
        }
        if (!j.is_object())
            return j;
        if (j.contains("type") && j["type"].is_string())
            return Json{{"type", {{"name", j["type"]}}}, {"value", convert(j.at("value"))}};
        if (j.contains("properties") && j.contains("native"))
            return {{"decoder", "original.source.hybrid/1"},
                    {"prefix", "tagged-properties"},
                    {"data", {{"properties", convert(j["properties"])}, {"native", convert(j["native"])}}}};
        if (j.value("kind", Json{}) == "export")
            return j.at("path");
        Json out = Json::object();
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.key() == "replace")
                continue;
            std::string key = it.key();
            if (key.rfind("/Script/", 0) == 0)
                key = key.substr(key.find_last_of('.') + 1);
            if (key.size() > 3 && key.compare(key.size() - 3, 3, "[0]") == 0)
                key.resize(key.size() - 3);
            out[key] = convert(it.value());
            auto owner = owners.find(key);
            if (owner != owners.end() && out[key].contains("type")) {
                auto type = structure(owner->second);
                if (key == "LodData" || key == "BoneData" || key == "Vertices" || key == "Normals")
                    type = {{"name", "ArrayProperty"}, {"parameters", Json::array({type})}};
                else if (key == "WeightMaps")
                    type = {{"name", "MapProperty"},
                            {"parameters", Json::array({Json{{"name", "UInt32Property"}}, type})}};
                out[key]["resolvedType"] = type;
            }
        }
        return out;
    };
    for (const auto &row : mapped.exports) {
        ObjectExport e;
        e.path = row.path;
        e.class_name = row.class_name;
        e.flags = row.flags;
        e.coverage = "properties";
        e.properties = convert(row.property_read["properties"]);
        p.exports.push_back(std::move(e));
    }
    return p;
}
inline Json &source_physical_fields(Package &p, unsigned lod = 0) {
    return p.exports[0]
        .properties["LodData"]["value"][lod]["data"]["properties"]["PhysicalMeshData"]["value"];
}
} // namespace cyuediff::test
