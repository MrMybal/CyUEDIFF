#include "../src/material_io.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
using namespace cyuediff;
namespace {
int checks = 0;
void check(bool good, const char *message) {
    ++checks;
    if (!good)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn, const char *message) {
    bool rejected = false;
    try {
        fn();
    } catch (const Error &) {
        rejected = true;
    }
    check(rejected, message);
}
struct Bytes {
    std::vector<unsigned char> data;
    template <class T> void put(T value) {
        auto p = reinterpret_cast<const unsigned char *>(&value);
        data.insert(data.end(), p, p + sizeof(T));
    }
    void i(int value) {
        put<std::int32_t>(value);
    }
    void patch(std::size_t at, int value) {
        std::memcpy(data.data() + at, &value, 4);
    }
    void string(const std::string &s) {
        i(static_cast<int>(s.size() + 1));
        data.insert(data.end(), s.begin(), s.end());
        put<char>(0);
    }
    void save(const fs::path &path) const {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    }
};
const char *core = "375ec13c06e448fbb50084f0262a717e", *framework = "cffc743f43b04480939114df171d2073";
Package profile() {
    Package p;
    p.summary.ue4_version = 522;
    p.summary.ue5_version = 1009;
    p.summary.custom_versions = {{core, 4}, {framework, 37}, {"697dd581e64f41abaa4a51ecbeb7b628", 96}};
    p.names = {"None", "Color"};
    ObjectExport e;
    e.name = e.path = "M.A";
    p.exports.push_back(e);
    return p;
}
Bytes input_bytes(bool legacy = false) {
    Bytes b;
    b.i(1);
    b.i(2);
    if (legacy)
        b.string("Color");
    else {
        b.i(1);
        b.i(0);
    }
    b.i(1);
    b.i(1);
    b.i(0);
    b.i(1);
    b.i(0);
    return b;
}
Json property(const Json &v, const std::string &type = "FloatProperty") {
    return {{"type", {{"name", type}}}, {"value", v}};
}
Json structure(const Json &v, const std::string &type) {
    return {{"type", {{"name", "StructProperty"}, {"parameters", Json::array({{{"name", type}}})}}},
            {"value", v}};
}
Json input(const std::string &source, int output = 0) {
    return structure({{"expression", source.empty() ? Json(nullptr) : Json(source)},
                      {"outputIndex", output},
                      {"inputName", "None"},
                      {"mask", {{"enabled", 1}, {"r", 1}, {"g", 0}, {"b", 0}, {"a", 0}}}},
                     "ExpressionInput");
}
Package material() {
    Package p;
    ObjectExport root;
    root.name = root.path = "M";
    root.class_name = "/Script/Engine.Material";
    root.coverage = "properties";
    root.properties = {{"EditorOnlyData", property("M.Editor", "ObjectProperty")}};
    ObjectExport editor;
    editor.path = "M.Editor";
    editor.class_name = "/Script/Engine.MaterialEditorOnlyData";
    editor.coverage = "properties";
    editor.properties = {{"ExpressionCollection",
                          structure({{"Expressions", property(Json::array({"M.A", "M.B"}), "ArrayProperty")}},
                                    "MaterialExpressionCollection")},
                         {"BaseColor", input("M.B")}};
    p.exports = {root, editor};
    for (int i = 0; i < 2; ++i) {
        ObjectExport e;
        e.path = i ? "M.B" : "M.A";
        e.name = i ? "B" : "A";
        e.class_name = i ? "/Script/Engine.MaterialExpressionMultiply"
                         : "/Script/Engine.MaterialExpressionScalarParameter";
        e.outer_index = 1;
        e.coverage = "properties";
        e.properties = {
            {"MaterialExpressionGuid", property(std::string(32, i ? 'b' : 'a'), "StructProperty")},
            {"Material", property("M", "ObjectProperty")},
            {"MaterialExpressionEditorX", property(i * 250, "IntProperty")}};
        if (i)
            e.properties["A"] = input("M.A");
        else {
            e.properties["ParameterName"] = property("Power", "NameProperty");
            e.properties["DefaultValue"] = property(0.5);
        }
        p.exports.push_back(e);
    }
    return p;
}
Json &collection(Package &p) {
    return p.exports[1].properties["ExpressionCollection"]["value"]["Expressions"]["value"];
}
Json graph(const Package &p) {
    return p.material_snapshot().at("graphs").at("M");
}
Json parameter(const std::string &name, float number, int index = -1,
               const std::string &association = "GlobalParameter") {
    return {{"ParameterInfo", structure({{"Name", property(name, "NameProperty")},
                                         {"Association", property(association, "EnumProperty")},
                                         {"Index", property(index, "IntProperty")}},
                                        "MaterialParameterInfo")},
            {"ParameterValue", property(number)}};
}
Package instance() {
    Package p;
    ObjectExport e;
    e.name = e.path = "MI";
    e.class_name = "/Script/Engine.MaterialInstanceConstant";
    e.coverage = "properties";
    e.properties = {
        {"Parent", property("/Game/M.M", "ObjectProperty")},
        {"ScalarParameterValues",
         property(Json::array({parameter("Power", 0.5f), parameter("Roughness", 0.8f)}), "ArrayProperty")}};
    p.exports = {e};
    return p;
}
} // namespace
int main() {
    auto file = fs::temp_directory_path() /
                ("cyuediff-material-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin");
    try {
        ReadOptions options;
        auto p = profile();
        auto decode = [&](Bytes b, const std::string &type, std::uint64_t limit = 0) {
            b.save(file);
            detail::Archive archive(file, options);
            auto result = detail::decode_material_input(archive, p, type, limit ? limit : b.data.size());
            check(archive.tell() == b.data.size(), "Native input consumption");
            return result;
        };
        for (const auto *type : {"ExpressionInput", "MaterialAttributesInput", "ScalarMaterialInput",
                                 "ColorMaterialInput", "VectorMaterialInput", "Vector2MaterialInput",
                                 "ShadingModelMaterialInput", "SubstrateMaterialInput"}) {
            auto b = input_bytes();
            if (std::string(type) != "ExpressionInput" && std::string(type) != "MaterialAttributesInput") {
                b.i(1);
                if (std::string(type) == "ColorMaterialInput") {
                    b.put<std::uint8_t>(3);
                    b.put<std::uint8_t>(2);
                    b.put<std::uint8_t>(1);
                    b.put<std::uint8_t>(255);
                } else if (std::string(type) == "ShadingModelMaterialInput" ||
                           std::string(type) == "SubstrateMaterialInput")
                    b.i(7);
                else {
                    int count = std::string(type) == "VectorMaterialInput"
                                    ? 3
                                    : (std::string(type) == "Vector2MaterialInput" ? 2 : 1);
                    for (int i = 0; i < count; ++i)
                        b.put<float>(0.25f + i);
                }
            }
            auto v = decode(b, type);
            check(v["expression"] == "M.A" && v["outputIndex"] == 2 && v["inputName"] == "Color",
                  "Native input fields");
            check(v["mask"]["b"] == 1 && v["mask"]["g"] == 0, "Native masks");
            if (std::string(type) == "VectorMaterialInput")
                check(v["constant"] == Json::array({0.25, 1.25, 2.25}) && b.data.size() == 52,
                      "Material FVector3f stays float under LWC");
            if (std::string(type) == "ColorMaterialInput")
                check(v["constant"]["r"] == 1 && v["constant"]["b"] == 3, "FColor byte order");
        }
        auto b = input_bytes();
        b.patch(0, 0);
        check(decode(b, "ExpressionInput")["expression"].is_null(), "Unconnected expression");
        p.summary.custom_versions[framework] = 30;
        check(decode(input_bytes(true), "ExpressionInput")["inputName"] == "Color",
              "Legacy FString input name");
        p = profile();
        p.summary.custom_versions.erase(core);
        check(detail::native_material_input(p.summary), "Known UE5 profile without Core custom version");
        p.summary.ue5_version = 0;
        rejects([&] { detail::native_material_input(p.summary); }, "No UE4 profile guessing");
        p = profile();
        p.summary.custom_versions[core] = 0;
        check(!detail::native_material_input(p.summary), "Legacy tagged input profile");
        p = profile();
        p.summary.custom_versions[framework] = 38;
        rejects([&] { detail::native_material_input(p.summary); }, "Future framework rejected");
        p = profile();
        p.summary.custom_versions[core] = 5;
        rejects([&] { detail::native_material_input(p.summary); }, "Future Core rejected");
        p = profile();
        p.summary.custom_versions.erase(framework);
        rejects([&] { detail::native_material_input(p.summary); }, "Missing framework rejected");
        p = profile();
        b = input_bytes();
        b.patch(8, 9);
        rejects([&] { decode(b, "ExpressionInput"); }, "Invalid FName rejected");
        b = input_bytes();
        b.patch(0, 20);
        rejects([&] { decode(b, "ExpressionInput"); }, "Invalid reference rejected");
        b = input_bytes();
        rejects([&] { decode(b, "ExpressionInput", 35); }, "Property read boundary");
        b.i(2);
        b.put<float>(1);
        rejects([&] { decode(b, "ScalarMaterialInput"); }, "Invalid bool32 rejected");
        b = input_bytes();
        b.i(1);
        b.put<float>(std::numeric_limits<float>::infinity());
        rejects([&] { decode(b, "ScalarMaterialInput"); }, "Nonfinite constant rejected");
        b = input_bytes();
        b.data.pop_back();
        rejects([&] { decode(b, "ExpressionInput"); }, "Truncated input rejected");
        auto m = material();
        auto g = graph(m);
        auto a = std::string(32, 'a'), c = std::string(32, 'b');
        check(g["complete"] == true && g["nodes"].size() == 3 && g["links"].size() == 2,
              "Material expressions and result");
        check(g["outputDefinitionsComplete"] == false, "Omitted class output defaults explicit");
        check(g["nodes"][c]["inputs"]["A"]["expression"] == a, "Links use GUID identity");
        check(g["nodes"][a]["title"] == "Power", "Parameter title");
        check(g["nodes"][c]["inputs"]["A"]["mask"]["r"] == 1, "Mask retained in model");
        auto n = m;
        n.exports[2].properties["DefaultValue"]["value"] = 0.75;
        auto diff = compare_materials(m, n);
        check(diff.complete && diff.changes.size() == 1 &&
                  diff.changes[0]["path"] == "/graphs/M/nodes/" + a + "/properties/DefaultValue/value",
              "Parameter default diff");
        n = m;
        n.exports[3].properties["A"]["value"]["expression"] = nullptr;
        check(graph(n)["links"].size() == 1, "Unconnected native input");
        n = m;
        n.exports[3].properties["A"]["value"]["expression"] = "Missing";
        check(graph(n)["complete"] == false && graph(n)["links"].size() == 2, "Dangling link retained");
        check(!compare_materials(n, n).complete && compare_materials(n, n).status() == "incomplete",
              "Incomplete self comparison not equal");
        n = m;
        n.exports[2].properties["Outputs"] = property(Json::array({Json::object()}), "ArrayProperty");
        auto ng = graph(n);
        bool verified = false;
        for (const auto &l : ng["links"])
            verified = verified || l["outputVerified"] == true;
        check(verified, "Serialized output range verified");
        n.exports[3].properties["A"]["value"]["outputIndex"] = 3;
        check(graph(n)["complete"] == false, "Out of range output flagged");
        n = m;
        n.exports[3].properties["A"]["value"]["outputIndex"] = -1;
        check(graph(n)["complete"] == false, "Negative output flagged");
        n = m;
        n.exports[3].properties["MaterialExpressionGuid"]["value"] = a;
        check(graph(n)["nodes"].size() == 3 && graph(n)["complete"] == false,
              "Duplicate GUID preserves nodes");
        n = m;
        collection(n).push_back("Missing");
        check(graph(n)["unresolvedMembers"].size() == 1, "Missing member retained");
        n = m;
        collection(n).erase(0);
        check(graph(n)["complete"] == false && graph(n)["nodes"].size() == 3,
              "Orphan exported expression retained");
        n = m;
        n.exports[3].properties["A"].erase("value");
        n.exports[3].properties["A"]["opaque"] = {{"bytes", 36}};
        check(graph(n)["nodes"][c]["inputs"]["A"]["decoded"] == false && graph(n)["complete"] == false,
              "Opaque input not silent");
        n = m;
        n.exports[2].path = "M.Renamed";
        collection(n)[0] = "M.Renamed";
        n.exports[3].properties["A"]["value"]["expression"] = "M.Renamed";
        check(graph(n)["links"] == g["links"], "GUID wire identity survives export rename");
        n = m;
        Json x = {{"ExpressionInputId", property(a)}, {"Input", input("M.A")}},
             y = {{"ExpressionInputId", property(c)}, {"Input", input("M.B")}};
        n.exports[3].properties.erase("A");
        n.exports[3].properties["FunctionInputs"] = property(Json::array({x, y}), "ArrayProperty");
        auto reordered = n;
        std::swap(reordered.exports[3].properties["FunctionInputs"]["value"][0],
                  reordered.exports[3].properties["FunctionInputs"]["value"][1]);
        check(graph(n) == graph(reordered), "Function inputs normalized by stable GUID");
        n.exports[3].properties["FunctionInputs"]["value"][1] = x;
        check(graph(n)["complete"] == false, "Duplicate function input identity");
        n = m;
        n.exports[2].class_name = "/Script/Engine.MaterialExpressionNamedRerouteDeclaration";
        n.exports[2].properties["VariableGuid"] = property(a);
        n.exports[3].class_name = "/Script/Engine.MaterialExpressionNamedRerouteUsage";
        n.exports[3].properties.erase("A");
        n.exports[3].properties["Declaration"] = property("M.A");
        n.exports[3].properties["DeclarationGuid"] = property(a);
        ng = graph(n);
        bool logical = false;
        for (const auto &l : ng["links"])
            logical = logical || (l["kind"] == "named-reroute" && l["resolved"] == true);
        check(logical && ng["complete"] == true, "Named reroute dependency");
        n.exports[3].properties["DeclarationGuid"]["value"] = c;
        check(graph(n)["complete"] == false, "Named reroute GUID mismatch");
        n = m;
        n.exports[0].class_name = "/Script/Engine.MaterialFunction";
        check(graph(n)["nodes"].size() == 2 && graph(n)["kind"] == "function",
              "Function has expression output terminals");
        auto mi = instance();
        auto snapshot = mi.material_snapshot();
        auto param = snapshot["instances"]["MI"];
        check(param["complete"] == true && param["parameters"]["scalar"].size() == 2,
              "Instance stored parameters");
        check(param["parent"] == "/Game/M.M" && param["parentResolved"] == false,
              "External parent remains unresolved");
        auto ri = mi;
        std::swap(ri.exports[0].properties["ScalarParameterValues"]["value"][0],
                  ri.exports[0].properties["ScalarParameterValues"]["value"][1]);
        check(compare_materials(mi, ri).changes.empty(), "Instance parameter reorder ignored");
        auto &values = ri.exports[0].properties["ScalarParameterValues"]["value"];
        values.push_back(parameter("Power", 0.9f, 0, "LayerParameter"));
        check(ri.material_snapshot()["instances"]["MI"]["parameters"]["scalar"].size() == 3,
              "Association and index distinguish equal names");
        values.push_back(values[0]);
        values.push_back(values[0]);
        param = ri.material_snapshot()["instances"]["MI"];
        check(param["complete"] == false && param["parameters"]["scalar"].size() == 5,
              "Duplicate parameters preserved");
        ri = mi;
        ri.exports[0].properties["EditorOnlyData"] = property("Missing");
        check(!compare_materials(ri, ri).complete, "Missing instance editor data not ignored");
        ri = mi;
        ri.exports[0].properties["StaticParameters"] = {{"type", {{"name", "StructProperty"}}},
                                                        {"opaque", {{"bytes", 20}}}};
        check(!compare_materials(ri, ri).complete, "Opaque static parameter set incomplete");
        ri = mi;
        Json static_switch = {{"ParameterName", property("Enabled", "NameProperty")},
                              {"Value", property(false, "BoolProperty")},
                              {"bOverride", property(false, "BoolProperty")}};
        ri.exports[0].properties["StaticSwitchParameters"] =
            property(Json::array({static_switch}), "ArrayProperty");
        param = ri.material_snapshot()["instances"]["MI"]["parameters"]["static-switch"].begin().value();
        check(param["value"] == false && param["override"] == false,
              "Disabled static override remains visible");
        auto nested = mi;
        auto another_switch = static_switch;
        another_switch["ParameterName"]["value"] = "Another";
        nested.exports[0].properties["StaticParameters"] =
            structure({{"StaticSwitchParameters",
                        property(Json::array({static_switch, another_switch}), "ArrayProperty")}},
                      "StaticParameterSet");
        auto nested_reordered = nested;
        auto &switches = nested_reordered.exports[0]
                             .properties["StaticParameters"]["value"]["StaticSwitchParameters"]["value"];
        std::swap(switches[0], switches[1]);
        check(compare_materials(nested, nested_reordered).changes.empty(),
              "Nested static parameters normalize in settings too");
        Json mask = {{"ParameterName", property("Channels", "NameProperty")},
                     {"R", property(true, "BoolProperty")}};
        ri.exports[0].properties["StaticComponentMaskParameters"] =
            property(Json::array({mask}), "ArrayProperty");
        param = ri.material_snapshot()["instances"]["MI"]["parameters"]["static-mask"].begin().value();
        check(param["value"]["R"] == true && param["value"]["G"] == false && param["valueDecoded"] == true,
              "Omitted mask components use native constructor defaults");
        ri.exports[0].properties["StaticComponentMaskParameters"]["value"][0]["G"] = {
            {"opaque", {{"bytes", 1}}}};
        check(!compare_materials(ri, ri).complete, "Opaque mask component is not a default");
        ri = mi;
        ri.exports[0].properties["ScalarParameterValues"]["value"][0]["ParameterValue"] = {
            {"opaque", {{"bytes", 4}, {"sha256", "before"}}}};
        auto opaque_after = ri;
        opaque_after.exports[0]
            .properties["ScalarParameterValues"]["value"][0]["ParameterValue"]["opaque"]["sha256"] = "after";
        auto opaque_diff = compare_materials(ri, opaque_after);
        check(!opaque_diff.complete && opaque_diff.changes.size() == 1 &&
                  opaque_diff.changes[0]["before"] == "before",
              "Undecoded parameter hashes retained for comparison");
        ri = mi;
        ri.exports[0].properties["ScalarParameterValues"]["value"][0]["ParameterInfo"]["value"]
                                ["Association"] = {{"opaque", {{"bytes", 8}}}};
        check(!compare_materials(ri, ri).complete, "Opaque parameter identity not defaulted to global");
        ri = mi;
        ri.exports[0].properties["ScalarParameterValues"]["value"][0].erase("ParameterValue");
        check(!compare_materials(ri, ri).complete, "Omitted value is not an effective parent value");
        n = m;
        n.exports[2].coverage = "metadata";
        check(graph(n)["complete"] == false, "Metadata-only expression incomplete");
        check(!compare_materials(Package{}, Package{}).complete, "No material is not an identical material");
        m.complete_input = false;
        check(!compare_materials(m, material()).complete, "Missing companion cannot establish equality");
        fs::remove(file);
        std::cout << checks << " material checks passed\n";
    } catch (const std::exception &e) {
        fs::remove(file);
        std::cerr << "Material test failure: " << e.what() << '\n';
        return 1;
    }
}
