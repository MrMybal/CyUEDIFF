#pragma once
#include "schema_fixture.hpp"
#include <cyuediff/physical_cloth.hpp>
#include <cyuediff/zen.hpp>
namespace cyuediff::test {
inline constexpr auto PhysicalRoot = "/Script/ClothingSystemRuntimeCommon.ClothingAssetCommon";
inline constexpr auto PhysicalLod = "/Script/ClothingSystemRuntimeCommon.ClothLODDataCommon";
inline constexpr auto PhysicalData = "/Script/ClothingSystemRuntimeCommon.ClothPhysicalMeshData";
inline constexpr auto PhysicalBone = "/Script/ClothingSystemRuntimeInterface.ClothVertBoneData";
inline Json cloth_field(const char *type, Json value) {
    return {{"type", type}, {"value", std::move(value)}};
}
inline std::string cloth_record(ClothMapping m) {
    Bytes b;
    for (auto *v : {&m.position, &m.normal, &m.tangent})
        for (float f : *v)
            b.put<float>(f);
    for (auto i : m.source_indices)
        b.put<std::uint16_t>(i);
    b.put<float>(m.weight);
    b.put<std::uint32_t>(m.padding);
    std::string s;
    for (auto v : b.data) {
        static const char h[] = "0123456789abcdef";
        s += h[v >> 4];
        s += h[v & 15];
    }
    return s;
}
inline ClothMapping physical_mapping() {
    ClothMapping m;
    m.position = {1, 0, 0, .25f};
    m.normal = {0, 1, 0, 0};
    m.tangent = {0, 0, 1, 0};
    m.source_indices = {0, 1, 2, 0};
    m.weight = 1;
    return m;
}
inline Json &physical_properties(ZenPackage &p) {
    return p.exports[0].property_read["properties"];
}
inline Json &physical_lod(ZenPackage &p, unsigned i) {
    return physical_properties(p)[std::string(PhysicalRoot) + ".LodData"]["value"][i];
}
inline Json &physical_fields(ZenPackage &p, unsigned i = 0) {
    return physical_lod(p, i)["properties"][std::string(PhysicalLod) + ".PhysicalMeshData"]["value"];
}
inline ZenPackage physical_fixture() {
    ZenPackage p;
    p.name = "/Game/Test";
    p.profile = "ue5.3";
    p.sha256 = std::string(64, 'a');
    p.schema_context_sha256 = std::string(64, 'b');
    p.script_objects_sha256 = std::string(64, 'c');
    auto add = [&](const std::string &path, const std::string &cls, const Json &props) {
        ZenExport e;
        e.path = path;
        e.class_name = cls;
        e.size = 10000;
        e.property_read = {{"status", "decoded"},
                           {"offset", std::uint64_t(0)},
                           {"consumedBytes", std::uint64_t(100)},
                           {"defaultsResolved", false},
                           {"properties", props}};
        p.exports.push_back(std::move(e));
    };
    auto f = [](const char *schema, const char *name) { return std::string(schema) + "." + name; };
    Json props = {{f("/Script/ClothingSystemRuntimeInterface.ClothingAssetBase", "AssetGuid"),
                   cloth_field("StructProperty", "00000001000000020000000300000004")},
                  {f(PhysicalRoot, "LodMap"), cloth_field("ArrayProperty", {0, 1})},
                  {f(PhysicalRoot, "UsedBoneIndices"), cloth_field("ArrayProperty", {2})},
                  {f(PhysicalRoot, "UsedBoneNames"), cloth_field("ArrayProperty", {"Hip"})},
                  {f(PhysicalRoot, "ReferenceBoneIndex"), cloth_field("IntProperty", 2)}};
    Json bone = {{f(PhysicalBone, "NumInfluences"), cloth_field("IntProperty", 1)}};
    for (unsigned k = 0; k < 12; ++k) {
        bone[std::string(PhysicalBone) + ".BoneIndices[" + std::to_string(k) + "]"] =
            cloth_field("UInt16Property", k ? 65535 : 0);
        bone[std::string(PhysicalBone) + ".BoneWeights[" + std::to_string(k) + "]"] =
            cloth_field("FloatProperty", k ? 0.f : 1.f);
    }
    Json tethers = {
        {"properties", Json::object()},
        {"native",
         {{"batches", Json::array({Json::array({{{"anchor", 0}, {"particle", 1}, {"length", 1.f}},
                                                {{"anchor", 0}, {"particle", 2}, {"length", 1.f}}})})}}}};
    Json empty = {{"properties", Json::object()}, {"native", {{"batches", Json::array()}}}};
    Json physical = {{f(PhysicalData, "Vertices"),
                      cloth_field("ArrayProperty", {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}})},
                     {f(PhysicalData, "Normals"),
                      cloth_field("ArrayProperty", {{0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}})},
                     {f(PhysicalData, "Indices"), cloth_field("ArrayProperty", {0, 1, 2})},
                     {f(PhysicalData, "InverseMasses"), cloth_field("ArrayProperty", {0.f, 1.f, 1.f})},
                     {f(PhysicalData, "SelfCollisionIndices"), cloth_field("ArrayProperty", {1, 2})},
                     {f(PhysicalData, "BoneData"), cloth_field("ArrayProperty", {bone, bone, bone})},
                     {f(PhysicalData, "MaxBoneWeights"), cloth_field("IntProperty", 1)},
                     {f(PhysicalData, "NumFixedVerts"), cloth_field("IntProperty", 1)},
                     {f(PhysicalData, "EuclideanTethers"), cloth_field("StructProperty", tethers)},
                     {f(PhysicalData, "GeodesicTethers"), cloth_field("StructProperty", empty)},
                     {f(PhysicalData, "WeightMaps"), cloth_field("MapProperty", Json::object())}};
    auto &weight_map = physical[std::string(PhysicalData) + ".WeightMaps"]["value"];
    weight_map = {{"kind", "stored-map-delta"},
                  {"replace", true},
                  {"removed", Json::object()},
                  {"entries", Json::object()}};
    weight_map["entries"]["1"] = {{"key", 1},
                                  {"value",
                                   {{"/Script/ClothingSystemRuntimeCommon.PointWeightMap.Values",
                                     cloth_field("ArrayProperty", {0.f, 1.f, 1.f})}}}};
    Json lp = {{f(PhysicalLod, "PhysicalMeshData"), cloth_field("StructProperty", physical)},
               {f(PhysicalLod, "SkinningKernelRadius"), cloth_field("FloatProperty", 100.f)},
               {f(PhysicalLod, "bUseMultipleInfluences"), cloth_field("BoolProperty", false)},
               {f(PhysicalLod, "bSmoothTransition"), cloth_field("BoolProperty", true)}};
    const auto records = Json::array({cloth_record(physical_mapping()), cloth_record(physical_mapping()),
                                      cloth_record(physical_mapping())});
    props[f(PhysicalRoot, "LodData")] = cloth_field(
        "ArrayProperty",
        Json::array(
            {{{"properties", lp}, {"native", {{"transitionUp", Json::array()}, {"transitionDown", records}}}},
             {{"properties", lp},
              {"native", {{"transitionUp", records}, {"transitionDown", Json::array()}}}}}));
    const std::string path = "/Game/Test.Asset.Cloth", cfg = path + ".ChaosClothConfig",
                      shared = path + ".SharedConfig";
    Json config_links = {{"kind", "stored-map-delta"},
                         {"replace", true},
                         {"removed", Json::object()},
                         {"entries", Json::object()}};
    config_links["entries"][Json("ChaosClothConfig").dump()] = {
        {"key", "ChaosClothConfig"}, {"value", {{"kind", "export"}, {"path", cfg}}}};
    config_links["entries"][Json("SharedConfig").dump()] = {
        {"key", "SharedConfig"}, {"value", {{"kind", "export"}, {"path", shared}}}};
    props[f(PhysicalRoot, "ClothConfigs")] = cloth_field("MapProperty", config_links);
    add(path, PhysicalRoot, props);
    add(cfg, "/Script/ChaosCloth.ChaosClothConfig",
        {{"/Script/ChaosCloth.ChaosClothConfig.DampingCoefficient", cloth_field("FloatProperty", .125f)},
         {"/Script/ChaosCloth.ChaosClothConfig.GravityScale", cloth_field("FloatProperty", 1.f)},
         {"/Script/ChaosCloth.ChaosClothConfig.EdgeStiffnessWeighted",
          cloth_field(
              "StructProperty",
              {{"/Script/ChaosCloth.ChaosClothWeightedValue.Low", cloth_field("FloatProperty", .25f)},
               {"/Script/ChaosCloth.ChaosClothWeightedValue.High", cloth_field("FloatProperty", .5f)}})}});
    add(shared, "/Script/ChaosCloth.ChaosClothSharedSimConfig",
        {{"/Script/ChaosCloth.ChaosClothSharedSimConfig.IterationCount", cloth_field("IntProperty", 4)}});
    return p;
}
} // namespace cyuediff::test
