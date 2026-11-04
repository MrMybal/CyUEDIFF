#include "material_io.hpp"
#include <cmath>
#include <set>
namespace cyuediff::detail {
bool is_material_input(const std::string &type) {
    static const std::set<std::string> types = {"ExpressionInput",           "MaterialAttributesInput",
                                                "ColorMaterialInput",        "ScalarMaterialInput",
                                                "VectorMaterialInput",       "Vector2MaterialInput",
                                                "ShadingModelMaterialInput", "SubstrateMaterialInput"};
    return types.count(type) != 0;
}
bool native_material_input(const PackageSummary &summary) {
    auto core = summary.custom_versions.value("375ec13c06e448fbb50084f0262a717e", -1);
    auto framework = summary.custom_versions.value("cffc743f43b04480939114df171d2073", -1);
    if (core > 4 || framework > 37)
        throw Error("material-profile", "Unsupported material input custom version");
    if (core == 0)
        return false;
    if (framework < 0)
        throw Error("material-profile", "Material input framework version unavailable");
    if (core < 0) {
        auto main = summary.custom_versions.value("697dd581e64f41abaa4a51ecbeb7b628", -1);
        if (summary.ue5_version < 1000 || main < 51 || main > 119 || framework < 31)
            throw Error("material-profile", "Material input serialization profile unavailable");
    }
    return true;
}
Json decode_material_input(Archive &a, const Package &p, const std::string &type, std::uint64_t end) {
    if (!is_material_input(type) || !native_material_input(p.summary))
        throw Error("material-profile", "Native material input not supported by this profile");
    Archive::ReadLimit window(a, end);
    auto expression = p.reference(a.get<std::int32_t>());
    auto output = a.get<std::int32_t>();
    std::string name;
    if (p.summary.custom_versions.value("cffc743f43b04480939114df171d2073", -1) >= 31) {
        auto index = a.get<std::int32_t>(), number = a.get<std::int32_t>();
        if (index < 0 || static_cast<std::size_t>(index) >= p.names.size() || number < 0)
            throw Error("name-index", "Invalid material input FName", a.tell() - 8);
        name = p.names[index] + (number ? "_" + std::to_string(number - 1) : "");
    } else
        name = a.string();
    Json mask = Json::object();
    for (const auto *field : {"enabled", "r", "g", "b", "a"})
        mask[field] = a.get<std::int32_t>();
    Json result = {{"expression", expression == "None" ? Json(nullptr) : Json(expression)},
                   {"outputIndex", output},
                   {"inputName", name},
                   {"mask", mask}};
    if (type != "ExpressionInput" && type != "MaterialAttributesInput") {
        auto constant = a.get<std::uint32_t>();
        if (constant > 1)
            throw Error("material-boolean", "Invalid material constant flag", a.tell() - 4);
        result["useConstant"] = constant != 0;
        if (type == "ShadingModelMaterialInput" || type == "SubstrateMaterialInput")
            result["constant"] = a.get<std::uint32_t>();
        else if (type == "ColorMaterialInput") {
            Json color = Json::object();
            for (const auto *channel : {"b", "g", "r", "a"})
                color[channel] = a.get<std::uint8_t>();
            result["constant"] = std::move(color);
        } else {
            unsigned count = type == "VectorMaterialInput" ? 3 : (type == "Vector2MaterialInput" ? 2 : 1);
            Json values = Json::array();
            for (unsigned i = 0; i < count; ++i) {
                auto f = a.get<float>();
                if (!std::isfinite(f))
                    throw Error("material-number", "Non-finite material input constant", a.tell() - 4);
                values.push_back(f);
            }
            result["constant"] = count == 1 ? values[0] : values;
        }
    }
    return result;
}
} // namespace cyuediff::detail
