#include <cyuediff/effective_defaults_view.hpp>
namespace cyuediff {
void EffectiveDefaultsView::register_translations() {
    cyui::registerTranslations(
        "fr",
        {{"Effective properties", "Propriétés effectives"},
         {"Effective changes", "Différences effectives"},
         {"Before values", "Valeurs avant"},
         {"After values", "Valeurs après"},
         {"Choose a prepared default context in the source panel, then Load.",
          "Choisissez un contexte de valeurs par défaut préparé dans le panneau source, puis Charger."},
         {"No effective comparison is available. Inspect each side and its diagnostics.",
          "Aucune comparaison effective disponible. Examinez chaque côté et ses diagnostics."},
         {"Verified capture scope; unsupported properties remain unavailable.",
          "Périmètre de capture vérifié ; les propriétés non prises en charge restent indisponibles."},
         {"All selected effective fields resolved", "Tous les champs effectifs sélectionnés sont résolus"},
         {"Some effective fields are unavailable", "Certains champs effectifs sont indisponibles"},
         {"Known effective changes", "Différences effectives connues"},
         {"Effective comparison is identical", "Les valeurs effectives sont identiques"},
         {"Effective comparison is partial", "La comparaison effective est partielle"},
         {"Before context unavailable", "Contexte avant indisponible"},
         {"After context unavailable", "Contexte après indisponible"}});
}
void EffectiveDefaultsView::set_models(std::shared_ptr<const EffectiveDefaults> a,
                                       std::shared_ptr<const EffectiveDefaults> b,
                                       std::shared_ptr<const EffectiveDefaultsComparison> c,
                                       PropertyTable table, std::string ae, std::string be) {
    before_ = std::move(a);
    after_ = std::move(b);
    comparison_ = std::move(c);
    before_error_ = std::move(ae);
    after_error_ = std::move(be);
    auto bind = [](PropertyBrowserView &view, const auto &model) {
        ValueCoverage coverage;
        if (model)
            for (const auto &entry : model->unavailable)
                coverage.before_unavailable.push_back(entry.at("path").template get<std::string>());
        view.set_values(model ? &model->fields : nullptr, nullptr, false, coverage);
    };
    bind(before_view_, before_);
    bind(after_view_, after_);
    changes_.set_table(std::move(table), true);
    mode_ = comparison_ ? "changes" : before_ ? "before" : after_ ? "after" : "changes";
}
Json EffectiveDefaultsView::view_state() const {
    return {{"mode", mode_},
            {"before", before_view_.view_state()},
            {"after", after_view_.view_state()},
            {"changes", changes_.view_state()}};
}
void EffectiveDefaultsView::restore_view_state(const Json &s) {
    view_state::validate(s, "effective-defaults");
    if (s.contains("mode"))
        mode_ = s.at("mode").get<std::string>();
    if (s.contains("before"))
        before_view_.restore_view_state(before_ ? &before_->fields : nullptr, nullptr, s.at("before"));
    if (s.contains("after"))
        after_view_.restore_view_state(after_ ? &after_->fields : nullptr, nullptr, s.at("after"));
    if (s.contains("changes"))
        changes_.restore_view_state(s.at("changes"));
}
Json EffectiveDefaultsView::report() const {
    return {{"mode", mode_},
            {"comparisonAvailable", bool(comparison_)},
            {"rows", changes_.table().rows.size()},
            {"truncated", changes_.table().truncated},
            {"visibleRows", changes_.visible_count()},
            {"state", view_state()}};
}
void EffectiveDefaultsView::draw() {
    for (const auto &pair : {std::pair<const char *, const char *>{"changes", "Effective changes"},
                             {"before", "Before values"},
                             {"after", "After values"}}) {
        if (pair.first != std::string("changes"))
            ImGui::SameLine();
        if (ImGui::RadioButton(cyui::trLabel(pair.second).c_str(), mode_ == pair.first))
            mode_ = pair.first;
    }
    cyui::textDim("%s",
                  cyui::tr("Verified capture scope; unsupported properties remain unavailable.").c_str());
    if (!before_error_.empty()) {
        ImGui::TextWrapped("%s: %s", cyui::tr("Before context unavailable").c_str(), before_error_.c_str());
    }
    if (!after_error_.empty()) {
        ImGui::TextWrapped("%s: %s", cyui::tr("After context unavailable").c_str(), after_error_.c_str());
    }
    if (mode_ == "changes") {
        if (!comparison_) {
            cyui::callout(
                cyui::Tone::info,
                cyui::tr(before_ || after_
                             ? "No effective comparison is available. Inspect each side and its diagnostics."
                             : "Choose a prepared default context in the source panel, then Load.")
                    .c_str());
            return;
        }
        cyui::textDim("%s (%zu / %zu)",
                      cyui::tr(comparison_->status() == "identical" ? "Effective comparison is identical"
                               : comparison_->unavailable.empty()   ? "Known effective changes"
                                                                    : "Effective comparison is partial")
                          .c_str(),
                      comparison_->changes.size(), comparison_->unavailable.size());
        changes_.draw("##effective-changes");
        return;
    }
    const auto &model = mode_ == "after" ? after_ : before_;
    if (!model) {
        cyui::callout(cyui::Tone::info,
                      cyui::tr("Choose a prepared default context in the source panel, then Load.").c_str());
        return;
    }
    cyui::textDim("%s", cyui::tr(model->complete ? "All selected effective fields resolved"
                                                 : "Some effective fields are unavailable")
                            .c_str());
    if (model->provenance.contains("contextSha256")) {
        const auto &hash = model->provenance.at("contextSha256").get_ref<const std::string &>();
        cyui::textDim("SHA256 %.16s", hash.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", hash.c_str());
    }
    (mode_ == "after" ? after_view_ : before_view_).draw(&model->fields, nullptr, "##effective-values");
}
} // namespace cyuediff
