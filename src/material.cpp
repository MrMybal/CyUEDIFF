#include "material_io.hpp"
#include <algorithm>
#include <cctype>
#include <set>
namespace cyuediff {
namespace {
const Json *value(const Json &properties, const char *name) {
    if (!properties.is_object())
        return nullptr;
    auto it = properties.find(name);
    return it != properties.end() && it->is_object() && it->contains("value") ? &it->at("value") : nullptr;
}
std::string text(const Json &props, const char *name, std::string fallback = {}) {
    auto v = value(props, name);
    return v && v->is_string() ? v->get<std::string>() : fallback;
}
std::string structure(const Json &field) {
    if (!field.is_object())
        return {};
    auto t = field.find("type");
    if (t == field.end() || !t->is_object() || t->value("name", std::string{}) != "StructProperty")
        return {};
    auto p = t->find("parameters");
    return p != t->end() && p->is_array() && !p->empty() ? p->at(0).value("name", std::string{})
                                                         : std::string{};
}
bool guid(const std::string &id) {
    return id.size() == 32 && id.find_first_not_of('0') != std::string::npos &&
           std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}
std::string identity(const ObjectExport &e) {
    auto id = text(e.properties, "MaterialExpressionGuid");
    if (!guid(id))
        return "path:" + e.path;
    std::transform(id.begin(), id.end(), id.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return id;
}
bool root_kind(const ObjectExport &e) {
    static const std::set<std::string> classes = {
        "/Script/Engine.Material", "/Script/Engine.MaterialFunction",
        "/Script/Engine.MaterialFunctionMaterialLayer", "/Script/Engine.MaterialFunctionMaterialLayerBlend"};
    return classes.count(e.class_name) != 0;
}
bool instance_kind(const ObjectExport &e) {
    return e.class_name == "/Script/Engine.MaterialInstanceConstant" ||
           e.class_name == "/Script/Engine.MaterialInstanceDynamic";
}
bool expression(const ObjectExport &e) {
    return e.class_name.rfind("/Script/Engine.MaterialExpression", 0) == 0 ||
           value(e.properties, "MaterialExpressionGuid");
}
Json position(const Json &props, const char *x, const char *y) {
    Json result = {{"x", 0}, {"y", 0}};
    if (auto v = value(props, x); v && v->is_number())
        result["x"] = *v;
    if (auto v = value(props, y); v && v->is_number())
        result["y"] = *v;
    return result;
}
std::string title(const ObjectExport &e) {
    for (const auto *field : {"ParameterName", "InputName", "OutputName", "Name", "Desc"}) {
        auto t = text(e.properties, field);
        if (!t.empty() && t != "None")
            return t;
    }
    auto function = text(e.properties, "MaterialFunction");
    if (!function.empty() && function != "None")
        return function.substr(function.find_last_of('.') + 1);
    auto name = e.class_name.substr(e.class_name.find_last_of('.') + 1);
    if (name.rfind("MaterialExpression", 0) == 0)
        name.erase(0, 18);
    return name;
}
using Objects = std::map<std::string, const ObjectExport *>;
struct Graph {
    Json data = {{"nodes", Json::object()},
                 {"links", Json::object()},
                 {"complete", true},
                 {"outputDefinitionsComplete", true},
                 {"diagnostics", Json::array()},
                 {"unresolvedMembers", Json::array()}};
    std::set<std::string> messages;
    void note(std::string message) {
        data["complete"] = false;
        if (messages.insert(message).second)
            data["diagnostics"].push_back(std::move(message));
    }
};
std::string entry_key(const Json &item, std::size_t index) {
    for (const auto *name : {"ExpressionInputId", "ExpressionOutputId"}) {
        auto id = text(item, name);
        if (guid(id))
            return id;
    }
    auto name = text(item, "InputName");
    return !name.empty() && name != "None" ? "name:" + name : "index:" + std::to_string(index);
}
void collect_inputs(const Json &source, Json &properties, Json &inputs, const std::string &prefix,
                    Graph &graph, unsigned depth = 0) {
    if (depth > 48) {
        graph.note("Material input nesting exceeds model limit");
        return;
    }
    if (!source.is_object())
        return;
    for (auto field = source.begin(); field != source.end(); ++field) {
        if (!field.value().is_object())
            continue;
        auto kind = structure(field.value());
        auto key = prefix + field.key();
        auto v = field.value().find("value");
        if (detail::is_material_input(kind)) {
            Json input = {{"name", field.key()}, {"type", kind}, {"kind", "expression"}, {"decoded", false}};
            if (v != field.value().end() && v->is_object() && v->contains("expression") &&
                v->contains("outputIndex") && v->at("outputIndex").is_number_integer()) {
                input.update(*v);
                input["decoded"] = true;
                auto name = v->value("inputName", std::string{});
                if (!name.empty() && name != "None")
                    input["name"] = name;
            } else {
                input["raw"] = field.value();
                graph.note("Material input unavailable: " + key);
            }
            inputs[key] = std::move(input);
            properties.erase(field.key());
            continue;
        }
        if (field.value().contains("opaque"))
            graph.note("Opaque expression property: " + key);
        if (v == field.value().end())
            continue;
        if (!kind.empty() && v->is_object())
            collect_inputs(*v, properties[field.key()]["value"], inputs, key + "/", graph, depth + 1);
        else if (field.value().contains("type") &&
                 field.value().at("type").value("name", std::string{}) == "ArrayProperty" && v->is_array()) {
            Json entries = Json::object();
            bool has_inputs = false;
            std::set<std::string> keys;
            for (std::size_t index = 0; index < v->size(); ++index) {
                const auto &item = v->at(index);
                auto entry = entry_key(item, index);
                if (!keys.insert(entry).second) {
                    graph.note("Duplicate material input entry identity: " + key);
                    entry = "duplicate-index:" + std::to_string(index);
                }
                auto cleaned = item;
                Json entry_inputs = Json::object();
                collect_inputs(item, cleaned, entry_inputs, key + "/" + entry + "/", graph, depth + 1);
                for (auto pin = entry_inputs.begin(); pin != entry_inputs.end(); ++pin) {
                    pin.value()["collection"] = field.key();
                    inputs[pin.key()] = std::move(pin.value());
                }
                has_inputs = has_inputs || !entry_inputs.empty();
                entries[entry] = std::move(cleaned);
            }
            if (has_inputs) {
                properties[field.key()].erase("value");
                properties[field.key()]["entries"] = std::move(entries);
            }
        }
    }
}
void outputs(const ObjectExport &source, Json &node, Graph &graph) {
    auto list = value(source.properties, "Outputs");
    node["outputs"] = Json::object();
    node["outputSchemaKnown"] = list && list->is_array();
    if (!list || !list->is_array())
        return;
    for (std::size_t i = 0; i < list->size(); ++i) {
        const auto &item = list->at(i);
        node["outputs"][std::to_string(i)] = {
            {"index", i}, {"name", text(item, "OutputName")}, {"declared", true}, {"properties", item}};
    }
    node["properties"].erase("Outputs");
    if (auto functions = value(source.properties, "FunctionOutputs"); functions && functions->is_array()) {
        if (functions->size() != list->size())
            graph.note("Function output cache differs from expression outputs: " + source.path);
        for (std::size_t i = 0; i < functions->size() && i < list->size(); ++i) {
            auto id = text(functions->at(i), "ExpressionOutputId");
            node["outputs"][std::to_string(i)]["functionOutputId"] = id;
        }
        node["properties"].erase("FunctionOutputs");
    }
}
Json make_node(const ObjectExport &source, Graph &graph) {
    Json props = source.properties;
    Json inputs = Json::object();
    collect_inputs(source.properties, props, inputs, "", graph);
    for (const auto *field : {"MaterialExpressionGuid", "MaterialExpressionEditorX",
                              "MaterialExpressionEditorY", "Material", "Function"})
        props.erase(field);
    Json result = {
        {"path", source.path},
        {"class", source.class_name},
        {"title", title(source)},
        {"position", position(source.properties, "MaterialExpressionEditorX", "MaterialExpressionEditorY")},
        {"properties", props},
        {"inputs", inputs},
        {"comment", source.class_name == "/Script/Engine.MaterialExpressionComment"}};
    if (source.coverage == "opaque" || source.coverage == "metadata" || source.coverage == "missing")
        graph.note("Expression property stream unavailable: " + source.path);
    outputs(source, result, graph);
    if (source.class_name == "/Script/Engine.MaterialExpressionNamedRerouteUsage") {
        result["inputs"]["$declaration"] = {{"name", "Declaration"},
                                            {"type", "ExpressionInput"},
                                            {"kind", "named-reroute"},
                                            {"decoded", true},
                                            {"expression", text(source.properties, "Declaration", "None")},
                                            {"outputIndex", 0},
                                            {"declarationGuid", text(source.properties, "DeclarationGuid")}};
    }
    return result;
}
void connect(Graph &graph, const Objects &objects, const std::map<std::string, std::string> &ids) {
    auto &nodes = graph.data["nodes"];
    for (auto node = nodes.begin(); node != nodes.end(); ++node) {
        for (auto pin = node.value()["inputs"].begin(); pin != node.value()["inputs"].end(); ++pin) {
            auto &input = pin.value();
            if (!input.value("decoded", false))
                continue;
            auto expr = input.find("expression");
            if (expr == input.end() || expr->is_null())
                continue;
            if (!expr->is_string()) {
                graph.note("Invalid expression reference: " + node.key() + "/" + pin.key());
                continue;
            }
            auto path = expr->get<std::string>();
            if (path == "None" || path.empty()) {
                input["expression"] = nullptr;
                continue;
            }
            auto found = ids.find(path);
            auto source_id = found == ids.end() ? "path:" + path : found->second;
            input["expression"] = source_id;
            auto index = input.at("outputIndex").get<std::int64_t>();
            bool resolved = found != ids.end() && nodes.contains(source_id) && index >= 0;
            bool verified = false;
            if (!resolved)
                graph.note("Unresolved material connection: " + path + " -> " + node.key() + "/" + pin.key());
            if (resolved) {
                auto &source = nodes[source_id];
                auto port = std::to_string(index);
                if (source.value("outputSchemaKnown", false)) {
                    verified = source["outputs"].contains(port);
                    if (!verified) {
                        resolved = false;
                        graph.note("Material output index out of range: " + path + "/" + port);
                    }
                } else {
                    graph.data["outputDefinitionsComplete"] = false;
                    source["outputs"][port] = {{"index", index}, {"name", ""}, {"declared", false}};
                }
            }
            if (input.value("kind", std::string{}) == "named-reroute") {
                auto declaration = objects.find(path);
                auto expected = input.value("declarationGuid", std::string{});
                if (declaration == objects.end() ||
                    declaration->second->class_name !=
                        "/Script/Engine.MaterialExpressionNamedRerouteDeclaration" ||
                    (!expected.empty() &&
                     expected != text(declaration->second->properties, "VariableGuid"))) {
                    resolved = false;
                    graph.note("Named reroute declaration mismatch: " + path);
                }
            }
            auto key = Json::array({node.key(), pin.key()}).dump();
            graph.data["links"][key] = {{"from", {{"node", source_id}, {"output", std::to_string(index)}}},
                                        {"to", {{"node", node.key()}, {"input", pin.key()}}},
                                        {"kind", input.value("kind", std::string("expression"))},
                                        {"resolved", resolved},
                                        {"outputVerified", verified}};
        }
    }
}
Json graph_for(const ObjectExport &root, const Objects &objects,
               const std::vector<const ObjectExport *> &owned) {
    Graph graph;
    if (root.coverage == "opaque" || root.coverage == "metadata" || root.coverage == "missing")
        graph.note("Material property stream unavailable: " + root.path);
    graph.data["path"] = root.path;
    graph.data["class"] = root.class_name;
    graph.data["kind"] = root.class_name == "/Script/Engine.Material" ? "material" : "function";
    Json settings = root.properties;
    for (const auto *key : {"Expressions", "FunctionExpressions", "EditorOnlyData", "EditorX", "EditorY",
                            "StateId", "LightingGuid", "MaterialGraph"})
        settings.erase(key);
    graph.data["settings"] = std::move(settings);
    const ObjectExport *editor = nullptr;
    auto editor_path = text(root.properties, "EditorOnlyData");
    if (!editor_path.empty() && editor_path != "None") {
        auto it = objects.find(editor_path);
        if (it != objects.end())
            editor = it->second;
        else
            graph.note("Material editor data missing: " + editor_path);
    }
    std::set<std::string> membership;
    bool have_membership = false;
    auto members = [&](const Json *list) {
        if (!list || !list->is_array()) {
            graph.note("Material expression collection unavailable");
            return;
        }
        have_membership = true;
        for (const auto &item : *list) {
            if (!item.is_string()) {
                graph.note("Invalid material expression member");
                continue;
            }
            auto path = item.get<std::string>();
            if (!membership.insert(path).second)
                graph.note("Duplicate material expression member: " + path);
        }
    };
    for (const auto *field : {"Expressions", "FunctionExpressions"})
        if (root.properties.contains(field))
            members(value(root.properties, field));
    if (editor) {
        if (editor->coverage == "opaque" || editor->coverage == "metadata" || editor->coverage == "missing")
            graph.note("Material editor property stream unavailable");
        else {
            auto collection = value(editor->properties, "ExpressionCollection");
            if (collection && collection->is_object()) {
                if (collection->contains("Expressions"))
                    members(value(*collection, "Expressions"));
                else
                    have_membership = true;
                if (collection->contains("EditorComments"))
                    members(value(*collection, "EditorComments"));
            } else if (editor->properties.contains("ExpressionCollection"))
                graph.note("Material expression collection opaque");
            else
                have_membership = true;
        }
    }
    if (!have_membership)
        graph.note("Serialized editor expression membership unavailable");
    Objects selected;
    for (auto object : owned)
        selected[object->path] = object;
    for (const auto &path : membership) {
        auto it = objects.find(path);
        if (it == objects.end()) {
            graph.note("Material expression member missing: " + path);
            graph.data["unresolvedMembers"].push_back(path);
            continue;
        }
        selected[path] = it->second;
        if (!expression(*it->second))
            graph.note("Unrecognized expression class in collection: " + path);
    }
    std::map<std::string, std::string> ids;
    std::set<std::string> used;
    for (const auto &pair : selected) {
        auto id = identity(*pair.second);
        if (id.rfind("path:", 0) == 0)
            graph.note("Expression GUID unavailable: " + pair.first);
        if (!used.insert(id).second) {
            graph.note("Duplicate material expression GUID: " + id);
            id = "path:" + pair.first;
            used.insert(id);
        }
        ids[pair.first] = id;
        if (have_membership && !membership.count(pair.first))
            graph.note("Exported expression absent from collection: " + pair.first);
        graph.data["nodes"][id] = make_node(*pair.second, graph);
        if (!graph.data["nodes"][id].value("outputSchemaKnown", false) &&
            !graph.data["nodes"][id].value("comment", false))
            graph.data["outputDefinitionsComplete"] = false;
    }
    if (root.class_name == "/Script/Engine.Material") {
        Json source = editor ? editor->properties : root.properties;
        Json props = source, inputs = Json::object();
        collect_inputs(source, props, inputs, "", graph);
        graph.data["nodes"]["$result"] = {{"path", root.path},
                                          {"class", "MaterialResult"},
                                          {"title", root.name},
                                          {"position", position(root.properties, "EditorX", "EditorY")},
                                          {"properties", Json::object()},
                                          {"inputs", inputs},
                                          {"outputs", Json::object()},
                                          {"outputSchemaKnown", true},
                                          {"comment", false}};
    }
    connect(graph, objects, ids);
    return std::move(graph.data);
}
const std::map<std::string, std::string> parameter_arrays = {
    {"ScalarParameterValues", "scalar"},
    {"VectorParameterValues", "vector"},
    {"DoubleVectorParameterValues", "double-vector"},
    {"TextureParameterValues", "texture"},
    {"RuntimeVirtualTextureParameterValues", "virtual-texture"},
    {"SparseVolumeTextureParameterValues", "volume-texture"},
    {"FontParameterValues", "font"},
    {"StaticSwitchParameters", "static-switch"},
    {"StaticComponentMaskParameters", "static-mask"}};
Json parameter_settings(const Json &properties, unsigned depth = 0) {
    auto result = properties;
    if (!properties.is_object() || depth > 48)
        return result;
    for (auto field = properties.begin(); field != properties.end(); ++field) {
        auto v = value(properties, field.key().c_str());
        if (parameter_arrays.count(field.key()) && v && v->is_array() &&
            std::all_of(v->begin(), v->end(), [](const Json &entry) { return entry.is_object(); }))
            result.erase(field.key());
        else if (v && v->is_object() && !structure(field.value()).empty())
            result[field.key()]["value"] = parameter_settings(*v, depth + 1);
    }
    return result;
}
void parameters(const Json &properties, Json &output, Json &issues, unsigned depth = 0) {
    if (depth > 48) {
        issues.push_back("Material parameter nesting exceeds model limit");
        return;
    }
    if (!properties.is_object())
        return;
    for (auto field = properties.begin(); field != properties.end(); ++field) {
        auto kind = parameter_arrays.find(field.key());
        auto v = value(properties, field.key().c_str());
        if (kind != parameter_arrays.end()) {
            if (!v || !v->is_array()) {
                issues.push_back("Parameter array unavailable: " + field.key());
                continue;
            }
            for (std::size_t i = 0; i < v->size(); ++i) {
                const auto &entry = v->at(i);
                if (!entry.is_object()) {
                    issues.push_back("Invalid material parameter entry");
                    continue;
                }
                auto info = value(entry, "ParameterInfo");
                bool identity_decoded = true;
                if (entry.contains("ParameterInfo") && (!info || !info->is_object())) {
                    identity_decoded = false;
                    issues.push_back("Material parameter info unavailable");
                }
                auto name = info ? text(*info, "Name") : text(entry, "ParameterName");
                Json association = "GlobalParameter", index = -1;
                if (info) {
                    if (auto a = value(*info, "Association"); a && a->is_string())
                        association = *a;
                    else if (info->contains("Association"))
                        identity_decoded = false;
                    if (auto n = value(*info, "Index"); n && n->is_number_integer())
                        index = *n;
                    else if (info->contains("Index"))
                        identity_decoded = false;
                    if (!identity_decoded)
                        issues.push_back("Material parameter association/index unavailable: " + name);
                }
                auto key = Json::array({association, index, name}).dump();
                if (!identity_decoded || name.empty() || name == "None") {
                    issues.push_back("Material parameter identity unavailable: " + field.key());
                    key = "index:" + std::to_string(i);
                }
                if (!output.contains(kind->second))
                    output[kind->second] = Json::object();
                if (output[kind->second].contains(key)) {
                    issues.push_back("Duplicate material parameter identity: " + key);
                    key = "duplicate:" + field.key() + ":" + std::to_string(i);
                    while (output[kind->second].contains(key))
                        key += ":duplicate";
                }
                Json item = {
                    {"name", name}, {"association", association}, {"index", index}, {"properties", entry}};
                auto parameter = value(entry, kind->second == "static-switch" ? "Value" : "ParameterValue");
                if (kind->second == "static-mask") {
                    Json mask = Json::object();
                    bool decoded = true;
                    for (const auto *c : {"R", "G", "B", "A"}) {
                        auto x = value(entry, c);
                        if (entry.contains(c) && (!x || !x->is_boolean())) {
                            decoded = false;
                            issues.push_back("Static mask component unavailable: " + name + "/" + c);
                        }
                        mask[c] = x ? *x : (entry.contains(c) ? Json(nullptr) : Json(false));
                    }
                    item["value"] = std::move(mask);
                    item["valueDecoded"] = decoded;
                } else if (parameter) {
                    item["value"] = *parameter;
                    item["valueDecoded"] = true;
                } else {
                    item["value"] = nullptr;
                    item["valueDecoded"] = false;
                    issues.push_back("Material parameter value unavailable or omitted: " + name);
                }
                if (kind->second.rfind("static-", 0) == 0) {
                    auto overridden = value(entry, "bOverride");
                    if (entry.contains("bOverride") && (!overridden || !overridden->is_boolean())) {
                        item["override"] = nullptr;
                        issues.push_back("Material override flag unavailable: " + name);
                    } else {
                        item["override"] = overridden ? *overridden : Json(false);
                        item["properties"].erase("bOverride");
                    }
                } else
                    item["override"] = true;
                if (identity_decoded && !name.empty() && name != "None") {
                    item["properties"].erase("ParameterInfo");
                    item["properties"].erase("ParameterName");
                }
                if (item["valueDecoded"] == true) {
                    if (kind->second == "static-mask")
                        for (const auto *channel : {"R", "G", "B", "A"})
                            item["properties"].erase(channel);
                    else
                        item["properties"].erase(kind->second == "static-switch" ? "Value"
                                                                                 : "ParameterValue");
                }
                output[kind->second][key] = std::move(item);
            }
        } else if (v && v->is_object() && !structure(field.value()).empty())
            parameters(*v, output, issues, depth + 1);
        else if (field.key().rfind("StaticParameters", 0) == 0 && field.value().contains("opaque"))
            issues.push_back("Static parameter set unavailable: " + field.key());
    }
}
} // namespace
Json Package::material_snapshot() const {
    Objects objects;
    std::map<std::string, const ObjectExport *> roots;
    std::map<std::string, std::vector<const ObjectExport *>> owned;
    for (const auto &e : exports) {
        objects[e.path] = &e;
        if (root_kind(e))
            roots[e.path] = &e;
    }
    for (const auto &e : exports)
        if (expression(e)) {
            std::string owner = text(e.properties, "Material", text(e.properties, "Function"));
            if (!roots.count(owner)) {
                auto index = e.outer_index;
                unsigned depth = 0;
                while (index > 0 && static_cast<std::size_t>(index) <= exports.size() && depth++ < 256) {
                    const auto &parent = exports[static_cast<std::size_t>(index - 1)];
                    if (roots.count(parent.path)) {
                        owner = parent.path;
                        break;
                    }
                    index = parent.outer_index;
                }
            }
            if (roots.count(owner))
                owned[owner].push_back(&e);
        }
    Json graphs = Json::object(), instances = Json::object();
    for (const auto &root : roots)
        graphs[root.first] = graph_for(*root.second, objects, owned[root.first]);
    for (const auto &e : exports)
        if (instance_kind(e)) {
            Json params = Json::object(), issues = Json::array();
            parameters(e.properties, params, issues);
            auto settings = parameter_settings(e.properties);
            for (const auto *field : {"Parent", "EditorOnlyData", "ParameterStateId", "LightingGuid"})
                settings.erase(field);
            auto editor_path = text(e.properties, "EditorOnlyData");
            auto editor = objects.find(editor_path);
            if (editor != objects.end()) {
                parameters(editor->second->properties, params, issues);
                if (editor->second->coverage == "opaque" || editor->second->coverage == "missing" ||
                    editor->second->coverage == "metadata")
                    issues.push_back("Instance editor property stream unavailable");
            } else if (!editor_path.empty() && editor_path != "None")
                issues.push_back("Instance editor data missing: " + editor_path);
            instances[e.path] = {{"class", e.class_name},
                                 {"parent", text(e.properties, "Parent", "None")},
                                 {"parameters", params},
                                 {"settings", settings},
                                 {"complete", issues.empty() && e.coverage != "opaque" &&
                                                  e.coverage != "missing" && e.coverage != "metadata"},
                                 {"diagnostics", issues},
                                 {"parentResolved", false}};
        }
    return {{"schema", "cyuediff.materials/1"}, {"graphs", graphs}, {"instances", instances}};
}
} // namespace cyuediff
