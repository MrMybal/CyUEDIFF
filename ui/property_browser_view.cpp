#include <cyuediff/property_browser_view.hpp>
#include <algorithm>
namespace cyuediff {
namespace {
std::string property_label(const std::string &name) {
    if (name.empty())
        return "\"\"";
    const auto dot = name.find_last_of('.');
    return name.rfind("/Script/", 0) == 0 && dot != std::string::npos ? name.substr(dot + 1) : name;
}
std::string scope_label(const std::string &scope) {
    auto pointer = Json::json_pointer(scope);
    std::vector<std::string> parts;
    while (!pointer.empty() && parts.size() < 4) {
        parts.push_back(property_label(pointer.back()));
        pointer = pointer.parent_pointer();
    }
    std::string label = pointer.empty() ? std::string{} : "… / ";
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (it != parts.rbegin())
            label += " / ";
        label += *it;
    }
    return label;
}
} // namespace
void PropertyBrowserView::register_translations() {
    PropertyView::register_translations();
    cyui::registerTranslations("fr",
                               {{"Object root", "Racine de l'objet"},
                                {"Parent value", "Valeur parente"},
                                {"Open selected value", "Ouvrir la valeur sélectionnée"},
                                {"Serialization fields", "Champs de sérialisation"},
                                {"Previous page", "Page précédente"},
                                {"Next page", "Page suivante"},
                                {"Last page", "Dernière page"},
                                {"Child index", "Indice de l’élément"},
                                {"Array index", "Indice du tableau"},
                                {"Go to index", "Aller à l'indice"},
                                {"Children", "Éléments"},
                                {"Filter applies to this page.", "Le filtre s'applique à cette page."},
                                {"This value has no children.", "Cette valeur ne contient aucun élément."},
                                {"This value is unavailable in both snapshots.",
                                 "Cette valeur est indisponible dans les deux instantanés."}});
}
bool PropertyBrowserView::bind(const Json *before, const Json *after, const std::string &scope,
                               std::size_t first, bool serialization, bool preserve) {
    if (!comparing_) {
        if (before)
            after = before;
        else
            before = after;
    }
    PropertyPageOptions options;
    options.first = first;
    options.max_compare_nodes = 16'000'000;
    options.serialization_fields = serialization;
    options.escape_non_bmp = sizeof(ImWchar) == 2;
    auto next = property_page(before, after, scope, options);
    if (!next.available)
        return false;
    const auto retained = preserve ? properties_.view_state() : Json::object();
    page_ = std::move(next);
    serialization_fields_ = serialization;
    auto display = page_.table;
    for (auto &row : display.rows)
        row.label = property_label(row.label);
    properties_.set_table(std::move(display), comparing_);
    if (preserve)
        properties_.restore_view_state(retained);
    jump_ = static_cast<std::int64_t>(page_.first);
    error_.clear();
    return true;
}
void PropertyBrowserView::set_values(const Json *before, const Json *after, bool comparing) {
    *this = PropertyBrowserView{};
    comparing_ = comparing;
    try {
        bind(before, after, "", 0, false, false);
    } catch (const Error &e) {
        error_ = e.what();
    }
}
bool PropertyBrowserView::navigate(const Json *before, const Json *after, const std::string &scope,
                                   std::size_t first) {
    return bind(before, after, scope, first, serialization_fields_, false);
}
bool PropertyBrowserView::open_selected(const Json *before, const Json *after) {
    const auto *selected = properties_.selected_row();
    if (!selected || selected->path == page_.scope)
        return false;
    const auto at = std::find_if(page_.table.rows.begin(), page_.table.rows.end(),
                                 [&](const PropertyRow &r) { return r.path == selected->path; });
    if (at == page_.table.rows.end() ||
        !page_.expandable.at(static_cast<std::size_t>(at - page_.table.rows.begin())))
        return false;
    const auto next = selected->path;
    return navigate(before, after, next);
}
bool PropertyBrowserView::go_to_index(const Json *before, const Json *after, std::size_t index) {
    if (index >= page_.total_children)
        return false;
    if (!bind(before, after, page_.scope, index / 128 * 128, serialization_fields_, false))
        return false;
    properties_.set_changed_only(false);
    return properties_.select_row(index - page_.first + 1);
}
Json PropertyBrowserView::view_state() const {
    return {{"scope", page_.scope},
            {"first", page_.first},
            {"serializationFields", serialization_fields_},
            {"properties", properties_.view_state()}};
}
void PropertyBrowserView::restore_view_state(const Json *before, const Json *after, const Json &state) {
    view_state::validate(state, "property-browser");
    const auto scope = state.value("scope", std::string{});
    const auto first = state.value("first", std::size_t(0));
    const auto serialization = state.value("serializationFields", false);
    // A stale scope is never replaced with an unrelated child or a misleading selected value.
    if (!bind(before, after, scope, first, serialization, false)) {
        bind(before, after, "", 0, serialization, false);
        return;
    }
    if (state.contains("properties"))
        properties_.restore_view_state(state.at("properties"));
}
void PropertyBrowserView::draw(const Json *before, const Json *after, const char *id) {
    cyui::ScopedID scoped(id);
    std::string destination;
    std::size_t first = page_.first;
    bool request = false, serialization = serialization_fields_, preserve = false, jump_request = false;
    auto next_line = [&](const char *label) {
        const auto width =
            ImGui::CalcTextSize(cyui::tr(label).c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
        const auto right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= right)
            ImGui::SameLine();
    };
    {
        cyui::ScopedDisabled disabled(page_.scope.empty());
        if (cyui::button(cyui::trLabel("Object root").c_str())) {
            request = true;
            first = 0;
        }
        next_line("Parent value");
        if (cyui::button(cyui::trLabel("Parent value").c_str())) {
            destination = page_.scope.substr(0, page_.scope.find_last_of('/'));
            request = true;
            first = 0;
        }
    }
    next_line("Open selected value");
    const auto *selected = properties_.selected_row();
    const auto at = selected ? std::find_if(page_.table.rows.begin(), page_.table.rows.end(),
                                            [&](const PropertyRow &r) { return r.path == selected->path; })
                             : page_.table.rows.end();
    const bool can_open = at != page_.table.rows.end() && at != page_.table.rows.begin() &&
                          page_.expandable.at(static_cast<std::size_t>(at - page_.table.rows.begin()));
    {
        cyui::ScopedDisabled disabled(!can_open);
        if (cyui::button(cyui::trLabel("Open selected value").c_str())) {
            destination = selected->path;
            request = true;
            first = 0;
        }
    }
    const auto location = page_.scope.empty() ? cyui::tr("Object root") : scope_label(page_.scope);
    cyui::textDim("%s", location.c_str());
    cyui::tooltip(page_.scope.empty() ? "/" : page_.scope.c_str());
    if (ImGui::Checkbox(cyui::trLabel("Serialization fields").c_str(), &serialization)) {
        destination = page_.scope;
        request = true;
        first = 0;
    }
    const auto count = page_.table.rows.empty() ? 0 : page_.table.rows.size() - 1;
    if (page_.total_children) {
        cyui::textDim("%s: %zu–%zu / %zu", cyui::tr("Children").c_str(), page_.first + 1, page_.first + count,
                      page_.total_children);
        if (page_.total_children > 128) {
            {
                cyui::ScopedDisabled disabled(page_.first == 0);
                if (cyui::button(cyui::trLabel("Previous page").c_str())) {
                    destination = page_.scope;
                    first = page_.first >= 128 ? page_.first - 128 : 0;
                    request = true;
                    preserve = true;
                }
            }
            next_line("Next page");
            {
                cyui::ScopedDisabled disabled(page_.first + count >= page_.total_children);
                if (cyui::button(cyui::trLabel("Next page").c_str())) {
                    destination = page_.scope;
                    first = page_.first + count;
                    request = true;
                    preserve = true;
                }
            }
            next_line("Last page");
            {
                cyui::ScopedDisabled disabled(page_.first + count >= page_.total_children);
                if (cyui::button(cyui::trLabel("Last page").c_str())) {
                    destination = page_.scope;
                    first = (page_.total_children - 1) / 128 * 128;
                    request = true;
                    preserve = true;
                }
            }
        }
        if (page_.array || page_.total_children > 128) {
            ImGui::SetNextItemWidth(cyui::dp(120));
            ImGui::InputScalar(cyui::trLabel(page_.array ? "Array index" : "Child index").c_str(),
                               ImGuiDataType_S64, &jump_);
            next_line("Go to index");
            cyui::ScopedDisabled disabled(jump_ < 0 ||
                                          static_cast<std::uint64_t>(jump_) >= page_.total_children);
            if (cyui::button(cyui::trLabel("Go to index").c_str())) {
                destination = page_.scope;
                first = static_cast<std::size_t>(jump_) / 128 * 128;
                request = true;
                jump_request = true;
            }
        }
        cyui::textDim("%s", cyui::tr("Filter applies to this page.").c_str());
    } else
        cyui::textDim("%s", cyui::tr(page_.available ? "This value has no children."
                                                     : "This value is unavailable in both snapshots.")
                                .c_str());
    if (!error_.empty())
        cyui::callout(cyui::Tone::warning, error_.c_str());
    properties_.draw("##property-page");
    // Rebind only after all selected-row references and the child drawing are finished.
    if (request) {
        const auto index = jump_;
        try {
            if (jump_request && index >= 0)
                go_to_index(before, after, static_cast<std::size_t>(index));
            else
                bind(before, after, destination, first, serialization, preserve);
        } catch (const Error &e) {
            error_ = e.what();
        }
    }
}
} // namespace cyuediff
