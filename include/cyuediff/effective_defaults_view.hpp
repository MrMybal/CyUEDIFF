#pragma once
#include <cyuediff/defaults.hpp>
#include <cyuediff/property_browser_view.hpp>
namespace cyuediff {
// Host supplies worker-prepared typed comparisons. Drawing never loads a context or compares JSON values.
class EffectiveDefaultsView {
    std::shared_ptr<const EffectiveDefaults> before_, after_;
    std::shared_ptr<const EffectiveDefaultsComparison> comparison_;
    PropertyBrowserView before_view_, after_view_;
    PropertyView changes_;
    std::string before_error_, after_error_;
    std::string mode_ = "changes";

  public:
    static void register_translations();
    void set_models(std::shared_ptr<const EffectiveDefaults>, std::shared_ptr<const EffectiveDefaults>,
                    std::shared_ptr<const EffectiveDefaultsComparison>, PropertyTable,
                    std::string before_error = {}, std::string after_error = {});
    void draw();
    Json view_state() const;
    void restore_view_state(const Json &);
    Json report() const;
};
} // namespace cyuediff
