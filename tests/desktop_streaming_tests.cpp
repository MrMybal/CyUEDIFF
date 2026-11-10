#include "../apps/desktop/document.hpp"
#include "../src/json_output.hpp"
#include "source_physical_fixture.hpp"
#include <sstream>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::desktop;
namespace {
unsigned checks = 0;
void check(bool v, const char *why) {
    ++checks;
    if (!v)
        throw std::runtime_error(why);
}
Json streamed(const detail::JsonObject &view) {
    std::ostringstream out;
    view.write(out);
    return Json::parse(out.str());
}
} // namespace
int main() {
    try {
        Document d;
        d.before = std::make_shared<Package>();
        auto changes = std::make_shared<DiffResult>();
        changes->binary_comparison_available = true;
        changes->binary_changed = true;
        changes->changes.push_back({{"op", "replace"},
                                    {"path", "/value"},
                                    {"before", std::int64_t(9007199254740993LL)},
                                    {"after", std::int64_t(9007199254740994LL)}});
        d.diff = changes;
        d.request.source_limits.max_properties = 2000000;
        d.layout_contexts = {{"beforeSha256", "a"}, {"afterSha256", "b"}, {"contextChanged", true}};
        auto data = streamed(d.report_view());
        check(data == d.report() && data["diff"] == changes->to_json(),
              "Streamed loose document retains every legacy fact and exact integer");
        check(data["readLimits"]["before"]["maxProperties"] == 2000000 &&
                  data["layoutContexts"]["contextChanged"] == true,
              "Independent source policy and decoder provenance remain present");
        auto effective = std::make_shared<EffectiveDefaultsDocument>();
        auto left = std::make_shared<EffectiveDefaults>();
        left->complete = true;
        left->fields = {{"/Script/Test.Class.Large", {{"value", std::int64_t(9007199254740993LL)}}}};
        auto right = std::make_shared<EffectiveDefaults>(*left);
        right->fields["/Script/Test.Class.Large"]["value"] = std::int64_t(9007199254740994LL);
        auto typed = std::make_shared<EffectiveDefaultsComparison>();
        typed->changes = changes->changes;
        effective->before = left;
        effective->after = right;
        effective->comparison = typed;
        effective->before_error = "A diagnostic retained as owned text";
        d.effective_defaults = effective;
        data = streamed(d.report_view());
        check(data["effectiveDefaults"]["before"] == left->to_json() &&
                  data["effectiveDefaults"]["after"] == right->to_json() &&
                  data["effectiveDefaults"]["comparison"] == typed->to_json(),
              "Effective streamed models retain exact typed values and comparison provenance");
        check(data["effectiveDefaults"]["beforeError"] == effective->before_error &&
                  data["status"] == "changed",
              "Owned diagnostics and effective status survive streaming");
        auto view = effective->report_view();
        left->fields["/Script/Test.Class.Large"]["value"] = std::int64_t(9007199254740995LL);
        check(streamed(view)["before"]["fields"] == left->fields,
              "Effective view borrows immutable model containers rather than snapshot copies");
        d.effective_defaults.reset();
        PhysicalClothOptions options;
        options.profile = PhysicalClothProfile::unreal_5_3_windows;
        auto source = test::source_physical_fixture();
        auto physical = std::make_shared<const PhysicalClothAsset>(read_physical_cloth(source, options));
        d.before_physical_cloth = {physical};
        d.after_physical_cloth = {physical};
        d.request.physical_cloth_profile = options.profile;
        data = streamed(d.report_view());
        check(data == d.report() && data["physicalCloth"]["before"][0] == physical->to_json(),
              "Physical models retain all data while each array element is generated independently");
        auto container = std::make_shared<ContainerDocument>();
        container->metadata = {{"example", 42}};
        container->physics = {{"bodies", Json::array({1, 2})}};
        auto after = std::make_shared<ContainerDocument>();
        after->metadata = {{"example", 43}};
        container->after = after;
        d.container = container;
        auto c = streamed(container->report_view());
        check(c == container->report() && c["after"]["metadata"]["example"] == 43,
              "Before/after container reports retain recursively nested facts");
        data = streamed(d.report_view());
        check(data.size() == 2 && data["status"] == "ready" && data["container"] == c,
              "Container documents retain their distinct root contract");
#ifdef CYUEDIFF_HAS_GIT
        d.container.reset();
        auto git = std::make_shared<GitComparison>();
        git->diff = *changes;
        git->before_context = {{"name", "old"}};
        git->after_context = {{"name", "new"}};
        git->context_changes.push_back(
            {{"op", "replace"}, {"path", "/name"}, {"before", "old"}, {"after", "new"}});
        d.git = git;
        d.diff = std::shared_ptr<const DiffResult>(d.git, &d.git->diff);
        data = streamed(d.report_view());
        check(data == d.report() && data["git"]["comparison"] == data["diff"],
              "Streamed Git/document mirrors preserve the same comparison without retained duplicates");
        std::ostringstream output;
        git->write_json(output);
        check(Json::parse(output.str()) == git->to_json(),
              "Public Git stream retains revision and project context facts");
#endif
        std::cout << checks << " desktop streaming report checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << checks << ": " << e.what() << '\n';
        return 1;
    }
}
