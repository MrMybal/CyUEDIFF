#include <cyuediff/attachments_view.hpp>
#include "../src/attachments_pose_internal.hpp"
#include "diff_style.hpp"
#include <algorithm>
#include <set>
namespace cyuediff {
namespace {
std::string token(const std::string &s) {
    std::string out;
    for (char c : s)
        out += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
    return out;
}
std::string socket_key(const std::string &p) { return "/sockets/" + token(p); }
std::string virtual_key(const std::string &p) { return "/virtual/" + token(p); }
} // namespace
void AttachmentsView::register_translations() {
    cyui::registerTranslations(
        "fr", {{"Attachments", "Attachements"},
               {"Sockets and stored virtual-bone definitions; omitted values stay unknown.",
                "Sockets et définitions d’os virtuels ; les valeurs absentes restent inconnues."},
               {"Some attachment data is incomplete. External objects and defaults are not loaded.",
                "Certaines données sont incomplètes. Les objets externes et les valeurs par défaut ne sont "
                "pas chargés."},
               {"Find sockets or virtual bones", "Rechercher des sockets ou os virtuels"},
               {"Virtual bone", "Os virtuel"},
               {"Socket", "Socket"},
               {"Attachment details", "Détails de l’attachement"},
               {"Socket origins 3D", "Origines des sockets en 3D"},
               {"Bone / target", "Os / cible"},
               {"No matching attachments", "Aucun attachement correspondant"},
               {"Only explicitly stored locations can be placed. Parent points are raw bone origins.",
                "Seules les positions enregistrées sont placées. Les points parents sont les origines des os "
                "bruts."},
               {"Attachment list is limited to 4,096 entries.", "La liste est limitée à 4 096 attachements."},
               {"Preview is limited to 2,048 sockets and their parent origins.",
                "L’aperçu est limité à 2 048 sockets et aux origines de leurs parents."},
               {"Unknown opposite membership or definition table; no confirmed removal or addition.",
                "Liste ou définitions opposées inconnues ; aucun ajout ni retrait confirmé."}});
}
const Json *AttachmentsView::owner(const Json &root) const {
    if (root.is_null() || owner_.empty())
        return nullptr;
    const auto it = root.at("owners").find(owner_);
    return it == root.at("owners").end() ? nullptr : &*it;
}
void AttachmentsView::set_models(Json a, Json b, const Json &sa, const Json &sb, bool comparing, bool ai,
                                 bool bi) {
    before_ = std::move(a);
    after_ = std::move(b);
    skeleton_before_ = Json::object();
    skeleton_after_ = Json::object();
    auto retain = [](const Json &source, Json &out) {
        if (!source.is_null())
            for (auto it = source.at("skeletons").begin(); it != source.at("skeletons").end(); ++it)
                out[it.key()] = {{"bones", it->at("bones")},
                                 {"retargeting", it->at("retargeting")},
                                 {"referenceComplete", it->at("referenceComplete")}};
    };
    retain(sa, skeleton_before_);
    retain(sb, skeleton_after_);
    comparing_ = comparing;
    input_before_ = ai;
    input_after_ = bi;
    owner_.clear();
    source_before_.clear();
    source_after_.clear();
    preview_ = false;
    filter_ = {};
    selected_.clear();
    changed_only_ = false;
    refresh();
}
bool AttachmentsView::select_owner(const std::string &path) {
    if (!path.empty() && (before_.is_null() || !before_.at("owners").contains(path)) &&
        (after_.is_null() || !after_.at("owners").contains(path)))
        return false;
    if (path == owner_)
        return true;
    owner_ = path;
    source_before_.clear();
    source_after_.clear();
    filter_ = {};
    changed_only_ = false;
    selected_.clear();
    preview_ = false;
    refresh();
    return true;
}
void AttachmentsView::refresh(bool preserve) {
    const auto state = preserve ? view_state() : Json::object();
    flat_before_ = Json::object();
    flat_after_ = Json::object();
    entries_.clear();
    visible_.clear();
    truncated_ = false;
    preview_truncated_ = false;
    const auto *a = owner(before_), *b = owner(after_);
    complete_ = (a || b) && (!a || (input_before_ && a->at("complete").get<bool>())) &&
                (!b || (input_after_ && b->at("complete").get<bool>()));
    std::set<std::string> sources;
    for (const auto *model : {&skeleton_before_, &skeleton_after_}) {
        const auto found = model->find(owner_);
        if (found != model->end())
            for (auto it = found->at("retargeting").at("sources").begin();
                 it != found->at("retargeting").at("sources").end(); ++it)
                sources.insert(it.key());
    }
    sources_.assign(sources.begin(), sources.end());
    ReferencePose bones_a, bones_b;
    auto prepare = [&](const Json &root, const Json &skeleton, bool input, const std::string &source,
                       ReferencePose &bones) {
        SocketPose out;
        out.object_path = owner_;
        out.retarget_source = source;
        const auto *body = owner(root);
        if (!body) {
            out.complete = true;
            return out;
        }
        const auto found = skeleton.find(owner_);
        if (found == skeleton.end())
            return out;
        ReferencePoseOptions options;
        options.retarget_source = source;
        try {
            return detail::compose_socket_pose(root, *found, owner_, input, options, &bones);
        } catch (const Error &error) {
            if (error.code == "cancelled")
                throw;
            out.diagnostics.push_back(error.what());
            return out;
        }
    };
    pose_before_ = prepare(before_, skeleton_before_, input_before_, source_before_, bones_a);
    pose_after_ = prepare(after_, skeleton_after_, input_after_, source_after_, bones_b);
    complete_ = complete_ && (!a || pose_before_.complete) && (!b || pose_after_.complete);
    auto display = [&](const SocketPose &pose, const ReferencePose &bones) {
        ReferencePose out = pose;
        out.joints.clear();
        std::map<std::string, const ReferenceJoint *> anchors;
        for (const auto &bone : bones.joints)
            anchors[bone.name] = &bone;
        std::set<std::string> used;
        std::size_t socket_count = 0;
        for (const auto &socket : pose.joints) {
            if (socket_count++ >= 2048) {
                preview_truncated_ = true;
                break;
            }
            auto joint = socket;
            joint.name = "s/" + socket.name;
            joint.parent = socket.parent.empty() ? "" : "b/" + socket.parent;
            out.joints.push_back(joint);
            if (used.insert(socket.parent).second) {
                const auto anchor = anchors.find(socket.parent);
                if (anchor != anchors.end()) {
                    auto origin = *anchor->second;
                    origin.name = "b/" + socket.parent;
                    origin.parent.clear();
                    out.joints.push_back(std::move(origin));
                }
            }
        }
        return out;
    };
    const auto plot_a = display(pose_before_, bones_a), plot_b = display(pose_after_, bones_b);
    viewport_.set_models(a ? &plot_a : nullptr, b ? &plot_b : nullptr, comparing_);
    auto flatten = [&](const Json &root, const Json *body, const SocketPose &pose, Json &out) {
        if (!body)
            return;
        std::map<std::string, std::size_t> listed;
        for (const auto &ref : body->at("socketReferences"))
            if (ref.at("path").is_string())
                ++listed[ref.at("path").get<std::string>()];
        std::map<std::string, const ReferenceJoint *> points;
        for (const auto &point : pose.joints)
            points[point.name] = &point;
        auto add = [&](const std::string &key, Json value, Entry entry) {
            if (!entries_.count(key) && entries_.size() >= 4096) {
                truncated_ = true;
                return;
            }
            entries_[key] = std::move(entry);
            out[key] = std::move(value);
        };
        for (auto it = root.at("sockets").begin(); it != root.at("sockets").end(); ++it)
            if (listed.count(it.key()) || it->at("outer") == owner_) {
                Json value = *it;
                value["listed"] = listed.count(it.key()) != 0;
                value["referenceOccurrences"] = listed[it.key()];
                const auto point = points.find(it.key());
                value["origin"] = {{"ready", point != points.end() && point->second->ready},
                                   {"position", point != points.end() && point->second->ready
                                                    ? Json(point->second->position)
                                                    : Json(nullptr)}};
                if (point != points.end())
                    value["origin"]["diagnostics"] = point->second->diagnostics;
                add(socket_key(it.key()), std::move(value),
                    {it->at("name").is_string() ? it->at("name").get<std::string>() : it.key(),
                     it->at("bone").is_string() ? it->at("bone").get<std::string>() : "", it.key(), false});
            }
        for (auto it = body->at("virtualBones").at("definitions").begin();
             it != body->at("virtualBones").at("definitions").end(); ++it)
            add(virtual_key(it.key()), *it,
                {it->at("name").is_string() ? it->at("name").get<std::string>() : it.key(),
                 it->at("target").is_string() ? it->at("target").get<std::string>() : "", "", true});
        for (const auto &ref : body->at("socketReferences"))
            if (ref.at("path").is_string() && ref.at("path") != "None" && !ref.at("resolved").get<bool>()) {
                const auto path = ref.at("path").get<std::string>();
                add(socket_key(path), {{"reference", ref}, {"complete", false}}, {path, "", path, false});
            }
    };
    flatten(before_, a, pose_before_, flat_before_);
    flatten(after_, b, pose_after_, flat_after_);
    auto known = [](const Json *body, bool v) {
        return !body || (v ? body->at("virtualBones").at("coverage") == "stored"
                           : body->at("socketListCoverage") == "stored");
    };
    for (auto &item : entries_) {
        auto &entry = item.second;
        const auto x = flat_before_.find(item.first), y = flat_after_.find(item.first);
        entry.known = !comparing_ || ((x != flat_before_.end() || known(a, entry.virtual_bone)) &&
                                      (y != flat_after_.end() || known(b, entry.virtual_bone)));
        entry.change = !comparing_ || !entry.known ? ChangeKind::unchanged
                                                   : classify_change(x == flat_before_.end() ? nullptr : &*x,
                                                                     y == flat_after_.end() ? nullptr : &*y);
    }
    selected_.clear();
    rebuild();
    bind_selection();
    if (preserve) {
        if (state.contains("inspection")) {
            const auto &inspection = state.at("inspection");
            filter_.query = inspection.value("filter", std::string{});
            changed_only_ = comparing_ && inspection.value("changedOnly", false);
            rebuild();
            if (inspection.contains("selected") && inspection.at("selected").is_string())
                select(inspection.at("selected").get<std::string>());
            if (!selected_.empty() && inspection.contains("properties"))
                details_.restore_view_state(inspection.at("properties"));
        }
        if (state.contains("camera"))
            viewport_.restore_view_state(state.at("camera"));
    }
}
void AttachmentsView::rebuild() {
    visible_.clear();
    for (const auto &item : entries_)
        if ((!changed_only_ || !item.second.known || item.second.change != ChangeKind::unchanged) &&
            filter_.matches(item.second.label, item.first + " " + item.second.bone))
            visible_.push_back(item.first);
    if (!selected_.empty() && std::find(visible_.begin(), visible_.end(), selected_) == visible_.end()) {
        selected_.clear();
        bind_selection();
    }
}
void AttachmentsView::bind_selection() {
    const auto a = flat_before_.find(selected_), b = flat_after_.find(selected_);
    const auto *x = a == flat_before_.end() ? nullptr : &*a, *y = b == flat_after_.end() ? nullptr : &*b;
    if (!selected_.empty() && !selected_known())
        details_.set_values(x ? x : y, nullptr, false);
    else
        details_.set_values(x, y, comparing_);
    const auto it = entries_.find(selected_);
    viewport_.select(it != entries_.end() && !it->second.virtual_bone ? "s/" + it->second.path : "");
}
bool AttachmentsView::select(const std::string &key) {
    if (!key.empty() && std::find(visible_.begin(), visible_.end(), key) == visible_.end())
        return false;
    if (selected_ != key) {
        selected_ = key;
        bind_selection();
    }
    return true;
}
bool AttachmentsView::select_socket(const std::string &path) { return select(socket_key(path)); }
bool AttachmentsView::select_virtual_bone(const std::string &key) { return select(virtual_key(key)); }
bool AttachmentsView::selected_known() const {
    const auto it = entries_.find(selected_);
    return it != entries_.end() && it->second.known;
}
void AttachmentsView::set_filter(std::string s) {
    filter_.query = std::move(s);
    rebuild();
}
void AttachmentsView::set_changed_only(bool v) {
    changed_only_ = comparing_ && v;
    rebuild();
}
bool AttachmentsView::next_change(bool previous) {
    std::vector<std::string> changed;
    for (const auto &key : visible_)
        if (entries_.at(key).known && entries_.at(key).change != ChangeKind::unchanged)
            changed.push_back(key);
    if (changed.empty())
        return false;
    const auto it = std::find(changed.begin(), changed.end(), selected_);
    const auto index =
        it == changed.end()
            ? (previous ? changed.size() - 1 : 0)
            : (std::size_t(it - changed.begin()) + changed.size() + (previous ? -1 : 1)) % changed.size();
    return select(changed[index]);
}
bool AttachmentsView::select_sources(std::string a, std::string b) {
    source_before_ = std::move(a);
    source_after_ = std::move(b);
    refresh(true);
    return (!owner(before_) || pose_before_.complete) && (!owner(after_) || pose_after_.complete);
}
Json AttachmentsView::view_state() const {
    Json out = Json::object(), inspection = Json::object();
    if (!filter_.query.empty())
        inspection["filter"] = filter_.query;
    if (changed_only_)
        inspection["changedOnly"] = true;
    if (!selected_.empty()) {
        inspection["selected"] = selected_;
        inspection["properties"] = details_.view_state();
    }
    if (!inspection.empty())
        out["inspection"] = inspection;
    if (preview_)
        out["preview"] = true;
    if (!source_before_.empty() || !source_after_.empty()) {
        out["beforeSource"] = source_before_;
        out["afterSource"] = source_after_;
    }
    const auto camera = viewport_.view_state();
    if (!camera.empty())
        out["camera"] = camera;
    return out;
}
void AttachmentsView::restore_view_state(const Json &state) {
    view_state::validate(state, "attachments");
    if (state.contains("beforeSource") || state.contains("afterSource"))
        select_sources(state.value("beforeSource", source_before_),
                       state.value("afterSource", source_after_));
    if (state.contains("preview"))
        preview_ = state.at("preview").get<bool>();
    if (state.contains("inspection")) {
        const auto &inspection = state.at("inspection");
        if (inspection.contains("filter"))
            filter_.query = inspection.at("filter").get<std::string>();
        if (inspection.contains("changedOnly"))
            changed_only_ = comparing_ && inspection.at("changedOnly").get<bool>();
        rebuild();
        if (inspection.contains("selected")) {
            select("");
            if (inspection.at("selected").is_string())
                select(inspection.at("selected").get<std::string>());
        }
        if (!selected_.empty() && inspection.contains("properties"))
            details_.restore_view_state(inspection.at("properties"));
    }
    if (state.contains("camera"))
        viewport_.restore_view_state(state.at("camera"));
}
void AttachmentsView::draw(const char *id) {
    cyui::ScopedID scope(id);
    ImGui::TextWrapped(
        "%s", cyui::tr("Sockets and stored virtual-bone definitions; omitted values stay unknown.").c_str());
    if (!complete_)
        cyui::callout(
            cyui::Tone::warning,
            cyui::tr("Some attachment data is incomplete. External objects and defaults are not loaded.")
                .c_str());
    if (truncated_)
        cyui::textDim("%s", cyui::tr("Attachment list is limited to 4,096 entries.").c_str());
    if (!sources_.empty() || !source_before_.empty() || !source_after_.empty()) {
        auto choice = [&](const char *label, bool after) {
            const auto &current = after ? source_after_ : source_before_;
            ImGui::SetNextItemWidth(std::min(cyui::dp(300), ImGui::GetContentRegionAvail().x * .6f));
            if (ImGui::BeginCombo(cyui::trLabel(label).c_str(), current.empty()
                                                                    ? cyui::tr("Stored raw reference").c_str()
                                                                    : current.c_str())) {
                std::optional<std::string> chosen;
                if (ImGui::Selectable(cyui::trLabel("Stored raw reference").c_str(), current.empty()))
                    chosen = "";
                for (const auto &source : sources_)
                    if (ImGui::Selectable(source.c_str(), current == source))
                        chosen = source;
                ImGui::EndCombo();
                if (chosen)
                    select_sources(after ? source_before_ : *chosen, after ? *chosen : source_after_);
            }
        };
        choice(comparing_ ? "Retarget source before" : "Retarget source", !comparing_ && !owner(before_));
        if (comparing_)
            choice("Retarget source after", true);
    }
    cyui::FilterOptions options;
    options.hint = "Find sockets or virtual bones";
    options.width = std::min(cyui::dp(260), ImGui::GetContentRegionAvail().x);
    bool update = cyui::filterBox("##filter", filter_, options);
    if (comparing_) {
        if (ImGui::GetContentRegionAvail().x > cyui::dp(260))
            ImGui::SameLine();
        update = ImGui::Checkbox(cyui::trLabel("Changed only").c_str(), &changed_only_) || update;
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::undo, cyui::tr("Previous changed entry").c_str()))
            next_change(true);
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::redo, cyui::tr("Next changed entry").c_str()))
            next_change();
    }
    if (update)
        rebuild();
    ImGui::Checkbox(cyui::trLabel("Socket origins 3D").c_str(), &preview_);
    const float available = ImGui::GetContentRegionAvail().y;
    const float list_height = preview_            ? std::max(cyui::dp(100), available * .23f)
                              : selected_.empty() ? 0
                                                  : std::max(cyui::dp(110), available * .35f);
    ImGui::BeginChild("##list", {0, list_height}, ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("##attachments", 3,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable,
                          {0, ImGui::GetContentRegionAvail().y})) {
        ImGui::TableSetupColumn(cyui::tr("Name").c_str(), ImGuiTableColumnFlags_WidthStretch, 2);
        ImGui::TableSetupColumn(cyui::tr("Bone / target").c_str(), ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn(cyui::tr("Status").c_str(), ImGuiTableColumnFlags_WidthStretch, 1);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(int(visible_.size()));
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto &key = visible_[std::size_t(i)];
                const auto &entry = entries_.at(key);
                cyui::ScopedID row(key.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, entry.known ? detail::change_color(entry.change)
                                                                 : cyui::palette().textDim);
                if (ImGui::Selectable("##entry", selected_ == key, ImGuiSelectableFlags_SpanAllColumns))
                    select(key);
                ImGui::SameLine();
                ImGui::TextUnformatted(entry.label.c_str());
                ImGui::PopStyleColor();
                cyui::tooltip(key.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(entry.bone.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(cyui::tr(!entry.known         ? "Unavailable"
                                                : comparing_         ? change_name(entry.change)
                                                : entry.virtual_bone ? "Virtual bone"
                                                                     : "Socket")
                                           .c_str());
            }
        ImGui::EndTable();
    }
    if (visible_.empty())
        cyui::textDim("%s", cyui::tr("No matching attachments").c_str());
    ImGui::EndChild();
    if (preview_) {
        cyui::textDim(
            "%s",
            cyui::tr("Only explicitly stored locations can be placed. Parent points are raw bone origins.")
                .c_str());
        if (preview_truncated_)
            cyui::textDim("%s",
                          cyui::tr("Preview is limited to 2,048 sockets and their parent origins.").c_str());
        const float height =
            selected_.empty() ? 0 : std::max(cyui::dp(160), ImGui::GetContentRegionAvail().y * .55f);
        ImGui::BeginChild("##origins", {0, height});
        viewport_.draw("Socket origins");
        const auto picked = viewport_.take_picked_joint();
        ImGui::EndChild();
        if (picked.rfind("s/", 0) == 0)
            select_socket(picked.substr(2));
    }
    if (!selected_.empty()) {
        if (!selected_known())
            cyui::textDim(
                "%s",
                cyui::tr("Unknown opposite membership or definition table; no confirmed removal or addition.")
                    .c_str());
        ImGui::BeginChild("##details", {0, 0}, ImGuiChildFlags_Borders);
        details_.draw("Attachment details");
        ImGui::EndChild();
    }
}
} // namespace cyuediff
