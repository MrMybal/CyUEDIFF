#pragma once
#include "collision_fixture.hpp"
#include "package_fixture.hpp"
namespace cyuediff::test {
inline Fixture collision_file_fixture(const Package &model, int version = 1013) {
    const std::map<std::string, std::string> structures = {{"AggGeom", "KAggregateGeom"},
                                                           {"SphereElems", "KSphereElem"},
                                                           {"BoxElems", "KBoxElem"},
                                                           {"SphylElems", "KSphylElem"},
                                                           {"TaperedCapsuleElems", "KTaperedCapsuleElem"},
                                                           {"ConvexElems", "KConvexElem"},
                                                           {"Center", "Vector"},
                                                           {"Rotation", "Rotator"},
                                                           {"Transform", "Transform"},
                                                           {"VertexData", "Vector"}};
    std::vector<std::string> names = {
        "BodySetup",   "/Script/Engine", "NameProperty", "FloatProperty", "Vector",  "Rotator", "Quat",
        "Translation", "Scale3D",        "Transform",    "BoneName",      "AggGeom", "Name",    "Center",
        "Rotation",    "Radius",         "Radius0",      "Radius1",       "Length",  "X",       "Y",
        "Z",           "RestOffset",     "VertexData",   "IndexData"};
    for (const auto &entry : structures) {
        names.push_back(entry.first);
        names.push_back(entry.second);
    }
    auto collect = [&](const auto &self, const Json &v) -> void {
        if (v.is_object()) {
            for (auto it = v.begin(); it != v.end(); ++it) {
                names.push_back(it.key());
                self(self, *it);
            }
        } else if (v.is_array()) {
            for (const auto &x : v)
                self(self, x);
        } else if (v.is_string())
            names.push_back(v.get<std::string>());
    };
    collect(collect, model.exports[0].properties);
    Bytes body_name, engine_name;
    auto output = fixture(version, 1, false, 0, names, [&](Bytes &script, const NameWriter &name) {
        name(body_name, "BodySetup");
        name(engine_name, "/Script/Engine");
        auto tag = [&](Bytes &target, const std::string &field, const std::string &type,
                       const std::string &inner, const std::string &structure, const Bytes &payload) {
            name(target, field);
            name(target, type);
            if (version >= 1012) {
                target.i(type == "StructProperty" || type == "ArrayProperty" ? 1 : 0);
                if (type == "StructProperty") {
                    name(target, structure);
                    target.i(0);
                } else if (type == "ArrayProperty") {
                    name(target, inner);
                    target.i(inner == "StructProperty" ? 1 : 0);
                    if (inner == "StructProperty") {
                        name(target, structure);
                        target.i(0);
                    }
                }
            }
            target.i(static_cast<int>(payload.data.size()));
            if (version < 1012) {
                target.i(0);
                if (type == "StructProperty") {
                    name(target, structure);
                    target.zeros(16);
                }
                if (type == "ArrayProperty")
                    name(target, inner);
            }
            target.put<std::uint8_t>(0);
            target.append(payload);
        };
        std::function<Bytes(const Json &)> fields;
        fields = [&](const Json &properties) {
            Bytes result;
            for (auto entry = properties.begin(); entry != properties.end(); ++entry) {
                const auto &field = entry.key();
                const auto &property = entry.value();
                if (!property.contains("value"))
                    throw std::runtime_error("Fixture requires an explicit value");
                const auto type = property.at("type").at("name").get<std::string>();
                const auto &v = property.at("value");
                Bytes payload;
                const auto structure = structures.count(field) ? structures.at(field) : std::string{};
                std::string inner;
                auto native_vector = [&](const Json &vector) {
                    for (const auto &x : vector)
                        payload.put<double>(x.get<double>());
                };
                if (type == "NameProperty")
                    name(payload, v.get<std::string>());
                else if (type == "FloatProperty")
                    payload.put<float>(v.get<float>());
                else if (type == "StructProperty") {
                    if (field == "Center" || field == "Rotation")
                        native_vector(v);
                    else if (field == "Transform") {
                        for (const auto &part : {std::make_pair("rotation", "Rotation"),
                                                 std::make_pair("translation", "Translation"),
                                                 std::make_pair("scale", "Scale3D")}) {
                            Bytes component;
                            for (const auto &number : v.at(part.first))
                                component.put<double>(number.get<double>());
                            tag(payload, part.second, "StructProperty", "",
                                std::string(part.first) == "rotation" ? "Quat" : "Vector", component);
                        }
                        name(payload, "None");
                    } else
                        payload = fields(v);
                } else if (type == "ArrayProperty") {
                    inner = field == "IndexData" ? "IntProperty" : "StructProperty";
                    payload.i(static_cast<int>(v.size()));
                    if (version < 1012 && inner == "StructProperty") {
                        Bytes empty;
                        tag(payload, field, "StructProperty", "", structure, empty);
                    }
                    for (const auto &element : v) {
                        if (field == "IndexData")
                            payload.i(element.get<int>());
                        else if (field == "VertexData")
                            native_vector(element);
                        else
                            payload.append(fields(element));
                    }
                } else
                    throw std::runtime_error("Unsupported collision fixture field");
                tag(result, field, type, inner, structure, payload);
            }
            name(result, "None");
            return result;
        };
        // Root script already has a terminator supplied by fixture().
        auto properties = fields(model.exports[0].properties);
        properties.data.resize(properties.data.size() - 8);
        script.append(properties);
    });
    const auto import_offset = output.export_offset - 40;
    std::copy(body_name.data.begin(), body_name.data.end(), output.header.data.begin() + import_offset + 20);
    std::copy(engine_name.data.begin(), engine_name.data.end(),
              output.header.data.begin() + import_offset + 28);
    return output;
}
} // namespace cyuediff::test
