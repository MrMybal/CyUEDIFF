#include <cyuediff/blueprint_view.hpp>
#include "diff_style.hpp"
#include <algorithm>
#include <map>
namespace cyuediff {
namespace {
std::string node_title(const Json &node) {
    const auto &properties = node.at("properties");
    for (const auto *field : {"FunctionReference", "EventReference"}) {
        auto value = properties.find(field);
        if (value != properties.end() && value->contains("value") &&
            value->at("value").contains("MemberName")) {
            const auto &member = value->at("value").at("MemberName");
            if (member.contains("value") && member.at("value").is_string())
                return member.at("value").get<std::string>();
        }
    }
    auto name = node.at("class").get<std::string>();
    return name.substr(name.find_last_of('.') + 1);
}
int pin_color(const Json &pin) {
    auto category = pin.at("type").value("category", std::string{});
    if (category == "exec")
        return 0;
    if (category == "bool")
        return 1;
    if (category == "int" || category == "int64" || category == "byte")
        return 2;
    if (category == "real" || category == "float" || category == "double")
        return 3;
    if (category == "object" || category == "class")
        return 4;
    if (category == "string" || category == "text" || category == "name")
        return 5;
    return 6;
}
} // namespace
void BlueprintView::set_package(const Package &package, const Package *peer, bool after) {
    graphs_.clear();
    selected_ = 0;
    auto model = package.blueprint_snapshot();
    auto other = peer ? peer->blueprint_snapshot() : Json{};
    for (auto graph = model.at("graphs").begin(); graph != model.at("graphs").end(); ++graph) {
        Graph prepared;
        prepared.id = graph.key();
        prepared.path = graph.value().at("path");
        prepared.complete = graph.value().at("complete");
        prepared.diagnostics = graph.value().at("diagnostics").get<std::vector<std::string>>();
        GraphChanges delta;
        if (peer) {
            const Json *match =
                other.at("graphs").contains(graph.key()) ? &other.at("graphs").at(graph.key()) : nullptr;
            delta = after ? graph_changes(match, &graph.value()) : graph_changes(&graph.value(), match);
            prepared.counts = delta.node_counts;
            prepared.comparing = true;
        }
        cyui::ItemId next = 1;
        std::map<std::string, cyui::ItemId> pins;
        for (auto node = graph.value().at("nodes").begin(); node != graph.value().at("nodes").end(); ++node) {
            cyui::Node item;
            item.id = next++;
            if (peer)
                prepared.changes[item.id] = delta.nodes.at(node.key());
            item.label = node_title(node.value());
            item.position = {node.value().at("position").at("x").get<double>(),
                             node.value().at("position").at("y").get<double>()};
            for (const auto &pin_id : node.value().value("pinOrder", Json::array())) {
                if (pin_id.is_null())
                    continue;
                auto key = pin_id.get<std::string>();
                const auto &pin = node.value().at("pins").at(key);
                auto id = next++;
                pins[node.key() + ":" + key] = id;
                cyui::NodePin entry{id, pin.at("name").get<std::string>(), pin_color(pin)};
                auto &target = pin.at("direction") == "output" ? item.outputs : item.inputs;
                target.push_back(std::move(entry));
                ++prepared.pins;
            }
            const double height =
                40.0 + 24.0 * static_cast<double>(std::max(item.inputs.size(), item.outputs.size()));
            if (prepared.nodes.empty()) {
                prepared.low = item.position;
                prepared.high = {item.position.x + 210.0, item.position.y + height};
            } else {
                prepared.low.x = std::min(prepared.low.x, item.position.x);
                prepared.low.y = std::min(prepared.low.y, item.position.y);
                prepared.high.x = std::max(prepared.high.x, item.position.x + 210.0);
                prepared.high.y = std::max(prepared.high.y, item.position.y + height);
            }
            prepared.nodes.push_back(std::move(item));
        }
        for (const auto &link : graph.value().at("links")) {
            const auto &from = link.at("from");
            const auto &to = link.at("to");
            auto a = pins.find(from.at("node").get<std::string>() + ":" + from.at("pin").get<std::string>());
            auto b = pins.find(to.at("node").get<std::string>() + ":" + to.at("pin").get<std::string>());
            if (a != pins.end() && b != pins.end() && link.at("validDirection") == true)
                prepared.links.push_back({next++, a->second, b->second, 0});
        }
        graphs_.push_back(std::move(prepared));
    }
}
void BlueprintView::draw(const char *id) {
    cyui::ScopedID scope(id);
    if (graphs_.empty()) {
        cyui::emptyState(cyui::tr("No Blueprint graph").c_str(),
                         cyui::tr("This package has no decoded editor graph.").c_str(), cyui::icon::code);
        return;
    }
    if (ImGui::BeginCombo(cyui::trLabel("Graph").c_str(), graphs_[selected_].path.c_str())) {
        for (std::size_t i = 0; i < graphs_.size(); ++i)
            if (ImGui::Selectable(graphs_[i].path.c_str(), selected_ == i))
                selected_ = i;
        ImGui::EndCombo();
    }
    auto &graph = graphs_[selected_];
    if (cyui::button(cyui::trLabel("Fit graph").c_str()))
        graph.needs_fit = true;
    ImGui::SameLine();
    cyui::textDim(cyui::tr("%zu nodes / %zu pins / %zu links").c_str(), graph.nodes.size(), graph.pins,
                  graph.links.size());
    if (graph.comparing)
        detail::change_legend(graph.counts);
    if (!graph.complete) {
        cyui::callout(
            cyui::Tone::warning,
            cyui::tr("This graph is incomplete. Some pins or connections could not be resolved.").c_str());
        if (ImGui::TreeNode(cyui::trLabel("Graph diagnostics").c_str())) {
            for (const auto &message : graph.diagnostics)
                ImGui::TextUnformatted(message.c_str());
            ImGui::TreePop();
        }
    }
    auto available = ImGui::GetContentRegionAvail();
    if (available.x <= 0 || available.y <= 0)
        return;
    if (graph.needs_fit && !graph.nodes.empty()) {
        graph.state.canvas.view.fit(graph.low, graph.high, available, cyui::dp(24));
        graph.needs_fit = false;
    }
    cyui::NodeOptions options;
    options.readOnly = true;
    options.revision = 1;
    options.drawBody = [&](const cyui::Node &node, const cyui::Rect &rect) {
        detail::node_marker(node, rect, graph.changes);
    };
    cyui::nodeEditor("##graph-canvas", graph.state, graph.nodes, graph.links, available, options);
}
} // namespace cyuediff
