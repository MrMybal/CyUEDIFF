#include <cyuediff/material_view.hpp>
#include "diff_style.hpp"
#include <algorithm>
#include <map>
namespace cyuediff {
void MaterialView::set_package(const Package &package, const Package *peer, bool after) {
    documents_.clear();
    selected_ = 0;
    auto model = package.material_snapshot();
    auto other = peer ? peer->material_snapshot() : Json{};
    for (auto graph = model.at("graphs").begin(); graph != model.at("graphs").end(); ++graph) {
        Document prepared;
        prepared.path = graph.key();
        prepared.complete = graph.value().at("complete");
        prepared.outputs_known = graph.value().at("outputDefinitionsComplete");
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
            item.label = node.value().at("title");
            item.colorIndex = node.key() == "$result" ? 4 : (node.value().at("comment") == true ? 6 : 0);
            item.position = {node.value().at("position").at("x").get<double>(),
                             node.value().at("position").at("y").get<double>()};
            for (const auto *direction : {"inputs", "outputs"}) {
                const auto &ports = node.value().at(direction);
                const bool output = std::string(direction) == "outputs";
                for (auto port = ports.begin(); port != ports.end(); ++port) {
                    auto id = next++;
                    pins[Json::array({node.key(), direction, port.key()}).dump()] = id;
                    auto label = port.value().value("name", std::string{});
                    if (label.empty() || label == "None")
                        label = "#" + port.key();
                    int color = port.value().value("kind", std::string{}) == "named-reroute" ? 5 : 0;
                    (output ? item.outputs : item.inputs).push_back({id, label, color});
                    ++prepared.pins;
                }
            }
            double height =
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
            if (!link.at("resolved").get<bool>())
                continue;
            const auto &from = link.at("from"), &to = link.at("to");
            auto a = pins.find(Json::array({from.at("node"), "outputs", from.at("output")}).dump());
            auto b = pins.find(Json::array({to.at("node"), "inputs", to.at("input")}).dump());
            if (a != pins.end() && b != pins.end())
                prepared.links.push_back(
                    {next++, a->second, b->second, link.at("kind") == "named-reroute" ? 5 : 0});
        }
        documents_.push_back(std::move(prepared));
    }
    for (auto instance = model.at("instances").begin(); instance != model.at("instances").end(); ++instance) {
        Document prepared;
        prepared.path = instance.key();
        prepared.instance = true;
        prepared.complete = instance.value().at("complete");
        prepared.parent = instance.value().at("parent");
        prepared.comparing = peer != nullptr;
        Json empty = Json::object();
        const auto &local = instance.value().at("parameters");
        const auto &remote = peer && other.at("instances").contains(instance.key())
                                 ? other.at("instances").at(instance.key()).at("parameters")
                                 : empty;
        using Entry = std::pair<std::string, const Json *>;
        std::map<std::string, Entry> a, b;
        auto collect = [](const Json &data, auto &target) {
            for (auto kind = data.begin(); kind != data.end(); ++kind)
                for (const auto &parameter : kind.value()) {
                    auto key = Json::array({kind.key(), parameter.at("association"), parameter.at("index"),
                                            parameter.at("name")})
                                   .dump();
                    target.emplace(key, Entry{kind.key(), &parameter});
                }
        };
        collect(after ? remote : local, a);
        collect(after ? local : remote, b);
        if (!peer)
            b = a;
        std::map<std::string, bool> all;
        for (const auto &p : a)
            all[p.first] = true;
        for (const auto &p : b)
            all[p.first] = true;
        auto render = [](const Json *p) {
            if (!p)
                return std::string{};
            Json value = {{"value", p->at("value")},
                          {"override", p->at("override")},
                          {"decoded", p->value("valueDecoded", false)}};
            return value.dump();
        };
        for (const auto &key : all) {
            auto x = a.find(key.first), y = b.find(key.first);
            const Json *av = x == a.end() ? nullptr : x->second.second,
                       *bv = y == b.end() ? nullptr : y->second.second;
            auto &entry = x == a.end() ? y->second : x->second;
            auto p = av ? av : bv;
            auto change = classify_change(av, bv);
            prepared.parameters.push_back(
                {p->at("name").get<std::string>(), entry.first, key.first, render(av), render(bv), change});
            prepared.counts.add(change);
        }
        prepared.diagnostics = instance.value().at("diagnostics").get<std::vector<std::string>>();
        documents_.push_back(std::move(prepared));
    }
}
bool MaterialView::select_object(const std::string &path) {
    for (std::size_t i = 0; i < documents_.size(); ++i)
        if (documents_[i].path == path) {
            selected_ = i;
            return true;
        }
    return false;
}
void MaterialView::draw(const char *id) {
    cyui::ScopedID scope(id);
    if (documents_.empty()) {
        cyui::emptyState(cyui::tr("No material").c_str(),
                         cyui::tr("This package has no material graph or instance.").c_str(),
                         cyui::icon::code);
        return;
    }
    if (ImGui::BeginCombo(cyui::trLabel("Material").c_str(), documents_[selected_].path.c_str())) {
        for (std::size_t i = 0; i < documents_.size(); ++i)
            if (ImGui::Selectable(documents_[i].path.c_str(), selected_ == i))
                selected_ = i;
        ImGui::EndCombo();
    }
    auto &document = documents_[selected_];
    if (!document.complete)
        cyui::callout(cyui::Tone::warning, cyui::tr("Some material data could not be resolved.").c_str());
    if (!document.diagnostics.empty() && ImGui::TreeNode(cyui::trLabel("Graph diagnostics").c_str())) {
        for (const auto &message : document.diagnostics)
            ImGui::TextUnformatted(message.c_str());
        ImGui::TreePop();
    }
    if (document.instance) {
        cyui::textDim(cyui::tr("Parent: %s").c_str(), document.parent.c_str());
        cyui::callout(cyui::Tone::info,
                      cyui::tr("Stored overrides only. Parent values are not evaluated.").c_str());
        if (document.comparing) {
            ImGui::TextUnformatted(cyui::tr("Changes").c_str());
            detail::change_legend(document.counts);
        }
        if (ImGui::BeginTable("##parameters", document.comparing ? 5 : 3,
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg,
                              ImGui::GetContentRegionAvail())) {
            for (const auto *label : {"Parameter", "Kind", document.comparing ? "Before" : "Value"})
                ImGui::TableSetupColumn(cyui::tr(label).c_str());
            if (document.comparing) {
                ImGui::TableSetupColumn(cyui::tr("After").c_str());
                ImGui::TableSetupColumn(cyui::tr("Change").c_str());
            }
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(document.parameters.size()));
            while (clipper.Step())
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    const auto &parameter = document.parameters[i];
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(parameter.name.c_str());
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", parameter.identity.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(cyui::tr(parameter.kind.c_str()).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(parameter.before.c_str());
                    if (document.comparing) {
                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextUnformatted(parameter.after.c_str());
                        ImGui::TableSetColumnIndex(4);
                        ImGui::PushStyleColor(ImGuiCol_Text, detail::change_color(parameter.change));
                        ImGui::TextUnformatted(cyui::tr(change_name(parameter.change)).c_str());
                        ImGui::PopStyleColor();
                    }
                }
            ImGui::EndTable();
        }
        return;
    }
    if (cyui::button(cyui::trLabel("Fit graph").c_str()))
        document.needs_fit = true;
    ImGui::SameLine();
    cyui::textDim(cyui::tr("%zu nodes / %zu pins / %zu links").c_str(), document.nodes.size(), document.pins,
                  document.links.size());
    if (document.comparing)
        detail::change_legend(document.counts);
    if (!document.outputs_known)
        cyui::textDim("%s", cyui::tr("Some output ports are inferred from saved connections.").c_str());
    auto available = ImGui::GetContentRegionAvail();
    if (available.x <= 0 || available.y <= 0)
        return;
    if (document.needs_fit && !document.nodes.empty()) {
        document.state.canvas.view.fit(document.low, document.high, available, cyui::dp(24));
        document.needs_fit = false;
    }
    cyui::NodeOptions options;
    options.readOnly = true;
    options.revision = 1;
    options.drawBody = [&](const cyui::Node &node, const cyui::Rect &rect) {
        detail::node_marker(node, rect, document.changes);
    };
    cyui::nodeEditor("##material-canvas", document.state, document.nodes, document.links, available, options);
}
} // namespace cyuediff
