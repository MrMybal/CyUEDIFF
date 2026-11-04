#include <cyuediff/material_view.hpp>
#include <algorithm>
#include <map>
namespace cyuediff {
void MaterialView::set_package(const Package &package) {
    documents_.clear();
    selected_ = 0;
    auto model = package.material_snapshot();
    for (auto graph = model.at("graphs").begin(); graph != model.at("graphs").end(); ++graph) {
        Document prepared;
        prepared.path = graph.key();
        prepared.complete = graph.value().at("complete");
        prepared.outputs_known = graph.value().at("outputDefinitionsComplete");
        prepared.diagnostics = graph.value().at("diagnostics").get<std::vector<std::string>>();
        cyui::ItemId next = 1;
        std::map<std::string, cyui::ItemId> pins;
        for (auto node = graph.value().at("nodes").begin(); node != graph.value().at("nodes").end(); ++node) {
            cyui::Node item;
            item.id = next++;
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
        prepared.parameters = instance.value().at("parameters");
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
        if (ImGui::BeginTable("##parameters", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                              ImGui::GetContentRegionAvail())) {
            for (const auto *label : {"Parameter", "Kind", "Value", "Override"})
                ImGui::TableSetupColumn(cyui::tr(label).c_str());
            ImGui::TableHeadersRow();
            for (auto kind = document.parameters.begin(); kind != document.parameters.end(); ++kind)
                for (const auto &parameter : kind.value()) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(parameter.at("name").get_ref<const std::string &>().c_str());
                    if (ImGui::IsItemHovered()) {
                        auto identity =
                            Json::array({parameter.at("association"), parameter.at("index")}).dump();
                        ImGui::SetTooltip("%s", identity.c_str());
                    }
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(cyui::tr(kind.key().c_str()).c_str());
                    ImGui::TableSetColumnIndex(2);
                    auto serialized = parameter.at("value").dump();
                    ImGui::TextUnformatted(parameter.value("valueDecoded", false)
                                               ? serialized.c_str()
                                               : cyui::tr("Unavailable").c_str());
                    ImGui::TableSetColumnIndex(3);
                    auto override_value = parameter.at("override").dump();
                    ImGui::TextUnformatted(override_value.c_str());
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
    cyui::nodeEditor("##material-canvas", document.state, document.nodes, document.links, available, options);
}
} // namespace cyuediff
