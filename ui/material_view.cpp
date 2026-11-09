#include <cyuediff/material_view.hpp>
#include "diff_style.hpp"
#include "../src/source_availability.hpp"
#include <algorithm>
#include <map>
namespace cyuediff {
void MaterialView::set_package(const Package &package, const Package *peer, bool after) {
    layout_.restore(Json::object(), "graph-layout");
    documents_.clear();
    selected_ = 0;
    auto model = package.material_snapshot();
    auto other = peer ? peer->material_snapshot() : Json{};
    detail::SourceCoverage coverage;
    if (peer) {
        auto raw = after ? detail::source_coverage(*peer, package) : detail::source_coverage(package, *peer);
        coverage = detail::projection_coverage(raw, after ? *peer : package, after ? package : *peer,
                                               after ? other : model, after ? model : other, "materials");
    }
    auto pointer = [](const std::string &key) {
        std::string out;
        for (char c : key)
            out += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
        return out;
    };
    for (auto graph = model.at("graphs").begin(); graph != model.at("graphs").end(); ++graph) {
        Document prepared;
        prepared.path = graph.key();
        prepared.complete = graph.value().at("complete");
        prepared.outputs_known = graph.value().at("outputDefinitionsComplete");
        prepared.diagnostics = graph.value().at("diagnostics").get<std::vector<std::string>>();
        prepared.comparison_unavailable = peer && coverage.intersects("/graphs/" + pointer(graph.key()));
        const bool comparing = peer && !prepared.comparison_unavailable;
        GraphChanges delta;
        const Json *match =
            peer && other.at("graphs").contains(graph.key()) ? &other.at("graphs").at(graph.key()) : nullptr;
        if (comparing) {
            delta = after ? graph_changes(match, &graph.value()) : graph_changes(&graph.value(), match);
            prepared.counts = delta.node_counts;
            prepared.comparing = true;
        }
        cyui::ItemId next = 1;
        std::map<std::string, cyui::ItemId> pins;
        std::vector<GraphInspectionItem> inspection;
        for (auto node = graph.value().at("nodes").begin(); node != graph.value().at("nodes").end(); ++node) {
            cyui::Node item;
            item.id = next++;
            if (comparing)
                prepared.changes[item.id] = delta.nodes.at(node.key());
            item.label = node.value().at("title");
            inspection.push_back({item.id, node.key(), item.label,
                                  comparing ? delta.nodes.at(node.key()) : ChangeKind::unchanged});
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
        const auto *local_nodes = &graph.value().at("nodes");
        const auto *peer_nodes = match ? &match->at("nodes") : nullptr;
        prepared.inspector.set_nodes(comparing ? (after ? peer_nodes : local_nodes) : local_nodes,
                                     comparing ? (after ? local_nodes : peer_nodes) : local_nodes,
                                     std::move(inspection), comparing);
        documents_.push_back(std::move(prepared));
    }
    for (auto instance = model.at("instances").begin(); instance != model.at("instances").end(); ++instance) {
        Document prepared;
        prepared.path = instance.key();
        prepared.instance = true;
        prepared.complete = instance.value().at("complete");
        prepared.parent = instance.value().at("parent");
        prepared.comparison_unavailable =
            peer && coverage.intersects("/instances/" + pointer(instance.key()));
        prepared.comparing = peer && !prepared.comparison_unavailable;
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
        if (!prepared.comparing) {
            a.clear();
            b.clear();
            collect(local, a);
            b = a;
        }
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
        rebuild_parameters(prepared);
        documents_.push_back(std::move(prepared));
    }
}
void MaterialView::rebuild_parameters(Document &document) {
    document.visible_parameters.clear();
    for (std::size_t i = 0; i < document.parameters.size(); ++i) {
        const auto &p = document.parameters[i];
        if ((!document.changed_parameters_only || p.change != ChangeKind::unchanged) &&
            document.parameter_filter.matches(p.name,
                                              p.kind + " " + p.identity + " " + p.before + " " + p.after))
            document.visible_parameters.push_back(i);
    }
}
bool MaterialView::set_parameter_filter(std::string query, bool changed_only) {
    if (documents_.empty() || !documents_[selected_].instance)
        return false;
    auto &document = documents_[selected_];
    document.parameter_filter.query = std::move(query);
    document.changed_parameters_only = document.comparing && changed_only;
    rebuild_parameters(document);
    return true;
}
std::size_t MaterialView::visible_parameter_count() const {
    return documents_.empty() ? 0 : documents_[selected_].visible_parameters.size();
}
bool MaterialView::select_node(const std::string &material, const std::string &node) {
    for (std::size_t i = 0; i < documents_.size(); ++i)
        if (!documents_[i].instance && documents_[i].path == material &&
            documents_[i].inspector.select_node(node)) {
            selected_ = i;
            return true;
        }
    return false;
}
bool MaterialView::next_changed_node(bool previous) {
    return !documents_.empty() && !documents_[selected_].instance &&
           documents_[selected_].inspector.next_change(previous);
}
bool MaterialView::select_object(const std::string &path) {
    for (std::size_t i = 0; i < documents_.size(); ++i)
        if (documents_[i].path == path) {
            selected_ = i;
            return true;
        }
    return false;
}
Json MaterialView::view_state() const {
    Json views = Json::object(), inspectors = Json::object(), filters = Json::object();
    auto add = [&](const auto &g) {
        auto state = g.inspector.view_state();
        if (!state.empty() && inspectors.size() < 128)
            inspectors[g.path] = std::move(state);
        if (g.instance && (!g.parameter_filter.query.empty() || g.changed_parameters_only) &&
            filters.size() < 128)
            filters[g.path] = {{"filter", g.parameter_filter.query},
                               {"changedOnly", g.changed_parameters_only}};
        if (!g.needs_fit && views.size() < 128)
            views[g.path] = {{"pan", {g.state.canvas.view.origin.x, g.state.canvas.view.origin.y}},
                             {"zoom", g.state.canvas.view.zoom}};
    };
    if (!documents_.empty())
        add(documents_[selected_]);
    for (const auto &g : documents_)
        add(g);
    Json result = {{"selected", documents_.empty() ? Json(nullptr) : Json(documents_[selected_].path)},
                   {"views", views}};
    if (!inspectors.empty())
        result["inspectors"] = std::move(inspectors);
    if (!filters.empty())
        result["parameterFilters"] = std::move(filters);
    if (!layout_.snapshot().empty())
        result["layout"] = layout_.snapshot();
    return result;
}
void MaterialView::restore_view_state(const Json &state) {
    cyuediff::view_state::validate(state, "graphs");
    if (state.contains("layout"))
        layout_.restore(state["layout"], "graph-layout");
    if (state.contains("selected")) {
        selected_ = 0;
        for (std::size_t i = 0; i < documents_.size(); ++i)
            if (state["selected"] == documents_[i].path) {
                selected_ = i;
                break;
            }
    }
    for (auto &g : documents_) {
        if (state.contains("inspectors")) {
            auto it = state["inspectors"].find(g.path);
            if (it != state["inspectors"].end() && !g.instance)
                g.inspector.restore_view_state(*it);
        }
        if (state.contains("parameterFilters")) {
            auto it = state["parameterFilters"].find(g.path);
            if (it != state["parameterFilters"].end() && g.instance) {
                if (it->contains("filter"))
                    g.parameter_filter.query = it->at("filter").get<std::string>();
                if (it->contains("changedOnly"))
                    g.changed_parameters_only = g.comparing && it->at("changedOnly").get<bool>();
                rebuild_parameters(g);
            }
        }
    }
    if (state.contains("views"))
        for (auto &g : documents_) {
            auto it = state["views"].find(g.path);
            if (it == state["views"].end())
                continue;
            const auto &v = *it;
            if (v.contains("pan"))
                g.state.canvas.view.origin = {v["pan"][0].get<double>(), v["pan"][1].get<double>()};
            if (v.contains("zoom"))
                g.state.canvas.view.zoom = v["zoom"].get<double>();
            if (v.contains("pan") || v.contains("zoom"))
                g.needs_fit = false;
        }
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
    if (document.comparison_unavailable)
        cyui::callout(cyui::Tone::info,
                      cyui::tr("Comparison is unavailable for this view because source values are unread or "
                               "use incompatible decoders. Inspect each revision or use scoped properties.")
                          .c_str());
    if (!document.complete)
        cyui::callout(cyui::Tone::warning, cyui::tr("Some material data could not be resolved.").c_str());
    if (!document.diagnostics.empty() && layout_.section("diagnostics", "Graph diagnostics", false, true)) {
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
        bool update = cyui::filterBox("##parameter-filter", document.parameter_filter);
        if (document.comparing)
            update =
                ImGui::Checkbox(cyui::trLabel("Changed only").c_str(), &document.changed_parameters_only) ||
                update;
        if (update)
            rebuild_parameters(document);
        if (document.visible_parameters.empty()) {
            cyui::emptyState(cyui::tr("No matching parameter").c_str(),
                             cyui::tr("Change the filter or show unchanged parameters.").c_str(),
                             cyui::icon::filter);
            return;
        }
        if (layout_.begin_table("##parameters", document.comparing ? "comparisonColumns" : "singleColumns",
                                document.comparing ? 5 : 3,
                                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg,
                                ImGui::GetContentRegionAvail())) {
            int column = 0;
            for (const auto *label : {"Parameter", "Kind", document.comparing ? "Before" : "Value"})
                layout_.column(label, column++);
            if (document.comparing) {
                layout_.column("After", 3);
                layout_.column("Change", 4);
            }
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(document.visible_parameters.size()));
            while (clipper.Step())
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    const auto &parameter =
                        document.parameters[document.visible_parameters[static_cast<std::size_t>(i)]];
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
            layout_.end_table();
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
    document.inspector.draw(document.state, document.nodes, document.links, document.changes,
                            document.needs_fit, document.low, document.high);
}
} // namespace cyuediff
