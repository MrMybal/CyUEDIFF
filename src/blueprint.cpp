#include <cyuediff/core.hpp>
#include <algorithm>
#include <map>
#include <set>
namespace cyuediff {
namespace {
const Json *property(const ObjectExport &object, const char *name) {
    auto field = object.properties.find(name);
    if (field == object.properties.end() || !field->contains("value"))
        return nullptr;
    return &field->at("value");
}
std::string identity(const ObjectExport &object, const char *field) {
    auto value = property(object, field);
    if (value && value->is_string()) {
        auto id = value->get<std::string>();
        if (id.size() == 32 && id.find_first_not_of('0') != std::string::npos)
            return id;
    }
    return "path:" + object.path;
}
Json position(const ObjectExport &object, const char *field) {
    auto value = property(object, field);
    return value && value->is_number() ? *value : Json(0);
}
Json stable_reference(const Json &reference, const std::map<std::string, std::string> &ids) {
    if (reference.is_null())
        return nullptr;
    auto result = reference;
    auto path = result.at("node").get<std::string>();
    auto it = ids.find(path);
    result["node"] = it == ids.end() ? "path:" + path : it->second;
    return result;
}
bool has_peer(const Json &pin, const std::string &path, const std::string &id) {
    for (const auto &peer : pin.at("linkedTo"))
        if (!peer.is_null() && peer.at("node") == path && peer.at("pin") == id)
            return true;
    return false;
}
} // namespace
Json Package::blueprint_snapshot() const {
    Json graphs = Json::object();
    for (std::size_t graph_index = 0; graph_index < exports.size(); ++graph_index) {
        const auto &graph = exports[graph_index];
        if (!property(graph, "GraphGuid") ||
            (!graph.properties.contains("Nodes") && graph.class_name.find("Graph") == std::string::npos))
            continue;
        auto graph_id = identity(graph, "GraphGuid");
        Json result = {{"path", graph.path},      {"class", graph.class_name},
                       {"complete", true},        {"nodes", Json::object()},
                       {"links", Json::object()}, {"diagnostics", Json::array()}};
        auto note = [&](std::string message) {
            result["complete"] = false;
            result["diagnostics"].push_back(std::move(message));
        };
        if (graph_id.rfind("path:", 0) == 0)
            note("Graph GUID unavailable: " + graph.path);
        std::map<std::string, std::string> ids;
        std::map<std::string, const ObjectExport *> nodes;
        std::set<std::string> used_node_ids;
        for (const auto &node : exports) {
            if (node.outer_index != static_cast<std::int32_t>(graph_index + 1) || !property(node, "NodeGuid"))
                continue;
            auto id = identity(node, "NodeGuid");
            if (id.rfind("path:", 0) == 0)
                note("Node GUID unavailable: " + node.path);
            if (!used_node_ids.insert(id).second) {
                note("Duplicate node GUID: " + id);
                id = "path:" + node.path;
            }
            ids[node.path] = id;
            nodes[node.path] = &node;
        }
        if (auto members = property(graph, "Nodes"); members && members->is_array()) {
            std::set<std::string> membership;
            for (const auto &member : *members) {
                if (!member.is_string()) {
                    note("Invalid graph membership entry");
                    continue;
                }
                auto path = member.get<std::string>();
                if (!membership.insert(path).second)
                    note("Duplicate graph member: " + path);
                if (!nodes.count(path))
                    note("Graph member not decoded: " + path);
            }
            for (const auto &node : nodes)
                if (!membership.count(node.first))
                    note("Exported node absent from graph membership: " + node.first);
        } else
            note("Graph membership is unavailable");
        for (const auto &pair : nodes) {
            const auto &node = *pair.second;
            auto id = ids.at(node.path);
            auto properties = node.properties;
            properties.erase("NodeGuid");
            properties.erase("NodePosX");
            properties.erase("NodePosY");
            Json item = {{"path", node.path},
                         {"class", node.class_name},
                         {"position", {{"x", position(node, "NodePosX")}, {"y", position(node, "NodePosY")}}},
                         {"properties", std::move(properties)},
                         {"pins", Json::object()},
                         {"pinsDecoded", node.native_coverage == "pins"}};
            if (node.native_coverage != "pins") {
                note("Native pins unavailable: " + node.path);
                result["nodes"][id] = std::move(item);
                continue;
            }
            item["pinOrder"] = node.native_data.value("pinOrder", Json::array());
            if (item["pinOrder"].empty())
                for (auto pin = node.native_data.at("pins").begin(); pin != node.native_data.at("pins").end();
                     ++pin)
                    item["pinOrder"].push_back(pin.key());
            for (auto it = node.native_data.at("pins").begin(); it != node.native_data.at("pins").end();
                 ++it) {
                auto pin = it.value();
                pin.erase("linkedTo");
                for (const auto *field : {"parentPin", "passThrough"})
                    pin[field] = stable_reference(pin.at(field), ids);
                for (auto &ref : pin["subPins"])
                    ref = stable_reference(ref, ids);
                item["pins"][it.key()] = std::move(pin);
            }
            result["nodes"][id] = std::move(item);
        }
        for (const auto &pair : nodes) {
            const auto &node = *pair.second;
            if (node.native_coverage != "pins")
                continue;
            for (auto pin = node.native_data.at("pins").begin(); pin != node.native_data.at("pins").end();
                 ++pin) {
                for (const auto &peer : pin.value().at("linkedTo")) {
                    if (peer.is_null()) {
                        note("Null connection reference on " + node.path + ":" + pin.key());
                        continue;
                    }
                    auto from = Json{{"node", ids.at(node.path)}, {"pin", pin.key()}};
                    auto to = stable_reference(peer, ids);
                    auto peer_node = nodes.find(peer.at("node").get<std::string>());
                    const Json *target = nullptr;
                    if (peer_node != nodes.end() && peer_node->second->native_coverage == "pins") {
                        const auto &pins = peer_node->second->native_data.at("pins");
                        auto found = pins.find(peer.at("pin").get<std::string>());
                        if (found != pins.end())
                            target = &*found;
                    }
                    bool resolved = target != nullptr;
                    bool reciprocal = resolved && has_peer(*target, node.path, pin.key());
                    bool valid_direction = resolved && pin.value().at("direction") != target->at("direction");
                    if ((valid_direction && pin.value().at("direction") == "input") ||
                        (!valid_direction && from.dump() > to.dump()))
                        std::swap(from, to);
                    auto key = from.at("node").get<std::string>() + ":" + from.at("pin").get<std::string>() +
                               "->" + to.at("node").get<std::string>() + ":" +
                               to.at("pin").get<std::string>();
                    Json link = {{"from", from},
                                 {"to", to},
                                 {"resolved", resolved},
                                 {"reciprocal", reciprocal},
                                 {"validDirection", valid_direction}};
                    if (!result["links"].contains(key)) {
                        if (!resolved)
                            note("Unresolved connection: " + key);
                        else if (!reciprocal)
                            note("Non-reciprocal connection: " + key);
                        else if (!valid_direction)
                            note("Invalid connection direction: " + key);
                    }
                    result["links"][key] = std::move(link);
                }
            }
        }
        if (graphs.contains(graph_id)) {
            note("Duplicate graph GUID: " + graph_id);
            graph_id = "path:" + graph.path;
        }
        graphs[graph_id] = std::move(result);
    }
    return {{"schema", "cyuediff.blueprint/1"}, {"graphs", std::move(graphs)}};
}
} // namespace cyuediff
